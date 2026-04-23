#ifndef APP_SLEEP_MANAGER_H
#define APP_SLEEP_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t sleep_manager_get_idle_timeout_ms(void);
void sleep_manager_report_activity(void);
bool sleep_manager_should_enter_standby(ui_screen_id_t active_id, uint32_t inactive_ms);
void sleep_manager_on_enter_standby(ui_screen_id_t from_screen);
void sleep_manager_on_exit_standby(ui_screen_id_t target_screen);
bool sleep_manager_is_sleeping(void);
void sleep_manager_request_wakeup(void);
/* Enter or re-enter the true sleep state after standby content refresh. */
void sleep_manager_resume_sleep_cycle(void);

#ifdef __cplusplus
}
#endif

#endif
