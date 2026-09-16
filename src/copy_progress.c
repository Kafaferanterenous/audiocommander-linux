#include "copy_progress.h"

#include "file_ops.h"

#include <wchar.h>

bool copy_progress_should_show(unsigned long long elapsed_ms, bool completed)
{
    return !completed && elapsed_ms >= COPY_PROGRESS_REVEAL_DELAY_MS;
}

bool copy_progress_copy(HWND owner, const wchar_t *source,
                        const wchar_t *destination, bool overwrite,
                        const wchar_t *display_name, int item_number,
                        int item_count, const CopyProgressStrings *strings)
{
    (void)owner;
    (void)display_name;
    (void)item_number;
    (void)item_count;
    (void)strings;
    if (source == NULL || destination == NULL) return false;
    return file_ops_copy_with_progress(source, destination, overwrite, NULL, NULL);
}
