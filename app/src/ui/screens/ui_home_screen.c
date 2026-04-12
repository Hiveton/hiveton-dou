#include <string.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "../../aw32001_debug.h"
#include "../../bq27220_monitor.h"
#include "cat1_modem.h"
#include "../../xiaozhi/weather/weather.h"

lv_obj_t *ui_Home = NULL;

static xiaozhi_home_screen_refs_t s_home_refs;
static lv_obj_t *s_aw32001_debug_panel = NULL;
static lv_obj_t *s_aw32001_debug_label = NULL;
static lv_obj_t *s_bq27220_debug_label = NULL;
static lv_obj_t *s_cat1_debug_label = NULL;
static lv_timer_t *s_aw32001_debug_timer = NULL;

#define AW32001_DEBUG_REFRESH_MS 1000U
#define AW32001_DEBUG_STATUS_TEXT_MAX 140U

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
    const lv_image_dsc_t *icon;
    const char *label_zh;
    const char *label_en;
    ui_screen_id_t target;
} ui_home_tile_t;

static const ui_home_tile_t s_home_tiles[] = {
    {&home_reading, "阅读", "Reading", UI_SCREEN_READING_LIST},
    {&home_pet, "宠物管理", "Pet", UI_SCREEN_PET},
    {&home_ai, "AI小豆", "AI Dou", UI_SCREEN_AI_DOU},
    {&home_clock, "时间管理", "Time", UI_SCREEN_TIME_MANAGE},
    {&home_weather, "天气", "Weather", UI_SCREEN_WEATHER},
    {&home_calendar, "日历", "Calendar", UI_SCREEN_CALENDAR},
    {&home_record, "录音", "Recorder", UI_SCREEN_RECORDER},
    {&home_music, "音乐", "Music", UI_SCREEN_MUSIC_LIST},
    {&home_settings, "设置", "Settings", UI_SCREEN_SETTINGS},
};

static void ui_home_aw32001_debug_refresh_cb(lv_timer_t *timer)
{
    char text[AW32001_DEBUG_STATUS_TEXT_MAX];
    char bq_text[AW32001_DEBUG_STATUS_TEXT_MAX];
    char cat1_text[AW32001_DEBUG_STATUS_TEXT_MAX];

    LV_UNUSED(timer);

    aw32001_debug_poll_once();
    aw32001_debug_get_status_text(text, sizeof(text));
    bq27220_monitor_get_status_text(bq_text, sizeof(bq_text));
    cat1_modem_get_status_text(cat1_text, sizeof(cat1_text));
    if (s_aw32001_debug_label != NULL)
    {
        lv_label_set_text(s_aw32001_debug_label, text);
    }
    if (s_bq27220_debug_label != NULL)
    {
        lv_label_set_text(s_bq27220_debug_label, bq_text);
    }
    if (s_cat1_debug_label != NULL)
    {
        lv_label_set_text(s_cat1_debug_label, cat1_text);
    }
}

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
    char debug_status[AW32001_DEBUG_STATUS_TEXT_MAX];
    char bq_debug_status[AW32001_DEBUG_STATUS_TEXT_MAX];
    char cat1_debug_status[AW32001_DEBUG_STATUS_TEXT_MAX];
    size_t i;
    size_t visible_index = 0U;

    if (ui_Home != NULL)
    {
        return;
    }

    memset(&s_home_refs, 0, sizeof(s_home_refs));
    ui_Home = ui_create_screen_base();
    s_home_refs.screen = ui_Home;
    ui_build_status_bar(ui_Home, &s_home_refs);
    (void)cat1_modem_request_online();

    aw32001_debug_init();
    section = lv_obj_create(ui_Home);
    lv_obj_remove_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_pos(section, 0, ui_px_y(68));
    lv_obj_set_size(section, ui_px_w(528), ui_px_h(724));

    s_aw32001_debug_panel = ui_create_card(ui_Home, 12, 78, 504, 96, UI_SCREEN_NONE, false, 16);
    s_aw32001_debug_label = ui_create_label(s_aw32001_debug_panel,
                                            "AW32001: init...",
                                            10,
                                            10,
                                            484,
                                            20,
                                            13,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            true);
    s_bq27220_debug_label = ui_create_label(s_aw32001_debug_panel,
                                            "BQ27220: init...",
                                            10,
                                            36,
                                            484,
                                            20,
                                            13,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            true);
    s_cat1_debug_label = ui_create_label(s_aw32001_debug_panel,
                                            "4G: init...",
                                            10,
                                            62,
                                            484,
                                            20,
                                            13,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            true);
    aw32001_debug_poll_once();
    aw32001_debug_get_status_text(debug_status, sizeof(debug_status));
    bq27220_monitor_get_status_text(bq_debug_status, sizeof(bq_debug_status));
    cat1_modem_get_status_text(cat1_debug_status, sizeof(cat1_debug_status));
    lv_label_set_text(s_aw32001_debug_label, debug_status);
    lv_label_set_text(s_bq27220_debug_label, bq_debug_status);
    lv_label_set_text(s_cat1_debug_label, cat1_debug_status);
    if (s_aw32001_debug_timer == NULL)
    {
        s_aw32001_debug_timer = lv_timer_create(ui_home_aw32001_debug_refresh_cb,
                                               AW32001_DEBUG_REFRESH_MS,
                                               NULL);
    }

    for (i = 0; i < sizeof(s_home_tiles) / sizeof(s_home_tiles[0]); ++i)
    {
        const ui_home_tile_t *tile = &s_home_tiles[i];
        int x;
        int y;
        lv_obj_t *zone;
        lv_obj_t *icon;

        if (tile->target == UI_SCREEN_WEATHER &&
            !xiaozhi_weather_is_home_entry_enabled())
        {
            continue;
        }

        x = 18 + (int)(visible_index % 3U) * 168;
        y = 178 + (int)(visible_index / 3U) * 198;
        zone = create_home_hotspot(section,
                                             x,
                                             y,
                                             156,
                                             198,
                                             tile->target);
        icon = ui_create_image_slot(zone, 38, 28, 80, 80);

        lv_img_set_src(icon, tile->icon);
        ui_create_label(zone,
                        ui_i18n_pick(tile->label_zh, tile->label_en),
                        0,
                        122,
                        156,
                        30,
                        24,
                        LV_TEXT_ALIGN_CENTER,
                        false,
                        false);
        ++visible_index;
    }
}

void ui_Home_screen_destroy(void)
{
    if (ui_Home != NULL)
    {
        lv_obj_delete(ui_Home);
        ui_Home = NULL;
    }
    if (s_aw32001_debug_timer != NULL)
    {
        lv_timer_delete(s_aw32001_debug_timer);
        s_aw32001_debug_timer = NULL;
    }
    s_aw32001_debug_panel = NULL;
    s_aw32001_debug_label = NULL;
    s_bq27220_debug_label = NULL;
    s_cat1_debug_label = NULL;

    memset(&s_home_refs, 0, sizeof(s_home_refs));
}
