#include "settings.h"

#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#ifndef SKINS_DIR
#define SKINS_DIR "data/skins"
#endif

#define TEMP_TEMPLATE "/tmp/acs_test_XXXXXX"

static int file_exists_wide(const wchar_t *path)
{
    char *utf8 = utf8_encode(path);
    int exists;
    if (utf8 == NULL) return 0;
    exists = access(utf8, F_OK) == 0;
    free(utf8);
    return exists;
}

static int delete_file_wide(const wchar_t *path)
{
    char *utf8 = utf8_encode(path);
    int result;
    if (utf8 == NULL) return -1;
    result = remove(utf8);
    free(utf8);
    return result;
}

int main(void)
{
    int index;
    COLORREF previous = CLR_INVALID;
    char temp_folder[256];
    char temp_ini_utf8[512];
    wchar_t temp_ini[MAX_PATH];
    wchar_t value[16];

    if (setenv("AUDIOCOMMANDER_SKINS_DIR", SKINS_DIR, 1) != 0) return 40;
    strcpy(temp_folder, TEMP_TEMPLATE);
    if (mkdtemp(temp_folder) == NULL) return 7;
    snprintf(temp_ini_utf8, sizeof(temp_ini_utf8), "%s/accept.ini", temp_folder);
    if (!utf8_to_buffer(temp_ini_utf8, temp_ini, ARRAYSIZE(temp_ini))) return 41;
    if (setenv("AUDIOCOMMANDER_INI_PATH", temp_ini_utf8, 1) != 0) return 42;

    if (settings_first_run_accepted(temp_ini) ||
        settings_first_run_accepted(NULL) ||
        settings_record_first_run_acceptance(NULL) ||
        settings_record_first_run_acceptance(L"/nonexistent_parent_dir/x.ini"))
        return 8;
    WritePrivateProfileStringW(L"Legal", L"Accepted", L"2", temp_ini);
    if (settings_first_run_accepted(temp_ini)) return 11;
    WritePrivateProfileStringW(L"Legal", L"Accepted", L"01", temp_ini);
    if (settings_first_run_accepted(temp_ini)) return 12;
    WritePrivateProfileStringW(L"Legal", L"Accepted", L"1x", temp_ini);
    if (settings_first_run_accepted(temp_ini)) return 13;
    if (!settings_record_first_run_acceptance(temp_ini) ||
        !settings_first_run_accepted(temp_ini))
        return 9;
    GetPrivateProfileStringW(L"Legal", L"Accepted", L"", value, ARRAYSIZE(value), temp_ini);
    if (wcscmp(value, L"1") != 0) return 10;
    delete_file_wide(temp_ini);

    for (index = 0; index < APP_THEME_COUNT; ++index) {
        const wchar_t *name = settings_theme_name((AppTheme)index);
        ThemeColors colors = settings_theme_colors((AppTheme)index);
        AppSettings settings = {0};
        HFONT font;
        if (name == NULL || name[0] == L'\0' ||
            !settings_theme_available((AppTheme)index) ||
            (index != APP_THEME_WINDOWS_NATIVE && colors.window == previous))
            return 2;
        previous = colors.window;
        wcscpy(settings.font_face, L"Segoe UI");
        settings.font_points = index == 0 ? 6 : 72;
        font = settings_create_font(&settings, NULL);
        if (font == NULL) return 3;
        wprintf(L"theme[%d]=%ls window=%06lx control=%06lx text=%06lx\n",
                index, name, (unsigned long)colors.window,
                (unsigned long)colors.control, (unsigned long)colors.text);
    }
    if (APP_THEME_COUNT != 12 ||
        wcscmp(settings_theme_name(APP_THEME_PASTEL), L"Pastel") != 0 ||
        wcscmp(settings_theme_name(APP_THEME_WINDOWS_NATIVE), L"Windows Native") != 0 ||
        wcscmp(settings_theme_name(APP_THEME_DARK), L"Dark") != 0 ||
        wcscmp(settings_theme_name(APP_THEME_BLUE), L"Blue") != 0 ||
        !settings_theme_available(APP_THEME_DARK) ||
        !settings_theme_available(APP_THEME_BLUE) ||
        settings_theme_is_external(APP_THEME_DARK) ||
        wcscmp(settings_theme_name(APP_THEME_OFFICE_2003_SKIN), L"Office 2003") != 0 ||
        settings_theme_colors(APP_THEME_OFFICE_2003_SKIN).hot_border != RGB(230,139,44) ||
        !settings_theme_is_external(APP_THEME_OFFICE_2003_SKIN) ||
        settings_theme_is_external(APP_THEME_WINDOWS_NATIVE) ||
        wcscmp(settings_theme_name((AppTheme)-1), L"Pastel") != 0)
        return 4;
    for (index = 0; index < APP_LANGUAGE_COUNT; ++index) {
        if (settings_language_name((AppLanguage)index)[0] == L'\0') return 5;
    }
    if (APP_LANGUAGE_COUNT != 4 ||
        wcscmp(settings_language_name((AppLanguage)-1), L"English") != 0)
        return 6;
    {
        AppSettings saved;
        AppSettings loaded;
        wchar_t settings_path[MAX_PATH];
        int persistence_error = 0;
        if (!settings_ini_path(settings_path, ARRAYSIZE(settings_path))) return 14;
        if (file_exists_wide(settings_path)) return 15;
        WritePrivateProfileStringW(L"Appearance", L"Version", L"4", settings_path);
        WritePrivateProfileStringW(L"Appearance", L"Theme", L"5", settings_path);
        settings_load(&saved);
        if (saved.theme != APP_THEME_WINDOWS_NATIVE) persistence_error = 25;
        WritePrivateProfileStringW(L"Appearance", L"Theme", L"6", settings_path);
        settings_load(&saved);
        if (saved.theme != APP_THEME_OFFICE_2003_SKIN) persistence_error = 26;
        WritePrivateProfileStringW(L"Appearance", L"Theme", L"1", settings_path);
        settings_load(&saved);
        if (saved.theme != APP_THEME_PASTEL) persistence_error = 27;
        WritePrivateProfileStringW(L"Appearance", L"DarkMode", L"1", settings_path);
        settings_load(&saved);
        if (saved.theme != APP_THEME_DARK) persistence_error = 30;
        if (delete_file_wide(settings_path) != 0 && persistence_error == 0)
            persistence_error = 28;
        settings_load(&saved);
        if (saved.theme != APP_THEME_PASTEL ||
            saved.sequential_playback || saved.remember_directories)
            persistence_error = 16;
        saved.sequential_playback = TRUE;
        saved.remember_directories = TRUE;
        saved.theme = APP_THEME_DARK;
        saved.font_color = RGB(255, 128, 0);
        settings_save(&saved);
        settings_load(&loaded);
        if (!loaded.sequential_playback || !loaded.remember_directories)
            persistence_error = 17;
        if (loaded.theme != APP_THEME_DARK || loaded.font_color != RGB(255, 128, 0))
            persistence_error = 29;
        loaded.theme = APP_THEME_PASTEL;
        loaded.font_color = 0;
        if (!settings_save_last_directories(L"/Audio Left", L"/Audio Right"))
            persistence_error = 20;
        {
            wchar_t left[64], right[64];
            if (!settings_load_last_directories(left, ARRAYSIZE(left),
                                                right, ARRAYSIZE(right)) ||
                wcscmp(left, L"/Audio Left") != 0 ||
                wcscmp(right, L"/Audio Right") != 0)
                persistence_error = 21;
        }
        {
            wchar_t *left = (wchar_t *)malloc(32768 * sizeof(*left));
            wchar_t *right = (wchar_t *)malloc(32768 * sizeof(*right));
            if (left == NULL || right == NULL ||
                !settings_load_last_directories(left, 32768, right, 32768) ||
                wcscmp(left, L"/Audio Left") != 0 ||
                wcscmp(right, L"/Audio Right") != 0)
                persistence_error = 24;
            free(left);
            free(right);
        }
        loaded.sequential_playback = FALSE;
        loaded.remember_directories = FALSE;
        settings_save(&loaded);
        if (!settings_clear_last_directories()) persistence_error = 22;
        settings_load(&saved);
        if (saved.sequential_playback || saved.remember_directories)
            persistence_error = 18;
        {
            wchar_t left[64] = L"x", right[64] = L"x";
            if (settings_load_last_directories(left, ARRAYSIZE(left),
                                               right, ARRAYSIZE(right)) ||
                left[0] != L'\0' || right[0] != L'\0')
                persistence_error = 23;
        }
        if (delete_file_wide(settings_path) != 0 && persistence_error == 0) persistence_error = 19;
        if (persistence_error != 0) return persistence_error;
    }
    wprintf(L"first-run acceptance, sequential and dual-directory persistence, two built-ins, eight external skins, four languages, and font creation passed\n");
    rmdir(temp_folder);
    return 0;
}
