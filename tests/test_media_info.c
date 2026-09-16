#define _XOPEN_SOURCE 700

#include "media_info.h"

#include "platform.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#define SAMPLE_RATE 44100
#define DURATION_SECONDS 1

static int write_wav(const char *utf8_path)
{
    FILE *file;
    unsigned int data_size = SAMPLE_RATE * 2;
    unsigned int file_size = 36 + data_size;
    unsigned char header[44];
    unsigned int sample;
    unsigned int index;
    memset(header, 0, sizeof(header));
    memcpy(header, "RIFF", 4);
    header[4] = (unsigned char)(file_size & 0xFF);
    header[5] = (unsigned char)((file_size >> 8) & 0xFF);
    header[6] = (unsigned char)((file_size >> 16) & 0xFF);
    header[7] = (unsigned char)((file_size >> 24) & 0xFF);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[20] = 1;
    header[22] = 1;
    header[24] = (unsigned char)(SAMPLE_RATE & 0xFF);
    header[25] = (unsigned char)((SAMPLE_RATE >> 8) & 0xFF);
    header[26] = (unsigned char)((SAMPLE_RATE >> 16) & 0xFF);
    header[27] = (unsigned char)((SAMPLE_RATE >> 24) & 0xFF);
    header[28] = 88;
    header[29] = 2;
    header[30] = 2;
    header[32] = 2;
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    header[40] = (unsigned char)(data_size & 0xFF);
    header[41] = (unsigned char)((data_size >> 8) & 0xFF);
    header[42] = (unsigned char)((data_size >> 16) & 0xFF);
    header[43] = (unsigned char)((data_size >> 24) & 0xFF);
    file = fopen(utf8_path, "wb");
    if (file == NULL) return 0;
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }
    for (index = 0; index < SAMPLE_RATE; ++index) {
        sample = 440 * index * 2 % SAMPLE_RATE;
        {
            unsigned char low = (unsigned char)(sample & 0xFF);
            unsigned char high = (unsigned char)((sample >> 8) & 0xFF);
            if (fwrite(&low, 1, 1, file) != 1 ||
                fwrite(&high, 1, 1, file) != 1) {
                fclose(file);
                return 0;
            }
        }
    }
    fclose(file);
    return 1;
}

int main(void)
{
    char template[] = "/tmp/ac_media_XXXXXX";
    char *dir = mkdtemp(template);
    char wav_utf8[512];
    wchar_t wav_wide[512];
    unsigned long duration;
    MediaInfo info;
    int failures = 0;
    if (dir == NULL) return 2;
    snprintf(wav_utf8, sizeof(wav_utf8), "%s/tone.wav", dir);
    if (!write_wav(wav_utf8)) return 3;
    if (!utf8_to_buffer(wav_utf8, wav_wide, ARRAYSIZE(wav_wide))) return 4;

    duration = media_duration_ms(wav_wide);
    if (duration < 950 || duration > 1050) {
        printf("FAIL: duration %lu ms, expected ~1000\n", duration);
        failures++;
    }
    if (media_duration_ms(wav_wide) != duration) {
        printf("FAIL: cache changed duration %lu -> %lu\n",
               duration, media_duration_ms(wav_wide));
        failures++;
    }
    memset(&info, 0, sizeof(info));
    media_read_info(wav_wide, &info);
    if (info.sample_rate_hz != SAMPLE_RATE) {
        printf("FAIL: sample rate %lu, expected %d\n",
               info.sample_rate_hz, SAMPLE_RATE);
        failures++;
    }
    if (info.channels != 1) {
        printf("FAIL: channels %lu, expected 1\n", info.channels);
        failures++;
    }
    if (info.bit_depth != 16) {
        printf("FAIL: bit depth %lu, expected 16\n", info.bit_depth);
        failures++;
    }
    if (info.container[0] == L'\0') {
        printf("FAIL: empty container name\n");
        failures++;
    }
    if (info.duration_ms < 950 || info.duration_ms > 1050) {
        printf("FAIL: info duration %lu ms, expected ~1000\n",
               info.duration_ms);
        failures++;
    }
    {
        wchar_t missing[512];
        snprintf(wav_utf8, sizeof(wav_utf8), "%s/nope.mp3", dir);
        utf8_to_buffer(wav_utf8, missing, ARRAYSIZE(missing));
        if (media_duration_ms(missing) != 0) {
            printf("FAIL: missing file returned nonzero duration\n");
            failures++;
        }
    }
    snprintf(wav_utf8, sizeof(wav_utf8), "%s", dir);
    {
        char wav_absolute[512];
        snprintf(wav_absolute, sizeof(wav_absolute), "%s/tone.wav", dir);
        unlink(wav_absolute);
        rmdir(dir);
    }
    if (failures != 0) return 1;
    printf("wav duration, metadata, and missing-file handling passed\n");
    return 0;
}
