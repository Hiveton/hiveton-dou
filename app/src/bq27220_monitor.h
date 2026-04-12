#ifndef BQ27220_MONITOR_H
#define BQ27220_MONITOR_H

#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

void bq27220_monitor_start(void);
void bq27220_monitor_get_status_text(char *buffer, rt_size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
