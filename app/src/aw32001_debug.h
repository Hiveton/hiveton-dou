#ifndef AW32001_DEBUG_H
#define AW32001_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void aw32001_debug_init(void);
void aw32001_debug_poll_once(void);
void aw32001_debug_get_status_text(char *buffer, size_t buffer_size);
void aw32001_debug_ensure_charge_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
