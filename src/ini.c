#include "ini.h"

#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

int wcsicasecmp(const wchar_t *left, const wchar_t *right)
{
    while (*left != L'\0' && *right != L'\0') {
        wchar_t a = *left, b = *right;
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return a < b ? -1 : 1;
        ++left;
        ++right;
    }
    if (*left == *right) return 0;
    return *left == L'\0' ? -1 : 1;
}

static wchar_t *wcsdup_text(const wchar_t *text)
{
    size_t count = wcslen(text) + 1;
    wchar_t *copy = (wchar_t *)malloc(count * sizeof(*copy));
    if (copy != NULL) memcpy(copy, text, count * sizeof(*copy));
    return copy;
}

typedef struct IniLines {
    wchar_t **lines;
    size_t count;
    size_t capacity;
} IniLines;

static void lines_free(IniLines *lines)
{
    size_t index;
    for (index = 0; index < lines->count; ++index) free(lines->lines[index]);
    free(lines->lines);
    memset(lines, 0, sizeof(*lines));
}

static bool lines_ensure(IniLines *lines)
{
    wchar_t **grown;
    size_t new_capacity;
    if (lines->count < lines->capacity) return true;
    new_capacity = lines->capacity == 0 ? 16 : lines->capacity * 2;
    grown = (wchar_t **)realloc(lines->lines, new_capacity * sizeof(*grown));
    if (grown == NULL) return false;
    lines->lines = grown;
    lines->capacity = new_capacity;
    return true;
}

static bool lines_append(IniLines *lines, const wchar_t *text)
{
    wchar_t *copy;
    if (!lines_ensure(lines)) return false;
    copy = wcsdup_text(text);
    if (copy == NULL) return false;
    lines->lines[lines->count++] = copy;
    return true;
}

static FILE *wopen_path(const wchar_t *path, const char *mode)
{
    char *utf8 = utf8_encode(path);
    FILE *file;
    if (utf8 == NULL) return NULL;
    file = fopen(utf8, mode);
    free(utf8);
    return file;
}

static bool read_lines(const wchar_t *path, IniLines *lines)
{
    FILE *file;
    unsigned char *data;
    long length;
    size_t index = 0;
    memset(lines, 0, sizeof(*lines));
    file = wopen_path(path, "rb");
    if (file == NULL) return true;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    data = (unsigned char *)malloc((size_t)length + 1);
    if (data == NULL) {
        fclose(file);
        return false;
    }
    if (length > 0 && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return false;
    }
    fclose(file);
    while (index < (size_t)length) {
        size_t start = index;
        size_t line_length;
        while (index < (size_t)length && data[index] != '\n') ++index;
        line_length = index - start;
        if (line_length > 0 && data[index - 1] == '\r') --line_length;
        if (line_length == 0) {
            if (!lines_append(lines, L"")) {
                free(data);
                lines_free(lines);
                return false;
            }
        } else {
            wchar_t *decoded = utf8_decode((const char *)data + start, line_length);
            if (decoded == NULL) {
                free(data);
                lines_free(lines);
                return false;
            }
            if (lines->count == lines->capacity) {
                if (!lines_ensure(lines)) {
                    free(decoded);
                    free(data);
                    lines_free(lines);
                    return false;
                }
            }
            lines->lines[lines->count++] = decoded;
        }
        if (index < (size_t)length && data[index] == '\n') ++index;
    }
    free(data);
    return true;
}

static bool write_lines(const wchar_t *path, const IniLines *lines)
{
    FILE *file;
    size_t index;
    file = wopen_path(path, "wb");
    if (file == NULL) return false;
    for (index = 0; index < lines->count; ++index) {
        char *encoded = utf8_encode(lines->lines[index]);
        size_t encoded_length;
        if (encoded == NULL) {
            fclose(file);
            return false;
        }
        encoded_length = strlen(encoded);
        if (fwrite(encoded, 1, encoded_length, file) != encoded_length ||
            fwrite("\n", 1, 1, file) != 1) {
            free(encoded);
            fclose(file);
            return false;
        }
        free(encoded);
    }
    if (fclose(file) != 0) return false;
    return true;
}

static const wchar_t *skip_leading_ws(const wchar_t *text)
{
    while (*text == L' ' || *text == L'\t') ++text;
    return text;
}

static bool keys_equal_region(const wchar_t *left, size_t left_length,
                              const wchar_t *right)
{
    size_t index;
    for (index = 0; index < left_length; ++index) {
        wchar_t a = left[index], b = right[index];
        if (b == L'\0') return false;
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return false;
    }
    return right[left_length] == L'\0';
}

static bool keys_equal(const wchar_t *left, const wchar_t *right)
{
    return keys_equal_region(left, wcslen(left), right);
}

static bool parse_section(const wchar_t *line, wchar_t *section, size_t count)
{
    const wchar_t *text;
    const wchar_t *end;
    size_t length;
    text = skip_leading_ws(line);
    if (*text != L'[') return false;
    end = wcschr(text, L']');
    if (end == NULL) return false;
    length = (size_t)(end - (text + 1));
    if (length + 1 > count) return false;
    wmemcpy(section, text + 1, length);
    section[length] = L'\0';
    return true;
}

static size_t trimmed_prefix_length(const wchar_t *text, const wchar_t *end)
{
    size_t length = (size_t)(end - text);
    while (length > 0 && (text[length - 1] == L' ' || text[length - 1] == L'\t'))
        --length;
    return length;
}

bool ini_get_string(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key, wchar_t *value, size_t value_count)
{
    IniLines lines;
    size_t index;
    bool in_section = false;
    if (path == NULL || section == NULL || key == NULL ||
        value == NULL || value_count == 0)
        return false;
    value[0] = L'\0';
    if (!read_lines(path, &lines)) return false;
    for (index = 0; index < lines.count; ++index) {
        wchar_t parsed_section[256];
        const wchar_t *text = skip_leading_ws(lines.lines[index]);
        if (parse_section(lines.lines[index], parsed_section, ARRAYSIZE(parsed_section))) {
            in_section = keys_equal(parsed_section, section);
            continue;
        }
        if (!in_section || *text == L'\0' || *text == L';' || *text == L'#')
            continue;
        {
            const wchar_t *equals = wcschr(text, L'=');
            const wchar_t *value_text;
            size_t value_length;
            if (equals == NULL) continue;
            if (trimmed_prefix_length(text, equals) == 0) continue;
            if (!keys_equal_region(text, trimmed_prefix_length(text, equals), key)) continue;
            value_text = skip_leading_ws(equals + 1);
            value_length = wcslen(value_text);
            while (value_length > 0 &&
                   (value_text[value_length - 1] == L' ' ||
                    value_text[value_length - 1] == L'\t'))
                --value_length;
            if (value_length + 1 > value_count) value_length = value_count - 1;
            wmemcpy(value, value_text, value_length);
            value[value_length] = L'\0';
            lines_free(&lines);
            return true;
        }
    }
    lines_free(&lines);
    return false;
}

int ini_get_int(const wchar_t *path, const wchar_t *section,
                const wchar_t *key, int default_value)
{
    wchar_t value[32];
    wchar_t *end = NULL;
    long parsed;
    if (!ini_get_string(path, section, key, value, ARRAYSIZE(value)))
        return default_value;
    parsed = wcstol(value, &end, 10);
    if (end == value || (*end != L'\0' && *end != L' ' && *end != L'\t'))
        return default_value;
    return (int)parsed;
}

static wchar_t *make_key_value_line(const wchar_t *key, const wchar_t *value)
{
    size_t length = wcslen(key) + 1 + wcslen(value);
    wchar_t *line = (wchar_t *)malloc((length + 1) * sizeof(*line));
    if (line == NULL) return NULL;
    swprintf(line, length + 1, L"%ls=%ls", key, value);
    return line;
}

static bool lines_insert(IniLines *lines, size_t position, wchar_t *line)
{
    size_t index;
    if (!lines_ensure(lines)) return false;
    if (position > lines->count) position = lines->count;
    for (index = lines->count; index > position; --index)
        lines->lines[index] = lines->lines[index - 1];
    lines->lines[position] = line;
    ++lines->count;
    return true;
}

static size_t find_section(const IniLines *lines, const wchar_t *section)
{
    size_t index;
    for (index = 0; index < lines->count; ++index) {
        wchar_t parsed_section[256];
        if (parse_section(lines->lines[index], parsed_section,
                          ARRAYSIZE(parsed_section)) &&
            keys_equal(parsed_section, section))
            return index;
    }
    return lines->count;
}

bool ini_set_string(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key, const wchar_t *value)
{
    IniLines lines;
    size_t section_line;
    if (path == NULL || section == NULL || key == NULL || value == NULL)
        return false;
    if (!read_lines(path, &lines)) return false;
    section_line = find_section(&lines, section);
    if (section_line != lines.count) {
        size_t probe = section_line + 1;
        while (probe < lines.count) {
            const wchar_t *probe_text;
            const wchar_t *equals;
            wchar_t parsed[256];
            if (parse_section(lines.lines[probe], parsed, ARRAYSIZE(parsed))) break;
            probe_text = skip_leading_ws(lines.lines[probe]);
            if (*probe_text != L'\0' && *probe_text != L';' && *probe_text != L'#' &&
                (equals = wcschr(probe_text, L'=')) != NULL &&
                keys_equal_region(probe_text, trimmed_prefix_length(probe_text, equals),
                                  key)) {
                wchar_t *line = make_key_value_line(key, value);
                if (line == NULL) {
                    lines_free(&lines);
                    return false;
                }
                free(lines.lines[probe]);
                lines.lines[probe] = line;
                if (!write_lines(path, &lines)) {
                    lines_free(&lines);
                    return false;
                }
                lines_free(&lines);
                return true;
            }
            ++probe;
        }
        {
            wchar_t *line = make_key_value_line(key, value);
            if (line == NULL) {
                lines_free(&lines);
                return false;
            }
            if (!lines_insert(&lines, section_line + 1, line)) {
                free(line);
                lines_free(&lines);
                return false;
            }
            if (!write_lines(path, &lines)) {
                lines_free(&lines);
                return false;
            }
            lines_free(&lines);
            return true;
        }
    }
    if (lines.count > 0 && lines.lines[lines.count - 1][0] != L'\0') {
        if (!lines_append(&lines, L"")) {
            lines_free(&lines);
            return false;
        }
    }
    {
        wchar_t *header;
        wchar_t *key_line;
        header = (wchar_t *)malloc((wcslen(section) + 3) * sizeof(*header));
        if (header == NULL) {
            lines_free(&lines);
            return false;
        }
        swprintf(header, wcslen(section) + 3, L"[%ls]", section);
        key_line = make_key_value_line(key, value);
        if (key_line == NULL) {
            free(header);
            lines_free(&lines);
            return false;
        }
        if (!lines_append(&lines, header)) {
            free(header);
            free(key_line);
            lines_free(&lines);
            return false;
        }
        if (!lines_append(&lines, key_line)) {
            free(key_line);
            lines_free(&lines);
            return false;
        }
        free(header);
        free(key_line);
    }
    if (!write_lines(path, &lines)) {
        lines_free(&lines);
        return false;
    }
    lines_free(&lines);
    return true;
}

bool ini_remove_key(const wchar_t *path, const wchar_t *section,
                    const wchar_t *key)
{
    IniLines lines;
    size_t section_line;
    size_t index;
    if (path == NULL || section == NULL || key == NULL) return false;
    if (!read_lines(path, &lines)) return true;
    section_line = find_section(&lines, section);
    if (section_line == lines.count) {
        lines_free(&lines);
        return true;
    }
    index = section_line + 1;
    while (index < lines.count) {
        const wchar_t *probe_text;
        const wchar_t *equals;
        wchar_t parsed[256];
        if (parse_section(lines.lines[index], parsed, ARRAYSIZE(parsed))) break;
        probe_text = skip_leading_ws(lines.lines[index]);
        if (*probe_text != L'\0' && *probe_text != L';' && *probe_text != L'#' &&
            (equals = wcschr(probe_text, L'=')) != NULL &&
            keys_equal_region(probe_text, trimmed_prefix_length(probe_text, equals),
                              key)) {
            free(lines.lines[index]);
            memmove(&lines.lines[index], &lines.lines[index + 1],
                    (lines.count - index - 1) * sizeof(*lines.lines));
            --lines.count;
        } else {
            ++index;
        }
    }
    if (!write_lines(path, &lines)) {
        lines_free(&lines);
        return false;
    }
    lines_free(&lines);
    return true;
}
