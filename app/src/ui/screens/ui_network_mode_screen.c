#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "network/net_manager.h"
#include "rtthread.h"

#include <stdint.h>

lv_obj_t *ui_Network_Mode = NULL;

static lv_obj_t *s_network_mode_current_label = NULL;
static lv_obj_t *s_network_mode_status_label = NULL;
static lv_obj_t *s_network_mode_bt_button = NULL;
static lv_obj_t *s_network_mode_4g_button = NULL;
static lv_timer_t *s_network_mode_timer = NULL;

static const char *ui_network_mode_title_text(void)
{
    return ui_i18n_pick("网络模式", "Network Mode");
}

static const char *ui_network_mode_mode_text(net_manager_mode_t mode)
{
    switch (mode)
    {
    case NET_MANAGER_MODE_BT:
        return ui_i18n_pick("蓝牙模式", "Bluetooth");
    case NET_MANAGER_MODE_4G:
        return ui_i18n_pick("4G模式", "4G");
    default:
        return ui_i18n_pick("未设置", "Unset");
    }
}

static void ui_network_mode_apply_button_state(lv_obj_t *button, bool selected)
{
    lv_obj_t *label;

    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button,
                              selected ? lv_color_black() : lv_color_white(),
                              0);

    label = lv_obj_get_child(button, 0);
    if (label != NULL)
    {
        lv_obj_set_style_text_color(label,
                                    selected ? lv_color_white() : lv_color_black(),
                                    0);
    }
}

static const char *ui_network_mode_status_text(const net_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return ui_i18n_pick("未就绪", "Not ready");
    }

    if (snapshot->radios_suspended)
    {
        return ui_i18n_pick("休眠中", "Sleeping");
    }

    if (snapshot->desired_mode == NET_MANAGER_MODE_BT)
    {
        if (!snapshot->bt_enabled || snapshot->net_4g_enabled)
        {
            return ui_i18n_pick("切换中", "Switching");
        }

        if (snapshot->active_link == NET_MANAGER_LINK_BT_PAN)
        {
            return ui_i18n_pick("蓝牙网络已连接", "Bluetooth connected");
        }

        if (snapshot->bt_connected)
        {
            return ui_i18n_pick("等待网络共享", "Waiting for PAN");
        }

        return ui_i18n_pick("等待蓝牙连接", "Waiting for Bluetooth");
    }

    if (snapshot->desired_mode == NET_MANAGER_MODE_4G)
    {
        if (!snapshot->net_4g_enabled || snapshot->bt_enabled)
        {
            return ui_i18n_pick("切换中", "Switching");
        }

        if (snapshot->active_link == NET_MANAGER_LINK_4G_CAT1 ||
            snapshot->network_ready)
        {
            return ui_i18n_pick("4G已联网", "4G online");
        }

        return ui_i18n_pick("4G启动中", "Starting 4G");
    }

    return ui_i18n_pick("未启用", "Disabled");
}

static void ui_network_mode_refresh(void)
{
    net_manager_snapshot_t snapshot;
    char line[64];
    bool bt_selected;
    bool fourg_selected;

    net_manager_get_snapshot(&snapshot);
    bt_selected = (snapshot.desired_mode == NET_MANAGER_MODE_BT);
    fourg_selected = (snapshot.desired_mode == NET_MANAGER_MODE_4G);

    if (s_network_mode_current_label != NULL)
    {
        rt_snprintf(line,
                    sizeof(line),
                    "%s%s",
                    ui_i18n_pick("当前模式：", "Current: "),
                    ui_network_mode_mode_text(snapshot.desired_mode));
        lv_label_set_text(s_network_mode_current_label, line);
    }

    if (s_network_mode_status_label != NULL)
    {
        rt_snprintf(line,
                    sizeof(line),
                    "%s%s",
                    ui_i18n_pick("状态：", "Status: "),
                    ui_network_mode_status_text(&snapshot));
        lv_label_set_text(s_network_mode_status_label, line);
    }

    ui_network_mode_apply_button_state(s_network_mode_bt_button, bt_selected);
    ui_network_mode_apply_button_state(s_network_mode_4g_button, fourg_selected);
}

static void ui_network_mode_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_network_mode_refresh();
}

static void ui_network_mode_select_event_cb(lv_event_t *e)
{
    net_manager_mode_t target_mode;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    target_mode = (net_manager_mode_t)(uintptr_t)lv_event_get_user_data(e);
    if (net_manager_get_desired_mode() == target_mode)
    {
        return;
    }

    ui_Settings_screen_destroy();

    if (target_mode == NET_MANAGER_MODE_BT)
    {
        net_manager_request_bt_mode();
    }
    else if (target_mode == NET_MANAGER_MODE_4G)
    {
        net_manager_request_4g_mode();
    }

    ui_network_mode_refresh();
}

static void ui_network_mode_switch_to(net_manager_mode_t target_mode)
{
    if (net_manager_get_desired_mode() == target_mode)
    {
        return;
    }

    ui_Settings_screen_destroy();

    if (target_mode == NET_MANAGER_MODE_BT)
    {
        net_manager_request_bt_mode();
    }
    else if (target_mode == NET_MANAGER_MODE_4G)
    {
        net_manager_request_4g_mode();
    }

    ui_network_mode_refresh();
}

void ui_network_mode_hardware_prev_option(void)
{
    ui_network_mode_switch_to(NET_MANAGER_MODE_BT);
}

void ui_network_mode_hardware_next_option(void)
{
    ui_network_mode_switch_to(NET_MANAGER_MODE_4G);
}

void ui_Network_Mode_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Network_Mode != NULL)
    {
        return;
    }

    ui_Network_Mode = ui_create_screen_base();
    ui_build_standard_screen(&page,
                             ui_Network_Mode,
                             ui_network_mode_title_text(),
                             UI_SCREEN_SETTINGS);

    s_network_mode_current_label = ui_create_label(page.content,
                                                   "",
                                                   36,
                                                   40,
                                                   456,
                                                   36,
                                                   24,
                                                   LV_TEXT_ALIGN_LEFT,
                                                   false,
                                                   false);
    s_network_mode_status_label = ui_create_label(page.content,
                                                  "",
                                                  36,
                                                  82,
                                                  456,
                                                  32,
                                                  20,
                                                  LV_TEXT_ALIGN_LEFT,
                                                  false,
                                                  false);

    s_network_mode_bt_button = ui_create_button(page.content,
                                                36,
                                                170,
                                                456,
                                                96,
                                                ui_i18n_pick("蓝牙模式", "Bluetooth"),
                                                28,
                                                UI_SCREEN_NONE,
                                                false);
    lv_obj_add_event_cb(s_network_mode_bt_button,
                        ui_network_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)NET_MANAGER_MODE_BT);
    ui_create_label(page.content,
                    ui_i18n_pick("关闭4G与4G供电，使用蓝牙共享网络", "Disable 4G and power, use Bluetooth sharing"),
                    44,
                    280,
                    440,
                    48,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);

    s_network_mode_4g_button = ui_create_button(page.content,
                                                36,
                                                388,
                                                456,
                                                96,
                                                ui_i18n_pick("4G模式", "4G"),
                                                28,
                                                UI_SCREEN_NONE,
                                                false);
    lv_obj_add_event_cb(s_network_mode_4g_button,
                        ui_network_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)NET_MANAGER_MODE_4G);
    ui_create_label(page.content,
                    ui_i18n_pick("关闭蓝牙全部功能，使用4G联网流程", "Disable Bluetooth, use 4G network"),
                    44,
                    498,
                    440,
                    48,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);

    ui_create_label(page.content,
                    ui_i18n_pick("模式互斥生效，重启后保持上次选择。", "Modes are exclusive and persist after reboot."),
                    36,
                    592,
                    456,
                    44,
                    18,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);

    s_network_mode_timer = lv_timer_create(ui_network_mode_timer_cb, 800, NULL);
    ui_network_mode_refresh();
}

void ui_Network_Mode_screen_destroy(void)
{
    if (s_network_mode_timer != NULL)
    {
        lv_timer_del(s_network_mode_timer);
        s_network_mode_timer = NULL;
    }

    s_network_mode_current_label = NULL;
    s_network_mode_status_label = NULL;
    s_network_mode_bt_button = NULL;
    s_network_mode_4g_button = NULL;

    if (ui_Network_Mode != NULL)
    {
        lv_obj_delete(ui_Network_Mode);
        ui_Network_Mode = NULL;
    }
}
