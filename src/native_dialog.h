#ifndef FOX_NATIVE_DIALOG_H
#define FOX_NATIVE_DIALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens native OS folder selection dialog.
 * Returns 0 on success (path stored in out_path), non-zero on cancel or failure. */
int native_dialog_browse_folder(const char *title, char *out_path, size_t max_len);

/* Opens native OS file selection dialog (filtering for FOX.EXE / executables).
 * Returns 0 on success (path stored in out_path), non-zero on cancel or failure. */
int native_dialog_browse_file(const char *title, char *out_path, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* FOX_NATIVE_DIALOG_H */
