#ifndef APP_TF_STORAGE_H
#define APP_TF_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool app_tf_card_inserted(void);
bool app_tf_storage_ready(void);
const char *app_tf_mount_root(void);
bool app_tf_build_path(const char *relative_path, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
