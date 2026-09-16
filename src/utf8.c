#include "utf8.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

char *utf8_encode(const wchar_t *text)
{
    size_t input_length = wcslen(text);
    size_t output_length = 0;
    size_t index;
    size_t used = 0;
    char *buffer;
    for (index = 0; index < input_length; ++index) {
        if (text[index] < 0x80) {
            output_length += 1;
        } else if (text[index] < 0x800) {
            output_length += 2;
        } else if (text[index] < 0x10000) {
            output_length += 3;
        } else {
            output_length += 4;
        }
    }
    buffer = (char *)malloc(output_length + 1);
    if (buffer == NULL) return NULL;
    for (index = 0; index < input_length; ++index) {
        char encoded[4];
        size_t length;
        unsigned int code_point = (unsigned int)text[index];
        if (code_point < 0x80) {
            encoded[0] = (char)code_point;
            length = 1;
        } else if (code_point < 0x800) {
            encoded[0] = (char)(0xC0 | (code_point >> 6));
            encoded[1] = (char)(0x80 | (code_point & 0x3F));
            length = 2;
        } else if (code_point < 0x10000) {
            encoded[0] = (char)(0xE0 | (code_point >> 12));
            encoded[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
            encoded[2] = (char)(0x80 | (code_point & 0x3F));
            length = 3;
        } else {
            encoded[0] = (char)(0xF0 | (code_point >> 18));
            encoded[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
            encoded[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
            encoded[3] = (char)(0x80 | (code_point & 0x3F));
            length = 4;
        }
        memcpy(buffer + used, encoded, length);
        used += length;
    }
    buffer[used] = '\0';
    return buffer;
}

wchar_t *utf8_decode(const char *text, size_t length)
{
    size_t index = 0;
    size_t used = 0;
    wchar_t *buffer = (wchar_t *)malloc((length + 1) * sizeof(*buffer));
    if (buffer == NULL) return NULL;
    if (length >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        index = 3;
    }
    while (index < length) {
        unsigned char first = (unsigned char)text[index++];
        unsigned int code_point;
        if (first < 0x80) {
            code_point = first;
        } else if ((first & 0xE0) == 0xC0 && index < length) {
            code_point = ((unsigned int)(first & 0x1F) << 6) |
                         ((unsigned char)text[index++] & 0x3F);
        } else if ((first & 0xF0) == 0xE0 && index + 1 < length) {
            unsigned char second = (unsigned char)text[index];
            unsigned char third = (unsigned char)text[index + 1];
            code_point = ((unsigned int)(first & 0x0F) << 12) |
                         ((unsigned int)(second & 0x3F) << 6) |
                         (unsigned int)(third & 0x3F);
            index += 2;
        } else if ((first & 0xF8) == 0xF0 && index + 2 < length) {
            unsigned char second = (unsigned char)text[index];
            unsigned char third = (unsigned char)text[index + 1];
            unsigned char fourth = (unsigned char)text[index + 2];
            code_point = ((unsigned int)(first & 0x07) << 18) |
                         ((unsigned int)(second & 0x3F) << 12) |
                         ((unsigned int)(third & 0x3F) << 6) |
                         (unsigned int)(fourth & 0x3F);
            index += 3;
        } else {
            code_point = 0xFFFD;
        }
        buffer[used++] = (wchar_t)code_point;
    }
    buffer[used] = L'\0';
    return buffer;
}

bool utf8_to_buffer(const char *text, wchar_t *output, size_t output_count)
{
    wchar_t *decoded;
    size_t length;
    if (output == NULL || output_count == 0) return false;
    decoded = utf8_decode(text, strlen(text));
    if (decoded == NULL) return false;
    length = wcslen(decoded);
    if (length + 1 > output_count) {
        free(decoded);
        return false;
    }
    wcscpy(output, decoded);
    free(decoded);
    return true;
}

char *utf8_from_wchar_buffer(const wchar_t *text)
{
    return utf8_encode(text);
}
