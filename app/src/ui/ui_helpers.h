#ifndef XIAOZHI_UI_HELPERS_H
#define XIAOZHI_UI_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "home_screen.h"
#include "lvgl/lvgl.h"
#include "ui_types.h"

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *content;
    xiaozhi_home_screen_refs_t status_refs;
} ui_screen_scaffold_t;

void ui_helpers_init(void);
void ui_helpers_deinit(void);
void ui_helpers_reset_font_cache(void);

lv_coord_t ui_px_x(int32_t value);
lv_coord_t ui_px_y(int32_t value);
lv_coord_t ui_px_w(int32_t value);
lv_coord_t ui_px_h(int32_t value);
uint16_t ui_scaled_font_size(uint16_t figma_size);
lv_font_t *ui_font_get(uint16_t figma_size);
lv_font_t *ui_font_get_actual(uint16_t actual_size);

lv_obj_t *ui_create_screen_base(void);
lv_obj_t *ui_create_card(lv_obj_t *parent,
                         int32_t x,
                         int32_t y,
                         int32_t w,
                         int32_t h,
                         ui_screen_id_t target,
                         bool filled,
                         int32_t radius);
lv_obj_t *ui_create_label(lv_obj_t *parent,
                          const char *text,
                          int32_t x,
                          int32_t y,
                          int32_t w,
                          int32_t h,
                          uint16_t figma_font_size,
                          lv_text_align_t align,
                          bool inverted,
                          bool wrap);
lv_obj_t *ui_create_button(lv_obj_t *parent,
                           int32_t x,
                           int32_t y,
                           int32_t w,
                           int32_t h,
                           const char *text,
                           uint16_t figma_font_size,
                           ui_screen_id_t target,
                           bool filled);
lv_obj_t *ui_create_nav_button(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h,
                               const char *text,
                               ui_screen_id_t target);
lv_obj_t *ui_create_icon_badge(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h,
                               const char *text);
lv_obj_t *ui_create_image_slot(lv_obj_t *parent,
                               int32_t x,
                               int32_t y,
                               int32_t w,
                               int32_t h);
lv_obj_t *ui_create_hidden_label(lv_obj_t *parent);

void ui_attach_nav_event(lv_obj_t *obj, ui_screen_id_t target);
void ui_build_status_bar_ex(lv_obj_t *parent,
                            xiaozhi_home_screen_refs_t *refs,
                            bool enable_detail_touch);
void ui_build_status_bar(lv_obj_t *parent, xiaozhi_home_screen_refs_t *refs);
void ui_build_standard_screen_ex(ui_screen_scaffold_t *scaffold,
                                 lv_obj_t *screen,
                                 const char *title,
                                 ui_screen_id_t back_target,
                                 bool enable_detail_touch);
void ui_build_standard_screen(ui_screen_scaffold_t *scaffold,
                              lv_obj_t *screen,
                              const char *title,
                              ui_screen_id_t back_target);
void ui_build_status_detail_content(lv_obj_t *screen, lv_obj_t *parent);
const xiaozhi_home_screen_refs_t *ui_screen_refs_get(lv_obj_t *screen);
void ui_screen_refs_unregister(lv_obj_t *screen);
bool ui_status_panel_is_visible(void);
void ui_refresh_global_status_bar(void);
void ui_force_refresh_global_status_bar(void);

#ifdef __cplusplus
}
#endif

#endif
