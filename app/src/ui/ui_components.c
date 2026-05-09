#include "ui_components.h"

#include "bq27220_monitor.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

extern const lv_image_dsc_t aihome_battery_0;
extern const lv_image_dsc_t aihome_battery_1;
extern const lv_image_dsc_t aihome_battery_2;
extern const lv_image_dsc_t aihome_battery_3;
extern const lv_image_dsc_t aihome_battery_4;
extern const lv_image_dsc_t aihome_battery_5;
extern const lv_image_dsc_t aihome_battery_charging;
extern const lv_image_dsc_t aihome_tab_ai_active;
extern const lv_image_dsc_t aihome_tab_ai_inactive;
extern const lv_image_dsc_t aihome_tab_pet_active;
extern const lv_image_dsc_t aihome_tab_pet_inactive;
extern const lv_image_dsc_t aihome_tab_books_active;
extern const lv_image_dsc_t aihome_tab_books_inactive;
extern const lv_image_dsc_t aihome_tab_calendar_active;
extern const lv_image_dsc_t aihome_tab_calendar_inactive;

typedef struct
{
    ui_top_tab_t tab;
    const char *zh;
    const char *en;
    const char *active_zh;
    const char *active_en;
    ui_screen_id_t target;
    int x;
    int w;
    int active_x;
    int active_w;
} ui_top_nav_item_t;

typedef struct
{
    ui_bottom_tab_t tab;
    const lv_image_dsc_t *active_icon;
    const lv_image_dsc_t *inactive_icon;
    const char *zh;
    const char *en;
    ui_screen_id_t target;
    int center_x;
} ui_bottom_nav_item_t;

static lv_obj_t *ui_component_plain_obj(lv_obj_t *parent,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        int radius,
                                        lv_opa_t opa,
                                        uint32_t bg,
                                        int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_component_make_clickable(lv_obj_t *obj, ui_screen_id_t target)
{
    if (obj == NULL || target == UI_SCREEN_NONE)
    {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    ui_attach_nav_event(obj, target);
}

static void ui_component_make_pressed_nav(lv_obj_t *obj, ui_screen_id_t target)
{
    if (obj == NULL || target == UI_SCREEN_NONE)
    {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    ui_attach_nav_pressed_event(obj, target);
}

static void ui_component_attach_top_swipe(lv_obj_t *obj)
{
    if (obj == NULL)
    {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, ui_status_panel_handle_top_swipe, LV_EVENT_GESTURE, NULL);
}

#define UI_TOP_NAV_BATTERY_REF_MAX 12

static lv_obj_t *s_top_nav_battery_refs[UI_TOP_NAV_BATTERY_REF_MAX];
static uint8_t s_top_nav_battery_percent = 100U;
static uint8_t s_top_nav_battery_charging = 0U;
static bool s_top_nav_battery_async_pending = false;

static const lv_image_dsc_t *ui_top_nav_battery_src(uint8_t percent, uint8_t is_charging)
{
    if (is_charging)
    {
        return &aihome_battery_charging;
    }

    if (percent == 0U)
    {
        return &aihome_battery_0;
    }
    if (percent <= 20U)
    {
        return &aihome_battery_1;
    }
    if (percent <= 40U)
    {
        return &aihome_battery_2;
    }
    if (percent <= 60U)
    {
        return &aihome_battery_3;
    }
    if (percent <= 80U)
    {
        return &aihome_battery_4;
    }

    return &aihome_battery_5;
}

static void ui_top_nav_apply_battery(void)
{
    const lv_image_dsc_t *src = ui_top_nav_battery_src(s_top_nav_battery_percent,
                                                       s_top_nav_battery_charging);
    size_t i;

    for (i = 0; i < UI_TOP_NAV_BATTERY_REF_MAX; ++i)
    {
        if (s_top_nav_battery_refs[i] != NULL)
        {
            ui_img_set_src(s_top_nav_battery_refs[i], src);
        }
    }
}

static void ui_top_nav_load_battery_snapshot(void)
{
    bq27220_power_snapshot_t snapshot = {0};

    bq27220_monitor_get_power_snapshot(&snapshot);
    if (!snapshot.valid)
    {
        return;
    }

    s_top_nav_battery_percent = snapshot.battery_percent > 100U ? 100U : snapshot.battery_percent;
    s_top_nav_battery_charging = snapshot.charging ? 1U : 0U;
}

static void ui_top_nav_battery_async_cb(void *user_data)
{
    (void)user_data;

    s_top_nav_battery_async_pending = false;
    ui_top_nav_apply_battery();
}

void ui_top_nav_update_battery(uint8_t percent, uint8_t is_charging)
{
    if (percent > 100U)
    {
        percent = 100U;
    }

    s_top_nav_battery_percent = percent;
    s_top_nav_battery_charging = is_charging ? 1U : 0U;

    if (!s_top_nav_battery_async_pending)
    {
        s_top_nav_battery_async_pending = true;
        lv_async_call(ui_top_nav_battery_async_cb, NULL);
    }
}

static void ui_top_nav_battery_delete_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    size_t i;

    for (i = 0; i < UI_TOP_NAV_BATTERY_REF_MAX; ++i)
    {
        if (s_top_nav_battery_refs[i] == target)
        {
            s_top_nav_battery_refs[i] = NULL;
        }
    }
}

static void ui_top_nav_register_battery(lv_obj_t *battery)
{
    size_t i;

    if (battery == NULL)
    {
        return;
    }

    for (i = 0; i < UI_TOP_NAV_BATTERY_REF_MAX; ++i)
    {
        if (s_top_nav_battery_refs[i] == battery)
        {
            ui_top_nav_load_battery_snapshot();
            ui_top_nav_apply_battery();
            return;
        }
    }

    for (i = 0; i < UI_TOP_NAV_BATTERY_REF_MAX; ++i)
    {
        if (s_top_nav_battery_refs[i] == NULL)
        {
            s_top_nav_battery_refs[i] = battery;
            lv_obj_add_event_cb(battery, ui_top_nav_battery_delete_cb, LV_EVENT_DELETE, NULL);
            ui_top_nav_load_battery_snapshot();
            ui_top_nav_apply_battery();
            return;
        }
    }
}

lv_obj_t *ui_top_nav_create(lv_obj_t *parent, ui_top_tab_t active)
{
    static const ui_top_nav_item_t items[] = {
        {UI_TOP_TAB_AI, "Wodle", "Wodle", NULL, NULL, UI_SCREEN_NONE, 14, 105, 14, 105},
        {UI_TOP_TAB_MUSIC, "音乐", "Music", NULL, NULL, UI_SCREEN_MUSIC_LIST, 126, 70, 126, 70},
        {UI_TOP_TAB_WEATHER, "天气", "Weather", NULL, NULL, UI_SCREEN_WEATHER, 194, 70, 194, 70},
        {UI_TOP_TAB_POMODORO, "番茄", "Focus", NULL, NULL, UI_SCREEN_POMODORO, 262, 72, 262, 72},
        {UI_TOP_TAB_SETTINGS, "设置", "Settings", NULL, NULL, UI_SCREEN_SETTINGS, 370, 78, 370, 78},
    };
    lv_obj_t *bar;
    lv_obj_t *line;
    lv_obj_t *battery;
    size_t i;

    if (parent == NULL)
    {
        return NULL;
    }

    bar = ui_component_plain_obj(parent, 0, 0, 528, 63, 0, LV_OPA_COVER, 0xffffff, 0);
    ui_component_attach_top_swipe(bar);
    line = ui_component_plain_obj(bar, 0, 62, 528, 1, 0, LV_OPA_COVER, 0x4d5668, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    battery = ui_create_image_slot(bar, 489, 13, 32, 37);
    ui_top_nav_register_battery(battery);
    ui_component_attach_top_swipe(battery);

    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
    {
        const ui_top_nav_item_t *item = &items[i];
        const bool is_logo = (item->tab == UI_TOP_TAB_AI);
        const bool selected = (!is_logo && item->tab == active);
        const bool pill_selected = (is_logo || selected);
        lv_obj_t *label_parent = bar;
        const int item_x = pill_selected ? item->active_x : item->x;
        const int item_w = pill_selected ? item->active_w : item->w;
        const char *text_zh = (pill_selected && item->active_zh != NULL) ? item->active_zh : item->zh;
        const char *text_en = (pill_selected && item->active_en != NULL) ? item->active_en : item->en;
        int label_x = item_x;
        int label_y = 16;
        uint16_t font_size = pill_selected ? 23 : 24;

        if (pill_selected)
        {
            lv_obj_t *pill = ui_component_plain_obj(bar, item_x, 10, item_w, 38, 19, LV_OPA_COVER, 0x30384f, 0);
            lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
            label_parent = pill;
            label_x = 0;
            label_y = 8;
        }

        lv_obj_t *label = ui_create_label(label_parent,
                                          ui_i18n_pick(text_zh, text_en),
                                          label_x,
                                          label_y,
                                          item_w,
                                          30,
                                          font_size,
                                          LV_TEXT_ALIGN_CENTER,
                                          false,
                                          false);
        if (pill_selected)
        {
            lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        }

        if (!is_logo)
        {
            lv_obj_t *hit = ui_component_plain_obj(bar, item_x - 8, 0, item_w + 16, 63, 0, LV_OPA_TRANSP, 0xffffff, 0);
            ui_component_make_clickable(hit, item->target);
            ui_component_attach_top_swipe(hit);
        }
    }

    return bar;
}

lv_obj_t *ui_bottom_nav_create(lv_obj_t *parent, ui_bottom_tab_t active)
{
    static const ui_bottom_nav_item_t items[] = {
        {UI_BOTTOM_TAB_AI, &aihome_tab_ai_active, &aihome_tab_ai_inactive, "AI小豆", "AI", UI_SCREEN_HOME, 77},
        {UI_BOTTOM_TAB_PET, &aihome_tab_pet_active, &aihome_tab_pet_inactive, "宠物", "Pet", UI_SCREEN_PET, 202},
        {UI_BOTTOM_TAB_BOOKS, &aihome_tab_books_active, &aihome_tab_books_inactive, "书架", "Books", UI_SCREEN_READING_LIST, 326},
        {UI_BOTTOM_TAB_CALENDAR, &aihome_tab_calendar_active, &aihome_tab_calendar_inactive, "日历", "Calendar", UI_SCREEN_CALENDAR, 451},
    };
    lv_obj_t *bar;
    lv_obj_t *line;
    size_t i;

    if (parent == NULL)
    {
        return NULL;
    }

    bar = ui_component_plain_obj(parent, 0, 710, 528, 82, 0, LV_OPA_COVER, 0xffffff, 0);
    line = ui_component_plain_obj(bar, 0, 0, 528, 1, 0, LV_OPA_COVER, 0x4d5668, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);

    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
    {
        const ui_bottom_nav_item_t *item = &items[i];
        const bool selected = (item->tab == active);
        lv_obj_t *icon = ui_create_image_slot(bar,
                                              item->center_x - 58,
                                              0,
                                              116,
                                              82);
        ui_img_set_src(icon, selected ? item->active_icon : item->inactive_icon);

        lv_obj_t *hit = ui_component_plain_obj(bar, item->center_x - 58, 0, 116, 82, 0, LV_OPA_TRANSP, 0xffffff, 0);
        ui_component_make_pressed_nav(hit, item->target);
    }

    return bar;
}
