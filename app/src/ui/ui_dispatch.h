#ifndef UI_DISPATCH_H
#define UI_DISPATCH_H

#include "rtthread.h"
#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_dispatch_init(void);
void ui_dispatch_process_pending(void);

void ui_dispatch_request_activity(void);
void ui_dispatch_request_status_refresh(void);
void ui_dispatch_request_time_refresh(void);
void ui_dispatch_request_weather_refresh(void);
void ui_dispatch_request_standby_refresh(void);
void ui_dispatch_request_exit_standby(void);
void ui_dispatch_request_back(void);
void ui_dispatch_request_hardkey_up(void);
void ui_dispatch_request_hardkey_down(void);
void ui_dispatch_request_poweroff_confirm(void);
void ui_dispatch_request_screen_switch(ui_screen_id_t screen_id);

void ui_dispatch_set_active_screen(ui_screen_id_t screen_id);
ui_screen_id_t ui_dispatch_get_active_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DISPATCH_H */
