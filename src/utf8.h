#ifndef AUDIOCOMMANDER_UTF8_H
#define AUDIOCOMMANDER_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

char *utf8_encode(const wchar_t *text);
wchar_t *utf8_decode(const char *text, size_t length);
bool utf8_to_buffer(const char *text, wchar_t *output, size_t output_count);
char *utf8_from_wchar_buffer(const wchar_t *text);

#endif
