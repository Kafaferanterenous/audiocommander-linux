#include "app.h"

#include "browser.h"
#include "copy_progress.h"
#include "file_ops.h"
#include "localization.h"
#include "media_info.h"
#include "playback.h"
#include "playlist.h"
#include "settings.h"
#include "utf8.h"

#include <gtk/gtk.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#define APP_PANE_COLUMN_NAME 0
#define APP_PANE_COLUMN_SIZE 1
#define APP_PANE_COLUMN_DURATION 2
#define APP_PANE_COLUMN_KIND 3
#define APP_PANE_COLUMN_COUNT 4

typedef struct AppContext AppContext;

typedef struct AppPane {
    wchar_t folder[MAX_PATH];
    struct AppPane *sibling;
    AppContext *context;
    const AppSettings *settings;
    bool is_left;
    GtkWidget *path_entry;
    GtkWidget *tree_view;
    GtkTreeStore *store;
    GtkWidget *summary_label;
    GtkWidget *up_button;
    GtkWidget *browse_button;
    GtkWidget *copy_button;
    GtkWidget *move_button;
    GtkWidget *delete_button;
    GtkWidget *settings_button;
    GtkWidget *home_button;
    GtkTreeViewColumn *name_column;
    GtkTreeViewColumn *size_column;
    GtkTreeViewColumn *duration_column;
    BrowserSortColumn sort_column;
    bool sort_descending;
} AppPane;

struct AppContext {
    GtkWidget *window;
    AppPane *left;
    AppPane *right;
    AppSettings *settings;
    wchar_t playing_path[MAX_PATH];
    AppPane *playing_pane;
    GtkWidget *seek_scale;
    GtkWidget *time_label;
    GtkWidget *volume_scale;
    GtkWidget *refresh_all_button;
    GtkWidget *stop_button;
    GtkWidget *swap_button;
    BOOL seeking;
    BOOL syncing;
    GtkCssProvider *theme_provider;
};

typedef struct AppSelection {
    BrowserEntryKind kind;
    wchar_t path[MAX_PATH];
} AppSelection;

static void app_pane_refresh(AppPane *pane);
static void app_pane_rebuild(AppPane *pane, const wchar_t *folder);
static char *pane_label(const AppPane *pane, UiText id);
static void set_button_label(GtkWidget *button, const wchar_t *label);
static void apply_language(AppContext *context);
static void apply_theme(AppContext *context);
static void error_dialog(AppPane *pane, const wchar_t *message);
static bool play_full_path(AppPane *pane, const wchar_t *path);
static bool pane_toggle_playback(AppPane *pane, const wchar_t *path);
static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer user_data);
static void on_column_clicked(GtkTreeViewColumn *column, gpointer user_data);
static void on_seek_changed(GtkRange *range, gpointer user_data);
static gboolean on_seek_press(GtkWidget *widget, GdkEventButton *event,
                              gpointer user_data);
static gboolean on_seek_release(GtkWidget *widget, GdkEventButton *event,
                                gpointer user_data);
static void on_volume_changed(GtkRange *range, gpointer user_data);
static gboolean on_volume_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data);
static bool playback_advance_sequential(AppContext *context);

static char *wchar_to_utf8(const wchar_t *text)
{
    return utf8_encode(text);
}

static const wchar_t *tr(const AppPane *pane, UiText id)
{
    return localization_text(pane->settings->language, id);
}

static void pane_fill_summary(AppPane *pane)
{
    BrowserListing listing;
    BrowserListingSummary summary;
    wchar_t text[256];
    const wchar_t *format;
    unsigned long long hours;
    unsigned long long minutes;
    unsigned long long seconds;
    double size_mb;
    if (pane == NULL) return;
    if (!browser_list_folder(pane->folder, &listing)) {
        gtk_label_set_text(GTK_LABEL(pane->summary_label), "");
        return;
    }
    browser_listing_summary(&listing, &summary);
    size_mb = (double)summary.total_size_bytes / (1024.0 * 1024.0);
    hours = summary.known_duration_ms / 3600000ULL;
    minutes = (summary.known_duration_ms / 60000ULL) % 60;
    seconds = (summary.known_duration_ms / 1000ULL) % 60;
    if (summary.unknown_duration_count > 0) {
        format = tr(pane, UI_DIRECTORY_SUMMARY_UNKNOWN_FORMAT);
        swprintf(text, ARRAYSIZE(text), format,
                 (unsigned long long)summary.audio_file_count, size_mb,
                 hours, minutes, seconds,
                 (unsigned long long)summary.unknown_duration_count);
    } else {
        format = tr(pane, UI_DIRECTORY_SUMMARY_FORMAT);
        swprintf(text, ARRAYSIZE(text), format,
                 (unsigned long long)summary.audio_file_count, size_mb,
                 hours, minutes, seconds);
    }
    browser_listing_free(&listing);
    {
        char *utf8 = wchar_to_utf8(text);
        gtk_label_set_text(GTK_LABEL(pane->summary_label), utf8 != NULL ? utf8 : "");
        free(utf8);
    }
}

static void pane_set_folder(AppPane *pane, const wchar_t *folder)
{
    wchar_t normalized[MAX_PATH];
    if (!browser_normalize_folder(folder, normalized, ARRAYSIZE(normalized)))
        return;
    wcscpy(pane->folder, normalized);
    {
        char *utf8 = wchar_to_utf8(normalized);
        gtk_entry_set_text(GTK_ENTRY(pane->path_entry), utf8 != NULL ? utf8 : "");
        free(utf8);
    }
    app_pane_refresh(pane);
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    const char *text = gtk_entry_get_text(entry);
    wchar_t wide[MAX_PATH];
    if (utf8_to_buffer(text, wide, ARRAYSIZE(wide))) pane_set_folder(pane, wide);
}

static void on_up_clicked(GtkButton *button, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    wchar_t parent[MAX_PATH];
    (void)button;
    if (browser_parent_folder(pane->folder, parent, ARRAYSIZE(parent)))
        pane_set_folder(pane, parent);
}


static void on_home_clicked(GtkButton *button, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    const char *home = getenv("HOME");
    wchar_t wide[MAX_PATH];
    (void)button;
    if (home != NULL && home[0] != '\0' && utf8_to_buffer(home, wide, ARRAYSIZE(wide)))
        pane_set_folder(pane, wide);
}

static void on_browse_clicked(GtkButton *button, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    GtkWidget *dialog;
    char *utf8_title;
    (void)button;
    utf8_title = pane_label(pane, UI_BROWSE);
    dialog = gtk_file_chooser_dialog_new(
        utf8_title != NULL ? utf8_title : "Browse", NULL,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    free(utf8_title);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        wchar_t wide[MAX_PATH];
        if (folder != NULL && utf8_to_buffer(folder, wide, ARRAYSIZE(wide)))
            pane_set_folder(pane, wide);
        g_free(folder);
    }
    gtk_widget_destroy(dialog);
}

static gboolean on_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                 GtkTreeViewColumn *column, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    GtkTreeIter iter;
    char *name = NULL;
    int kind = BROWSER_ENTRY_AUDIO_FILE;
    (void)tree_view;
    (void)column;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(pane->store), &iter, path))
        return FALSE;
    gtk_tree_model_get(GTK_TREE_MODEL(pane->store), &iter,
                       APP_PANE_COLUMN_NAME, &name,
                       APP_PANE_COLUMN_KIND, &kind, -1);
    if (name != NULL) {
        wchar_t wide_name[MAX_PATH];
        wchar_t joined[MAX_PATH];
        if (utf8_to_buffer(name, wide_name, ARRAYSIZE(wide_name)) &&
            browser_join_path(pane->folder, wide_name, joined, ARRAYSIZE(joined))) {
            if (kind == BROWSER_ENTRY_AUDIO_FILE) {
                if (!pane_toggle_playback(pane, joined)) return FALSE;
            } else {
                wchar_t normalized[MAX_PATH];
                if (browser_normalize_folder(joined, normalized, ARRAYSIZE(normalized)))
                    pane_set_folder(pane, normalized);
            }
        }
        g_free(name);
    }
    return TRUE;
}

static void create_new_folder(AppPane *pane)
{
    wchar_t new_folder[MAX_PATH];
    wchar_t candidate[MAX_PATH];
    unsigned int suffix = 0;
    if (pane == NULL) return;
    for (;;) {
        if (suffix == 0) {
            if (wcscpy_s(new_folder, ARRAYSIZE(new_folder),
                         tr(pane, UI_NEW_FOLDER)) != 0) {
                error_dialog(pane, tr(pane, UI_NEW_FOLDER_PATH_LONG));
                return;
            }
        } else if (swprintf_s(new_folder, ARRAYSIZE(new_folder),
                              tr(pane, UI_NEW_FOLDER_NUMBERED), suffix) < 0) {
            error_dialog(pane, tr(pane, UI_NEW_FOLDER_PATH_LONG));
            return;
        }
        if (!browser_join_path(pane->folder, new_folder, candidate,
                               ARRAYSIZE(candidate))) {
            error_dialog(pane, tr(pane, UI_NEW_FOLDER_PATH_LONG));
            return;
        }
        if (!file_ops_exists(candidate)) break;
        ++suffix;
    }
    {
        char *utf8_path = utf8_encode(candidate);
        if (utf8_path == NULL) return;
        if (mkdir(utf8_path, 0755) == 0) {
            app_pane_refresh(pane);
        } else {
            const wchar_t *format = tr(pane, UI_FAILED_FORMAT);
            const wchar_t *verb = tr(pane, UI_CREATE_FOLDER_ACTION);
            const char *reason = file_ops_last_error();
            wchar_t message[512];
            wchar_t reason_wide[256];
            if (utf8_to_buffer(reason, reason_wide, ARRAYSIZE(reason_wide)))
                swprintf(message, ARRAYSIZE(message), format, verb, reason_wide);
            else
                wcscpy(message, verb);
            error_dialog(pane, message);
        }
        free(utf8_path);
    }
}

static void on_new_folder_activate(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    create_new_folder((AppPane *)user_data);
}

static void context_menu_deactivate(GtkWidget *menu, gpointer user_data)
{
    (void)user_data;
    gtk_widget_destroy(menu);
    g_object_unref(menu);
}

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    GtkTreePath *path = NULL;
    GtkTreeViewColumn *column = NULL;
    GtkTreeIter iter;
    char *name = NULL;
    int kind = BROWSER_ENTRY_AUDIO_FILE;
    if (event == NULL) return FALSE;
    if (event->button == 3 &&
        (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *item;
        char *label = wchar_to_utf8(tr(pane, UI_NEW_FOLDER_MENU));
        item = gtk_menu_item_new_with_label(label != NULL ? label : "New Folder...");
        free(label);
        g_signal_connect(item, "activate", G_CALLBACK(on_new_folder_activate), pane);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show_all(menu);
        g_object_ref_sink(menu);
        g_signal_connect(menu, "deactivate",
                         G_CALLBACK(context_menu_deactivate), NULL);
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 3, event->time);
        return TRUE;
    }
    if (event->button != 1 ||
        (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) != 0)
        return FALSE;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                       (gint)event->x, (gint)event->y,
                                       &path, &column, NULL, NULL))
        return FALSE;
    if (path == NULL || column == NULL) {
        if (path != NULL) gtk_tree_path_free(path);
        return FALSE;
    }
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(pane->store), &iter, path)) {
        gtk_tree_path_free(path);
        return FALSE;
    }
    gtk_tree_path_free(path);
    gtk_tree_model_get(GTK_TREE_MODEL(pane->store), &iter,
                       APP_PANE_COLUMN_NAME, &name,
                       APP_PANE_COLUMN_KIND, &kind, -1);
    if (kind != BROWSER_ENTRY_AUDIO_FILE || name == NULL) {
        g_free(name);
        return FALSE;
    }
    {
        wchar_t wide_name[MAX_PATH];
        wchar_t joined[MAX_PATH];
        if (utf8_to_buffer(name, wide_name, ARRAYSIZE(wide_name)) &&
            browser_join_path(pane->folder, wide_name, joined, ARRAYSIZE(joined)))
            pane_toggle_playback(pane, joined);
    }
    g_free(name);
    return FALSE;
}

static void update_sort_indicator(AppPane *pane)
{
    GtkTreeViewColumn *columns[3];
    BrowserSortColumn ids[3];
    int i;
    columns[0] = pane->name_column;
    columns[1] = pane->size_column;
    columns[2] = pane->duration_column;
    ids[0] = BROWSER_SORT_NAME;
    ids[1] = BROWSER_SORT_SIZE;
    ids[2] = BROWSER_SORT_DURATION;
    for (i = 0; i < 3; ++i) {
        if (columns[i] == NULL) continue;
        if (ids[i] == pane->sort_column) {
            gtk_tree_view_column_set_sort_indicator(columns[i], TRUE);
            gtk_tree_view_column_set_sort_order(columns[i],
                pane->sort_descending ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
        } else {
            gtk_tree_view_column_set_sort_indicator(columns[i], FALSE);
        }
    }
}

static void on_column_clicked(GtkTreeViewColumn *column, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    BrowserSortColumn clicked;
    if (column == pane->name_column) clicked = BROWSER_SORT_NAME;
    else if (column == pane->size_column) clicked = BROWSER_SORT_SIZE;
    else if (column == pane->duration_column) clicked = BROWSER_SORT_DURATION;
    else return;

    if (pane->sort_column == clicked)
        pane->sort_descending = !pane->sort_descending;
    else {
        pane->sort_column = clicked;
        pane->sort_descending = false;
    }
    update_sort_indicator(pane);
    app_pane_refresh(pane);
}

static void app_pane_rebuild(AppPane *pane, const wchar_t *folder)
{
    BrowserListing listing;
    size_t index;
    if (!browser_list_folder(folder, &listing)) return;
    browser_listing_sort(&listing, pane->sort_column, pane->sort_descending);
    for (index = 0; index < listing.count; ++index) {
        const BrowserEntry *entry = &listing.entries[index];
        GtkTreeIter iter;
        char *name = wchar_to_utf8(entry->name);
        char size_text[64];
        char duration_text[64];
        if (entry->kind == BROWSER_ENTRY_DIRECTORY) {
            snprintf(size_text, sizeof(size_text), "%s", "--");
            snprintf(duration_text, sizeof(duration_text), "%s", "--");
        } else {
            snprintf(size_text, sizeof(size_text), "%llu bytes",
                     (unsigned long long)entry->size);
            if (entry->duration_ms == 0) {
                snprintf(duration_text, sizeof(duration_text), "%s", "--");
            } else {
                unsigned long total_seconds = entry->duration_ms / 1000;
                snprintf(duration_text, sizeof(duration_text), "%lu:%02lu",
                         total_seconds / 60, total_seconds % 60);
            }
        }
        gtk_tree_store_append(pane->store, &iter, NULL);
        gtk_tree_store_set(pane->store, &iter,
                           APP_PANE_COLUMN_NAME, name != NULL ? name : "",
                           APP_PANE_COLUMN_SIZE, size_text,
                           APP_PANE_COLUMN_DURATION, duration_text,
                           APP_PANE_COLUMN_KIND, (int)entry->kind, -1);
        free(name);
    }
    browser_listing_free(&listing);
}

static void app_pane_refresh(AppPane *pane)
{
    gtk_tree_store_clear(pane->store);
    app_pane_rebuild(pane, pane->folder);
    pane_fill_summary(pane);
}

static AppSelection *collect_selection(AppPane *pane, size_t *out_count)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GList *rows;
    GList *cursor;
    size_t count;
    AppSelection *items;
    if (out_count == NULL) return NULL;
    *out_count = 0;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(pane->tree_view));
    rows = gtk_tree_selection_get_selected_rows(selection, &model);
    count = g_list_length(rows);
    if (count == 0) {
        g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
        return NULL;
    }
    items = (AppSelection *)malloc(count * sizeof(*items));
    if (items == NULL) {
        g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
        return NULL;
    }
    count = 0;
    for (cursor = rows; cursor != NULL; cursor = cursor->next) {
        GtkTreeIter iter;
        GtkTreePath *path = (GtkTreePath *)cursor->data;
        char *name = NULL;
        int kind = BROWSER_ENTRY_AUDIO_FILE;
        wchar_t wide_name[MAX_PATH];
        if (!gtk_tree_model_get_iter(model, &iter, path)) continue;
        gtk_tree_model_get(model, &iter,
                           APP_PANE_COLUMN_NAME, &name,
                           APP_PANE_COLUMN_KIND, &kind, -1);
        if (name != NULL && utf8_to_buffer(name, wide_name, ARRAYSIZE(wide_name)) &&
            browser_join_path(pane->folder, wide_name, items[count].path,
                              ARRAYSIZE(items[count].path))) {
            items[count].kind = (BrowserEntryKind)kind;
            ++count;
        }
        g_free(name);
    }
    g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);
    if (count == 0) {
        free(items);
        return NULL;
    }
    *out_count = count;
    return items;
}

static void on_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    gboolean enabled = gtk_tree_selection_count_selected_rows(selection) > 0;
    gtk_widget_set_sensitive(pane->copy_button, enabled);
    gtk_widget_set_sensitive(pane->move_button, enabled);
    gtk_widget_set_sensitive(pane->delete_button, enabled);
}

static bool confirm_dialog(AppPane *pane, const wchar_t *message,
                           const wchar_t *title, bool warning)
{
    char *utf8_message = wchar_to_utf8(message);
    char *utf8_title = wchar_to_utf8(title);
    GtkWidget *dialog;
    gint response;
    if (utf8_message == NULL || utf8_title == NULL) {
        free(utf8_message);
        free(utf8_title);
        return false;
    }
    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                    warning ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION,
                                    GTK_BUTTONS_YES_NO, "%s", utf8_message);
    gtk_window_set_title(GTK_WINDOW(dialog), utf8_title);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    free(utf8_message);
    free(utf8_title);
    return response == GTK_RESPONSE_YES;
}

static void error_dialog(AppPane *pane, const wchar_t *message)
{
    char *utf8_message = wchar_to_utf8(message);
    char *utf8_title = wchar_to_utf8(tr(pane, UI_ERROR_TITLE));
    GtkWidget *dialog;
    if (utf8_message == NULL) {
        free(utf8_title);
        return;
    }
    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                    "%s", utf8_message);
    if (utf8_title != NULL) gtk_window_set_title(GTK_WINDOW(dialog), utf8_title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    free(utf8_message);
    free(utf8_title);
}

static void transfer_between(AppPane *pane, bool move_file)
{
    AppPane *sibling = pane->sibling;
    AppSelection *items;
    size_t count;
    size_t index;
    int file_number = 0;
    int file_count = 0;
    int processed = 0;
    bool failed = false;
    CopyProgressStrings copy_strings;
    if (sibling == NULL) return;
    items = collect_selection(pane, &count);
    if (items == NULL) {
        error_dialog(pane, tr(pane, UI_SELECT_SOURCE));
        return;
    }
    for (index = 0; index < count; ++index)
        if (items[index].kind != BROWSER_ENTRY_DIRECTORY) ++file_count;
    copy_strings.title = tr(pane, UI_COPY_PROGRESS_TITLE);
    copy_strings.patient_message = tr(pane, UI_COPY_PATIENT_MESSAGE);
    copy_strings.file_format = tr(pane, UI_COPY_FILE_PROGRESS_FORMAT);
    copy_strings.bytes_format = tr(pane, UI_COPY_BYTES_PROGRESS_FORMAT);
    for (index = 0; index < count; ++index) {
        const wchar_t *name = wcsrchr(items[index].path, L'/');
        wchar_t destination[MAX_PATH];
        bool overwrite = false;
        bool success;
        if (name == NULL) continue;
        ++name;
        if (!browser_join_path(sibling->folder, name, destination,
                               ARRAYSIZE(destination))) {
            error_dialog(pane, tr(pane, UI_SOURCE_PATH_LONG));
            failed = true;
            break;
        }
        if (wcscmp(items[index].path, destination) == 0) {
            error_dialog(pane, tr(pane, UI_SAME_ITEM));
            failed = true;
            break;
        }
        if (file_ops_exists(destination)) {
            if (items[index].kind == BROWSER_ENTRY_DIRECTORY) {
                size_t message_count = wcslen(tr(pane, UI_FOLDER_MERGE_FORMAT)) +
                                       wcslen(destination) + 1;
                wchar_t *message = (wchar_t *)malloc(message_count * sizeof(*message));
                if (message == NULL) continue;
                swprintf(message, message_count, tr(pane, UI_FOLDER_MERGE_FORMAT),
                         destination);
                if (!confirm_dialog(pane, message, tr(pane, UI_CONFIRM_FOLDER_MERGE), true)) {
                    free(message);
                    continue;
                }
                free(message);
            } else {
                size_t message_count = wcslen(tr(pane, UI_OVERWRITE_FORMAT)) +
                                       wcslen(destination) + 1;
                wchar_t *message = (wchar_t *)malloc(message_count * sizeof(*message));
                if (message == NULL) continue;
                swprintf(message, message_count, tr(pane, UI_OVERWRITE_FORMAT),
                         destination);
                if (!confirm_dialog(pane, message, tr(pane, UI_CONFIRM_OVERWRITE), true)) {
                    free(message);
                    continue;
                }
                free(message);
                overwrite = true;
            }
        }
        if (items[index].kind == BROWSER_ENTRY_DIRECTORY) {
            success = move_file ?
                file_ops_move_directory(NULL, items[index].path, sibling->folder) :
                file_ops_copy_directory(NULL, items[index].path, sibling->folder);
        } else {
            ++file_number;
            success = move_file ?
                file_ops_move(items[index].path, destination, overwrite) :
                copy_progress_copy(NULL, items[index].path, destination, overwrite,
                                   name, file_number, file_count, &copy_strings);
        }
        if (!success) {
            const wchar_t *format = tr(pane, UI_FAILED_FORMAT);
            const wchar_t *verb = move_file ? tr(pane, UI_MOVE_ACTION) :
                                              tr(pane, UI_COPY_ACTION);
            const char *reason = file_ops_last_error();
            wchar_t message[512];
            wchar_t reason_wide[256];
            if (utf8_to_buffer(reason, reason_wide, ARRAYSIZE(reason_wide)))
                swprintf(message, ARRAYSIZE(message), format, verb, reason_wide);
            else
                wcscpy(message, verb);
            error_dialog(pane, message);
            failed = true;
            break;
        }
        ++processed;
    }
    free(items);
    if (processed > 0 || failed) {
        app_pane_refresh(pane);
        app_pane_refresh(sibling);
    }
}

static void delete_selection(AppPane *pane)
{
    AppSelection *items;
    size_t count;
    size_t index;
    int directory_count = 0;
    bool any_deleted = false;
    FileOpsDeleteDisposition disposition;
    wchar_t message[512];
    disposition = file_ops_delete_disposition(pane->folder);
    items = collect_selection(pane, &count);
    if (items == NULL) {
        error_dialog(pane, tr(pane, UI_SELECT_ITEMS));
        return;
    }
    for (index = 0; index < count; ++index)
        if (items[index].kind == BROWSER_ENTRY_DIRECTORY) ++directory_count;
    if (disposition == FILE_OPS_DELETE_PERMANENT) {
        if (directory_count > 0)
            swprintf(message, ARRAYSIZE(message), tr(pane, UI_DELETE_REMOVABLE_DIR_FORMAT),
                     (int)count, directory_count);
        else
            swprintf(message, ARRAYSIZE(message), tr(pane, UI_DELETE_REMOVABLE_FILES_FORMAT),
                     (int)count);
    } else if (directory_count > 0) {
        swprintf(message, ARRAYSIZE(message), tr(pane, UI_DELETE_DIR_FORMAT),
                 (int)count, directory_count);
    } else {
        swprintf(message, ARRAYSIZE(message), tr(pane, UI_DELETE_FILES_FORMAT), (int)count);
    }
    if (!confirm_dialog(pane, message,
                        disposition == FILE_OPS_DELETE_PERMANENT ?
                            tr(pane, UI_CONFIRM_PERMANENT_DELETE) : tr(pane, UI_CONFIRM_DELETE),
                        true)) {
        free(items);
        return;
    }
    for (index = 0; index < count; ++index) {
        bool ok = disposition == FILE_OPS_DELETE_PERMANENT ?
            file_ops_delete_permanently(NULL, items[index].path) :
            file_ops_recycle(NULL, items[index].path);
        if (!ok) {
            error_dialog(pane, disposition == FILE_OPS_DELETE_PERMANENT ?
                              tr(pane, UI_PERMANENT_DELETE_ACTION) :
                              tr(pane, UI_RECYCLE_DELETE_ACTION));
            break;
        }
        any_deleted = true;
    }
    free(items);
    if (any_deleted) app_pane_refresh(pane);
}

static void swap_folders(AppContext *context)
{
    wchar_t temp[MAX_PATH];
    if (context == NULL || context->left == NULL || context->right == NULL) return;
    wcscpy(temp, context->left->folder);
    pane_set_folder(context->left, context->right->folder);
    pane_set_folder(context->right, temp);
}

static void on_copy_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    transfer_between((AppPane *)user_data, false);
}

static void on_move_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    transfer_between((AppPane *)user_data, true);
}

static void on_delete_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    delete_selection((AppPane *)user_data);
}

static void on_swap_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    swap_folders((AppContext *)user_data);
}

static void on_refresh_all_clicked(GtkButton *button, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)button;
    if (context == NULL) return;
    app_pane_refresh(context->left);
    app_pane_refresh(context->right);
}

static void on_stop_all_clicked(GtkButton *button, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)button;
    playback_stop();
    if (context != NULL) context->playing_path[0] = L'\0';
}

static AppPane *get_active_pane(AppContext *context)
{
    GtkWidget *focused;
    if (context == NULL) return context->left;
    focused = gtk_window_get_focus(GTK_WINDOW(context->window));
    if (focused == context->right->tree_view) return context->right;
    return context->left;
}

static void show_help(AppContext *context)
{
    const wchar_t *text;
    char *utf8;
    GtkWidget *dialog, *content, *scroll, *textview;
    GtkTextBuffer *buffer;

    if (context == NULL || context->settings == NULL) return;
    text = localization_help_text(context->settings->language);
    if (text == NULL || text[0] == L'\0') return;

    utf8 = wchar_to_utf8(text);
    if (utf8 == NULL) return;

    dialog = gtk_dialog_new_with_buttons(
        tr(context->left, UI_HELP_TITLE),
        GTK_WINDOW(context->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        tr(context->left, UI_CLOSE), GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 450);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, utf8, -1);
    free(utf8);

    gtk_container_add(GTK_CONTAINER(scroll), textview);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static gboolean on_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    AppPane *active;
    bool ctrl_held;
    (void)widget;

    if (context == NULL) return FALSE;
    active = get_active_pane(context);
    ctrl_held = (event->state & GDK_CONTROL_MASK) != 0;

    switch (event->keyval) {
    case GDK_KEY_F1:
        show_help(context);
        return TRUE;
    case GDK_KEY_F2:
        if (active->is_left && !ctrl_held) { transfer_between(active, false); return TRUE; }
        if (!active->is_left && ctrl_held) { transfer_between(active, false); return TRUE; }
        return FALSE;
    case GDK_KEY_F3:
        if (active->is_left && !ctrl_held) { transfer_between(active, true); return TRUE; }
        if (!active->is_left && ctrl_held) { transfer_between(active, true); return TRUE; }
        return FALSE;
    case GDK_KEY_F4:
        if (active->is_left && !ctrl_held) { delete_selection(active); return TRUE; }
        if (!active->is_left && ctrl_held) { delete_selection(active); return TRUE; }
        return FALSE;
    case GDK_KEY_F5:
        if (active->is_left && !ctrl_held) {
            app_pane_refresh(active);
            if (active->sibling != NULL) app_pane_refresh(active->sibling);
            return TRUE;
        }
        if (!active->is_left && ctrl_held) {
            app_pane_refresh(active);
            if (active->sibling != NULL) app_pane_refresh(active->sibling);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_F6:
        if (!ctrl_held) { swap_folders(context); return TRUE; }
        return FALSE;
    default:
        return FALSE;
    }
}

static void on_seek_changed(GtkRange *range, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    gdouble value;
    if (context == NULL || context->syncing) return;
    value = gtk_range_get_value(range);
    if (value < 0.0) value = 0.0;
    if (playback_is_active()) playback_seek_ms((unsigned long)value);
}

static gboolean on_seek_press(GtkWidget *widget, GdkEventButton *event,
                              gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)widget;
    (void)event;
    if (context != NULL) context->seeking = TRUE;
    return FALSE;
}

static gboolean on_seek_release(GtkWidget *widget, GdkEventButton *event,
                                gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)event;
    if (context != NULL) {
        context->seeking = FALSE;
        if (playback_is_active())
            playback_seek_ms((unsigned long)gtk_range_get_value(GTK_RANGE(widget)));
    }
    return FALSE;
}

static void on_volume_changed(GtkRange *range, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    gint volume;
    if (context == NULL || context->syncing) return;
    volume = (gint)gtk_range_get_value(range);
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    playback_set_volume(volume);
    context->settings->volume = volume;
}

static gboolean on_volume_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)widget;
    (void)event;
    if (context != NULL) settings_save(context->settings);
    return FALSE;
}

static bool playback_advance_sequential(AppContext *context)
{
    AppPane *pane;
    BrowserListing listing;
    Playlist playlist = {0};
    size_t current_index;
    bool advanced = false;
    if (context == NULL || context->playing_pane == NULL) return false;
    pane = context->playing_pane;
    if (!browser_list_folder(pane->folder, &listing)) return false;
    for (current_index = 0; current_index < listing.count; ++current_index) {
        const BrowserEntry *entry = &listing.entries[current_index];
        wchar_t full[MAX_PATH];
        if (entry->kind != BROWSER_ENTRY_AUDIO_FILE) continue;
        if (browser_join_path(pane->folder, entry->name, full, ARRAYSIZE(full)) &&
            wcscmp(full, context->playing_path) == 0) {
            if (playlist_build_after(&playlist, pane->folder, &listing,
                                     current_index)) {
                const wchar_t *next = playlist_next(&playlist);
                if (next != NULL) advanced = play_full_path(pane, next);
            }
            break;
        }
    }
    playlist_clear(&playlist);
    browser_listing_free(&listing);
    return advanced;
}

static gboolean playback_poll_cb(gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    bool active;
    if (context == NULL) return TRUE;
    active = playback_is_active();
    if (active) {
        unsigned long position = playback_position_ms();
        unsigned long duration = playback_duration_ms();
        if (context->time_label != NULL) {
            char text[64];
            snprintf(text, sizeof(text), "%lu:%02lu / %lu:%02lu",
                     position / 60000, (position / 1000) % 60,
                     duration / 60000, (duration / 1000) % 60);
            gtk_label_set_text(GTK_LABEL(context->time_label), text);
        }
        if (context->seek_scale != NULL) {
            GtkAdjustment *adjustment =
                gtk_range_get_adjustment(GTK_RANGE(context->seek_scale));
            gdouble upper = duration > 0 ? (gdouble)duration : 1.0;
            if (gtk_adjustment_get_upper(adjustment) != upper)
                gtk_adjustment_set_upper(adjustment, upper);
            if (!context->seeking) {
                context->syncing = TRUE;
                gtk_range_set_value(GTK_RANGE(context->seek_scale),
                                    position <= duration ? (gdouble)position : 0.0);
                context->syncing = FALSE;
            }
        }
    } else {
        if (context->seek_scale != NULL) {
            GtkAdjustment *adjustment =
                gtk_range_get_adjustment(GTK_RANGE(context->seek_scale));
            context->syncing = TRUE;
            if (gtk_adjustment_get_upper(adjustment) != 1.0)
                gtk_adjustment_set_upper(adjustment, 1.0);
            gtk_range_set_value(GTK_RANGE(context->seek_scale), 0.0);
            context->syncing = FALSE;
        }
        if (playback_was_finished()) {
            bool advanced = context->settings->sequential_playback &&
                playback_advance_sequential(context);
            if (!advanced && context->playing_path[0] != L'\0')
                context->playing_path[0] = L'\0';
        }
    }
    return TRUE;
}

static bool play_full_path(AppPane *pane, const wchar_t *path)
{
    bool started = playback_play(path);
    if (!started) {
        error_dialog(pane, tr(pane, UI_PLAY_FAILED));
        return false;
    }
    if (pane->context != NULL) {
        wcscpy(pane->context->playing_path, path);
        pane->context->playing_pane = pane;
    }
    return true;
}

static bool pane_toggle_playback(AppPane *pane, const wchar_t *path)
{
    AppContext *context = pane->context;
    bool started;
    if (context != NULL && context->playing_path[0] != L'\0' &&
        wcscmp(context->playing_path, path) == 0) {
        playback_stop();
        context->playing_path[0] = L'\0';
        started = true;
    } else {
        started = play_full_path(pane, path);
    }
    return started;
}

static char *pane_label(const AppPane *pane, UiText id)
{
    return wchar_to_utf8(tr(pane, id));
}

static void set_button_label(GtkWidget *button, const wchar_t *label)
{
    char *utf8 = wchar_to_utf8(label);
    gtk_button_set_label(GTK_BUTTON(button), utf8 != NULL ? utf8 : "");
    free(utf8);
}

static void set_column_title(GtkTreeViewColumn *column, const wchar_t *title)
{
    char *utf8 = wchar_to_utf8(title);
    gtk_tree_view_column_set_title(column, utf8 != NULL ? utf8 : "");
    free(utf8);
}

static void apply_language(AppContext *context)
{
    AppPane *panes[2];
    int pane_index;
    char *title_utf8;
    if (context == NULL) return;
    panes[0] = context->left;
    panes[1] = context->right;
    title_utf8 = wchar_to_utf8(localization_text(context->settings->language, UI_APP_TITLE));
    gtk_window_set_title(GTK_WINDOW(context->window),
                         title_utf8 != NULL ? title_utf8 : "AudioCommander");
    free(title_utf8);
    if (context->refresh_all_button != NULL) {
        char *refresh_label = wchar_to_utf8(
            localization_text(context->settings->language, UI_REFRESH));
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (F5)", refresh_label != NULL ? refresh_label : "Refresh");
        gtk_button_set_label(GTK_BUTTON(context->refresh_all_button), buf);
        free(refresh_label);
    }
    if (context->stop_button != NULL) {
        char *stop_label = wchar_to_utf8(
            localization_text(context->settings->language, UI_STOP));
        gtk_button_set_label(GTK_BUTTON(context->stop_button),
                             stop_label != NULL ? stop_label : "Stop");
        free(stop_label);
    }
    if (context->swap_button != NULL) {
        wchar_t wbuf[128];
        char *swap_label = wchar_to_utf8(
            localization_text(context->settings->language, UI_SWAP));
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (F6)", swap_label != NULL ? swap_label : "Swap");
        if (utf8_to_buffer(buf, wbuf, ARRAYSIZE(wbuf)))
            set_button_label(context->swap_button, wbuf);
        free(swap_label);
    }
    for (pane_index = 0; pane_index < 2; ++pane_index) {
        AppPane *pane = panes[pane_index];
        if (pane == NULL) continue;
        set_button_label(pane->up_button, tr(pane, UI_UP));
        set_button_label(pane->browse_button, tr(pane, UI_BROWSE));
        {
            const char *fk = pane->is_left ? "" : "Ctrl+";
            char buf[128];
            char *copy_utf8 = wchar_to_utf8(pane == context->left ?
                                            tr(pane, UI_COPY_RIGHT) : tr(pane, UI_COPY_LEFT));
            wchar_t wbuf[128];
            snprintf(buf, sizeof(buf), "%s (%sF2)", copy_utf8 ? copy_utf8 : "Copy", fk);
            if (utf8_to_buffer(buf, wbuf, ARRAYSIZE(wbuf)))
                set_button_label(pane->copy_button, wbuf);
            free(copy_utf8);
        }
        {
            const char *fk = pane->is_left ? "" : "Ctrl+";
            char buf[128];
            char *move_utf8 = wchar_to_utf8(pane == context->left ?
                                            tr(pane, UI_MOVE_RIGHT) : tr(pane, UI_MOVE_LEFT));
            wchar_t wbuf[128];
            snprintf(buf, sizeof(buf), "%s (%sF3)", move_utf8 ? move_utf8 : "Move", fk);
            if (utf8_to_buffer(buf, wbuf, ARRAYSIZE(wbuf)))
                set_button_label(pane->move_button, wbuf);
            free(move_utf8);
        }
        {
            const char *fk = pane->is_left ? "" : "Ctrl+";
            char buf[128];
            char *del_utf8 = wchar_to_utf8(tr(pane, UI_DELETE));
            wchar_t wbuf[128];
            snprintf(buf, sizeof(buf), "%s (%sF4)", del_utf8 ? del_utf8 : "Delete", fk);
            if (utf8_to_buffer(buf, wbuf, ARRAYSIZE(wbuf)))
                set_button_label(pane->delete_button, wbuf);
            free(del_utf8);
        }
        set_button_label(pane->settings_button, tr(pane, UI_SETTINGS));
        set_button_label(pane->home_button, tr(pane, UI_HOME));
        set_column_title(pane->name_column, tr(pane, UI_NAME));
        set_column_title(pane->size_column, tr(pane, UI_SIZE));
        set_column_title(pane->duration_column, tr(pane, UI_DURATION));
        app_pane_refresh(pane);
    }
}

static void colorref_to_css(COLORREF color, char *out, size_t count)
{
    snprintf(out, count, "#%02X%02X%02X",
             (unsigned)(color & 0xFF),
             (unsigned)((color >> 8) & 0xFF),
             (unsigned)((color >> 16) & 0xFF));
}

static void apply_theme(AppContext *context)
{
    ThemeColors colors;
    char window_color[8];
    char control_color[8];
    char text_color[8];
    char selection_color[8];
    char selection_text_color[8];
    char summary_color[8];
    char summary_text_color[8];
    char button_color[8];
    char hover_color[8];
    char border_color[8];
    char css[8192];
    char *font_family_utf8;
    const char *font_family_css;
    int font_size;
    GtkCssProvider *provider;
    GdkScreen *screen;
    if (context == NULL) return;
    colors = settings_theme_colors(context->settings->theme);
    colorref_to_css(colors.window, window_color, sizeof(window_color));
    colorref_to_css(colors.control, control_color, sizeof(control_color));
    colorref_to_css(colors.text, text_color, sizeof(text_color));
    colorref_to_css(colors.selection, selection_color, sizeof(selection_color));
    colorref_to_css(colors.selection_text, selection_text_color, sizeof(selection_text_color));
    colorref_to_css(colors.summary, summary_color, sizeof(summary_color));
    colorref_to_css(colors.summary_text, summary_text_color, sizeof(summary_text_color));
    colorref_to_css(colors.button_top, button_color, sizeof(button_color));
    colorref_to_css(colors.hot_border, hover_color, sizeof(hover_color));
    colorref_to_css(colors.border, border_color, sizeof(border_color));
    if (context->settings->font_color != 0)
        colorref_to_css(context->settings->font_color, text_color, sizeof(text_color));
    font_family_utf8 = wchar_to_utf8(context->settings->font_face);
    font_family_css = font_family_utf8 != NULL && font_family_utf8[0] != '\0' ?
        font_family_utf8 : "sans";
    font_size = context->settings->font_points >= 6 && context->settings->font_points <= 72 ?
        context->settings->font_points : 11;
    snprintf(css, sizeof(css),
             "window, .background, dialog, dialog.background, dialog .background { background-color: %s; }\n"
             "* { color: %s; font-family: \"%s\"; font-size: %dpt; }\n"
             "window button, combobox button, spinbutton button, toolbar button { background-image: none; background-color: %s; color: %s; border-color: %s; padding: 0px 6px; min-width: 0; }\n"
             "button:hover, combobox button:hover, spinbutton button:hover { background-color: %s; }\n"
             "fontbutton, colorbutton, fontbutton label, colorbutton label, button label { background-color: %s; color: %s; }\n"
             "entry, spinbutton, combobox, combobox cellview, combobox > .cellview { background-color: %s; color: %s; }\n"
             "treeview.view { background-color: %s; }\n"
             "treeview:selected, treeview:selected:focus { background-color: %s; color: %s; }\n"
             "treeview { border: none; }\n"
             "menu, menuitem, popover, popover.background, modelbutton { background-color: %s; color: %s; }\n"
             "menuitem:hover, modelbutton:hover, modelbutton:selected { background-color: %s; color: %s; }\n"
             "checkbutton, radiobutton, switch, checkbutton label, radiobutton label { background-color: transparent; color: %s; }\n"
             "scale trough { background-color: %s; }\n"
             "scale highlight { background-color: %s; }\n"
             "scrollbar trough { background-color: %s; }\n"
             "scrollbar slider { background-color: %s; }\n"
             "#ac_summary { background-color: %s; color: %s; }\n"
             ".fkey-label { font-size: 7pt; padding: 0; margin: 0; }\n"
             "window button { min-height: 24px; }\n"
             ".ac-bottom-btn { padding: 2px 8px; min-height: 0; font-size: 9pt; }",
             window_color, text_color, font_family_css, font_size,
             button_color, text_color, border_color, hover_color,
             button_color, text_color, control_color, text_color,
             control_color, selection_color, selection_text_color,
             control_color, text_color, selection_color, selection_text_color,
             text_color, control_color, selection_color, control_color,
             button_color, summary_color, summary_text_color);
    if (font_family_utf8 != NULL) free(font_family_utf8);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    screen = gdk_screen_get_default();
    if (screen != NULL) {
        if (context->theme_provider != NULL) {
            gtk_style_context_remove_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(context->theme_provider));
            g_object_unref(context->theme_provider);
        }
        context->theme_provider = g_object_ref(provider);
        gtk_style_context_add_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

static AppTheme theme_at_combo_index(gint index)
{
    int theme;
    gint count = 0;
    for (theme = 0; theme < APP_THEME_COUNT; ++theme) {
        if (!settings_theme_available((AppTheme)theme)) continue;
        if (count == index) return (AppTheme)theme;
        ++count;
    }
    return APP_THEME_PASTEL;
}

static gint theme_combo_index(AppTheme theme)
{
    gint index = 0;
    int candidate;
    for (candidate = 0; candidate < (int)theme; ++candidate)
        if (settings_theme_available((AppTheme)candidate)) ++index;
    return settings_theme_available(theme) ? index : 0;
}

static void on_settings_response(GtkDialog *dialog, gint response, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    GtkWidget *theme_combo;
    GtkWidget *language_combo;
    GtkWidget *font_button;
    GtkWidget *colour_button;
    gint theme_index;
    gint language_index;
    const gchar *fontname;
    GdkRGBA rgba;
    if (response != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }
    theme_combo = g_object_get_data(G_OBJECT(dialog), "ac_theme_combo");
    language_combo = g_object_get_data(G_OBJECT(dialog), "ac_language_combo");
    font_button = g_object_get_data(G_OBJECT(dialog), "ac_font_button");
    colour_button = g_object_get_data(G_OBJECT(dialog), "ac_colour_button");
    theme_index = gtk_combo_box_get_active(GTK_COMBO_BOX(theme_combo));
    language_index = gtk_combo_box_get_active(GTK_COMBO_BOX(language_combo));
    context->settings->theme = theme_at_combo_index(theme_index);
    context->settings->language = language_index >= 0 &&
        language_index < APP_LANGUAGE_COUNT ? (AppLanguage)language_index :
                                              APP_LANGUAGE_ENGLISH;
    {
        GtkWidget *sequential_check =
            g_object_get_data(G_OBJECT(dialog), "ac_sequential_check");
        if (sequential_check != NULL)
            context->settings->sequential_playback =
                gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(sequential_check)) ? TRUE : FALSE;
    }
    {
        GtkWidget *check;
        check = g_object_get_data(G_OBJECT(dialog), "ac_remember_window_check");
        if (check != NULL)
            context->settings->remember_window =
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)) ? TRUE : FALSE;
        check = g_object_get_data(G_OBJECT(dialog), "ac_remember_dirs_check");
        if (check != NULL)
            context->settings->remember_directories =
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)) ? TRUE : FALSE;
    }
    fontname = font_button != NULL ?
        gtk_font_chooser_get_font(GTK_FONT_CHOOSER(font_button)) : NULL;
    if (fontname != NULL && fontname[0] != '\0') {
        const gchar *space = strrchr(fontname, ' ');
        if (space != NULL) {
            char *family = g_strndup(fontname, (gsize)(space - fontname));
            int size = atoi(space + 1);
            if (size >= 6 && size <= 72) context->settings->font_points = size;
            if (family != NULL && family[0] != '\0') {
                utf8_to_buffer(family, context->settings->font_face, LF_FACESIZE);
            }
            g_free(family);
        }
    }
    if (colour_button != NULL) {
        gint initial = GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(dialog), "ac_initial_font_color"));
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colour_button), &rgba);
        {
            COLORREF new_color = (COLORREF)(
                (((COLORREF)(rgba.red * 255.0 + 0.5)) & 0xFF) |
                ((((COLORREF)(rgba.green * 255.0 + 0.5)) & 0xFF) << 8) |
                ((((COLORREF)(rgba.blue * 255.0 + 0.5)) & 0xFF) << 16));
            COLORREF initial_color = (COLORREF)(unsigned int)initial;
            bool same = ((int)(new_color & 0xFF) - (int)(initial_color & 0xFF) < 2) &&
                        ((int)(new_color & 0xFF) - (int)(initial_color & 0xFF) > -2) &&
                        ((int)((new_color >> 8) & 0xFF) - (int)((initial_color >> 8) & 0xFF) < 2) &&
                        ((int)((new_color >> 8) & 0xFF) - (int)((initial_color >> 8) & 0xFF) > -2) &&
                        ((int)((new_color >> 16) & 0xFF) - (int)((initial_color >> 16) & 0xFF) < 2) &&
                        ((int)((new_color >> 16) & 0xFF) - (int)((initial_color >> 16) & 0xFF) > -2);
            if (!same) context->settings->font_color = new_color;
        }
    }
    settings_save(context->settings);
    apply_language(context);
    apply_theme(context);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_settings_clicked(GtkButton *button, gpointer user_data)
{
    AppPane *pane = (AppPane *)user_data;
    AppContext *context = pane->context;
    GtkWidget *dialog;
    GtkWidget *grid;
    GtkWidget *theme_label;
    GtkWidget *language_label;
    GtkWidget *font_label;
    GtkWidget *colour_label;
    GtkWidget *theme_combo;
    GtkWidget *language_combo;
    GtkWidget *font_button;
    GtkWidget *colour_button;
    char *utf8_title;
    char *utf8_theme;
    char *utf8_language;
    char *utf8_font;
    char *utf8_colour;
    char *utf8_cancel;
    char *utf8_ok;
    char fontname[256];
    GdkRGBA rgba;
    ThemeColors colors;
    COLORREF initial_color;
    int theme;
    int language;
    (void)button;
    if (context == NULL) return;
    utf8_title = pane_label(pane, UI_SETTINGS_TITLE);
    utf8_cancel = pane_label(pane, UI_CANCEL);
    utf8_ok = pane_label(pane, UI_OK);
    dialog = gtk_dialog_new_with_buttons(
        utf8_title != NULL ? utf8_title : "Settings", GTK_WINDOW(context->window),
        GTK_DIALOG_MODAL,
        utf8_cancel != NULL ? utf8_cancel : "_Cancel", GTK_RESPONSE_CANCEL,
        utf8_ok != NULL ? utf8_ok : "_OK", GTK_RESPONSE_ACCEPT, NULL);
    free(utf8_title);
    free(utf8_cancel);
    free(utf8_ok);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    utf8_theme = pane_label(pane, UI_THEME);
    utf8_language = pane_label(pane, UI_LANGUAGE);
    utf8_font = pane_label(pane, UI_FONT);
    utf8_colour = pane_label(pane, UI_COLOUR);
    theme_label = gtk_label_new(utf8_theme != NULL ? utf8_theme : "Theme");
    language_label = gtk_label_new(utf8_language != NULL ? utf8_language : "Language");
    font_label = gtk_label_new(utf8_font != NULL ? utf8_font : "Font...");
    colour_label = gtk_label_new(utf8_colour != NULL ? utf8_colour : "Colour...");
    free(utf8_theme);
    free(utf8_language);
    free(utf8_font);
    free(utf8_colour);
    gtk_widget_set_halign(theme_label, GTK_ALIGN_START);
    gtk_widget_set_halign(language_label, GTK_ALIGN_START);
    gtk_widget_set_halign(font_label, GTK_ALIGN_START);
    gtk_widget_set_halign(colour_label, GTK_ALIGN_START);
    theme_combo = gtk_combo_box_text_new();
    language_combo = gtk_combo_box_text_new();
    for (theme = 0; theme < APP_THEME_COUNT; ++theme) {
        char *utf8;
        if (!settings_theme_available((AppTheme)theme)) continue;
        utf8 = wchar_to_utf8(
            localization_theme_name(context->settings->language, (AppTheme)theme));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(theme_combo),
                                       utf8 != NULL ? utf8 : "");
        free(utf8);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo),
                             theme_combo_index(context->settings->theme));
    for (language = 0; language < APP_LANGUAGE_COUNT; ++language) {
        char *utf8 = wchar_to_utf8(settings_language_name((AppLanguage)language));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(language_combo),
                                       utf8 != NULL ? utf8 : "");
        free(utf8);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(language_combo), context->settings->language);
    {
        char *face = wchar_to_utf8(context->settings->font_face);
        snprintf(fontname, sizeof(fontname), "%s %d",
                 face != NULL && face[0] != '\0' ? face : "Sans",
                 context->settings->font_points);
        free(face);
    }
    font_button = gtk_font_button_new_with_font(fontname);
    colors = settings_theme_colors(context->settings->theme);
    initial_color = context->settings->font_color != 0 ?
        context->settings->font_color : colors.text;
    rgba.red = (double)(initial_color & 0xFF) / 255.0;
    rgba.green = (double)((initial_color >> 8) & 0xFF) / 255.0;
    rgba.blue = (double)((initial_color >> 16) & 0xFF) / 255.0;
    rgba.alpha = 1.0;
    colour_button = gtk_color_button_new_with_rgba(&rgba);
    gtk_color_button_set_title(GTK_COLOR_BUTTON(colour_button),
                               utf8_colour != NULL ? utf8_colour : "Text colour");
    gtk_grid_attach(GTK_GRID(grid), theme_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), language_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), language_combo, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), font_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), font_button, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), colour_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), colour_button, 1, 3, 1, 1);
    {
        GtkWidget *sequential_check;
        char *utf8_sequential = pane_label(pane, UI_PLAY_NEXT_AUTOMATICALLY);
        sequential_check = gtk_check_button_new_with_label(
            utf8_sequential != NULL ? utf8_sequential : "Play next file automatically");
        free(utf8_sequential);
        gtk_widget_set_halign(sequential_check, GTK_ALIGN_START);
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(sequential_check),
            context->settings->sequential_playback != FALSE);
        gtk_grid_attach(GTK_GRID(grid), sequential_check, 0, 4, 2, 1);
        g_object_set_data(G_OBJECT(dialog), "ac_sequential_check", sequential_check);
    }
    {
        GtkWidget *remember_window_check;
        char *utf8 = pane_label(pane, UI_REMEMBER_WINDOW);
        remember_window_check = gtk_check_button_new_with_label(
            utf8 != NULL ? utf8 : "Remember window position");
        free(utf8);
        gtk_widget_set_halign(remember_window_check, GTK_ALIGN_START);
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(remember_window_check),
            context->settings->remember_window != FALSE);
        gtk_grid_attach(GTK_GRID(grid), remember_window_check, 0, 5, 2, 1);
        g_object_set_data(G_OBJECT(dialog), "ac_remember_window_check", remember_window_check);
    }
    {
        GtkWidget *remember_dirs_check;
        char *utf8 = pane_label(pane, UI_REMEMBER_DIRECTORIES);
        remember_dirs_check = gtk_check_button_new_with_label(
            utf8 != NULL ? utf8 : "Remember directories");
        free(utf8);
        gtk_widget_set_halign(remember_dirs_check, GTK_ALIGN_START);
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(remember_dirs_check),
            context->settings->remember_directories != FALSE);
        gtk_grid_attach(GTK_GRID(grid), remember_dirs_check, 0, 6, 2, 1);
        g_object_set_data(G_OBJECT(dialog), "ac_remember_dirs_check", remember_dirs_check);
    }
    g_object_set_data(G_OBJECT(dialog), "ac_theme_combo", theme_combo);
    g_object_set_data(G_OBJECT(dialog), "ac_language_combo", language_combo);
    g_object_set_data(G_OBJECT(dialog), "ac_font_button", font_button);
    g_object_set_data(G_OBJECT(dialog), "ac_colour_button", colour_button);
    g_object_set_data(G_OBJECT(dialog), "ac_initial_font_color",
                      GINT_TO_POINTER((gint)initial_color));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    g_signal_connect(dialog, "response", G_CALLBACK(on_settings_response), context);
}

static AppPane *app_pane_create(GtkWidget *parent, const AppSettings *settings, bool is_left)
{
    AppPane *pane = (AppPane *)g_malloc0(sizeof(*pane));
    GtkWidget *toolbar;
    GtkWidget *up_button;
    GtkWidget *browse_button;
    GtkWidget *scrolled;
    GtkWidget *tree_view;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;
    char *utf8_label;
    const char *fkey_prefix = is_left ? "" : "Ctrl+";

    pane->settings = settings;
    pane->is_left = is_left;
    {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            if (!utf8_to_buffer(home, pane->folder, ARRAYSIZE(pane->folder)))
                wcscpy(pane->folder, L"/");
        } else {
            wcscpy(pane->folder, L"/");
        }
    }
    pane->store = gtk_tree_store_new(APP_PANE_COLUMN_COUNT,
                                     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_INT);

    pane->path_entry = gtk_entry_new();
    g_signal_connect(pane->path_entry, "activate", G_CALLBACK(on_entry_activate), pane);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    utf8_label = pane_label(pane, UI_UP);
    up_button = gtk_button_new_with_label(utf8_label != NULL ? utf8_label : "Up");
    free(utf8_label);
    utf8_label = pane_label(pane, UI_BROWSE);
    browse_button = gtk_button_new_with_label(utf8_label != NULL ? utf8_label : "Browse");
    free(utf8_label);
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Copy (%sF2)", fkey_prefix);
        pane->copy_button = gtk_button_new_with_label(buf);
        snprintf(buf, sizeof(buf), "Move (%sF3)", fkey_prefix);
        pane->move_button = gtk_button_new_with_label(buf);
        snprintf(buf, sizeof(buf), "Delete (%sF4)", fkey_prefix);
        pane->delete_button = gtk_button_new_with_label(buf);
    }
    utf8_label = pane_label(pane, UI_SETTINGS);
    pane->settings_button = gtk_button_new_with_label(utf8_label != NULL ? utf8_label : "Settings");
    free(utf8_label);
    utf8_label = pane_label(pane, UI_HOME);
    pane->home_button = gtk_button_new_with_label(utf8_label != NULL ? utf8_label : "Home");
    free(utf8_label);
    g_signal_connect(up_button, "clicked", G_CALLBACK(on_up_clicked), pane);
    g_signal_connect(browse_button, "clicked", G_CALLBACK(on_browse_clicked), pane);
    g_signal_connect(pane->copy_button, "clicked", G_CALLBACK(on_copy_clicked), pane);
    g_signal_connect(pane->move_button, "clicked", G_CALLBACK(on_move_clicked), pane);
    g_signal_connect(pane->delete_button, "clicked", G_CALLBACK(on_delete_clicked), pane);
    g_signal_connect(pane->settings_button, "clicked", G_CALLBACK(on_settings_clicked), pane);
    g_signal_connect(pane->home_button, "clicked", G_CALLBACK(on_home_clicked), pane);
    pane->up_button = up_button;
    pane->browse_button = browse_button;

    gtk_container_add(GTK_CONTAINER(toolbar), up_button);
    gtk_container_add(GTK_CONTAINER(toolbar), browse_button);
    gtk_container_add(GTK_CONTAINER(toolbar), pane->copy_button);
    gtk_container_add(GTK_CONTAINER(toolbar), pane->move_button);
    gtk_container_add(GTK_CONTAINER(toolbar), pane->delete_button);
    gtk_container_add(GTK_CONTAINER(toolbar), pane->settings_button);
    gtk_container_add(GTK_CONTAINER(toolbar), pane->home_button);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(pane->store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), TRUE);
    utf8_label = pane_label(pane, UI_NAME);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);
    column = gtk_tree_view_column_new_with_attributes(
        utf8_label != NULL ? utf8_label : "Name", renderer,
        "text", APP_PANE_COLUMN_NAME, NULL);
    free(utf8_label);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 300);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    pane->name_column = column;
    utf8_label = pane_label(pane, UI_SIZE);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(
        utf8_label != NULL ? utf8_label : "Size", renderer,
        "text", APP_PANE_COLUMN_SIZE, NULL);
    free(utf8_label);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    pane->size_column = column;
    utf8_label = pane_label(pane, UI_DURATION);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    column = gtk_tree_view_column_new_with_attributes(
        utf8_label != NULL ? utf8_label : "Duration", renderer,
        "text", APP_PANE_COLUMN_DURATION, NULL);
    free(utf8_label);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, 90);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    pane->duration_column = column;
    g_signal_connect(pane->name_column, "clicked",
                     G_CALLBACK(on_column_clicked), pane);
    g_signal_connect(pane->size_column, "clicked",
                     G_CALLBACK(on_column_clicked), pane);
    g_signal_connect(pane->duration_column, "clicked",
                     G_CALLBACK(on_column_clicked), pane);
    update_sort_indicator(pane);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), pane);
    g_signal_connect(tree_view, "row-activated",
                     G_CALLBACK(on_row_activated), pane);
    g_signal_connect(tree_view, "button-press-event",
                     G_CALLBACK(on_tree_button_press), pane);
    pane->tree_view = tree_view;

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), tree_view);

    pane->summary_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(pane->summary_label), 0.0);

    gtk_box_pack_start(GTK_BOX(parent), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(parent), pane->path_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(parent), scrolled, TRUE, TRUE, 0);

    gtk_widget_set_sensitive(pane->copy_button, FALSE);
    gtk_widget_set_sensitive(pane->move_button, FALSE);
    gtk_widget_set_sensitive(pane->delete_button, FALSE);

    app_pane_refresh(pane);
    return pane;
}

static int smoke_exit_code;

static gboolean smoke_quit_cb(gpointer data)
{
    (void)data;
    exit(smoke_exit_code);
    return FALSE;
}

static void gui_smoke_quit(void)
{
    g_idle_add(smoke_quit_cb, NULL);
}

static void gui_smoke_fail(const char *reason)
{
    fprintf(stderr, "GUI_SMOKE_FAIL: %s\n", reason);
    fflush(stderr);
    smoke_exit_code = 1;
    gui_smoke_quit();
}

static bool smoke_write_file(const wchar_t *path, const char *content)
{
    char *utf8_path = utf8_encode(path);
    FILE *file;
    bool ok;
    if (utf8_path == NULL) return false;
    file = fopen(utf8_path, "wb");
    free(utf8_path);
    if (file == NULL) return false;
    ok = fwrite(content, 1, strlen(content), file) == strlen(content);
    fclose(file);
    return ok;
}

static void smoke_capture_window(GtkWindow *window, const char *label)
{
    GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    GdkPixbuf *pixbuf;
    int width;
    int height;
    char path[512];
    if (gdk_window == NULL) return;
    width = gdk_window_get_width(gdk_window);
    height = gdk_window_get_height(gdk_window);
    if (width <= 0 || height <= 0) return;
    gdk_window_process_updates(gdk_window, TRUE);
    gdk_flush();
    pixbuf = gdk_pixbuf_get_from_window(gdk_window, 0, 0, width, height);
    if (pixbuf == NULL) return;
    snprintf(path, sizeof(path), "/tmp/opencode/smoke-%s.png", label);
    gdk_pixbuf_save(pixbuf, path, "png", NULL, NULL);
    g_object_unref(pixbuf);
    fprintf(stderr, "GUI_SMOKE capture: %s\n", path);
}

static void smoke_capture_widget_draw(GtkWidget *widget, const char *label)
{
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    char path[512];
    GdkPixbuf *pixbuf;
    cairo_surface_t *surface;
    cairo_t *cr;
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "GUI_SMOKE draw-capture %s: bad size %dx%d\n",
                label, width, height);
        return;
    }
    surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0.20, 0.20, 0.23);
    cairo_paint(cr);
    gtk_widget_draw(widget, cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
    if (pixbuf == NULL) {
        cairo_surface_destroy(surface);
        return;
    }
    snprintf(path, sizeof(path), "/tmp/opencode/smoke-%s.png", label);
    gdk_pixbuf_save(pixbuf, path, "png", NULL, NULL);
    g_object_unref(pixbuf);
    cairo_surface_destroy(surface);
    fprintf(stderr, "GUI_SMOKE draw-capture: %s\n", path);
}

static GtkWidget *smoke_find_dialog(AppContext *context)
{
    GList *toplevels = gtk_window_list_toplevels();
    GList *cursor;
    GtkWidget *found = NULL;
    for (cursor = toplevels; cursor != NULL; cursor = cursor->next) {
        GtkWidget *window = GTK_WIDGET(cursor->data);
        if (window == context->window) continue;
        if (GTK_IS_DIALOG(window)) {
            found = window;
            break;
        }
    }
    g_list_free(toplevels);
    return found;
}

static void smoke_dump_blocking_dialog(int signo)
{
    char buffer[512];
    GList *toplevels;
    GList *cursor;
    (void)signo;
    toplevels = gtk_window_list_toplevels();
    for (cursor = toplevels; cursor != NULL; cursor = cursor->next) {
        GtkWidget *window = GTK_WIDGET(cursor->data);
        if (GTK_IS_DIALOG(window)) {
            GtkWidget *label = gtk_bin_get_child(GTK_BIN(window));
            const char *text = GTK_IS_LABEL(label) ?
                gtk_label_get_label(GTK_LABEL(label)) : "?";
            int length = snprintf(buffer, sizeof(buffer),
                                  "GUI_SMOKE_FAIL blocking dialog: %s\n", text);
            if (length > 0 && (size_t)length < sizeof(buffer))
                (void)write(2, buffer, (size_t)length);
        }
    }
    if (toplevels != NULL) g_list_free(toplevels);
    _exit(1);
}

static bool smoke_make_dir(const wchar_t *path)
{
    char *utf8_path = utf8_encode(path);
    bool ok;
    if (utf8_path == NULL) return false;
    ok = mkdir(utf8_path, 0755) == 0;
    free(utf8_path);
    return ok;
}

static void smoke_select_row_named(AppPane *pane, const char *utf8_name)
{
    GtkTreeIter iter;
    gboolean valid;
    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(pane->store), &iter);
    while (valid) {
        char *name = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(pane->store), &iter,
                           APP_PANE_COLUMN_NAME, &name, -1);
        if (name != NULL && strcmp(name, utf8_name) == 0) {
            GtkTreeSelection *sel =
                gtk_tree_view_get_selection(GTK_TREE_VIEW(pane->tree_view));
            gtk_tree_selection_select_iter(sel, &iter);
            g_free(name);
            return;
        }
        g_free(name);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(pane->store), &iter);
    }
}

static void smoke_dump_widget_tree(GtkWidget *root, int depth)
{
    GList *children;
    GList *cursor;
    GtkAllocation alloc;
    const char *text = NULL;
    char indent[64];
    int i;
    if (root == NULL) return;
    gtk_widget_get_allocation(root, &alloc);
    if (GTK_IS_LABEL(root))
        text = gtk_label_get_text(GTK_LABEL(root));
    else if (GTK_IS_BUTTON(root) && !GTK_IS_TOGGLE_BUTTON(root))
        text = gtk_button_get_label(GTK_BUTTON(root));
    for (i = 0; i < depth && i < 30; ++i) indent[i] = ' ';
    indent[i] = '\0';
    fprintf(stderr,
            "SMOKE_TREE %s%s @%d,%d %dx%d text='%s'\n",
            indent, G_OBJECT_TYPE_NAME(root), alloc.x, alloc.y,
            alloc.width, alloc.height, text != NULL ? text : "");
    if (!GTK_IS_CONTAINER(root)) return;
    children = gtk_container_get_children(GTK_CONTAINER(root));
    for (cursor = children; cursor != NULL; cursor = cursor->next)
        smoke_dump_widget_tree(GTK_WIDGET(cursor->data), depth + 1);
    g_list_free(children);
}

static void smoke_dump_widget_colors(GtkWidget *root)
{    GList *children;
    GList *cursor;
    if (!GTK_IS_CONTAINER(root)) return;
    children = gtk_container_get_children(GTK_CONTAINER(root));
    for (cursor = children; cursor != NULL; cursor = cursor->next) {
        GtkWidget *child = GTK_WIDGET(cursor->data);
        GtkStyleContext *style;
        GdkRGBA fg;
        GdkRGBA bg;
        GtkStateFlags state;
        style = gtk_widget_get_style_context(child);
        state = gtk_widget_get_state_flags(child);
        gtk_style_context_get_color(style, state, &fg);
        gtk_style_context_get(style, state, "background-color", &bg, NULL);
        if (GTK_IS_BUTTON(child) || GTK_IS_ENTRY(child) ||
            GTK_IS_COMBO_BOX(child) || GTK_IS_LABEL(child) ||
            GTK_IS_CHECK_BUTTON(child)) {
            fprintf(stderr,
                    "SMOKE_COLORS %s fg=(%u,%u,%u) bg=(%u,%u,%u)\n",
                    G_OBJECT_TYPE_NAME(child),
                    (unsigned)(fg.red * 255.0 + 0.5),
                    (unsigned)(fg.green * 255.0 + 0.5),
                    (unsigned)(fg.blue * 255.0 + 0.5),
                    (unsigned)(bg.red * 255.0 + 0.5),
                    (unsigned)(bg.green * 255.0 + 0.5),
                    (unsigned)(bg.blue * 255.0 + 0.5));
        }
        if (GTK_IS_CONTAINER(child))
            smoke_dump_widget_colors(child);
    }
    g_list_free(children);
}

static gboolean smoke_merge_seen;

static gboolean smoke_capture_and_dismiss_dialog(gpointer data)
{
    AppContext *context = (AppContext *)data;
    GtkWidget *dialog = smoke_find_dialog(context);
    if (dialog == NULL) return TRUE;
    smoke_capture_window(GTK_WINDOW(dialog), "merge-confirm");
    gtk_widget_destroy(dialog);
    smoke_merge_seen = TRUE;
    return FALSE;
}

static void gui_smoke_run(AppPane *left, AppPane *right)
{
    const char *left_dir = getenv("AUDIOCOMMANDER_SMOKE_LEFT");
    const char *right_dir = getenv("AUDIOCOMMANDER_SMOKE_RIGHT");
    wchar_t wide_left[MAX_PATH];
    wchar_t wide_right[MAX_PATH];
    wchar_t expected[MAX_PATH];
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    if (left_dir == NULL || right_dir == NULL) {
        gui_smoke_fail("env missing");
        return;
    }
    if (!utf8_to_buffer(left_dir, wide_left, ARRAYSIZE(wide_left)) ||
        !utf8_to_buffer(right_dir, wide_right, ARRAYSIZE(wide_right))) {
        gui_smoke_fail("path conversion");
        return;
    }
    {
        wchar_t stale_left[MAX_PATH];
        wchar_t stale_right[MAX_PATH];
        wchar_t stale_l[MAX_PATH];
        wchar_t stale_r[MAX_PATH];
        wchar_t stale_m[MAX_PATH];
        wchar_t stale_n[MAX_PATH];
        const wchar_t *fixtures[] = {
            L"move-me.mp3", L"smoke-dir", L"merge-dir", L"nested-copy",
        };
        size_t index;
        for (index = 0; index < G_N_ELEMENTS(fixtures); ++index) {
            browser_join_path(wide_left, fixtures[index], stale_left,
                              ARRAYSIZE(stale_left));
            browser_join_path(wide_right, fixtures[index], stale_right,
                              ARRAYSIZE(stale_right));
            if (file_ops_exists(stale_left))
                file_ops_delete_permanently(NULL, stale_left);
            if (file_ops_exists(stale_right))
                file_ops_delete_permanently(NULL, stale_right);
        }
        browser_join_path(wide_left, L"move-me.mp3", stale_l, ARRAYSIZE(stale_l));
        browser_join_path(wide_right, L"move-me.mp3", stale_r, ARRAYSIZE(stale_r));
        if (!file_ops_exists(stale_l) && !smoke_write_file(stale_l, "audio-payload")) {
            gui_smoke_fail("cannot create smoke fixture");
            return;
        }
    }
    pane_set_folder(left, wide_left);
    pane_set_folder(right, wide_right);
    {
        AppContext *context = left->context;
        int theme;
        int pump;
        const char *home = getenv("HOME");
        wchar_t home_wide[MAX_PATH];
        wchar_t saved_left_folder[MAX_PATH];
        wcscpy(saved_left_folder, left->folder);
        for (pump = 0;
             pump < 80 && !gtk_widget_get_mapped(GTK_WIDGET(context->window));
             ++pump)
            gtk_main_iteration_do(FALSE);
        gtk_widget_queue_draw(GTK_WIDGET(context->window));
        gtk_window_present(GTK_WINDOW(context->window));
        gdk_window_raise(gtk_widget_get_window(GTK_WINDOW(context->window)));
        for (pump = 0; pump < 80; ++pump)
            gtk_main_iteration_do(FALSE);
        gdk_flush();
        for (theme = 0; theme < APP_THEME_COUNT; ++theme) {
            char label[128];
            GtkWidget *dialog;
            if (!settings_theme_available((AppTheme)theme)) continue;
            context->settings->theme = (AppTheme)theme;
            apply_theme(context);
            gtk_widget_queue_draw(GTK_WIDGET(context->window));
            for (pump = 0; pump < 60; ++pump) gtk_main_iteration_do(FALSE);
            gdk_flush();
            snprintf(label, sizeof(label), "main-%d", theme);
            smoke_capture_window(GTK_WINDOW(context->window), label);
            {
                const char *lbl =
                    gtk_label_get_text(GTK_LABEL(left->summary_label));
                fprintf(stderr,
                        "SMOKE_SUMMARY: text='%s' alloc=%dx%d vis=%d map=%d\n",
                        lbl != NULL ? lbl : "(null)",
                        gtk_widget_get_allocated_width(left->summary_label),
                        gtk_widget_get_allocated_height(left->summary_label),
                        gtk_widget_get_visible(left->summary_label),
                        gtk_widget_get_mapped(left->summary_label));
            }
            on_settings_clicked(GTK_BUTTON(left->settings_button), left);
            for (pump = 0; pump < 80; ++pump) gtk_main_iteration_do(FALSE);
            gdk_flush();
            dialog = smoke_find_dialog(context);
            if (dialog != NULL) {
                GtkWidget *theme_combo =
                    g_object_get_data(G_OBJECT(dialog), "ac_theme_combo");
                GtkWidget *colour_button =
                    g_object_get_data(G_OBJECT(dialog), "ac_colour_button");
                gtk_window_present(GTK_WINDOW(dialog));
                gdk_window_raise(gtk_widget_get_window(dialog));
                gtk_widget_queue_draw(dialog);
                for (pump = 0; pump < 40; ++pump) gtk_main_iteration_do(FALSE);
                gdk_flush();
                snprintf(label, sizeof(label), "settings-%d", theme);
                smoke_capture_window(GTK_WINDOW(dialog), label);
                if (theme == APP_THEME_BLUE)
                    smoke_dump_widget_colors(dialog);
                if (theme == APP_THEME_DARK)
                    smoke_dump_widget_tree(dialog, 0);
                if (theme_combo != NULL) {
                    GList *toplevels;
                    GList *cursor;
                    gtk_combo_box_popup(GTK_COMBO_BOX(theme_combo));
                    for (pump = 0; pump < 30; ++pump) gtk_main_iteration_do(FALSE);
                    toplevels = gtk_window_list_toplevels();
                    for (cursor = toplevels; cursor != NULL; cursor = cursor->next) {
                        GtkWidget *toplevel = GTK_WIDGET(cursor->data);
                        if (toplevel != context->window && toplevel != dialog &&
                            gtk_widget_get_mapped(toplevel)) {
                            snprintf(label, sizeof(label), "settings-popup-%d", theme);
                            smoke_capture_window(GTK_WINDOW(toplevel), label);
                        }
                    }
                    g_list_free(toplevels);
                    gtk_combo_box_popdown(GTK_COMBO_BOX(theme_combo));
                    for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
                }
                {
                    GtkWidget *picker = gtk_color_chooser_dialog_new(NULL,
                        GTK_WINDOW(context->window));
                    gtk_widget_show_all(picker);
                    for (pump = 0; pump < 40; ++pump) gtk_main_iteration_do(FALSE);
                    gdk_flush();
                    snprintf(label, sizeof(label), "colour-picker-%d", theme);
                    smoke_capture_window(GTK_WINDOW(picker), label);
                    gtk_widget_destroy(picker);
                    for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
                    GtkWidget *font_chooser = gtk_font_chooser_dialog_new(NULL,
                        GTK_WINDOW(context->window));
                    gtk_widget_show_all(font_chooser);
                    gtk_window_present(GTK_WINDOW(font_chooser));
                    gdk_window_raise(gtk_widget_get_window(font_chooser));
                    gtk_widget_queue_draw(font_chooser);
                    for (pump = 0; pump < 80; ++pump) gtk_main_iteration_do(FALSE);
                    gdk_flush();
                    snprintf(label, sizeof(label), "font-chooser-%d", theme);
                    smoke_capture_window(GTK_WINDOW(font_chooser), label);
                    if (theme == APP_THEME_DARK)
                        smoke_dump_widget_colors(font_chooser);
                    gtk_widget_destroy(font_chooser);
                    for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
                }
                gtk_widget_destroy(dialog);
                for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
            }
            for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
            gdk_flush();
        }
        context->settings->theme = APP_THEME_PASTEL;
        apply_theme(context);
        if (home != NULL && home[0] != '\0' &&
            utf8_to_buffer(home, home_wide, ARRAYSIZE(home_wide))) {
            on_home_clicked(GTK_BUTTON(left->home_button), left);
            if (wcscmp(left->folder, home_wide) != 0) {
                gui_smoke_fail("home button did not navigate");
                return;
            }
        }
        pane_set_folder(left, saved_left_folder);
        for (pump = 0; pump < 10; ++pump) gtk_main_iteration_do(FALSE);
    }
    {
        int pump;
        GtkWidget *left_root = gtk_widget_get_parent(
            gtk_widget_get_parent(GTK_WIDGET(left->tree_view)));
        GtkWidget *right_root = gtk_widget_get_parent(
            gtk_widget_get_parent(GTK_WIDGET(right->tree_view)));
        gint left_width;
        gint right_width;
        gint orig_left_width;
        gint orig_window_width;
        for (pump = 0; pump < 20; ++pump) {
            if (gtk_main_iteration_do(FALSE)) break;
        }
        left_width = gtk_widget_get_allocated_width(left_root);
        right_width = gtk_widget_get_allocated_width(right_root);
        orig_left_width = left_width;
        orig_window_width =
            gtk_widget_get_allocated_width(GTK_WIDGET(left->context->window));
        fprintf(stderr, "GUI_SMOKE pane widths: left=%d right=%d window=%d\n",
                left_width, right_width, orig_window_width);
        if (left_width <= 0 || right_width <= 0) {
            gui_smoke_fail("panes not allocated");
            return;
        }
        if (left_width > right_width ? left_width - right_width > 400 :
                                       right_width - left_width > 400) {
            gui_smoke_fail("panes not equal width");
            return;
        }
        gtk_window_resize(GTK_WINDOW(left->context->window), 1600, 900);
        for (pump = 0; pump < 20; ++pump) {
            if (gtk_main_iteration_do(FALSE)) break;
        }
        left_width = gtk_widget_get_allocated_width(left_root);
        right_width = gtk_widget_get_allocated_width(right_root);
        orig_window_width =
            gtk_widget_get_allocated_width(GTK_WIDGET(left->context->window));
        fprintf(stderr, "GUI_SMOKE resized widths: left=%d right=%d window=%d\n",
                left_width, right_width, orig_window_width);
        if (orig_window_width > 1280 + 8) {
            if (left_width <= 0 || right_width <= 0 ||
                left_width > right_width ? left_width - right_width > 400 :
                                           right_width - left_width > 400) {
                gui_smoke_fail("panes not equal after resize");
                return;
            }
            if (left_width < orig_left_width) {
                gui_smoke_fail("panes did not rescale with window");
                return;
            }
        }
    }
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(left->tree_view));
    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(left->store), &iter)) {
        gui_smoke_fail("left pane empty");
        return;
    }
    gtk_tree_selection_select_iter(selection, &iter);
    fprintf(stderr, "GUI_SMOKE_STEP: selecting left row, moving\n");
    fflush(stderr);
    alarm(20);
    transfer_between(left, true);
    alarm(0);
    browser_join_path(wide_right, L"move-me.mp3", expected, ARRAYSIZE(expected));
    {
        wchar_t source_gone[MAX_PATH];
        browser_join_path(wide_left, L"move-me.mp3", source_gone, ARRAYSIZE(source_gone));
        if (!file_ops_exists(expected) || file_ops_exists(source_gone)) {
            gui_smoke_fail("move did not land");
            return;
        }
    }
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(right->tree_view));
    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(right->store), &iter)) {
        gui_smoke_fail("right pane empty after move");
        return;
    }
    gtk_tree_selection_select_iter(selection, &iter);
    fprintf(stderr, "GUI_SMOKE_STEP: selecting right row, copying\n");
    fflush(stderr);
    transfer_between(right, false);
    {
        wchar_t copy_back[MAX_PATH];
        wchar_t stay_right[MAX_PATH];
        browser_join_path(wide_left, L"move-me.mp3", copy_back, ARRAYSIZE(copy_back));
        browser_join_path(wide_right, L"move-me.mp3", stay_right, ARRAYSIZE(stay_right));
        if (!file_ops_exists(copy_back) || !file_ops_exists(stay_right)) {
            gui_smoke_fail("copy did not land");
            return;
        }
    }
    {
        wchar_t dir_src[MAX_PATH];
        wchar_t dir_dst[MAX_PATH];
        wchar_t dir_child[MAX_PATH];
        wchar_t expected_child[MAX_PATH];
        signal(SIGALRM, smoke_dump_blocking_dialog);
        browser_join_path(wide_left, L"smoke-dir", dir_src, ARRAYSIZE(dir_src));
        browser_join_path(wide_right, L"smoke-dir", dir_dst, ARRAYSIZE(dir_dst));
        browser_join_path(dir_src, L"inner.mp3", dir_child, ARRAYSIZE(dir_child));
        browser_join_path(dir_dst, L"inner.mp3", expected_child, ARRAYSIZE(expected_child));
        if (file_ops_exists(dir_dst)) file_ops_delete_permanently(NULL, dir_dst);
        if (!file_ops_exists(dir_src)) {
            if (!smoke_make_dir(dir_src) ||
                !smoke_write_file(dir_child, "nested-payload")) {
                gui_smoke_fail("cannot create dir fixture");
                return;
            }
        }
        app_pane_refresh(left);
        alarm(20);
        smoke_select_row_named(left, "smoke-dir");
        fprintf(stderr, "GUI_SMOKE_STEP: selecting left dir row, copying\n");
        fflush(stderr);
        transfer_between(left, false);
        alarm(0);
        if (!file_ops_exists(expected_child) || !file_ops_exists(dir_src)) {
            gui_smoke_fail("directory copy did not land");
            return;
        }
        file_ops_delete_permanently(NULL, dir_dst);
        app_pane_refresh(left);
        alarm(20);
        smoke_select_row_named(left, "smoke-dir");
        fprintf(stderr, "GUI_SMOKE_STEP: selecting left dir row, moving\n");
        fflush(stderr);
        transfer_between(left, true);
        alarm(0);
        if (!file_ops_exists(expected_child) || file_ops_exists(dir_src)) {
            gui_smoke_fail("directory move did not land");
            return;
        }
    }
    {
        wchar_t merge_src[MAX_PATH];
        wchar_t merge_dst[MAX_PATH];
        GtkWidget *swap = left->context->swap_button;
        gboolean saw_merge_dialog = smoke_merge_seen;
        smoke_merge_seen = FALSE;
        browser_join_path(wide_left, L"merge-dir", merge_src, ARRAYSIZE(merge_src));
        browser_join_path(wide_right, L"merge-dir", merge_dst, ARRAYSIZE(merge_dst));
        if (!file_ops_exists(merge_src) && !smoke_make_dir(merge_src)) {
            gui_smoke_fail("cannot create merge fixture");
            return;
        }
        if (file_ops_exists(merge_dst)) file_ops_delete_permanently(NULL, merge_dst);
        if (!smoke_make_dir(merge_dst)) {
            gui_smoke_fail("cannot create merge destination");
            return;
        }
        app_pane_refresh(left);
        smoke_select_row_named(left, "merge-dir");
        g_timeout_add(50, smoke_capture_and_dismiss_dialog, left->context);
        alarm(20);
        transfer_between(left, false);
        alarm(0);
        if (smoke_merge_seen) saw_merge_dialog = TRUE;
        if (!saw_merge_dialog) {
            gui_smoke_fail("merge-confirm dialog did not appear");
            return;
        }
        if (swap == NULL) {
            gui_smoke_fail("swap button missing");
            return;
        }
        {
            wchar_t saved_left[MAX_PATH];
            wchar_t saved_right[MAX_PATH];
            wcscpy(saved_left, left->folder);
            wcscpy(saved_right, right->folder);
            on_swap_clicked(GTK_BUTTON(swap), left->context);
            if (wcscmp(left->folder, saved_right) != 0 ||
                wcscmp(right->folder, saved_left) != 0) {
                gui_smoke_fail("swap button did not swap folders");
                return;
            }
            on_swap_clicked(GTK_BUTTON(swap), left->context);
        }
    }
    /* F5 keyboard accelerator test: verify key-press handler fires */
    {
        GdkEventKey ev = {0};
        ev.type = GDK_KEY_PRESS;
        ev.keyval = GDK_KEY_F5;
        ev.window = gtk_widget_get_window(GTK_WIDGET(left->context->window));
        if (!on_key_pressed(GTK_WIDGET(left->context->window), &ev, left->context)) {
            gui_smoke_fail("F5 key handler did not handle the event");
            return;
        }
    }
    fprintf(stderr, "GUI_SMOKE_PASS\n");
    fflush(stderr);
    gui_smoke_quit();
}

static void on_window_destroy(GtkWidget *window, gpointer user_data)
{
    AppContext *context = (AppContext *)user_data;
    (void)window;
    if (context == NULL || context->settings == NULL) {
        gtk_main_quit();
        return;
    }
    if (context->settings->remember_directories) {
        settings_save_last_directories(context->left->folder, context->right->folder);
    }
    if (context->settings->remember_window) {
        gint width;
        gint height;
        gint x;
        gint y;
        gtk_window_get_size(GTK_WINDOW(window), &width, &height);
        gtk_window_get_position(GTK_WINDOW(window), &x, &y);
        context->settings->window_width = width;
        context->settings->window_height = height;
        context->settings->window_x = x;
        context->settings->window_y = y;
        settings_save(context->settings);
    }
    gtk_main_quit();
}

int app_main(int argc, char *argv[])
{
    GtkWidget *paned;
    GtkWidget *left_box;
    GtkWidget *right_box;
    GtkWidget *main_box;
    GtkWidget *playback_bar;
    GtkWidget *seek_scale;
    GtkWidget *volume_scale;
    AppPane *left_pane;
    AppPane *right_pane;
    AppContext context;
    wchar_t last_left[MAX_PATH];
    wchar_t last_right[MAX_PATH];

    gtk_init(&argc, &argv);
    memset(&context, 0, sizeof(context));
    context.settings = (AppSettings *)g_malloc0(sizeof(AppSettings));
    settings_load(context.settings);
    context.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(context.window), 1280, 720);
    if (context.settings->remember_window &&
        context.settings->window_width >= 320 &&
        context.settings->window_height >= 240) {
        gtk_window_resize(GTK_WINDOW(context.window),
                          context.settings->window_width,
                          context.settings->window_height);
        if (context.settings->window_x >= 0 && context.settings->window_y >= 0) {
            gtk_window_move(GTK_WINDOW(context.window),
                            context.settings->window_x,
                            context.settings->window_y);
        }
    }
    g_signal_connect(context.window, "destroy", G_CALLBACK(on_window_destroy), &context);
    g_signal_connect(context.window, "key-press-event", G_CALLBACK(on_key_pressed), &context);

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    left_pane = app_pane_create(left_box, context.settings, true);
    right_pane = app_pane_create(right_box, context.settings, false);
    left_pane->sibling = right_pane;
    right_pane->sibling = left_pane;
    left_pane->context = &context;
    right_pane->context = &context;
    context.left = left_pane;
    context.right = right_pane;
    g_object_set_data_full(G_OBJECT(left_box), "pane", left_pane, g_free);
    g_object_set_data_full(G_OBJECT(right_box), "pane", right_pane, g_free);

    gtk_paned_pack1(GTK_PANED(paned), left_box, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right_box, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(main_box), paned, TRUE, TRUE, 0);

    {
        GtkWidget *bottom_bar;
        GtkStyleContext *ctx;
        char *refresh_label = wchar_to_utf8(
            localization_text(context.settings->language, UI_REFRESH));
        char *stop_label = wchar_to_utf8(
            localization_text(context.settings->language, UI_STOP));

        bottom_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(bottom_bar), 2);

        gtk_label_set_xalign(GTK_LABEL(left_pane->summary_label), 0.0);
        gtk_widget_set_hexpand(left_pane->summary_label, TRUE);
        gtk_box_pack_start(GTK_BOX(bottom_bar), left_pane->summary_label, TRUE, TRUE, 0);

        {
            char *swap_label = wchar_to_utf8(
                localization_text(context.settings->language, UI_SWAP));
            char buf[128];
            snprintf(buf, sizeof(buf), "%s (F6)", swap_label != NULL ? swap_label : "Swap");
            context.swap_button = gtk_button_new_with_label(buf);
            free(swap_label);
        }
        ctx = gtk_widget_get_style_context(context.swap_button);
        gtk_style_context_add_class(ctx, "ac-bottom-btn");
        gtk_box_pack_start(GTK_BOX(bottom_bar), context.swap_button, FALSE, FALSE, 0);

        context.stop_button = gtk_button_new_with_label(
            stop_label != NULL ? stop_label : "Stop");
        free(stop_label);
        ctx = gtk_widget_get_style_context(context.stop_button);
        gtk_style_context_add_class(ctx, "ac-bottom-btn");
        gtk_box_pack_start(GTK_BOX(bottom_bar), context.stop_button, FALSE, FALSE, 0);

        context.refresh_all_button = gtk_button_new_with_label(
            refresh_label != NULL ? refresh_label : "Refresh (F5)");
        free(refresh_label);
        ctx = gtk_widget_get_style_context(context.refresh_all_button);
        gtk_style_context_add_class(ctx, "ac-bottom-btn");
        gtk_box_pack_start(GTK_BOX(bottom_bar), context.refresh_all_button, FALSE, FALSE, 0);

        gtk_label_set_xalign(GTK_LABEL(right_pane->summary_label), 1.0);
        gtk_widget_set_hexpand(right_pane->summary_label, TRUE);
        gtk_box_pack_start(GTK_BOX(bottom_bar), right_pane->summary_label, TRUE, TRUE, 0);

        g_signal_connect(context.stop_button, "clicked",
                         G_CALLBACK(on_stop_all_clicked), &context);
        g_signal_connect(context.swap_button, "clicked",
                         G_CALLBACK(on_swap_clicked), &context);
        g_signal_connect(context.refresh_all_button, "clicked",
                         G_CALLBACK(on_refresh_all_clicked), &context);
        gtk_box_pack_start(GTK_BOX(main_box), bottom_bar, FALSE, FALSE, 0);
    }

    playback_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(playback_bar), 4);
    seek_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(seek_scale), FALSE);
    gtk_widget_set_hexpand(seek_scale, TRUE);
    context.time_label = gtk_label_new("0:00 / 0:00");
    volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                            0.0, 100.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(volume_scale), FALSE);
    gtk_widget_set_size_request(volume_scale, 140, -1);
    context.syncing = TRUE;
    gtk_range_set_value(GTK_RANGE(volume_scale),
                        (gdouble)context.settings->volume);
    context.syncing = FALSE;
    g_signal_connect(seek_scale, "value-changed",
                     G_CALLBACK(on_seek_changed), &context);
    g_signal_connect(seek_scale, "button-press-event",
                     G_CALLBACK(on_seek_press), &context);
    g_signal_connect(seek_scale, "button-release-event",
                     G_CALLBACK(on_seek_release), &context);
    g_signal_connect(volume_scale, "value-changed",
                     G_CALLBACK(on_volume_changed), &context);
    g_signal_connect(volume_scale, "button-release-event",
                     G_CALLBACK(on_volume_release), &context);
    gtk_box_pack_start(GTK_BOX(playback_bar), seek_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(playback_bar), context.time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(playback_bar), volume_scale, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), playback_bar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(context.window), main_box);
    context.seek_scale = seek_scale;
    context.volume_scale = volume_scale;

    apply_theme(&context);
    apply_language(&context);
    playback_set_volume(context.settings->volume);
    g_timeout_add(500, playback_poll_cb, &context);
    if (context.settings->remember_directories &&
        settings_load_last_directories(last_left, ARRAYSIZE(last_left),
                                       last_right, ARRAYSIZE(last_right))) {
        if (last_left[0] != L'\0') pane_set_folder(left_pane, last_left);
        if (last_right[0] != L'\0') pane_set_folder(right_pane, last_right);
    }

    gtk_widget_show_all(context.window);
    {
        int w = gtk_widget_get_allocated_width(GTK_WIDGET(context.window));
        gtk_paned_set_position(GTK_PANED(paned), w / 2);
    }
    if (getenv("AUDIOCOMMANDER_GUI_SMOKE") != NULL)
        gui_smoke_run(left_pane, right_pane);
    gtk_main();
    return 0;
}
int main(int argc, char *argv[])
{
    return app_main(argc, argv);
}
