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
extern const lv_image_dsc_t aihome_top_home_active;
extern const lv_image_dsc_t aihome_top_home_inactive;
extern const lv_image_dsc_t aihome_top_nav_active;
extern const lv_image_dsc_t aihome_top_nav_inactive;
extern const lv_image_dsc_t aihome_top_settings_active;
extern const lv_image_dsc_t aihome_top_settings_inactive;
extern const lv_image_dsc_t aihome_tab_ai_active;
extern const lv_image_dsc_t aihome_tab_ai_inactive;
extern const lv_image_dsc_t aihome_tab_pet_active;
extern const lv_image_dsc_t aihome_tab_pet_inactive;
extern const lv_image_dsc_t aihome_tab_books_active;
extern const lv_image_dsc_t aihome_tab_books_inactive;
extern const lv_image_dsc_t aihome_tab_calendar_active;
extern const lv_image_dsc_t aihome_tab_calendar_inactive;
extern const lv_image_dsc_t pomodoro_back;

typedef struct
{
    ui_top_tab_t tab;
    const lv_image_dsc_t *active_icon;
    const lv_image_dsc_t *inactive_icon;
    ui_screen_id_t target;
    int x;
    int y;
    int w;
    int h;
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
    uint8_t percent;
    uint8_t charging;
    const lv_image_dsc_t *src;
    size_t i;

    rt_enter_critical();
    percent = s_top_nav_battery_percent;
    charging = s_top_nav_battery_charging;
    rt_exit_critical();

    src = ui_top_nav_battery_src(percent, charging);

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

    rt_enter_critical();
    s_top_nav_battery_percent = snapshot.battery_percent > 100U ? 100U : snapshot.battery_percent;
    s_top_nav_battery_charging = snapshot.charging ? 1U : 0U;
    rt_exit_critical();
}

static void ui_top_nav_battery_async_cb(void *user_data)
{
    (void)user_data;

    rt_enter_critical();
    s_top_nav_battery_async_pending = false;
    rt_exit_critical();
    ui_top_nav_apply_battery();
}

void ui_top_nav_update_battery(uint8_t percent, uint8_t is_charging)
{
    bool should_schedule = false;

    if (percent > 100U)
    {
        percent = 100U;
    }

    rt_enter_critical();
    s_top_nav_battery_percent = percent;
    s_top_nav_battery_charging = is_charging ? 1U : 0U;
    if (!s_top_nav_battery_async_pending)
    {
        s_top_nav_battery_async_pending = true;
        should_schedule = true;
    }
    rt_exit_critical();

    if (should_schedule)
    {
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
        {UI_TOP_TAB_AI, &aihome_top_home_active, &aihome_top_home_inactive, UI_SCREEN_HOME, 27, 19, 41, 41},
        {UI_TOP_TAB_NAV, &aihome_top_nav_active, &aihome_top_nav_inactive, UI_SCREEN_MORE, 111, 19, 41, 41},
        {UI_TOP_TAB_SETTINGS, &aihome_top_settings_active, &aihome_top_settings_inactive, UI_SCREEN_SETTINGS, 187, 20, 39, 39},
    };
    lv_obj_t *bar;
    lv_obj_t *battery;
    size_t i;

    if (parent == NULL)
    {
        return NULL;
    }

    bar = ui_component_plain_obj(parent, 0, 0, 528, 80, 0, LV_OPA_COVER, 0xffffff, 0);
    ui_component_attach_top_swipe(bar);
    battery = ui_create_image_slot(bar, 462, 31, 34, 18);
    ui_top_nav_register_battery(battery);
    ui_component_attach_top_swipe(battery);

    ui_create_label(bar,
                    "12:08",
                    386,
                    25,
                    72,
                    29,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
    {
        const ui_top_nav_item_t *item = &items[i];
        const bool selected = (item->tab == active);
        lv_obj_t *icon = ui_create_image_slot(bar,
                                              item->x,
                                              item->y,
                                              item->w,
                                              item->h);
        ui_img_set_src(icon, selected ? item->active_icon : item->inactive_icon);
        ui_component_attach_top_swipe(icon);

        lv_obj_t *hit = ui_component_plain_obj(bar,
                                               item->x - 14,
                                               0,
                                               item->w + 28,
                                               80,
                                               0,
                                               LV_OPA_TRANSP,
                                               0xffffff,
                                               0);
        ui_component_make_clickable(hit, item->target);
        ui_component_attach_top_swipe(hit);
    }

    return bar;
}

lv_obj_t *ui_secondary_top_nav_action_create(lv_obj_t *parent,
                                             const char *title,
                                             ui_screen_id_t back_target,
                                             const void *right_icon_src,
                                             lv_event_cb_t right_cb,
                                             void *right_user_data)
{
    lv_obj_t *bar;
    lv_obj_t *back_icon;
    lv_obj_t *back_hit;
    lv_obj_t *right_icon;
    lv_obj_t *right_hit;

    if (parent == NULL)
    {
        return NULL;
    }

    bar = ui_component_plain_obj(parent, 0, 0, 528, 90, 0, LV_OPA_COVER, 0xffffff, 0);
    ui_component_attach_top_swipe(bar);

    back_icon = ui_create_image_slot(bar, 36, 28, 24, 28);
    ui_img_set_src(back_icon, &pomodoro_back);
    lv_obj_clear_flag(back_icon, LV_OBJ_FLAG_CLICKABLE);
    ui_component_attach_top_swipe(back_icon);

    back_hit = ui_component_plain_obj(bar, 0, 0, 96, 90, 0, LV_OPA_TRANSP, 0xffffff, 0);
    ui_component_make_clickable(back_hit, back_target);
    ui_component_attach_top_swipe(back_hit);

    ui_create_label(bar,
                    title != NULL ? title : "",
                    114,
                    27,
                    300,
                    36,
                    30,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    if (right_icon_src != NULL && right_cb != NULL)
    {
        right_icon = ui_create_image_slot(bar, 445, 25, 40, 40);
        ui_img_set_src(right_icon, right_icon_src);
        lv_obj_clear_flag(right_icon, LV_OBJ_FLAG_CLICKABLE);
        ui_component_attach_top_swipe(right_icon);

        right_hit = ui_component_plain_obj(bar, 420, 0, 96, 90, 0, LV_OPA_TRANSP, 0xffffff, 0);
        lv_obj_add_flag(right_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(right_hit, right_cb, LV_EVENT_CLICKED, right_user_data);
        ui_component_attach_top_swipe(right_hit);
    }

    return bar;
}

lv_obj_t *ui_secondary_top_nav_create(lv_obj_t *parent, const char *title, ui_screen_id_t back_target)
{
    return ui_secondary_top_nav_action_create(parent, title, back_target, NULL, NULL, NULL);
}

lv_obj_t *ui_bottom_nav_create(lv_obj_t *parent, ui_bottom_tab_t active)
{
    static const ui_bottom_nav_item_t items[] = {
        {UI_BOTTOM_TAB_AI, &aihome_tab_ai_active, &aihome_tab_ai_inactive, "AI小豆", "AI", UI_SCREEN_HOME, 77},
        {UI_BOTTOM_TAB_PET, &aihome_tab_pet_active, &aihome_tab_pet_inactive, "宠物", "Pet", UI_SCREEN_PET, 202},
        {UI_BOTTOM_TAB_BOOKS, &aihome_tab_books_active, &aihome_tab_books_inactive, "书架", "Books", UI_SCREEN_READING_LIST, 326},
        {UI_BOTTOM_TAB_MUSIC, &aihome_tab_calendar_active, &aihome_tab_calendar_inactive, "音乐", "Music", UI_SCREEN_MUSIC_LIST, 451},
    };
    lv_obj_t *bar;
    size_t i;

    if (parent == NULL)
    {
        return NULL;
    }

    bar = ui_component_plain_obj(parent, 0, 682, 528, 110, 0, LV_OPA_COVER, 0xffffff, 0);

    for (i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
    {
        const ui_bottom_nav_item_t *item = &items[i];
        const bool selected = (item->tab == active);
        lv_obj_t *icon = ui_create_image_slot(bar,
                                              item->center_x - 36,
                                              16,
                                              72,
                                              72);
        ui_img_set_src(icon, selected ? item->active_icon : item->inactive_icon);

        lv_obj_t *hit = ui_component_plain_obj(bar, item->center_x - 54, 0, 108, 110, 0, LV_OPA_TRANSP, 0xffffff, 0);
        ui_component_make_pressed_nav(hit, item->target);
    }

    return bar;
}
