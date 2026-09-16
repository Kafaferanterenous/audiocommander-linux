#ifndef AUDIOCOMMANDER_PLATFORM_H
#define AUDIOCOMMANDER_PLATFORM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#include "ini.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define _countof(a) ARRAYSIZE(a)

typedef int BOOL;
#define TRUE 1
#define FALSE 0

#define MAX_PATH 4096
#define MAXDWORD ((unsigned int)0xFFFFFFFFu)
#define CW_USEDEFAULT ((int)0x80000000)
#define LF_FACESIZE 32
#define CLR_INVALID ((COLORREF)0xFFFFFFFFu)

typedef unsigned long DWORD;
typedef void *HWND;
typedef void *HFONT;

typedef unsigned long COLORREF;
#define RGB(r, g, b) \
    ((COLORREF)((((unsigned char)(b)) << 16) | (((unsigned char)(g)) << 8) | \
                ((unsigned char)(r))))

int wcsicasecmp(const wchar_t *left, const wchar_t *right);
#define _wcsicmp wcsicasecmp

static inline unsigned long shim_gpps(const wchar_t *section, const wchar_t *key,
                                      const wchar_t *default_value, wchar_t *value,
                                      size_t count, const wchar_t *path)
{
    if (!ini_get_string(path, section, key, value, count)) {
        if (default_value != NULL) {
            size_t length = wcslen(default_value);
            if (length + 1 > count) length = count - 1;
            wmemcpy(value, default_value, length);
            value[length] = L'\0';
        } else if (count > 0) {
            value[0] = L'\0';
        }
        return 0;
    }
    return (unsigned long)wcslen(value);
}
#define GetPrivateProfileStringW(section, key, def, value, count, path) \
    shim_gpps((section), (key), (def), (value), (count), (path))

static inline int shim_gppi(const wchar_t *section, const wchar_t *key,
                            int default_value, const wchar_t *path)
{
    return ini_get_int(path, section, key, default_value);
}
#define GetPrivateProfileIntW(section, key, def, path) \
    shim_gppi((section), (key), (def), (path))

static inline BOOL shim_wpps(const wchar_t *section, const wchar_t *key,
                             const wchar_t *value, const wchar_t *path)
{
    if (section == NULL || key == NULL) return TRUE;
    if (value == NULL) return ini_remove_key(path, section, key) ? TRUE : FALSE;
    return ini_set_string(path, section, key, value) ? TRUE : FALSE;
}
#define WritePrivateProfileStringW(section, key, value, path) \
    shim_wpps((section), (key), (value), (path))

static inline int swprintf_s(wchar_t *buffer, size_t count,
                             const wchar_t *format, ...)
{
    va_list arguments;
    int written;
    if (buffer == NULL || count == 0) return -1;
    va_start(arguments, format);
    written = vswprintf(buffer, count, format, arguments);
    va_end(arguments);
    return written < 0 ? -1 : written;
}

static inline int swscanf_s(const wchar_t *buffer, const wchar_t *format, ...)
{
    va_list arguments;
    int converted;
    va_start(arguments, format);
    converted = vswscanf(buffer, format, arguments);
    va_end(arguments);
    return converted;
}

static inline int wcscpy_s(wchar_t *destination, size_t count,
                           const wchar_t *source)
{
    size_t length;
    if (destination == NULL || source == NULL || count == 0) return -1;
    length = wcslen(source);
    if (length + 1 > count) return -1;
    wmemcpy(destination, source, length + 1);
    return 0;
}

#endif
