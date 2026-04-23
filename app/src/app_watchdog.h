#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <rtthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum app_watchdog_mode_t
{
    APP_WDT_MODE_BOOT = 0,
    APP_WDT_MODE_ACTIVE,
    APP_WDT_MODE_SLEEP,
    APP_WDT_MODE_LONG_TASK,
    APP_WDT_MODE_SHUTDOWN,
} app_watchdog_mode_t;

typedef enum app_watchdog_module_t
{
    APP_WDT_MODULE_UI = 0,
    APP_WDT_MODULE_NET,
    APP_WDT_MODULE_CAT1,
    APP_WDT_MODULE_XIAOZHI,
    APP_WDT_MODULE_AUDIO,
    APP_WDT_MODULE_READING,
    APP_WDT_MODULE_BUTTON,
    APP_WDT_MODULE_WEATHER,
    APP_WDT_MODULE_COUNT,
} app_watchdog_module_t;

void app_watchdog_set_mode(app_watchdog_mode_t mode);
void app_watchdog_heartbeat(app_watchdog_module_t module);
void app_watchdog_begin_long_task(app_watchdog_module_t module, uint32_t timeout_ms);
void app_watchdog_progress(app_watchdog_module_t module);
void app_watchdog_end_long_task(app_watchdog_module_t module);
void app_watchdog_set_module_required(app_watchdog_module_t module, bool required);

void app_watchdog_pet(void);
void app_watchdog_touch_hint(void);
void app_watchdog_input_hint(void);

#endif
