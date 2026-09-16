#define _XOPEN_SOURCE 700

#include "settings.h"

#include "utf8.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

typedef struct ExternalThemeDefinition {
    const wchar_t *name;
    const wchar_t *file_name;
} ExternalThemeDefinition;

static const ExternalThemeDefinition external_themes[] = {
    {L"Acryl", L"Acryl.skn"},
    {L"Air", L"Air.skn"},
    {L"MetroUI", L"MetroUI.skn"},
    {L"Office 2003", L"Office2003.skn"},
    {L"Office 2007 Black", L"Office2007 Black.skn"},
    {L"XP Luna", L"XPLuna.skn"},
    {L"XP Silver", L"XPSilver.skn"},
    {L"Zest", L"Zest.skn"}
};

static ThemeColors pastel_colors(void)
{
    return (ThemeColors){
        RGB(235,233,248), RGB(255,255,255), RGB(35,32,48),
        RGB(115,135,210), RGB(255,255,255), RGB(180,178,198),
        RGB(90,115,195), RGB(215,220,242), RGB(198,205,232),
        RGB(205,212,240), RGB(35,32,48), RGB(105,130,210)};
}

static bool settings_is_external(AppTheme theme)
{
    return theme >= APP_THEME_ACRYL && theme < APP_THEME_DARK;
}

bool settings_theme_is_external(AppTheme theme)
{
    return settings_is_external(theme);
}

static const ExternalThemeDefinition *external_theme_definition(AppTheme theme)
{
    size_t index;
    if (!settings_is_external(theme)) return NULL;
    index = (size_t)(theme - APP_THEME_ACRYL);
    return index < ARRAYSIZE(external_themes) ? &external_themes[index] : NULL;
}

static bool env_wchar_path(const char *name, wchar_t *output, size_t count)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') return false;
    return utf8_to_buffer(value, output, count);
}

static bool append_single_component(wchar_t *buffer, size_t count,
                                    const char *component)
{
    size_t length = wcslen(buffer);
    size_t component_length = strlen(component);
    if (length + 1 + component_length + 1 > count) return false;
    if (length > 0 && buffer[length - 1] != L'/') buffer[length++] = L'/';
    {
        wchar_t *wide = utf8_decode(component, component_length);
        if (wide == NULL) return false;
        wcscpy(buffer + length, wide);
        free(wide);
    }
    return true;
}

static bool exe_directory(wchar_t *output, size_t count)
{
    char link_path[4096];
    ssize_t length;
    ssize_t slash;
    wchar_t *wide;
    length = readlink("/proc/self/exe", link_path, sizeof(link_path) - 1);
    if (length < 0) return false;
    link_path[length] = '\0';
    slash = -1;
    {
        ssize_t index;
        for (index = 0; index < length; ++index) {
            if (link_path[index] == '/') slash = index;
        }
    }
    if (slash < 0) return false;
    link_path[slash] = '\0';
    wide = utf8_decode(link_path, (size_t)slash);
    if (wide == NULL) return false;
    if (wcslen(wide) + 1 > count) {
        free(wide);
        return false;
    }
    wcscpy(output, wide);
    free(wide);
    return true;
}

static bool settings_skins_directory(wchar_t *output, size_t count)
{
    if (env_wchar_path("AUDIOCOMMANDER_SKINS_DIR", output, count)) return true;
    {
        const char *base = getenv("XDG_DATA_HOME");
        if (base != NULL && base[0] != '\0') {
            if (!utf8_to_buffer(base, output, count)) return false;
            if (!append_single_component(output, count, "audiocommander") ||
                !append_single_component(output, count, "skins"))
                return false;
            return true;
        }
    }
    {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            if (!utf8_to_buffer(home, output, count)) return false;
            if (!append_single_component(output, count, ".local") ||
                !append_single_component(output, count, "share") ||
                !append_single_component(output, count, "audiocommander") ||
                !append_single_component(output, count, "skins"))
                return false;
            return true;
        }
    }
    return exe_directory(output, count) && append_single_component(output, count, "skins");
}

static bool settings_skin_path(AppTheme theme, wchar_t *path, size_t count)
{
    const ExternalThemeDefinition *definition = external_theme_definition(theme);
    wchar_t directory[MAX_PATH];
    if (definition == NULL || path == NULL || count < 2)
        return false;
    if (!settings_skins_directory(directory, ARRAYSIZE(directory))) return false;
    return swprintf_s(path, count, L"%ls/%ls", directory, definition->file_name) > 0;
}

bool settings_theme_available(AppTheme theme)
{
    wchar_t path[MAX_PATH];
    ThemeColors colors;
    if (theme == APP_THEME_PASTEL || theme == APP_THEME_WINDOWS_NATIVE ||
        theme == APP_THEME_DARK || theme == APP_THEME_BLUE)
        return true;
    return settings_skin_path(theme, path, ARRAYSIZE(path)) &&
           skin_load_file(path, &colors);
}

static bool create_directories(const char *path)
{
    char buffer[4096];
    size_t length;
    size_t index;
    if (path == NULL || strlen(path) == 0) return false;
    length = strlen(path);
    if (length + 1 > sizeof(buffer)) return false;
    memcpy(buffer, path, length + 1);
    if (buffer[length - 1] == '/') buffer[length - 1] = '\0';
    for (index = 1; buffer[index] != '\0'; ++index) {
        if (buffer[index] != '/') continue;
        buffer[index] = '\0';
        if (mkdir(buffer, 0777) != 0 && errno != EEXIST) return false;
        buffer[index] = '/';
    }
    if (mkdir(buffer, 0777) != 0 && errno != EEXIST) return false;
    return true;
}

bool settings_ini_path(wchar_t *path, size_t count)
{
    if (path == NULL || count == 0) return false;
    if (env_wchar_path("AUDIOCOMMANDER_INI_PATH", path, count)) return true;
    {
        const char *base = getenv("XDG_CONFIG_HOME");
        char scratch[4096];
        if (base != NULL && base[0] != '\0') {
            if (snprintf(scratch, sizeof(scratch), "%s/audiocommander/audiocommander.ini",
                         base) < 0)
                return false;
        } else {
            const char *home = getenv("HOME");
            if (home == NULL || home[0] == '\0') return false;
            if (snprintf(scratch, sizeof(scratch), "%s/.config/audiocommander/audiocommander.ini",
                         home) < 0)
                return false;
        }
        return utf8_to_buffer(scratch, path, count);
    }
}

bool settings_first_run_accepted(const wchar_t *path)
{
    wchar_t value[8];
    if (path == NULL || path[0] == L'\0') return false;
    if (GetPrivateProfileStringW(L"Legal", L"Accepted", L"", value,
                                 ARRAYSIZE(value), path) != 1)
        return false;
    return wcscmp(value, L"1") == 0;
}

bool settings_record_first_run_acceptance(const wchar_t *path)
{
    if (path == NULL || path[0] == L'\0') return false;
    if (!WritePrivateProfileStringW(L"Legal", L"Accepted", L"1", path)) return false;
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return settings_first_run_accepted(path);
}

static bool load_directory_value(const wchar_t *ini_path, const wchar_t *key,
                                 wchar_t *value, size_t count)
{
    unsigned long length;
    unsigned long capacity;
    if (ini_path == NULL || key == NULL || value == NULL || count < 2)
        return false;
    value[0] = L'\0';
    capacity = (unsigned long)(count > 32767 ? 32767 : count);
    length = GetPrivateProfileStringW(L"Folders", key, L"", value,
                                      capacity, ini_path);
    if (length == 0 || length >= capacity - 1) {
        value[0] = L'\0';
        return false;
    }
    return true;
}

bool settings_load_last_directories(wchar_t *left, size_t left_count,
                                    wchar_t *right, size_t right_count)
{
    wchar_t path[MAX_PATH];
    bool left_loaded, right_loaded;
    if (left == NULL || right == NULL || left_count < 2 || right_count < 2)
        return false;
    left[0] = L'\0';
    right[0] = L'\0';
    if (!settings_ini_path(path, ARRAYSIZE(path))) return false;
    left_loaded = load_directory_value(path, L"Left", left, left_count);
    right_loaded = load_directory_value(path, L"Right", right, right_count);
    return left_loaded || right_loaded;
}

bool settings_save_last_directories(const wchar_t *left, const wchar_t *right)
{
    wchar_t path[MAX_PATH];
    char *utf8_path;
    const char *slash;
    if (left == NULL || left[0] == L'\0' || right == NULL || right[0] == L'\0' ||
        !settings_ini_path(path, ARRAYSIZE(path)))
        return false;
    utf8_path = utf8_encode(path);
    if (utf8_path == NULL) return false;
    slash = strrchr(utf8_path, '/');
    if (slash != NULL) {
        size_t directory_length = (size_t)(slash - utf8_path);
        char *directory = (char *)malloc(directory_length + 1);
        if (directory == NULL) {
            free(utf8_path);
            return false;
        }
        memcpy(directory, utf8_path, directory_length);
        directory[directory_length] = '\0';
        create_directories(directory);
        free(directory);
    }
    free(utf8_path);
    if (!WritePrivateProfileStringW(L"Folders", L"Left", left, path) ||
        !WritePrivateProfileStringW(L"Folders", L"Right", right, path))
        return false;
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return true;
}

bool settings_clear_last_directories(void)
{
    wchar_t path[MAX_PATH];
    bool left_cleared, right_cleared;
    if (!settings_ini_path(path, ARRAYSIZE(path))) return false;
    left_cleared = WritePrivateProfileStringW(L"Folders", L"Left", NULL, path) != FALSE;
    right_cleared = WritePrivateProfileStringW(L"Folders", L"Right", NULL, path) != FALSE;
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
    return left_cleared && right_cleared;
}

const wchar_t *settings_theme_name(AppTheme theme)
{
    static const wchar_t *names[APP_THEME_COUNT] = {
        L"Pastel", L"Windows Native", L"Acryl", L"Air", L"MetroUI",
        L"Office 2003", L"Office 2007 Black", L"XP Luna", L"XP Silver",
        L"Zest", L"Dark", L"Blue"
    };
    return theme >= 0 && theme < APP_THEME_COUNT ? names[theme] : names[APP_THEME_PASTEL];
}

const wchar_t *settings_language_name(AppLanguage language)
{
    static const wchar_t *names[APP_LANGUAGE_COUNT] = {
        L"English", L"\x4E2D\x6587\xFF08\x7B80\x4F53\xFF09", L"Italiano", L"Polski"
    };
    return language >= 0 && language < APP_LANGUAGE_COUNT ? names[language] : names[APP_LANGUAGE_ENGLISH];
}

void settings_load(AppSettings *settings)
{
    wchar_t path[MAX_PATH];
    wcscpy_s(settings->font_face, ARRAYSIZE(settings->font_face), L"Sans");
    settings->font_points = 11;
    settings->theme = APP_THEME_PASTEL;
    settings->language = APP_LANGUAGE_ENGLISH;
    settings->volume = 75;
    settings->sequential_playback = FALSE;
    settings->interface_scale = 100;
    settings->opacity = 100;
    settings->remember_window = FALSE;
    settings->remember_directories = FALSE;
    settings->window_x = CW_USEDEFAULT;
    settings->window_y = CW_USEDEFAULT;
    settings->window_width = 0;
    settings->window_height = 0;
    settings->font_color = 0;
    if (!settings_ini_path(path, ARRAYSIZE(path))) return;
    GetPrivateProfileStringW(L"Appearance", L"FontFace", settings->font_face,
                             settings->font_face, ARRAYSIZE(settings->font_face), path);
    settings->font_points = GetPrivateProfileIntW(L"Appearance", L"FontSize",
                                                    settings->font_points, path);
    {
        int stored_theme = GetPrivateProfileIntW(
            L"Appearance", L"Theme", settings->theme, path);
        int version = GetPrivateProfileIntW(L"Appearance", L"Version", 1, path);
        if (version == 4) {
            switch (stored_theme) {
            case 5: stored_theme = APP_THEME_WINDOWS_NATIVE; break;
            case 6: stored_theme = APP_THEME_OFFICE_2003_SKIN; break;
            default: stored_theme = APP_THEME_PASTEL; break;
            }
        } else if (version < 4) {
            stored_theme = APP_THEME_PASTEL;
        }
        settings->theme = (AppTheme)stored_theme;
    }
    if (GetPrivateProfileIntW(L"Appearance", L"DarkMode", 0, path) != 0 &&
        settings->theme != APP_THEME_DARK)
        settings->theme = APP_THEME_DARK;
    settings->language = (AppLanguage)GetPrivateProfileIntW(
        L"Appearance", L"Language", APP_LANGUAGE_ENGLISH, path);
    settings->volume = GetPrivateProfileIntW(L"Playback", L"Volume", 75, path);
    settings->sequential_playback = GetPrivateProfileIntW(
        L"Playback", L"Sequential", 0, path) != 0;
    settings->interface_scale = GetPrivateProfileIntW(L"Appearance", L"InterfaceSize", 100, path);
    settings->opacity = GetPrivateProfileIntW(L"Appearance", L"Opacity", 100, path);
    settings->remember_window = GetPrivateProfileIntW(L"Window", L"RememberPosition", 0, path) != 0;
    settings->remember_directories = GetPrivateProfileIntW(
        L"Folders", L"Remember", 0, path) != 0;
    settings->window_x = GetPrivateProfileIntW(L"Window", L"X", CW_USEDEFAULT, path);
    settings->window_y = GetPrivateProfileIntW(L"Window", L"Y", CW_USEDEFAULT, path);
    settings->window_width = GetPrivateProfileIntW(L"Window", L"Width", 0, path);
    settings->window_height = GetPrivateProfileIntW(L"Window", L"Height", 0, path);
    settings->font_color = (COLORREF)GetPrivateProfileIntW(
        L"Appearance", L"FontColor", 0, path);
    if (settings->font_points < 6 || settings->font_points > 72) settings->font_points = 11;
    if (settings->theme < 0 || settings->theme >= APP_THEME_COUNT ||
        !settings_theme_available(settings->theme))
        settings->theme = APP_THEME_PASTEL;
    if (settings->language < 0 || settings->language >= APP_LANGUAGE_COUNT)
        settings->language = APP_LANGUAGE_ENGLISH;
    if (settings->volume < 0 || settings->volume > 100) settings->volume = 75;
    if (settings->interface_scale < 75 || settings->interface_scale > 200) settings->interface_scale = 100;
    if (settings->opacity < 50 || settings->opacity > 100) settings->opacity = 100;
}

void settings_save(const AppSettings *settings)
{
    wchar_t path[MAX_PATH];
    wchar_t number[16];
    char *utf8_path;
    const char *slash;
    if (settings == NULL || !settings_ini_path(path, ARRAYSIZE(path))) return;
    utf8_path = utf8_encode(path);
    if (utf8_path != NULL) {
        slash = strrchr(utf8_path, '/');
        if (slash != NULL) {
            size_t directory_length = (size_t)(slash - utf8_path);
            char *directory = (char *)malloc(directory_length + 1);
            if (directory != NULL) {
                memcpy(directory, utf8_path, directory_length);
                directory[directory_length] = '\0';
                create_directories(directory);
                free(directory);
            }
        }
        free(utf8_path);
    }
    WritePrivateProfileStringW(L"Appearance", L"Version", L"5", path);
    WritePrivateProfileStringW(L"Appearance", L"FontFace", settings->font_face, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->font_points);
    WritePrivateProfileStringW(L"Appearance", L"FontSize", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", (int)settings->theme);
    WritePrivateProfileStringW(L"Appearance", L"Theme", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", (int)settings->language);
    WritePrivateProfileStringW(L"Appearance", L"Language", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->interface_scale);
    WritePrivateProfileStringW(L"Appearance", L"InterfaceSize", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->opacity);
    WritePrivateProfileStringW(L"Appearance", L"Opacity", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->volume);
    WritePrivateProfileStringW(L"Playback", L"Volume", number, path);
    WritePrivateProfileStringW(L"Playback", L"Sequential",
                               settings->sequential_playback ? L"1" : L"0", path);
    WritePrivateProfileStringW(L"Window", L"RememberPosition", settings->remember_window ? L"1" : L"0", path);
    WritePrivateProfileStringW(L"Folders", L"Remember",
                               settings->remember_directories ? L"1" : L"0", path);
    swprintf_s(number, ARRAYSIZE(number), L"%u", (unsigned int)settings->font_color);
    WritePrivateProfileStringW(L"Appearance", L"FontColor", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->window_x);
    WritePrivateProfileStringW(L"Window", L"X", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->window_y);
    WritePrivateProfileStringW(L"Window", L"Y", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->window_width);
    WritePrivateProfileStringW(L"Window", L"Width", number, path);
    swprintf_s(number, ARRAYSIZE(number), L"%d", settings->window_height);
    WritePrivateProfileStringW(L"Window", L"Height", number, path);
    WritePrivateProfileStringW(NULL, NULL, NULL, path);
}

HFONT settings_create_font(const AppSettings *settings, HWND dpi_window)
{
    (void)dpi_window;
    if (settings == NULL || settings->font_face[0] == L'\0' ||
        settings->font_points < 6 || settings->font_points > 72)
        return NULL;
    return (HFONT)(intptr_t)1;
}

ThemeColors settings_theme_colors(AppTheme theme)
{
    ThemeColors colors = pastel_colors();
    wchar_t path[MAX_PATH];
    if (settings_theme_is_external(theme)) {
        if (settings_skin_path(theme, path, ARRAYSIZE(path)))
            skin_load_file(path, &colors);
        return colors;
    }
    switch (theme) {
    case APP_THEME_PASTEL:
        break;
    case APP_THEME_WINDOWS_NATIVE:
        colors = (ThemeColors){
            RGB(221,221,221), RGB(255,255,255), RGB(0,0,0),
            RGB(53,132,228), RGB(255,255,255), RGB(128,128,128),
            RGB(53,132,228), RGB(255,255,255), RGB(221,221,221),
            RGB(221,221,221), RGB(0,0,0), RGB(53,132,228)}; break;
    case APP_THEME_DARK:
        colors = (ThemeColors){
            RGB(38,38,44), RGB(58,58,66), RGB(220,220,226),
            RGB(70,120,190), RGB(255,255,255), RGB(70,70,78),
            RGB(120,120,132), RGB(88,88,98), RGB(70,70,78),
            RGB(50,50,58), RGB(205,205,212), RGB(70,120,190)}; break;
    case APP_THEME_BLUE:
        colors = (ThemeColors){
            RGB(28,58,112), RGB(40,74,134), RGB(235,242,255),
            RGB(90,150,220), RGB(255,255,255), RGB(20,45,90),
            RGB(120,170,240), RGB(52,88,150), RGB(30,60,115),
            RGB(34,66,124), RGB(235,242,255), RGB(90,150,220)}; break;
    default:
        break;
    }
    return colors;
}
