#ifndef AUDIOCOMMANDER_INI_H
#define AUDIOCOMMANDER_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

bool ini_get_string(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key, wchar_t *value, size_t value_count);
int ini_get_int(const wchar_t *path, const wchar_t *section,
                const wchar_t *key, int default_value);
bool ini_set_string(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key, const wchar_t *value);
bool ini_remove_key(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key);

#endif
