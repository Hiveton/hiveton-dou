#include <stdbool.h>
#include <stdint.h>

#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

typedef enum
{
    POMODORO_ACTION_PRIMARY = 0,
    POMODORO_ACTION_RESET = 1,
} pomodoro_action_t;

typedef struct
{
    lv_obj_t *screen;
    lv_obj_t *panel;
    lv_obj_t *status_label;
    lv_obj_t *title_label;
    lv_obj_t *time_label;
    lv_obj_t *detail_label;
    lv_obj_t *hint_label;
    lv_obj_t *progress_bar;
    lv_obj_t *primary_button;
    lv_obj_t *primary_button_label;
    lv_obj_t *reset_button;
    lv_obj_t *reset_button_label;
    lv_timer_t *timer;
} pomodoro_ui_t;

static pomodoro_ui_t s_pomodoro_ui = {0};
static bool s_pomodoro_running = false;
static bool s_pomodoro_completed = false;
static uint32_t s_pomodoro_remaining_ms = 25U * 60U * 1000U;
static uint32_t s_pomodoro_run_started_tick = 0U;
static uint32_t s_pomodoro_run_start_remaining_ms = 25U * 60U * 1000U;

static void pomodoro_timer_cb(lv_timer_t *timer);
static void pomodoro_action_event_cb(lv_event_t *e);

static void pomodoro_sync_remaining(void)
{
    if (!s_pomodoro_running)
    {
        return;
    }

    uint32_t elapsed = lv_tick_elaps(s_pomodoro_run_started_tick);
    if (elapsed >= s_pomodoro_run_start_remaining_ms)
    {
        s_pomodoro_remaining_ms = 0U;
        s_pomodoro_running = false;
        s_pomodoro_completed = true;
        return;
    }

    s_pomodoro_remaining_ms = s_pomodoro_run_start_remaining_ms - elapsed;
}

static void pomodoro_update_timer_state(void)
{
    if (s_pomodoro_ui.screen == NULL)
    {
        return;
    }

    if (s_pomodoro_running)
    {
        if (s_pomodoro_ui.timer == NULL)
        {
            s_pomodoro_ui.timer = lv_timer_create(pomodoro_timer_cb, 1000, NULL);
        }
    }
    else if (s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }
}

static const char *pomodoro_primary_text(void)
{
    if (s_pomodoro_running)
    {
        return ui_i18n_pick("暂停", "Pause");
    }

    if (s_pomodoro_completed)
    {
        return ui_i18n_pick("开始", "Start");
    }

    if (s_pomodoro_remaining_ms < 25U * 60U * 1000U)
    {
        return ui_i18n_pick("继续", "Resume");
    }

    return ui_i18n_pick("开始", "Start");
}

static const char *pomodoro_status_text(void)
{
    if (s_pomodoro_running)
    {
        return ui_i18n_pick("状态：专注中", "Status: Focused");
    }

    if (s_pomodoro_completed)
    {
        return ui_i18n_pick("状态：已完成", "Status: Completed");
    }

    if (s_pomodoro_remaining_ms < 25U * 60U * 1000U)
    {
        return ui_i18n_pick("状态：已暂停", "Status: Paused");
    }

    return ui_i18n_pick("状态：未开始", "Status: Ready");
}

static void pomodoro_render_time(void)
{
    uint32_t remaining = s_pomodoro_remaining_ms;
    uint32_t total = 25U * 60U * 1000U;
    uint32_t elapsed = total - remaining;
    uint32_t minutes = remaining / 60000U;
    uint32_t seconds = (remaining / 1000U) % 60U;
    uint32_t progress = total > 0U ? (remaining * 1000U) / total : 0U;
    uint32_t progress_percent = total > 0U ? (elapsed * 100U) / total : 0U;

    if (s_pomodoro_ui.time_label != NULL)
    {
        lv_label_set_text_fmt(s_pomodoro_ui.time_label, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
    }

    if (s_pomodoro_ui.detail_label != NULL)
    {
        lv_label_set_text_fmt(s_pomodoro_ui.detail_label,
                              "%s  ·  %u%%",
                              ui_i18n_pick("剩余", "Remaining"),
                              (unsigned)progress_percent);
    }

    if (s_pomodoro_ui.progress_bar != NULL)
    {
        lv_bar_set_value(s_pomodoro_ui.progress_bar, (int32_t)progress, LV_ANIM_OFF);
    }
}

static void pomodoro_refresh_ui(void)
{
    pomodoro_sync_remaining();

    if (s_pomodoro_ui.status_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.status_label, pomodoro_status_text());
    }

    if (s_pomodoro_ui.primary_button_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.primary_button_label, pomodoro_primary_text());
    }

    if (s_pomodoro_ui.reset_button_label != NULL)
    {
        lv_label_set_text(s_pomodoro_ui.reset_button_label, ui_i18n_pick("重置", "Reset"));
    }

    pomodoro_render_time();

    if (s_pomodoro_ui.hint_label != NULL)
    {
        const char *hint = s_pomodoro_running
                               ? ui_i18n_pick("专注时页面会每秒刷新，暂停后进度会保留", "The page refreshes every second while running")
                               : ui_i18n_pick("点击开始后即可进入番茄专注，状态会在本轮会话内保留", "Start a focus session and keep the state during this session");
        lv_label_set_text(s_pomodoro_ui.hint_label, hint);
    }
}

static void pomodoro_set_running(bool running)
{
    if (running)
    {
        if (s_pomodoro_remaining_ms == 0U || s_pomodoro_completed)
        {
            s_pomodoro_remaining_ms = 25U * 60U * 1000U;
            s_pomodoro_completed = false;
        }

        s_pomodoro_run_started_tick = lv_tick_get();
        s_pomodoro_run_start_remaining_ms = s_pomodoro_remaining_ms;
        s_pomodoro_running = true;
    }
    else
    {
        pomodoro_sync_remaining();
        s_pomodoro_running = false;
    }

    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

static void pomodoro_reset_session(void)
{
    s_pomodoro_running = false;
    s_pomodoro_completed = false;
    s_pomodoro_remaining_ms = 25U * 60U * 1000U;
    s_pomodoro_run_started_tick = 0U;
    s_pomodoro_run_start_remaining_ms = 25U * 60U * 1000U;
    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

static lv_obj_t *pomodoro_create_button(lv_obj_t *parent,
                                        int32_t x,
                                        int32_t y,
                                        int32_t w,
                                        int32_t h,
                                        const char *text,
                                        bool filled,
                                        pomodoro_action_t action,
                                        lv_obj_t **out_label)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = NULL;

    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(button, ui_px_x(16), 0);
    lv_obj_set_style_border_width(button, ui_px_x(2), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, filled ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_outline_width(button, 0, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, pomodoro_action_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

    label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_font(label, ui_font_get(22), 0);
    lv_obj_set_style_text_color(label, filled ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_center(label);

    if (out_label != NULL)
    {
        *out_label = label;
    }

    return button;
}

static void pomodoro_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    pomodoro_refresh_ui();

    if (!s_pomodoro_running && s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }
}

static void pomodoro_action_event_cb(lv_event_t *e)
{
    pomodoro_action_t action = (pomodoro_action_t)(uintptr_t)lv_event_get_user_data(e);

    if (action == POMODORO_ACTION_PRIMARY)
    {
        if (s_pomodoro_running)
        {
            pomodoro_set_running(false);
        }
        else
        {
            pomodoro_set_running(true);
        }
    }
    else if (action == POMODORO_ACTION_RESET)
    {
        pomodoro_reset_session();
    }
}

lv_obj_t *ui_Pomodoro = NULL;

void ui_Pomodoro_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *panel;

    if (ui_Pomodoro != NULL)
    {
        return;
    }

    ui_Pomodoro = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Pomodoro, ui_i18n_pick("番茄时间", "Pomodoro"), UI_SCREEN_TIME_MANAGE);

    panel = ui_create_card(page.content, 62, 54, 402, 366, UI_SCREEN_NONE, false, 24);

    s_pomodoro_ui.screen = ui_Pomodoro;
    s_pomodoro_ui.panel = panel;
    s_pomodoro_ui.title_label = ui_create_label(panel,
                                                 ui_i18n_pick("番茄专注", "Focus Session"),
                                                 24,
                                                 18,
                                                 220,
                                                 22,
                                                 20,
                                                 LV_TEXT_ALIGN_LEFT,
                                                 false,
                                                 false);
    s_pomodoro_ui.status_label = ui_create_label(panel,
                                                  ui_i18n_pick("状态：未开始", "Status: Ready"),
                                                  24,
                                                  52,
                                                  280,
                                                  22,
                                                  17,
                                                  LV_TEXT_ALIGN_LEFT,
                                                  false,
                                                  false);
    s_pomodoro_ui.time_label = ui_create_label(panel,
                                               "25:00",
                                               0,
                                               92,
                                               402,
                                               88,
                                               62,
                                               LV_TEXT_ALIGN_CENTER,
                                               false,
                                               false);
    s_pomodoro_ui.detail_label = ui_create_label(panel,
                                                 ui_i18n_pick("剩余 25:00 · 0%", "Remaining 25:00 · 0%"),
                                                 24,
                                                 186,
                                                 354,
                                                 24,
                                                 18,
                                                 LV_TEXT_ALIGN_CENTER,
                                                 false,
                                                 false);
    s_pomodoro_ui.progress_bar = lv_bar_create(panel);
    lv_obj_set_pos(s_pomodoro_ui.progress_bar, ui_px_x(24), ui_px_y(220));
    lv_obj_set_size(s_pomodoro_ui.progress_bar, ui_px_w(354), ui_px_h(18));
    lv_bar_set_range(s_pomodoro_ui.progress_bar, 0, 1000);
    lv_bar_set_value(s_pomodoro_ui.progress_bar, 1000, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_pomodoro_ui.progress_bar, ui_px_x(999), 0);
    lv_obj_set_style_bg_color(s_pomodoro_ui.progress_bar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_pomodoro_ui.progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_pomodoro_ui.progress_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_pomodoro_ui.progress_bar, ui_px_x(2), 0);
    lv_obj_set_style_pad_all(s_pomodoro_ui.progress_bar, 0, 0);
    lv_obj_set_style_anim_time(s_pomodoro_ui.progress_bar, 120, 0);
    lv_obj_set_style_radius(s_pomodoro_ui.progress_bar, ui_px_x(999), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_pomodoro_ui.progress_bar, lv_color_hex(0x000000), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_pomodoro_ui.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_pomodoro_ui.hint_label = ui_create_label(panel,
                                               ui_i18n_pick("点击开始后即可进入番茄专注", "Tap Start to begin a focus session"),
                                               24,
                                               251,
                                               354,
                                               42,
                                               17,
                                               LV_TEXT_ALIGN_CENTER,
                                               false,
                                               true);

    s_pomodoro_ui.primary_button = pomodoro_create_button(page.content,
                                                          108,
                                                          480,
                                                          136,
                                                          52,
                                                          pomodoro_primary_text(),
                                                          true,
                                                          POMODORO_ACTION_PRIMARY,
                                                          &s_pomodoro_ui.primary_button_label);
    s_pomodoro_ui.reset_button = pomodoro_create_button(page.content,
                                                        282,
                                                        480,
                                                        136,
                                                        52,
                                                        ui_i18n_pick("重置", "Reset"),
                                                        false,
                                                        POMODORO_ACTION_RESET,
                                                        &s_pomodoro_ui.reset_button_label);

    pomodoro_update_timer_state();
    pomodoro_refresh_ui();
}

void ui_Pomodoro_screen_destroy(void)
{
    if (s_pomodoro_ui.timer != NULL)
    {
        lv_timer_del(s_pomodoro_ui.timer);
        s_pomodoro_ui.timer = NULL;
    }

    s_pomodoro_ui.screen = NULL;
    s_pomodoro_ui.panel = NULL;
    s_pomodoro_ui.status_label = NULL;
    s_pomodoro_ui.title_label = NULL;
    s_pomodoro_ui.time_label = NULL;
    s_pomodoro_ui.detail_label = NULL;
    s_pomodoro_ui.hint_label = NULL;
    s_pomodoro_ui.progress_bar = NULL;
    s_pomodoro_ui.primary_button = NULL;
    s_pomodoro_ui.primary_button_label = NULL;
    s_pomodoro_ui.reset_button = NULL;
    s_pomodoro_ui.reset_button_label = NULL;

    if (ui_Pomodoro != NULL)
    {
        lv_obj_delete(ui_Pomodoro);
        ui_Pomodoro = NULL;
    }
}
