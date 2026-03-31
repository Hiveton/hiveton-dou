#include <string.h>

#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Home = NULL;

static xiaozhi_home_screen_refs_t s_home_refs;

extern const lv_image_dsc_t home_ai;
extern const lv_image_dsc_t home_calendar;
extern const lv_image_dsc_t home_clock;
extern const lv_image_dsc_t home_music;
extern const lv_image_dsc_t home_pet;
extern const lv_image_dsc_t home_reading;
extern const lv_image_dsc_t home_record;
extern const lv_image_dsc_t home_settings;
extern const lv_image_dsc_t home_weather;

typedef struct
{
    int x;
    int y;
    const lv_image_dsc_t *icon;
    const char *label;
    ui_screen_id_t target;
} ui_home_tile_t;

static const ui_home_tile_t s_home_tiles[] = {
    {18, 58, &home_reading, "阅读", UI_SCREEN_READING_LIST},
    {200, 58, &home_pet, "宠物管理", UI_SCREEN_PET},
    {382, 58, &home_ai, "AI小豆", UI_SCREEN_AI_DOU},
    {18, 256, &home_clock, "时间管理", UI_SCREEN_TIME_MANAGE},
    {200, 256, &home_weather, "天气", UI_SCREEN_WEATHER},
    {382, 256, &home_calendar, "日历", UI_SCREEN_CALENDAR},
    {18, 454, &home_record, "录音", UI_SCREEN_RECORDER},
    {200, 454, &home_music, "音乐", UI_SCREEN_MUSIC_LIST},
    {382, 454, &home_settings, "设置", UI_SCREEN_SETTINGS},
};

static lv_obj_t *create_home_hotspot(lv_obj_t *parent,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     ui_screen_id_t target)
{
    lv_obj_t *zone = lv_obj_create(parent);

    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(zone, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(zone, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_radius(zone, 0, 0);
    lv_obj_set_style_shadow_width(zone, 0, 0);
    lv_obj_set_style_outline_width(zone, 0, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    ui_attach_nav_event(zone, target);
    return zone;
}

const xiaozhi_home_screen_refs_t *ui_home_screen_refs_get(void)
{
    return &s_home_refs;
}

void ui_Home_screen_init(void)
{
    lv_obj_t *section;
    size_t i;

    if (ui_Home != NULL)
    {
        return;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
    ui_Home = ui_create_screen_base();
    s_home_refs.screen = ui_Home;
    ui_build_status_bar(ui_Home, &s_home_refs);

    section = lv_obj_create(ui_Home);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_pos(section, 0, ui_px_y(68));
    lv_obj_set_size(section, ui_px_w(582), ui_px_h(723));

    for (i = 0; i < sizeof(s_home_tiles) / sizeof(s_home_tiles[0]); ++i)
    {
        const ui_home_tile_t *tile = &s_home_tiles[i];
        lv_obj_t *zone = create_home_hotspot(section,
                                             tile->x,
                                             tile->y,
                                             182,
                                             198,
                                             tile->target);
        lv_obj_t *icon = ui_create_image_slot(zone, 51, 28, 80, 80);

        lv_img_set_src(icon, tile->icon);
        ui_create_label(zone,
                        tile->label,
                        0,
                        122,
                        182,
                        30,
                        24,
                        LV_TEXT_ALIGN_CENTER,
                        false,
                        false);
    }
}

void ui_Home_screen_destroy(void)
{
    if (ui_Home != NULL)
    {
        lv_obj_delete(ui_Home);
        ui_Home = NULL;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
}
