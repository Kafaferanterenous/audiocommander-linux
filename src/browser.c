#define _XOPEN_SOURCE 700

#include "browser.h"

#include "media_info.h"
#include "platform.h"
#include "utf8.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>
#include <wctype.h>

static void free_entries(BrowserEntry *entries, size_t count)
{
    size_t index;
    if (entries == NULL) return;
    for (index = 0; index < count; ++index) free(entries[index].name);
    free(entries);
}

bool browser_is_audio_name(const wchar_t *name)
{
    const wchar_t *dot = wcsrchr(name, L'.');
    return dot != NULL && (_wcsicmp(dot, L".wav") == 0 || _wcsicmp(dot, L".mp3") == 0 ||
                           _wcsicmp(dot, L".m4a") == 0 || _wcsicmp(dot, L".mp4") == 0 ||
                           _wcsicmp(dot, L".flac") == 0 || _wcsicmp(dot, L".wma") == 0 ||
                           _wcsicmp(dot, L".ogg") == 0 || _wcsicmp(dot, L".opus") == 0 ||
                           _wcsicmp(dot, L".mid") == 0 || _wcsicmp(dot, L".mod") == 0 ||
                           _wcsicmp(dot, L".s3m") == 0 || _wcsicmp(dot, L".xm") == 0);
}

static int compare_entries(const BrowserEntry *left, const BrowserEntry *right,
                           BrowserSortColumn column, bool descending)
{
    int result = 0;
    if (left->kind != right->kind) {
        return left->kind == BROWSER_ENTRY_DIRECTORY ? -1 : 1;
    }
    if (column == BROWSER_SORT_SIZE && left->kind == BROWSER_ENTRY_AUDIO_FILE) {
        result = left->size < right->size ? -1 : left->size > right->size ? 1 : 0;
    } else if (column == BROWSER_SORT_DURATION && left->kind == BROWSER_ENTRY_AUDIO_FILE) {
        if (left->duration_ms == 0 || right->duration_ms == 0) {
            if (left->duration_ms != right->duration_ms)
                return left->duration_ms == 0 ? 1 : -1;
        } else {
            result = left->duration_ms < right->duration_ms ? -1 :
                     left->duration_ms > right->duration_ms ? 1 : 0;
        }
    }
    if (result == 0) result = _wcsicmp(left->name, right->name);
    return descending ? -result : result;
}

void browser_listing_sort(BrowserListing *listing, BrowserSortColumn column, bool descending)
{
    size_t index;
    if (listing == NULL || listing->entries == NULL) return;
    for (index = 1; index < listing->count; ++index) {
        BrowserEntry entry = listing->entries[index];
        size_t destination = index;
        while (destination > 0 &&
               compare_entries(&entry, &listing->entries[destination - 1],
                               column, descending) < 0) {
            listing->entries[destination] = listing->entries[destination - 1];
            --destination;
        }
        listing->entries[destination] = entry;
    }
}

void browser_listing_summary(const BrowserListing *listing, BrowserListingSummary *summary)
{
    size_t index;
    if (summary == NULL) return;
    memset(summary, 0, sizeof(*summary));
    if (listing == NULL || listing->entries == NULL) return;
    for (index = 0; index < listing->count; ++index) {
        const BrowserEntry *entry = &listing->entries[index];
        if (entry->kind != BROWSER_ENTRY_AUDIO_FILE) continue;
        ++summary->audio_file_count;
        summary->total_size_bytes += entry->size;
        if (entry->duration_ms == 0)
            ++summary->unknown_duration_count;
        else
            summary->known_duration_ms += entry->duration_ms;
    }
}

bool browser_normalize_folder(const wchar_t *input, wchar_t *output, size_t output_count)
{
    char *utf8_input;
    char *canonical;
    wchar_t *wide;
    size_t length;
    if (input == NULL || input[0] == L'\0' || output == NULL || output_count == 0) {
        return false;
    }
    utf8_input = utf8_encode(input);
    if (utf8_input == NULL) return false;
    canonical = realpath(utf8_input, NULL);
    free(utf8_input);
    if (canonical == NULL) return false;
    wide = utf8_decode(canonical, strlen(canonical));
    free(canonical);
    if (wide == NULL) return false;
    length = wcslen(wide);
    while (length > 1 && (wide[length - 1] == L'/' || wide[length - 1] == L'\\')) {
        wide[--length] = L'\0';
    }
    if (length + 1 > output_count) {
        free(wide);
        return false;
    }
    wcscpy(output, wide);
    free(wide);
    return true;
}

static bool is_separator(wchar_t character)
{
    return character == L'/' || character == L'\\';
}

bool browser_parent_folder(const wchar_t *folder, wchar_t *output, size_t output_count)
{
    wchar_t *separator;
    if (!browser_normalize_folder(folder, output, output_count)) {
        return false;
    }
    if (wcslen(output) <= 1) {
        return true;
    }
    {
        wchar_t *slash = wcsrchr(output, L'/');
        wchar_t *backslash = wcsrchr(output, L'\\');
        separator = slash == NULL ? backslash :
                    backslash == NULL ? slash :
                    slash > backslash ? slash : backslash;
    }
    if (separator == NULL) {
        return false;
    }
    if (separator == output) {
        output[1] = L'\0';
    } else {
        *separator = L'\0';
    }
    return true;
}

bool browser_join_path(const wchar_t *folder, const wchar_t *name, wchar_t *output, size_t output_count)
{
    int written;
    size_t folder_length = folder == NULL ? 0 : wcslen(folder);
    if (folder == NULL || folder_length == 0 || name == NULL) return false;
    written = swprintf_s(output, output_count, L"%ls%ls%ls", folder,
                         is_separator(folder[folder_length - 1]) ? L"" : L"/", name);
    return written > 0 && (size_t)written < output_count;
}

bool browser_list_folder(const wchar_t *folder, BrowserListing *listing)
{
    char *utf8_folder;
    DIR *directory;
    struct dirent *entry;
    BrowserEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct stat stat_buffer;

    if (listing == NULL || folder == NULL || folder[0] == L'\0') {
        return false;
    }
    listing->entries = NULL;
    listing->count = 0;
    utf8_folder = utf8_encode(folder);
    if (utf8_folder == NULL) return false;
    directory = opendir(utf8_folder);
    if (directory == NULL) {
        free(utf8_folder);
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        BrowserEntry browser_entry;
        wchar_t *wide_name;
        char *full_path;
        size_t name_length;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            entry->d_name[0] == '.') {
            continue;
        }
        memset(&browser_entry, 0, sizeof(browser_entry));
        name_length = strlen(entry->d_name);
        wide_name = utf8_decode(entry->d_name, name_length);
        if (wide_name == NULL) {
            free_entries(entries, count);
            closedir(directory);
            free(utf8_folder);
            return false;
        }
        full_path = (char *)malloc(strlen(utf8_folder) + name_length + 2);
        if (full_path == NULL) {
            free(wide_name);
            free_entries(entries, count);
            closedir(directory);
            free(utf8_folder);
            return false;
        }
        snprintf(full_path, strlen(utf8_folder) + name_length + 2,
                 "%s/%s", utf8_folder, entry->d_name);        if (stat(full_path, &stat_buffer) != 0) {
            free(full_path);
            free(wide_name);
            free_entries(entries, count);
            closedir(directory);
            free(utf8_folder);
            return false;
        }
        if (S_ISDIR(stat_buffer.st_mode)) {
            browser_entry.kind = BROWSER_ENTRY_DIRECTORY;
        } else if (browser_is_audio_name(wide_name)) {
            browser_entry.kind = BROWSER_ENTRY_AUDIO_FILE;
        } else {
            free(full_path);
            free(wide_name);
            continue;
        }
        browser_entry.name = wide_name;
        browser_entry.size = (unsigned long long)stat_buffer.st_size;
        browser_entry.duration_ms = 0;
        if (browser_entry.kind == BROWSER_ENTRY_AUDIO_FILE) {
            wchar_t full_wide[MAX_PATH];
            if (utf8_to_buffer(full_path, full_wide, ARRAYSIZE(full_wide)))
                browser_entry.duration_ms = media_duration_ms(full_wide);
        }
        free(full_path);
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 32 : capacity * 2;
            BrowserEntry *grown = (BrowserEntry *)realloc(entries, new_capacity * sizeof(*grown));
            if (grown == NULL) {
                free(browser_entry.name);
                free_entries(entries, count);
                closedir(directory);
                free(utf8_folder);
                return false;
            }
            entries = grown;
            capacity = new_capacity;
        }
        entries[count++] = browser_entry;
    }
    closedir(directory);
    free(utf8_folder);
    listing->entries = entries;
    listing->count = count;
    browser_listing_sort(listing, BROWSER_SORT_NAME, false);
    return true;
}

void browser_listing_free(BrowserListing *listing)
{
    if (listing == NULL) {
        return;
    }
    free_entries(listing->entries, listing->count);
    listing->entries = NULL;
    listing->count = 0;
}
