#include "media_info.h"

#include "platform.h"
#include "utf8.h"

#include <libavformat/avformat.h>

#include <string.h>
#include <stdlib.h>

#define MEDIA_INFO_CACHE_CAPACITY 256

typedef struct MediaDurationCacheEntry {
    char path[512];
    unsigned long duration_ms;
} MediaDurationCacheEntry;

static MediaDurationCacheEntry duration_cache[MEDIA_INFO_CACHE_CAPACITY];
static size_t duration_cache_count;
static size_t duration_cache_cursor;

static bool cache_lookup(const char *path, unsigned long *duration_ms)
{
    size_t index;
    if (path == NULL || duration_ms == NULL) return false;
    for (index = 0; index < duration_cache_count; ++index) {
        if (strcmp(duration_cache[index].path, path) == 0) {
            *duration_ms = duration_cache[index].duration_ms;
            return true;
        }
    }
    return false;
}

static void cache_store(const char *path, unsigned long duration_ms)
{
    if (path == NULL) return;
    if (duration_cache_count < MEDIA_INFO_CACHE_CAPACITY) {
        size_t index = duration_cache_count++;
        strncpy(duration_cache[index].path, path,
                sizeof(duration_cache[index].path) - 1);
        duration_cache[index].path[sizeof(duration_cache[index].path) - 1] = '\0';
        duration_cache[index].duration_ms = duration_ms;
        return;
    }
    strncpy(duration_cache[duration_cache_cursor].path, path,
            sizeof(duration_cache[duration_cache_cursor].path) - 1);
    duration_cache[duration_cache_cursor].path[
        sizeof(duration_cache[duration_cache_cursor].path) - 1] = '\0';
    duration_cache[duration_cache_cursor].duration_ms = duration_ms;
    duration_cache_cursor = (duration_cache_cursor + 1) % MEDIA_INFO_CACHE_CAPACITY;
}

static AVFormatContext *open_format(const wchar_t *path)
{
    AVFormatContext *format = NULL;
    char *utf8_path;
    int result;
    if (path == NULL || path[0] == L'\0') return NULL;
    utf8_path = utf8_encode(path);
    if (utf8_path == NULL) return NULL;
    result = avformat_open_input(&format, utf8_path, NULL, NULL);
    free(utf8_path);
    if (result < 0) return NULL;
    if (avformat_find_stream_info(format, NULL) < 0) {
        avformat_close_input(&format);
        return NULL;
    }
    return format;
}

static int find_best_audio_stream(AVFormatContext *format)
{
    int index;
    for (index = 0; index < (int)format->nb_streams; ++index) {
        const AVStream *stream = format->streams[index];
        if (stream->codecpar != NULL &&
            stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return index;
        }
    }
    return -1;
}

static unsigned long stream_duration_ms(const AVStream *stream)
{
    int64_t duration = stream->duration;
    if (duration == AV_NOPTS_VALUE) duration = 0;
    if (duration > 0 && stream->time_base.num > 0 && stream->time_base.den > 0) {
        return (unsigned long)(duration * 1000 * stream->time_base.num /
                               stream->time_base.den);
    }
    if (duration > 0 && stream->time_base.den > 0) {
        return (unsigned long)(duration * 1000 / stream->time_base.den);
    }
    return 0;
}

unsigned long media_duration_ms(const wchar_t *path)
{
    AVFormatContext *format;
    unsigned long duration_ms;
    char *utf8_path;
    int audio_index;
    if (path == NULL) return 0;
    utf8_path = utf8_encode(path);
    if (utf8_path == NULL) return 0;
    if (cache_lookup(utf8_path, &duration_ms)) {
        free(utf8_path);
        return duration_ms;
    }
    format = open_format(path);
    if (format == NULL) {
        free(utf8_path);
        return 0;
    }
    duration_ms = 0;
    audio_index = find_best_audio_stream(format);
    if (audio_index >= 0) {
        duration_ms = stream_duration_ms(format->streams[audio_index]);
    }
    if (duration_ms == 0 && format->duration != AV_NOPTS_VALUE &&
        format->duration > 0) {
        duration_ms = (unsigned long)(format->duration / 1000);
    }
    avformat_close_input(&format);
    if (duration_ms > 0) cache_store(utf8_path, duration_ms);
    free(utf8_path);
    return duration_ms;
}

static void copy_metadata_string(AVDictionary *metadata, const char *key,
                                 wchar_t *out, size_t out_count)
{
    AVDictionaryEntry *entry;
    if (out == NULL || out_count == 0) return;
    out[0] = L'\0';
    if (metadata == NULL) return;
    entry = av_dict_get(metadata, key, NULL, 0);
    if (entry != NULL && entry->value != NULL) {
        wchar_t *decoded = utf8_decode(entry->value, strlen(entry->value));
        if (decoded != NULL) {
            wcsncpy(out, decoded, out_count - 1);
            out[out_count - 1] = L'\0';
            free(decoded);
        }
    }
}

static unsigned long codec_bits_per_sample(const AVCodecParameters *params)
{
    if (params == NULL) return 0;
    if (params->bits_per_raw_sample > 0) return (unsigned long)params->bits_per_raw_sample;
    if (params->bits_per_coded_sample > 0) return (unsigned long)params->bits_per_coded_sample;
    if (params->format >= 0) return 16;
    return 0;
}

void media_read_info(const wchar_t *path, MediaInfo *info)
{
    AVFormatContext *format;
    AVStream *stream;
    const AVCodec *codec;
    int audio_index;
    if (info == NULL) return;
    memset(info, 0, sizeof(*info));
    if (path == NULL) return;
    format = open_format(path);
    if (format == NULL) return;
    audio_index = find_best_audio_stream(format);
    if (audio_index >= 0) {
        stream = format->streams[audio_index];
        info->duration_ms = stream_duration_ms(stream);
        if (info->duration_ms == 0 && format->duration != AV_NOPTS_VALUE &&
            format->duration > 0) {
            info->duration_ms = (unsigned long)(format->duration / 1000);
        }
        info->bitrate_kbps = stream->codecpar != NULL &&
            stream->codecpar->bit_rate > 0 ?
                (unsigned long)(stream->codecpar->bit_rate / 1000) : 0;
        if (info->bitrate_kbps == 0 && format->bit_rate > 0)
            info->bitrate_kbps = (unsigned long)(format->bit_rate / 1000);
        if (stream->codecpar != NULL) {
            info->sample_rate_hz = (unsigned long)stream->codecpar->sample_rate;
            info->channels = (unsigned long)stream->codecpar->ch_layout.nb_channels;
            info->bit_depth = codec_bits_per_sample(stream->codecpar);
            if (stream->codecpar->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
                char layout[128];
                av_channel_layout_describe(&stream->codecpar->ch_layout,
                                           layout, sizeof(layout));
                utf8_to_buffer(layout, info->channel_layout,
                               ARRAYSIZE(info->channel_layout));
            }
            codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec != NULL) {
                utf8_to_buffer(codec->name, info->codec,
                               ARRAYSIZE(info->codec));
            }
        }
    }
    if (format->iformat != NULL && format->iformat->name != NULL)
        utf8_to_buffer(format->iformat->name, info->container,
                       ARRAYSIZE(info->container));
    copy_metadata_string(format->metadata, "title", info->title,
                         ARRAYSIZE(info->title));
    copy_metadata_string(format->metadata, "artist", info->artist,
                         ARRAYSIZE(info->artist));
    copy_metadata_string(format->metadata, "album", info->album,
                         ARRAYSIZE(info->album));
    avformat_close_input(&format);
}
