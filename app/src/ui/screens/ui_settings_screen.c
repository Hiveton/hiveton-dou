#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"
#include "config/app_config.h"
#include "network/app_network.h"
#include "network/net_manager.h"
#include "rtthread.h"

#include <stdint.h>
#include <string.h>

lv_obj_t *ui_Settings = NULL;

typedef const char *(*settings_text_fn_t)(void);

typedef enum
{
    SETTINGS_ROW_ACTION_NONE = 0,
    SETTINGS_ROW_ACTION_NAV,
    SETTINGS_ROW_ACTION_TOGGLE_BT,
    SETTINGS_ROW_ACTION_TOGGLE_4G,
} settings_row_action_t;

typedef struct
{
    const char *zh;
    const char *en;
    settings_text_fn_t summary_fn;
    settings_row_action_t action;
    ui_screen_id_t target;
} settings_row_t;

#define SETTINGS_BT_ROW_INDEX 5U
#define SETTINGS_4G_ROW_INDEX 6U

static const char *ui_settings_language_summary(void);
static const char *ui_settings_sleep_summary(void);
static const char *ui_settings_lock_screen_summary(void);
static const char *ui_settings_page_turn_summary(void);
static const char *ui_settings_bluetooth_summary(void);
static const char *ui_settings_4g_summary(void);
static void ui_settings_confirm_close(void);
static void ui_settings_row_event_cb(lv_event_t *e);

static lv_obj_t *settings_plain_obj(lv_obj_t *parent,
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
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

ui_settings_language_t ui_settings_get_language(void)
{
    ui_settings_language_t language = app_config_get_ui_language();

    if (language >= UI_SETTINGS_LANGUAGE_COUNT)
    {
        language = UI_SETTINGS_LANGUAGE_ZH_CN;
    }

    return language;
}

void ui_settings_set_language(ui_settings_language_t language)
{
    if (language >= UI_SETTINGS_LANGUAGE_COUNT)
    {
        return;
    }

    app_config_set_ui_language(language);
    app_config_save();
}

const char *ui_settings_get_language_label(void)
{
    switch (ui_settings_get_language())
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "English";
    case UI_SETTINGS_LANGUAGE_ZH_TW:
        return "中文（繁体中文）";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "简体中文";
    }
}

static const char *ui_settings_language_summary(void)
{
    return ui_settings_get_language_label();
}

static const char *ui_settings_sleep_summary(void)
{
    static char buffer[24];
    uint32_t timeout_sec = app_config_get_display_standby_timeout_sec();
    uint32_t hours;
    uint32_t minutes;

    if (timeout_sec == 0U)
    {
        return ui_i18n_pick("永不", "Never");
    }

    if (timeout_sec < 60U)
    {
        rt_snprintf(buffer,
                    sizeof(buffer),
                    ui_i18n_pick("%u秒", "%u sec"),
                    (unsigned int)timeout_sec);
        return buffer;
    }

    if ((timeout_sec % 3600U) == 0U)
    {
        hours = timeout_sec / 3600U;
        rt_snprintf(buffer,
                    sizeof(buffer),
                    ui_i18n_pick("%u小时", "%u h"),
                    (unsigned int)hours);
        return buffer;
    }

    minutes = (timeout_sec + 59U) / 60U;
    rt_snprintf(buffer,
                sizeof(buffer),
                ui_i18n_pick("%u分钟", "%u min"),
                (unsigned int)minutes);
    return buffer;
}

static const char *ui_settings_lock_screen_summary(void)
{
    return ui_i18n_pick("时钟", "Clock");
}

static const char *ui_settings_page_turn_summary(void)
{
    return ui_i18n_pick("触屏翻页", "Touch");
}

static const char *ui_settings_bluetooth_summary(void)
{
    switch (net_manager_get_bt_ui_state())
    {
    case NET_MANAGER_BT_UI_CONNECTED:
        return ui_i18n_pick("已连接", "Connected");
    case NET_MANAGER_BT_UI_CONNECTING:
        return ui_i18n_pick("连接中", "Connecting");
    case NET_MANAGER_BT_UI_ON:
        return ui_i18n_pick("已开启", "On");
    case NET_MANAGER_BT_UI_OFF:
    default:
        return ui_i18n_pick("已关闭", "Off");
    }
}

static const char *ui_settings_4g_summary(void)
{
    app_network_snapshot_t snapshot;

    app_network_get_snapshot(&snapshot);
    if (snapshot.desired_mode == APP_NETWORK_MODE_4G)
    {
        if (snapshot.active_link == APP_NETWORK_LINK_4G_CAT1)
        {
            return ui_i18n_pick("已联网", "Online");
        }
        if (snapshot.state >= APP_NETWORK_STATE_RADIO_READY)
        {
            return ui_i18n_pick("启动中", "Starting");
        }
        return ui_i18n_pick("切换中", "Switching");
    }

    return ui_i18n_pick("已关闭", "Off");
}

static const settings_row_t s_settings_rows[] = {
    {"语言", "Language", ui_settings_language_summary, SETTINGS_ROW_ACTION_NAV, UI_SCREEN_LANGUAGE},
    {"休眠时间", "Sleep Time", ui_settings_sleep_summary, SETTINGS_ROW_ACTION_NAV, UI_SCREEN_SLEEP_TIME},
    {"锁屏界面", "Lock Screen", ui_settings_lock_screen_summary, SETTINGS_ROW_ACTION_NONE, UI_SCREEN_NONE},
    {"文件管理", "File Manager", NULL, SETTINGS_ROW_ACTION_NAV, UI_SCREEN_FILE_MANAGER},
    {"翻页", "Page Turn", ui_settings_page_turn_summary, SETTINGS_ROW_ACTION_NONE, UI_SCREEN_NONE},
    {"蓝牙", "Bluetooth", ui_settings_bluetooth_summary, SETTINGS_ROW_ACTION_TOGGLE_BT, UI_SCREEN_NONE},
    {"4G", "4G", ui_settings_4g_summary, SETTINGS_ROW_ACTION_TOGGLE_4G, UI_SCREEN_NONE},
};

static lv_obj_t *s_settings_summary_labels[sizeof(s_settings_rows) / sizeof(s_settings_rows[0])];
static lv_obj_t *s_settings_wodle_prefix_labels[sizeof(s_settings_rows) / sizeof(s_settings_rows[0])];
static lv_obj_t *s_settings_confirm_overlay = NULL;
static app_network_mode_t s_settings_confirm_target_mode = APP_NETWORK_MODE_NONE;

static bool ui_settings_bluetooth_requires_wodle_prefix(void)
{
    return net_manager_get_bt_ui_state() != NET_MANAGER_BT_UI_OFF;
}

void ui_settings_refresh_summaries(void)
{
    size_t i;

    for (i = 0; i < sizeof(s_settings_rows) / sizeof(s_settings_rows[0]); ++i)
    {
        if (s_settings_summary_labels[i] != NULL && s_settings_rows[i].summary_fn != NULL)
        {
            lv_label_set_text(s_settings_summary_labels[i], s_settings_rows[i].summary_fn());
        }
        if (i == SETTINGS_BT_ROW_INDEX && s_settings_summary_labels[i] != NULL)
        {
            if (ui_settings_bluetooth_requires_wodle_prefix())
            {
                lv_obj_set_pos(s_settings_summary_labels[i], ui_px_x(388), ui_px_y(28));
                lv_obj_set_size(s_settings_summary_labels[i], ui_px_w(82), ui_px_h(31));
                if (s_settings_wodle_prefix_labels[i] != NULL)
                {
                    lv_obj_clear_flag(s_settings_wodle_prefix_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            else
            {
                lv_obj_set_pos(s_settings_summary_labels[i], ui_px_x(250), ui_px_y(28));
                lv_obj_set_size(s_settings_summary_labels[i], ui_px_w(220), ui_px_h(31));
                if (s_settings_wodle_prefix_labels[i] != NULL)
                {
                    lv_obj_add_flag(s_settings_wodle_prefix_labels[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}

static const char *ui_settings_mode_text(app_network_mode_t mode)
{
    if (mode == APP_NETWORK_MODE_BT)
    {
        return ui_i18n_pick("蓝牙", "Bluetooth");
    }
    if (mode == APP_NETWORK_MODE_4G)
    {
        return "4G";
    }

    return ui_i18n_pick("网络", "Network");
}

static void ui_settings_confirm_cancel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_settings_confirm_close();
    }
}

static void ui_settings_confirm_apply_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_settings_confirm_target_mode == APP_NETWORK_MODE_BT)
    {
        app_network_request_mode(APP_NETWORK_MODE_BT);
    }
    else if (s_settings_confirm_target_mode == APP_NETWORK_MODE_4G)
    {
        app_network_request_mode(APP_NETWORK_MODE_4G);
    }

    ui_settings_confirm_close();
    ui_settings_refresh_summaries();
}

static void ui_settings_confirm_close(void)
{
    if (s_settings_confirm_overlay != NULL)
    {
        lv_obj_del(s_settings_confirm_overlay);
        s_settings_confirm_overlay = NULL;
    }
    s_settings_confirm_target_mode = APP_NETWORK_MODE_NONE;
}

static void ui_settings_show_network_confirm(settings_row_action_t action)
{
    app_network_snapshot_t snapshot;
    app_network_mode_t target_mode;
    const char *title;
    const char *message;
    lv_obj_t *panel;
    lv_obj_t *cancel_button;
    lv_obj_t *apply_button;

    if (ui_Settings == NULL)
    {
        return;
    }

    app_network_get_snapshot(&snapshot);

    if (action == SETTINGS_ROW_ACTION_TOGGLE_BT)
    {
        if (snapshot.desired_mode == APP_NETWORK_MODE_BT)
        {
            target_mode = APP_NETWORK_MODE_4G;
            title = ui_i18n_pick("关闭蓝牙？", "Turn off Bluetooth?");
            message = ui_i18n_pick("关闭 wodle 蓝牙后会自动开启 4G 网络。", "4G will be enabled automatically.");
        }
        else
        {
            target_mode = APP_NETWORK_MODE_BT;
            title = ui_i18n_pick("开启蓝牙？", "Turn on Bluetooth?");
            message = ui_i18n_pick("开启后请在手机连接 wodle 蓝牙。", "Connect to wodle Bluetooth on your phone.");
        }
    }
    else if (action == SETTINGS_ROW_ACTION_TOGGLE_4G)
    {
        if (snapshot.desired_mode == APP_NETWORK_MODE_4G)
        {
            target_mode = APP_NETWORK_MODE_BT;
            title = ui_i18n_pick("关闭4G？", "Turn off 4G?");
            message = ui_i18n_pick("关闭 4G 后会自动开启 wodle 蓝牙。", "Bluetooth will be enabled automatically.");
        }
        else
        {
            target_mode = APP_NETWORK_MODE_4G;
            title = ui_i18n_pick("开启4G？", "Turn on 4G?");
            message = ui_i18n_pick("开启 4G 后会自动关闭 wodle 蓝牙。", "Bluetooth will be disabled automatically.");
        }
    }
    else
    {
        return;
    }

    ui_settings_confirm_close();
    s_settings_confirm_target_mode = target_mode;

    s_settings_confirm_overlay = settings_plain_obj(ui_Settings, 0, 0, 528, 792, 0, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(s_settings_confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = ui_create_card(s_settings_confirm_overlay, 54, 256, 420, 230, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel, title, 24, 24, 372, 36, 28, LV_TEXT_ALIGN_CENTER, true, false);
    ui_create_label(panel, message, 34, 76, 352, 60, 22, LV_TEXT_ALIGN_CENTER, false, true);
    ui_create_label(panel,
                    ui_settings_mode_text(target_mode),
                    34,
                    132,
                    352,
                    28,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    cancel_button = ui_create_button(panel, 34, 170, 154, 42, ui_i18n_pick("取消", "Cancel"), 20, UI_SCREEN_NONE, false);
    apply_button = ui_create_button(panel, 232, 170, 154, 42, ui_i18n_pick("确认", "OK"), 20, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(cancel_button, ui_settings_confirm_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(apply_button, ui_settings_confirm_apply_event_cb, LV_EVENT_CLICKED, NULL);
}

static void ui_settings_row_event_cb(lv_event_t *e)
{
    uintptr_t index;
    const settings_row_t *row;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    index = (uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (index >= sizeof(s_settings_rows) / sizeof(s_settings_rows[0]))
    {
        return;
    }

    row = &s_settings_rows[index];
    if (row->action == SETTINGS_ROW_ACTION_NAV && row->target != UI_SCREEN_NONE)
    {
        ui_runtime_switch_to(row->target);
    }
    else if (row->action == SETTINGS_ROW_ACTION_TOGGLE_BT)
    {
        ui_settings_show_network_confirm(row->action);
    }
    else if (row->action == SETTINGS_ROW_ACTION_TOGGLE_4G)
    {
        ui_settings_show_network_confirm(row->action);
    }
}

static void ui_settings_create_row(lv_obj_t *parent, const settings_row_t *row, size_t index, int y)
{
    lv_obj_t *hit;
    lv_obj_t *prefix_label;
    lv_obj_t *summary_label;
    const char *summary;

    hit = settings_plain_obj(parent, 0, y, 528, 84, 0, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(hit, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(hit, ui_settings_row_event_cb, LV_EVENT_CLICKED, NULL);

    ui_create_label(hit,
                    ui_i18n_pick(row->zh, row->en),
                    32,
                    28,
                    180,
                    31,
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    summary = row->summary_fn != NULL ? row->summary_fn() : "";
    summary_label = ui_create_label(hit,
                                    summary,
                                    250,
                                    28,
                                    220,
                                    31,
                                    26,
                                    LV_TEXT_ALIGN_RIGHT,
                                    false,
                                    false);
    lv_label_set_long_mode(summary_label, LV_LABEL_LONG_DOT);
    if (index < sizeof(s_settings_summary_labels) / sizeof(s_settings_summary_labels[0]))
    {
        s_settings_summary_labels[index] = summary_label;
    }

    prefix_label = ui_create_label(hit,
                                   "wodle",
                                   342,
                                   35,
                                   42,
                                   20,
                                   14,
                                   LV_TEXT_ALIGN_RIGHT,
                                   false,
                                   false);
    if (index < sizeof(s_settings_wodle_prefix_labels) / sizeof(s_settings_wodle_prefix_labels[0]))
    {
        s_settings_wodle_prefix_labels[index] = prefix_label;
    }
    if (index != SETTINGS_BT_ROW_INDEX || !ui_settings_bluetooth_requires_wodle_prefix())
    {
        lv_obj_add_flag(prefix_label, LV_OBJ_FLAG_HIDDEN);
    }

    ui_create_label(hit,
                    ">",
                    480,
                    22,
                    20,
                    34,
                    26,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

void ui_settings_hardware_prev_page(void)
{
}

void ui_settings_hardware_next_page(void)
{
}

void ui_Settings_screen_init(void)
{
    size_t i;

    if (ui_Settings != NULL)
    {
        return;
    }

    memset(s_settings_summary_labels, 0, sizeof(s_settings_summary_labels));
    memset(s_settings_wodle_prefix_labels, 0, sizeof(s_settings_wodle_prefix_labels));
    ui_settings_confirm_close();

    ui_Settings = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Settings, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Settings, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Settings, LV_OBJ_FLAG_SCROLLABLE);

    ui_top_nav_create(ui_Settings, UI_TOP_TAB_SETTINGS);

    for (i = 0; i < sizeof(s_settings_rows) / sizeof(s_settings_rows[0]); ++i)
    {
        ui_settings_create_row(ui_Settings, &s_settings_rows[i], i, 98 + (int)i * 89);
    }
    ui_bottom_nav_create(ui_Settings, UI_BOTTOM_TAB_NONE);
    ui_settings_refresh_summaries();

}

void ui_Settings_screen_destroy(void)
{
    s_settings_confirm_overlay = NULL;
    s_settings_confirm_target_mode = APP_NETWORK_MODE_NONE;
    memset(s_settings_summary_labels, 0, sizeof(s_settings_summary_labels));
    memset(s_settings_wodle_prefix_labels, 0, sizeof(s_settings_wodle_prefix_labels));

    if (ui_Settings != NULL)
    {
        lv_obj_delete(ui_Settings);
        ui_Settings = NULL;
    }
}
