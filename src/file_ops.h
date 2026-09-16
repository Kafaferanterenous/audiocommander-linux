#ifndef AUDIOCOMMANDER_FILE_OPS_H
#define AUDIOCOMMANDER_FILE_OPS_H

#include "platform.h"

#include <stdbool.h>

typedef enum FileOpsDeleteDisposition {
    FILE_OPS_DELETE_RECYCLE,
    FILE_OPS_DELETE_PERMANENT
} FileOpsDeleteDisposition;

typedef void (*FileOpsCopyProgressCallback)(unsigned long long total_bytes,
                                            unsigned long long transferred_bytes,
                                            void *context);

#define FILE_OPS_DRIVE_TYPE_UNKNOWN 0
#define FILE_OPS_DRIVE_TYPE_REMOVABLE 2
#define FILE_OPS_DRIVE_TYPE_FIXED 3
#define FILE_OPS_DRIVE_TYPE_REMOTE 4
#define FILE_OPS_DRIVE_TYPE_CDROM 5
#define FILE_OPS_DRIVE_TYPE_RAMDISK 6

#define FILE_OPS_DELETE_FLAG_NONE 0u
#define FILE_OPS_DELETE_FLAG_CONFIRM 0x0001u
#define FILE_OPS_DELETE_FLAG_ALLOW_RECOVER 0x0002u

bool file_ops_exists(const wchar_t *path);
bool file_ops_copy(const wchar_t *source, const wchar_t *destination, bool overwrite);
bool file_ops_copy_with_progress(const wchar_t *source, const wchar_t *destination,
                                 bool overwrite, FileOpsCopyProgressCallback callback,
                                 void *context);
bool file_ops_move(const wchar_t *source, const wchar_t *destination, bool overwrite);
bool file_ops_copy_directory(HWND owner, const wchar_t *source, const wchar_t *destination_folder);
bool file_ops_move_directory(HWND owner, const wchar_t *source, const wchar_t *destination_folder);
bool file_ops_recycle(HWND owner, const wchar_t *path);
bool file_ops_delete_permanently(HWND owner, const wchar_t *path);
FileOpsDeleteDisposition file_ops_delete_disposition(const wchar_t *path);
FileOpsDeleteDisposition file_ops_delete_disposition_for_drive_type(unsigned int drive_type);
unsigned int file_ops_delete_flags(FileOpsDeleteDisposition disposition);
const char *file_ops_last_error(void);

#endif
