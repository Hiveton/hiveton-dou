#ifndef UI_RUNTIME_ADAPTER_H
#define UI_RUNTIME_ADAPTER_H

#include "lvgl/lvgl.h"
#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_runtime_screen_init_cb_t)(void);

void ui_runtime_switch_to(ui_screen_id_t target);
void ui_runtime_reload(ui_screen_id_t target);
void ui_runtime_screen_change(lv_obj_t **target,
                              ui_runtime_screen_init_cb_t target_init,
                              uint32_t delay_ms);
lv_obj_t *ui_runtime_get_home_screen(void);
void ui_runtime_ensure_home_screen(void);
ui_screen_id_t ui_runtime_get_active_screen_id(void);

#ifdef __cplusplus
}
#endif

#endif
