#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <rtthread.h>

void app_watchdog_pet(void);
void app_watchdog_touch_hint(void);
void app_watchdog_input_hint(void);

#endif
