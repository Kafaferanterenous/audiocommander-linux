#define _XOPEN_SOURCE 700

#include "file_ops.h"

#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

typedef struct CopyProgressEvidence {
    unsigned int callbacks;
    unsigned long long total;
    unsigned long long transferred;
} CopyProgressEvidence;

static void record_copy_progress(unsigned long long total,
                                 unsigned long long transferred,
                                 void *context)
{
    CopyProgressEvidence *evidence = (CopyProgressEvidence *)context;
    evidence->callbacks++;
    evidence->total = total;
    evidence->transferred = transferred;
}

static int join(const wchar_t *left, const wchar_t *right, wchar_t *out, size_t count)
{
    return swprintf(out, count, L"%ls/%ls", left, right) > 0;
}

static int write_text(const wchar_t *path, const char *text)
{
    char *utf8_path = utf8_encode(path);
    FILE *file;
    int ok;
    if (utf8_path == NULL) return 0;
    file = fopen(utf8_path, "wb");
    free(utf8_path);
    if (file == NULL) return 0;
    ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    fclose(file);
    return ok;
}

static int write_text_at(const char *utf8_path, const char *text)
{
    FILE *file = fopen(utf8_path, "wb");
    int ok;
    if (file == NULL) return 0;
    ok = fwrite(text, 1, strlen(text), file) == strlen(text);
    fclose(file);
    return ok;
}

static int make_dir(const wchar_t *path)
{
    char *utf8_path = utf8_encode(path);
    int ok;
    if (utf8_path == NULL) return 0;
    ok = mkdir(utf8_path, 0755) == 0;
    free(utf8_path);
    return ok;
}

static int exists_utf8(const char *path)
{
    struct stat stat_buffer;
    return stat(path, &stat_buffer) == 0;
}

int main(int argc, char *argv[])
{
    char temp_template[] = "/tmp/audio_file_ops_XXXXXX";
    char trash_template[] = "/tmp/audio_file_ops_trash_XXXXXX";
    char *temp_dir;
    char *trash_dir;
    char temp_utf8[512];
    char trash_utf8[512];
    wchar_t temp_wide[512];
    wchar_t trash_wide[512];
    wchar_t source[1024], copy[1024], moved[1024], tree[1024], child[1024];
    wchar_t destination[1024], copied_child[1024], moved_tree[1024];
    wchar_t trashed_file[1024];
    char trash_files_dir[560];
    char trash_info_dir[560];
    CopyProgressEvidence progress = {0};
    (void)argc;
    (void)argv;

    if (file_ops_delete_disposition_for_drive_type(FILE_OPS_DRIVE_TYPE_REMOVABLE) !=
            FILE_OPS_DELETE_PERMANENT ||
        file_ops_delete_disposition_for_drive_type(FILE_OPS_DRIVE_TYPE_FIXED) !=
            FILE_OPS_DELETE_RECYCLE ||
        file_ops_delete_disposition_for_drive_type(FILE_OPS_DRIVE_TYPE_REMOTE) !=
            FILE_OPS_DELETE_RECYCLE ||
        (file_ops_delete_flags(FILE_OPS_DELETE_RECYCLE) & FILE_OPS_DELETE_FLAG_ALLOW_RECOVER) == 0 ||
        (file_ops_delete_flags(FILE_OPS_DELETE_PERMANENT) & FILE_OPS_DELETE_FLAG_ALLOW_RECOVER) != 0 ||
        (file_ops_delete_flags(FILE_OPS_DELETE_PERMANENT) & FILE_OPS_DELETE_FLAG_CONFIRM) == 0)
        return 12;
    if (file_ops_delete_disposition(NULL) != FILE_OPS_DELETE_RECYCLE ||
        file_ops_delete_disposition(L"") != FILE_OPS_DELETE_RECYCLE ||
        file_ops_delete_disposition(L"/definitely/not/here") != FILE_OPS_DELETE_RECYCLE)
        return 13;

    temp_dir = mkdtemp(temp_template);
    trash_dir = mkdtemp(trash_template);
    if (temp_dir == NULL || trash_dir == NULL) return 2;
    utf8_to_buffer(temp_dir, temp_wide, ARRAYSIZE(temp_wide));
    utf8_to_buffer(trash_dir, trash_wide, ARRAYSIZE(trash_wide));

    join(temp_wide, L"source.txt", source, ARRAYSIZE(source));
    join(temp_wide, L"copy.txt", copy, ARRAYSIZE(copy));
    join(temp_wide, L"moved.txt", moved, ARRAYSIZE(moved));
    if (!write_text(source, "alpha") ||
        !file_ops_copy_with_progress(source, copy, false,
                                     record_copy_progress, &progress)) return 3;
    if (progress.callbacks == 0 || progress.total != 5 ||
        progress.transferred != progress.total) return 4;
    if (file_ops_copy(source, copy, false) || !file_ops_copy(source, copy, true)) return 5;
    if (!file_ops_move(copy, moved, false) || file_ops_exists(copy) || !file_ops_exists(moved)) return 6;

    join(temp_wide, L"tree", tree, ARRAYSIZE(tree));
    join(tree, L"child", child, ARRAYSIZE(child));
    if (!make_dir(tree) || !make_dir(child)) return 7;
    join(child, L"payload.txt", source, ARRAYSIZE(source));
    if (!write_text(source, "nested")) return 8;

    join(temp_wide, L"copy-destination", destination, ARRAYSIZE(destination));
    if (!make_dir(destination)) return 9;
    if (!file_ops_copy_directory(NULL, tree, destination)) return 10;
    join(destination, L"tree/child/payload.txt", copied_child, ARRAYSIZE(copied_child));
    if (!file_ops_exists(copied_child)) return 11;

    join(temp_wide, L"move-destination", destination, ARRAYSIZE(destination));
    if (!make_dir(destination)) return 9;
    if (!file_ops_move_directory(NULL, tree, destination)) return 12;
    join(destination, L"tree/child/payload.txt", moved_tree, ARRAYSIZE(moved_tree));
    if (file_ops_exists(tree) || !file_ops_exists(moved_tree)) return 13;

    join(temp_wide, L"trash-me.txt", trashed_file, ARRAYSIZE(trashed_file));
    if (!write_text(trashed_file, "trash")) return 14;
    setenv("XDG_DATA_HOME", trash_dir, 1);
    if (!file_ops_recycle(NULL, trashed_file) || file_ops_exists(trashed_file)) return 15;
    snprintf(trash_files_dir, sizeof(trash_files_dir), "%s/Trash/files/trash-me.txt", trash_dir);
    snprintf(trash_info_dir, sizeof(trash_info_dir), "%s/Trash/info/trash-me.txt.trashinfo", trash_dir);
    if (!exists_utf8(trash_files_dir) || !exists_utf8(trash_info_dir)) return 16;

    join(temp_wide, L"nuke.txt", source, ARRAYSIZE(source));
    if (!write_text(source, "nuke") || !file_ops_delete_permanently(NULL, source)) return 17;
    if (file_ops_exists(source)) return 18;

    unsetenv("XDG_DATA_HOME");
    snprintf(temp_utf8, sizeof(temp_utf8), "%s", temp_dir);
    snprintf(trash_utf8, sizeof(trash_utf8), "%s", trash_dir);
    {
        wchar_t wipe[512];
        utf8_to_buffer(temp_utf8, wipe, ARRAYSIZE(wipe));
        file_ops_delete_permanently(NULL, wipe);
        utf8_to_buffer(trash_utf8, wipe, ARRAYSIZE(wipe));
        file_ops_delete_permanently(NULL, wipe);
    }
    printf("delete policy, file collision policy, nested copy/move, trash, and permanent delete passed\n");
    return 0;
}
