#define _XOPEN_SOURCE 700

#include "file_ops.h"

#include "utf8.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define FILE_OPS_COPY_BUFFER_SIZE (64 * 1024)

static char *path_to_utf8(const wchar_t *path)
{
    return utf8_encode(path);
}

bool file_ops_exists(const wchar_t *path)
{
    struct stat stat_buffer;
    char *utf8_path;
    bool exists;
    if (path == NULL || path[0] == L'\0') return false;
    utf8_path = path_to_utf8(path);
    if (utf8_path == NULL) return false;
    exists = stat(utf8_path, &stat_buffer) == 0;
    free(utf8_path);
    return exists;
}

static bool copy_file_bytes(const char *source, const char *destination, bool overwrite,
                            FileOpsCopyProgressCallback callback, void *context)
{
    int source_fd = -1;
    int destination_fd = -1;
    char *buffer;
    struct stat stat_buffer;
    unsigned long long total = 0;
    unsigned long long transferred = 0;
    bool ok = false;

    if (stat(source, &stat_buffer) != 0) return false;
    if (!S_ISREG(stat_buffer.st_mode)) return false;
    total = (unsigned long long)stat_buffer.st_size;
    source_fd = open(source, O_RDONLY);
    if (source_fd < 0) return false;
    if (!overwrite && access(destination, F_OK) == 0) {
        close(source_fd);
        return false;
    }
    destination_fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (destination_fd < 0) {
        close(source_fd);
        return false;
    }
    buffer = (char *)malloc(FILE_OPS_COPY_BUFFER_SIZE);
    if (buffer == NULL) {
        close(destination_fd);
        close(source_fd);
        return false;
    }
    for (;;) {
        ssize_t bytes_read = read(source_fd, buffer, FILE_OPS_COPY_BUFFER_SIZE);
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (bytes_read == 0) {
            ok = true;
            break;
        }
        {
            const char *cursor = buffer;
            ssize_t remaining = bytes_read;
            while (remaining > 0) {
                ssize_t bytes_written = write(destination_fd, cursor, (size_t)remaining);
                if (bytes_written < 0) {
                    if (errno == EINTR) continue;
                    remaining = 0;
                    break;
                }
                cursor += bytes_written;
                remaining -= bytes_written;
                transferred += (unsigned long long)bytes_written;
            }
            if (remaining != 0) break;
            if (callback != NULL) callback(total, transferred, context);
        }
    }
    free(buffer);
    close(destination_fd);
    close(source_fd);
    return ok;
}

bool file_ops_copy_with_progress(const wchar_t *source, const wchar_t *destination,
                                 bool overwrite, FileOpsCopyProgressCallback callback,
                                 void *context)
{
    char *utf8_source;
    char *utf8_destination;
    bool result;
    if (source == NULL || destination == NULL) return false;
    utf8_source = path_to_utf8(source);
    utf8_destination = path_to_utf8(destination);
    if (utf8_source == NULL || utf8_destination == NULL) {
        free(utf8_source);
        free(utf8_destination);
        return false;
    }
    result = copy_file_bytes(utf8_source, utf8_destination, overwrite, callback, context);
    free(utf8_source);
    free(utf8_destination);
    return result;
}

bool file_ops_copy(const wchar_t *source, const wchar_t *destination, bool overwrite)
{
    return file_ops_copy_with_progress(source, destination, overwrite, NULL, NULL);
}

static bool copy_path_recursive(const char *source, const char *destination,
                                FileOpsCopyProgressCallback callback, void *context)
{
    struct stat stat_buffer;
    if (lstat(source, &stat_buffer) != 0) return false;
    if (S_ISREG(stat_buffer.st_mode)) {
        return copy_file_bytes(source, destination, true, callback, context);
    }
    if (S_ISDIR(stat_buffer.st_mode)) {
        DIR *directory;
        struct dirent *entry;
        if (mkdir(destination, 0755) != 0 && errno != EEXIST) return false;
        directory = opendir(source);
        if (directory == NULL) return false;
        while ((entry = readdir(directory)) != NULL) {
            char *child_source;
            char *child_destination;
            bool ok;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            child_source = (char *)malloc(strlen(source) + strlen(entry->d_name) + 2);
            child_destination = (char *)malloc(strlen(destination) + strlen(entry->d_name) + 2);
            if (child_source == NULL || child_destination == NULL) {
                free(child_source);
                free(child_destination);
                closedir(directory);
                return false;
            }
            snprintf(child_source, strlen(source) + strlen(entry->d_name) + 2,
                     "%s/%s", source, entry->d_name);
            snprintf(child_destination, strlen(destination) + strlen(entry->d_name) + 2,
                     "%s/%s", destination, entry->d_name);
            ok = copy_path_recursive(child_source, child_destination, callback, context);
            free(child_source);
            free(child_destination);
            if (!ok) {
                closedir(directory);
                return false;
            }
        }
        closedir(directory);
        return true;
    }
    return false;
}

static bool delete_path_recursive(const char *path)
{
    struct stat stat_buffer;
    if (lstat(path, &stat_buffer) != 0) return errno == ENOENT;
    if (S_ISDIR(stat_buffer.st_mode)) {
        DIR *directory = opendir(path);
        struct dirent *entry;
        if (directory == NULL) return false;
        while ((entry = readdir(directory)) != NULL) {
            char *child;
            bool ok;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            child = (char *)malloc(strlen(path) + strlen(entry->d_name) + 2);
            if (child == NULL) {
                closedir(directory);
                return false;
            }
            snprintf(child, strlen(path) + strlen(entry->d_name) + 2,
                     "%s/%s", path, entry->d_name);
            ok = delete_path_recursive(child);
            free(child);
            if (!ok) {
                closedir(directory);
                return false;
            }
        }
        closedir(directory);
        return rmdir(path) == 0;
    }
    return unlink(path) == 0;
}

bool file_ops_move(const wchar_t *source, const wchar_t *destination, bool overwrite)
{
    char *utf8_source;
    char *utf8_destination;
    bool result = false;
    if (source == NULL || destination == NULL) return false;
    if (file_ops_exists(destination) && !overwrite) return false;
    utf8_source = path_to_utf8(source);
    utf8_destination = path_to_utf8(destination);
    if (utf8_source == NULL || utf8_destination == NULL) {
        free(utf8_source);
        free(utf8_destination);
        return false;
    }
    if (rename(utf8_source, utf8_destination) == 0) {
        result = true;
    } else if (errno == EXDEV) {
        if (copy_file_bytes(utf8_source, utf8_destination, overwrite, NULL, NULL))
            result = unlink(utf8_source) == 0;
    }
    free(utf8_source);
    free(utf8_destination);
    return result;
}

static char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash == NULL ? path : slash + 1;
    char *copy = (char *)malloc(strlen(base) + 1);
    if (copy != NULL) strcpy(copy, base);
    return copy;
}

static bool join_utf8(const char *folder, const char *name, char *out, size_t count)
{
    size_t folder_length = strlen(folder);
    const char *separator = (folder_length > 0 && folder[folder_length - 1] == '/') ? "" : "/";
    return snprintf(out, count, "%s%s%s", folder, separator, name) < (int)count;
}

bool file_ops_copy_directory(HWND owner, const wchar_t *source, const wchar_t *destination_folder)
{
    char *utf8_source;
    char *utf8_destination_folder;
    char *base;
    char *destination;
    bool result = false;
    size_t destination_capacity;
    (void)owner;
    if (source == NULL || destination_folder == NULL) return false;
    utf8_source = path_to_utf8(source);
    utf8_destination_folder = path_to_utf8(destination_folder);
    if (utf8_source == NULL || utf8_destination_folder == NULL) {
        free(utf8_source);
        free(utf8_destination_folder);
        return false;
    }
    base = path_basename(utf8_source);
    destination_capacity = strlen(utf8_destination_folder) + strlen(base) + 2;
    destination = (char *)malloc(destination_capacity);
    if (base == NULL || destination == NULL) {
        free(base);
        free(destination);
        free(utf8_source);
        free(utf8_destination_folder);
        return false;
    }
    if (join_utf8(utf8_destination_folder, base, destination, destination_capacity))
        result = copy_path_recursive(utf8_source, destination, NULL, NULL);
    free(base);
    free(destination);
    free(utf8_source);
    free(utf8_destination_folder);
    return result;
}

bool file_ops_move_directory(HWND owner, const wchar_t *source, const wchar_t *destination_folder)
{
    char *utf8_source;
    char *utf8_destination_folder;
    char *base;
    char *destination;
    size_t destination_capacity;
    bool result = false;
    (void)owner;
    if (source == NULL || destination_folder == NULL) return false;
    utf8_source = path_to_utf8(source);
    utf8_destination_folder = path_to_utf8(destination_folder);
    if (utf8_source == NULL || utf8_destination_folder == NULL) {
        free(utf8_source);
        free(utf8_destination_folder);
        return false;
    }
    base = path_basename(utf8_source);
    destination_capacity = strlen(utf8_destination_folder) + strlen(base) + 2;
    destination = (char *)malloc(destination_capacity);
    if (base == NULL || destination == NULL) {
        free(base);
        free(destination);
        free(utf8_source);
        free(utf8_destination_folder);
        return false;
    }
    if (!join_utf8(utf8_destination_folder, base, destination, destination_capacity)) {
        free(base);
        free(destination);
        free(utf8_source);
        free(utf8_destination_folder);
        return false;
    }
    if (access(destination, F_OK) == 0) {
        result = copy_path_recursive(utf8_source, destination, NULL, NULL) &&
                 delete_path_recursive(utf8_source);
    } else {
        result = rename(utf8_source, destination) == 0;
        if (!result && errno == EXDEV)
            result = copy_path_recursive(utf8_source, destination, NULL, NULL) &&
                     delete_path_recursive(utf8_source);
    }
    free(base);
    free(destination);
    free(utf8_source);
    free(utf8_destination_folder);
    return result;
}

static char *trash_directory(char *out, size_t count)
{
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg_data_home != NULL && xdg_data_home[0] != L'\0') {
        snprintf(out, count, "%s/Trash", xdg_data_home);
        return out;
    }
    if (home != NULL && home[0] != L'\0') {
        snprintf(out, count, "%s/.local/share/Trash", home);
        return out;
    }
    return NULL;
}

static bool ensure_directory(const char *path)
{
    char *copy;
    char *cursor;
    if (path == NULL) return false;
    copy = (char *)malloc(strlen(path) + 1);
    if (copy == NULL) return false;
    strcpy(copy, path);
    for (cursor = copy + 1; *cursor != L'\0'; ++cursor) {
        if (*cursor == '/') {
            *cursor = L'\0';
            if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
                free(copy);
                return false;
            }
            *cursor = '/';
        }
    }
    if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
        free(copy);
        return false;
    }
    free(copy);
    return true;
}

static bool find_free_trash_name(const char *trash_files, const char *base,
                                 char *out, size_t count)
{
    unsigned int suffix = 0;
    size_t base_length = strlen(base);
    while (base_length > 0 && base[base_length - 1] == '/') --base_length;
    for (;;) {
        if (suffix == 0) {
            if ((size_t)snprintf(out, count, "%s/%s", trash_files, base) >= count)
                return false;
        } else {
            if ((size_t)snprintf(out, count, "%s/%s.%u", trash_files, base, suffix) >= count)
                return false;
        }
        if (access(out, F_OK) != 0) return true;
        ++suffix;
    }
}

static bool write_trash_info(const char *trash_info, const char *trash_name,
                             const char *original_path)
{
    char *info_path;
    char *deletion_date = NULL;
    char timestamp[48];
    time_t now;
    struct tm local_time;
    FILE *file;
    const char *base;
    bool ok = false;
    base = strrchr(trash_name, '/');
    base = base == NULL ? trash_name : base + 1;
    info_path = (char *)malloc(strlen(trash_info) + strlen(base) + 64);
    if (info_path == NULL) return false;
    snprintf(info_path, strlen(trash_info) + strlen(base) + 64, "%s/%s.trashinfo",
             trash_info, base);
    now = time(NULL);
    if (localtime_r(&now, &local_time) != NULL) {
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d",
                 local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday,
                 local_time.tm_hour, local_time.tm_min, local_time.tm_sec);
        deletion_date = timestamp;
    }
    file = fopen(info_path, "w");
    if (file != NULL) {
        fprintf(file, "[Trash Info]\n");
        fprintf(file, "Path=%s\n", original_path);
        if (deletion_date != NULL) fprintf(file, "DeletionDate=%s\n", deletion_date);
        ok = fclose(file) == 0;
    }
    free(info_path);
    return ok;
}

bool file_ops_recycle(HWND owner, const wchar_t *path)
{
    char *utf8_path;
    char trash_root[4096];
    char trash_files[4120];
    char trash_info[4120];
    char *trash_name;
    char *base;
    char *original;
    bool result = false;
    (void)owner;
    if (path == NULL || path[0] == L'\0') return false;
    if (trash_directory(trash_root, sizeof(trash_root)) == NULL) return false;
    snprintf(trash_files, sizeof(trash_files), "%s/files", trash_root);
    snprintf(trash_info, sizeof(trash_info), "%s/info", trash_root);
    if (!ensure_directory(trash_files) || !ensure_directory(trash_info)) return false;
    utf8_path = path_to_utf8(path);
    if (utf8_path == NULL) return false;
    base = path_basename(utf8_path);
    trash_name = (char *)malloc(sizeof(trash_files) + strlen(base) + 16);
    original = (char *)malloc(strlen(utf8_path) + 1);
    if (base == NULL || trash_name == NULL || original == NULL) {
        free(base);
        free(trash_name);
        free(original);
        free(utf8_path);
        return false;
    }
    strcpy(original, utf8_path);
    if (find_free_trash_name(trash_files, base, trash_name,
                             sizeof(trash_files) + strlen(base) + 16)) {
        struct stat stat_buffer;
        if (lstat(utf8_path, &stat_buffer) == 0 && S_ISDIR(stat_buffer.st_mode))
            result = copy_path_recursive(utf8_path, trash_name, NULL, NULL) &&
                     delete_path_recursive(utf8_path);
        else
            result = rename(utf8_path, trash_name) == 0 ||
                     (errno == EXDEV &&
                      copy_file_bytes(utf8_path, trash_name, true, NULL, NULL) &&
                      unlink(utf8_path) == 0);
        if (result) write_trash_info(trash_info, trash_name, original);
    }
    free(base);
    free(trash_name);
    free(original);
    free(utf8_path);
    return result;
}

bool file_ops_delete_permanently(HWND owner, const wchar_t *path)
{
    char *utf8_path;
    bool result;
    (void)owner;
    if (path == NULL || path[0] == L'\0') return false;
    utf8_path = path_to_utf8(path);
    if (utf8_path == NULL) return false;
    result = delete_path_recursive(utf8_path);
    free(utf8_path);
    return result;
}

static bool path_on_removable_mount(const char *path)
{
    const char *prefixes[] = { "/media/", "/run/media/", "/mnt/" };
    size_t index;
    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        if (strncmp(path, prefixes[index], strlen(prefixes[index])) == 0) return true;
    }
    return false;
}

FileOpsDeleteDisposition file_ops_delete_disposition_for_drive_type(unsigned int drive_type)
{
    return drive_type == FILE_OPS_DRIVE_TYPE_REMOVABLE ? FILE_OPS_DELETE_PERMANENT :
                                                         FILE_OPS_DELETE_RECYCLE;
}

FileOpsDeleteDisposition file_ops_delete_disposition(const wchar_t *path)
{
    char *utf8_path;
    FileOpsDeleteDisposition disposition;
    if (path == NULL || path[0] == L'\0') return FILE_OPS_DELETE_RECYCLE;
    utf8_path = path_to_utf8(path);
    if (utf8_path == NULL) return FILE_OPS_DELETE_RECYCLE;
    disposition = path_on_removable_mount(utf8_path) ? FILE_OPS_DELETE_PERMANENT :
                                                       FILE_OPS_DELETE_RECYCLE;
    free(utf8_path);
    return disposition;
}

unsigned int file_ops_delete_flags(FileOpsDeleteDisposition disposition)
{
    if (disposition == FILE_OPS_DELETE_RECYCLE)
        return FILE_OPS_DELETE_FLAG_ALLOW_RECOVER;
    return FILE_OPS_DELETE_FLAG_CONFIRM;
}

const char *file_ops_last_error(void)
{
    return strerror(errno);
}
