#include "playback.h"

#include "utf8.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <pulse/simple.h>
#include <pulse/error.h>

#ifdef HAS_FLUIDSYNTH
#include "minimal_fluidsynth.h"
#endif

#include <pthread.h>
#include <ctype.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct DecoderState {
    AVFormatContext *format;
    AVCodecContext *codec;
    struct SwrContext *swr;
    int stream_index;
    enum AVSampleFormat source_format;
    int source_rate;
    int channels;
    int output_rate;
} DecoderState;

typedef struct PulseStream {
    pa_simple *handle;
    pa_sample_spec spec;
} PulseStream;

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cond = PTHREAD_COND_INITIALIZER;
static pthread_t decoder_thread;
static bool thread_running;
static bool stop_requested;
static bool pause_requested;
static bool seek_requested;
static bool finished_naturally;
static unsigned long requested_seek_ms;
static int64_t frames_written;
static unsigned long track_duration_ms;
static double volume_gain = 0.75;

static DecoderState decoder_state;
static PulseStream pulse_stream;

#ifdef HAS_FLUIDSYNTH
static bool midi_mode;
static fluid_synth_t *midi_synth;
static fluid_player_t *midi_player;
static fluid_settings_t *midi_settings;
static int midi_sfont_id;
static int midi_total_ticks;
static int midi_output_rate;
static int midi_channels;
#endif

static int clamp_audio_rate(int rate)
{
    if (rate <= 0) return 44100;
    if (rate < 4000) return 4000;
    if (rate > 384000) return 384000;
    return rate;
}

static int clamp_audio_channels(int channels)
{
    if (channels <= 0) return 2;
    if (channels > 8) return 8;
    return channels;
}

static void apply_volume(int16_t *samples, size_t count, double gain)
{
    size_t index;
    if (gain >= 1.0) return;
    if (gain <= 0.0) {
        memset(samples, 0, count * sizeof(*samples));
        return;
    }
    for (index = 0; index < count; ++index) {
        double scaled = (double)samples[index] * gain;
        if (scaled > 32767.0) scaled = 32767.0;
        if (scaled < -32768.0) scaled = -32768.0;
        samples[index] = (int16_t)scaled;
    }
}

static bool ends_with_ext(const wchar_t *path, const wchar_t *ext)
{
    size_t path_len;
    size_t ext_len;
    size_t i;
    if (path == NULL || ext == NULL) return false;
    path_len = wcslen(path);
    ext_len = wcslen(ext);
    if (path_len < ext_len) return false;
    for (i = 0; i < ext_len; ++i) {
        if (towlower(path[path_len - ext_len + i]) != ext[i]) return false;
    }
    return true;
}

static bool is_midi_path(const wchar_t *path)
{
    return ends_with_ext(path, L".mid") || ends_with_ext(path, L".midi");
}

static bool open_decoder(const wchar_t *path, DecoderState *state)
{
    const AVCodec *codec;
    int index;
    int result;
    char *utf8_path;
    if (path == NULL || state == NULL) return false;
    memset(state, 0, sizeof(*state));
    utf8_path = utf8_encode(path);
    if (utf8_path == NULL) return false;
    result = avformat_open_input(&state->format, utf8_path, NULL, NULL);
    free(utf8_path);
    if (result < 0) return false;
    if (avformat_find_stream_info(state->format, NULL) < 0) {
        avformat_close_input(&state->format);
        return false;
    }
    state->stream_index = -1;
    for (index = 0; index < (int)state->format->nb_streams; ++index) {
        if (state->format->streams[index]->codecpar != NULL &&
            state->format->streams[index]->codecpar->codec_type ==
                AVMEDIA_TYPE_AUDIO) {
            state->stream_index = index;
            break;
        }
    }
    if (state->stream_index < 0) {
        avformat_close_input(&state->format);
        return false;
    }
    codec = avcodec_find_decoder(
        state->format->streams[state->stream_index]->codecpar->codec_id);
    if (codec == NULL) {
        avformat_close_input(&state->format);
        return false;
    }
    state->codec = avcodec_alloc_context3(codec);
    if (state->codec == NULL) {
        avformat_close_input(&state->format);
        return false;
    }
    result = avcodec_parameters_to_context(
        state->codec, state->format->streams[state->stream_index]->codecpar);
    if (result < 0 || avcodec_open2(state->codec, codec, NULL) < 0) {
        avcodec_free_context(&state->codec);
        avformat_close_input(&state->format);
        return false;
    }
    state->source_format = state->codec->sample_fmt;
    state->source_rate = state->codec->sample_rate;
    state->channels = clamp_audio_channels(
        state->codec->ch_layout.nb_channels);
    state->output_rate = clamp_audio_rate(state->codec->sample_rate);
    return true;
}

static void close_decoder(DecoderState *state)
{
    if (state == NULL) return;
    if (state->swr != NULL) swr_free(&state->swr);
    if (state->codec != NULL) avcodec_free_context(&state->codec);
    if (state->format != NULL) avformat_close_input(&state->format);
    memset(state, 0, sizeof(*state));
}

static void close_pulse(PulseStream *stream)
{
    if (stream == NULL || stream->handle == NULL) return;
    pa_simple_free(stream->handle);
    stream->handle = NULL;
}

static bool open_pulse(PulseStream *stream, int rate, int channels)
{
    pa_sample_spec spec;
    int error;
    if (stream == NULL) return false;
    memset(stream, 0, sizeof(*stream));
    memset(&spec, 0, sizeof(spec));
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = (uint32_t)rate;
    spec.channels = (uint8_t)channels;
    stream->handle = pa_simple_new(NULL, "AudioCommander", PA_STREAM_PLAYBACK,
                                   NULL, "audio", &spec, NULL, NULL, &error);
    if (stream->handle == NULL) return false;
    stream->spec = spec;
    return true;
}

static unsigned long track_duration_ms_from(const DecoderState *state)
{
    AVStream *stream;
    int64_t duration;
    if (state == NULL || state->format == NULL ||
        state->stream_index < 0) return 0;
    stream = state->format->streams[state->stream_index];
    duration = stream->duration;
    if (duration == AV_NOPTS_VALUE || duration <= 0)
        duration = state->format->duration;
    if (duration <= 0 || duration == AV_NOPTS_VALUE) return 0;
    if (stream->time_base.num > 0 && stream->time_base.den > 0) {
        return (unsigned long)(duration * 1000 * stream->time_base.num /
                               stream->time_base.den);
    }
    return (unsigned long)(duration / 1000);
}

static void perform_seek(const DecoderState *state, unsigned long position_ms)
{
    AVStream *stream;
    int64_t timestamp;
    if (state == NULL || state->format == NULL ||
        state->stream_index < 0) return;
    stream = state->format->streams[state->stream_index];
    timestamp = av_rescale_q((int64_t)position_ms, (AVRational){1, 1000},
                             stream->time_base);
    if (av_seek_frame(state->format, state->stream_index, timestamp,
                      AVSEEK_FLAG_BACKWARD) >= 0) {
        avcodec_flush_buffers(state->codec);
        frames_written = (int64_t)position_ms * state->output_rate / 1000;
    }
}

static int convert_and_write(const DecoderState *state, AVFrame *frame,
                             int16_t *buffer, size_t buffer_samples)
{
    int output_samples;
    uint8_t *output = (uint8_t *)buffer;
    int converted;
    int error;
    if (frame == NULL || buffer == NULL) return 0;
    output_samples = (int)(buffer_samples / (size_t)state->channels);
    if (output_samples <= 0) return 0;
    converted = swr_convert(state->swr, &output, output_samples,
                            (const uint8_t **)frame->extended_data,
                            frame->nb_samples);
    if (converted <= 0) return 0;
    apply_volume(buffer, (size_t)converted * (size_t)state->channels,
                 volume_gain);
    if (pa_simple_write(pulse_stream.handle,
                        (const void *)buffer,
                        (size_t)converted * (size_t)state->channels * 2u,
                        &error) < 0) {
        return -1;
    }
    return converted;
}

static void *decoder_main(void *arg)
{
    DecoderState *state = (DecoderState *)arg;
    AVPacket *packet;
    AVFrame *frame;
    int buffer_samples;
    int16_t *buffer;
    bool read_error = false;
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    buffer_samples = state->output_rate * state->channels / 4;
    buffer = (int16_t *)malloc((size_t)buffer_samples * sizeof(*buffer));
    if (packet == NULL || frame == NULL || buffer == NULL) {
        if (packet != NULL) av_packet_free(&packet);
        if (frame != NULL) av_frame_free(&frame);
        free(buffer);
        goto finish;
    }
    while (!stop_requested) {
        int read_result;
        pthread_mutex_lock(&state_mutex);
        while (!stop_requested && pause_requested)
            pthread_cond_wait(&state_cond, &state_mutex);
        if (seek_requested) {
            perform_seek(state, requested_seek_ms);
            seek_requested = false;
        }
        pthread_mutex_unlock(&state_mutex);
        if (stop_requested) break;
        read_result = av_read_frame(state->format, packet);
        if (read_result < 0) {
            if (read_result == AVERROR_EOF) {
                avcodec_send_packet(state->codec, NULL);
                while (avcodec_receive_frame(state->codec, frame) == 0) {
                    int written = convert_and_write(state, frame, buffer,
                                                    (size_t)buffer_samples);
                    if (written < 0) { read_error = true; break; }
                    pthread_mutex_lock(&state_mutex);
                    frames_written += written;
                    pthread_mutex_unlock(&state_mutex);
                    av_frame_unref(frame);
                    if (stop_requested) break;
                }
                break;
            }
            read_error = true;
            break;
        }
        if (packet->stream_index == state->stream_index) {
            if (avcodec_send_packet(state->codec, packet) == 0) {
                while (avcodec_receive_frame(state->codec, frame) == 0) {
                    int written = convert_and_write(state, frame, buffer,
                                                    (size_t)buffer_samples);
                    if (written < 0) { read_error = true; break; }
                    pthread_mutex_lock(&state_mutex);
                    frames_written += written;
                    pthread_mutex_unlock(&state_mutex);
                    av_frame_unref(frame);
                    if (stop_requested) break;
                }
            }
        }
        av_packet_unref(packet);
        if (read_error) break;
    }
    if (!stop_requested && !read_error && pulse_stream.handle != NULL) {
        pa_simple_drain(pulse_stream.handle, NULL);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    free(buffer);
finish:
    close_pulse(&pulse_stream);
    close_decoder(state);
    pthread_mutex_lock(&state_mutex);
    if (!stop_requested && !read_error) finished_naturally = true;
    thread_running = false;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

#ifdef HAS_FLUIDSYNTH
static const char *find_soundfont(void)
{
    static const char *paths[] = {
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/FluidR3_GS.sf2",
        "/usr/share/sounds/sf2/FluidR3_Lite.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/FluidR3_GS.sf2",
        "/usr/share/soundfonts/FluidR3_Lite.sf2",
        "/usr/share/soundfonts/default-GM.sf2",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/soundfonts/yamaha.sf2",
        "/usr/share/soundfonts/freepats-general-midi.sf2",
        NULL
    };
    int i;
    FILE *f;
    for (i = 0; paths[i] != NULL; ++i) {
        f = fopen(paths[i], "rb");
        if (f != NULL) {
            fclose(f);
            return paths[i];
        }
    }
    return NULL;
}

static void close_midi(void)
{
    if (midi_player != NULL) {
        fluid_player_stop(midi_player);
        delete_fluid_player(midi_player);
        midi_player = NULL;
    }
    if (midi_synth != NULL) {
        delete_fluid_synth(midi_synth);
        midi_synth = NULL;
    }
    if (midi_settings != NULL) {
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
    }
    midi_sfont_id = -1;
    midi_total_ticks = 0;
    midi_output_rate = 0;
    midi_channels = 0;
}

static bool open_midi(const wchar_t *path)
{
    const char *sfont_path;
    char *utf8_path;
    memset(&decoder_state, 0, sizeof(decoder_state));
    midi_mode = false;
    midi_synth = NULL;
    midi_player = NULL;
    midi_settings = NULL;
    midi_sfont_id = -1;
    midi_total_ticks = 0;
    sfont_path = find_soundfont();
    if (sfont_path == NULL) return false;
    midi_settings = new_fluid_settings();
    if (midi_settings == NULL) return false;
    midi_synth = new_fluid_synth(midi_settings);
    if (midi_synth == NULL) {
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
        return false;
    }
    fluid_synth_set_gain(midi_synth, 1.0f);
    midi_sfont_id = fluid_synth_sfload(midi_synth, sfont_path, 1);
    if (midi_sfont_id < 0) {
        delete_fluid_synth(midi_synth);
        midi_synth = NULL;
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
        return false;
    }
    midi_player = new_fluid_player(midi_synth);
    if (midi_player == NULL) {
        fluid_synth_sfunload(midi_synth, midi_sfont_id, 1);
        delete_fluid_synth(midi_synth);
        midi_synth = NULL;
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
        return false;
    }
    utf8_path = utf8_encode(path);
    if (utf8_path == NULL) {
        delete_fluid_player(midi_player);
        midi_player = NULL;
        fluid_synth_sfunload(midi_synth, midi_sfont_id, 1);
        delete_fluid_synth(midi_synth);
        midi_synth = NULL;
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
        return false;
    }
    if (fluid_player_add(midi_player, utf8_path) != FLUID_OK) {
        free(utf8_path);
        delete_fluid_player(midi_player);
        midi_player = NULL;
        fluid_synth_sfunload(midi_synth, midi_sfont_id, 1);
        delete_fluid_synth(midi_synth);
        midi_synth = NULL;
        delete_fluid_settings(midi_settings);
        midi_settings = NULL;
        return false;
    }
    free(utf8_path);
    midi_output_rate = 44100;
    midi_channels = 2;
    midi_mode = true;
    return true;
}

static void *midi_decoder_main(void *arg)
{
    (void)arg;
    int buffer_samples;
    int16_t *buffer;
    bool read_error = false;
    if (midi_synth == NULL || midi_player == NULL) goto finish;
    if (fluid_player_play(midi_player) != FLUID_OK) {
        read_error = true;
        goto finish;
    }
    buffer_samples = midi_output_rate * midi_channels / 4;
    buffer = (int16_t *)malloc((size_t)buffer_samples * sizeof(*buffer));
    if (buffer == NULL) {
        read_error = true;
        goto finish;
    }
    while (!stop_requested) {
        int status;
        int written;
        int error;
        int samples_this;
        pthread_mutex_lock(&state_mutex);
        while (!stop_requested && pause_requested)
            pthread_cond_wait(&state_cond, &state_mutex);
        if (seek_requested) {
            int total = fluid_player_get_total_ticks(midi_player);
            int target_tick = (int)((int64_t)requested_seek_ms *
                                    total / (int64_t)track_duration_ms);
            if (target_tick > total) target_tick = total;
            fluid_player_seek(midi_player, target_tick);
            frames_written = (int64_t)requested_seek_ms *
                             midi_output_rate / 1000;
            seek_requested = false;
        }
        pthread_mutex_unlock(&state_mutex);
        if (stop_requested) break;
        status = fluid_player_get_status(midi_player);
        if (status == FLUID_PLAYER_DONE) break;
        samples_this = buffer_samples / midi_channels;
        written = fluid_synth_write_s16(midi_synth, samples_this,
                                        buffer, 0, midi_channels,
                                        buffer, 1, midi_channels);
        if (written != FLUID_OK) {
            read_error = true;
            break;
        }
        apply_volume(buffer, (size_t)buffer_samples, volume_gain);
        if (pa_simple_write(pulse_stream.handle,
                            (const void *)buffer,
                            (size_t)buffer_samples * 2u,
                            &error) < 0) {
            read_error = true;
            break;
        }
        pthread_mutex_lock(&state_mutex);
        frames_written += samples_this;
        pthread_mutex_unlock(&state_mutex);
    }
    free(buffer);
finish:
    if (!stop_requested && !read_error && pulse_stream.handle != NULL) {
        pa_simple_drain(pulse_stream.handle, NULL);
    }
    close_pulse(&pulse_stream);
    close_midi();
    pthread_mutex_lock(&state_mutex);
    if (!stop_requested && !read_error) finished_naturally = true;
    thread_running = false;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
    return NULL;
}

static bool playback_play_midi(const wchar_t *path)
{
    if (!open_midi(path)) return false;
    if (!open_pulse(&pulse_stream, midi_output_rate, midi_channels)) {
        close_midi();
        return false;
    }
    pthread_mutex_lock(&state_mutex);
    midi_total_ticks = fluid_player_get_total_ticks(midi_player);
    if (midi_total_ticks > 0)
        track_duration_ms = (unsigned long)midi_total_ticks;
    else
        track_duration_ms = 0;
    frames_written = 0;
    stop_requested = false;
    pause_requested = false;
    seek_requested = false;
    finished_naturally = false;
    requested_seek_ms = 0;
    if (pthread_create(&decoder_thread, NULL, midi_decoder_main, NULL) != 0) {
        pthread_mutex_unlock(&state_mutex);
        close_pulse(&pulse_stream);
        close_midi();
        return false;
    }
    thread_running = true;
    pthread_mutex_unlock(&state_mutex);
    return true;
}
#endif

bool playback_play(const wchar_t *path)
{
    int result;
    if (path == NULL || path[0] == L'\0') return false;
    playback_stop();
#ifdef HAS_FLUIDSYNTH
    if (is_midi_path(path)) {
        return playback_play_midi(path);
    }
#endif
    if (!open_decoder(path, &decoder_state)) return false;
    if (!open_pulse(&pulse_stream, decoder_state.output_rate,
                    decoder_state.channels)) {
        close_decoder(&decoder_state);
        return false;
    }
    {
        AVChannelLayout output_layout;
        av_channel_layout_default(&output_layout, decoder_state.channels);
        result = swr_alloc_set_opts2(
            &decoder_state.swr, &output_layout, AV_SAMPLE_FMT_S16,
            decoder_state.output_rate, &decoder_state.codec->ch_layout,
            decoder_state.source_format, decoder_state.source_rate, 0, NULL);
        if (result < 0 ||
            swr_init(decoder_state.swr) < 0) {
            av_channel_layout_uninit(&output_layout);
            close_pulse(&pulse_stream);
            close_decoder(&decoder_state);
            return false;
        }
        av_channel_layout_uninit(&output_layout);
    }
    pthread_mutex_lock(&state_mutex);
    track_duration_ms = track_duration_ms_from(&decoder_state);
    frames_written = 0;
    stop_requested = false;
    pause_requested = false;
    seek_requested = false;
    finished_naturally = false;
    requested_seek_ms = 0;
    if (pthread_create(&decoder_thread, NULL, decoder_main,
                       &decoder_state) != 0) {
        pthread_mutex_unlock(&state_mutex);
        close_pulse(&pulse_stream);
        close_decoder(&decoder_state);
        return false;
    }
    thread_running = true;
    pthread_mutex_unlock(&state_mutex);
    return true;
}

void playback_stop(void)
{
    pthread_mutex_lock(&state_mutex);
    if (!thread_running) {
        pthread_mutex_unlock(&state_mutex);
        if (pulse_stream.handle != NULL) close_pulse(&pulse_stream);
#ifdef HAS_FLUIDSYNTH
        if (midi_mode) {
            close_midi();
            midi_mode = false;
        } else
#endif
        if (decoder_state.format != NULL) close_decoder(&decoder_state);
        return;
    }
    stop_requested = true;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
    pthread_join(decoder_thread, NULL);
}

bool playback_is_active(void)
{
    bool active;
    pthread_mutex_lock(&state_mutex);
    active = thread_running;
    pthread_mutex_unlock(&state_mutex);
    return active;
}

bool playback_was_finished(void)
{
    bool finished;
    pthread_mutex_lock(&state_mutex);
    finished = finished_naturally;
    finished_naturally = false;
    pthread_mutex_unlock(&state_mutex);
    return finished;
}

unsigned long playback_position_ms(void)
{
    unsigned long position;
    int rate;
    pthread_mutex_lock(&state_mutex);
    rate = decoder_state.output_rate;
#ifdef HAS_FLUIDSYNTH
    if (midi_mode) rate = midi_output_rate;
#endif
    if (rate > 0)
        position = (unsigned long)(frames_written * 1000 /
                                   (int64_t)rate);
    else
        position = 0;
    pthread_mutex_unlock(&state_mutex);
    return position;
}

unsigned long playback_duration_ms(void)
{
    unsigned long duration;
    pthread_mutex_lock(&state_mutex);
    duration = track_duration_ms;
    pthread_mutex_unlock(&state_mutex);
    return duration;
}

bool playback_seek_ms(unsigned long position_ms)
{
    pthread_mutex_lock(&state_mutex);
    if (!thread_running) {
        pthread_mutex_unlock(&state_mutex);
        return false;
    }
    requested_seek_ms = position_ms;
    seek_requested = true;
    pthread_cond_signal(&state_cond);
    pthread_mutex_unlock(&state_mutex);
    return true;
}

void playback_set_volume(int percent)
{
    double gain;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    gain = (double)percent / 100.0;
    pthread_mutex_lock(&state_mutex);
    volume_gain = gain;
    pthread_mutex_unlock(&state_mutex);
}
