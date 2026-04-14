#ifndef XIAOZHI_UI_STATUS_BAR_H
#define XIAOZHI_UI_STATUS_BAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "home_screen.h"
#include "lvgl/lvgl.h"

void ui_status_bar_component_build(lv_obj_t *parent,
                                   xiaozhi_home_screen_refs_t *refs,
                                   bool enable_detail_touch,
                                   lv_event_cb_t detail_touch_cb);
const xiaozhi_home_screen_refs_t *ui_status_bar_component_refs_get(lv_obj_t *screen);
void ui_status_bar_component_refs_unregister(lv_obj_t *screen);
void ui_status_bar_component_refresh(void);
void ui_status_bar_component_force_refresh(void);
void ui_status_bar_component_update_charge(uint8_t is_charging);
void ui_status_bar_component_update_battery_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif
