#include "ui.h"
#include "ui_helpers.h"

#include <string.h>

#include "bts2_app_inc.h"
#include "bts2_app_interface.h"
#include "network/net_manager.h"
#include "rtthread.h"

lv_obj_t *ui_Bluetooth_Config = NULL;

typedef struct
{
    lv_obj_t *bt_enabled_value;
    lv_obj_t *bt_connected_value;
    lv_obj_t *link_value;
    lv_obj_t *fourg_value;
    lv_obj_t *device_name_value;
    lv_obj_t *toggle_bt_button;
    lv_obj_t *switch_4g_button;
    lv_obj_t *preset_buttons[3];
} ui_bluetooth_config_refs_t;

static ui_screen_scaffold_t s_scaffold;
static ui_bluetooth_config_refs_t s_refs;
static lv_timer_t *s_refresh_timer = NULL;
static int s_selected_name_index = 0;

static const char *s_name_presets[] = {
    "ink",
    "ink-office",
    "ink-home",
};

static const char *ui_bt_cfg_pick(const char *zh, const char *en)
{
    return ui_settings_get_language() == UI_SETTINGS_LANGUAGE_EN_US ? en : zh;
}

static const char *ui_bt_cfg_get_link_text(void)
{
    switch (net_manager_get_active_link())
    {
    case NET_MANAGER_LINK_BT_PAN:
        return ui_bt_cfg_pick("蓝牙共享", "Bluetooth PAN");
    case NET_MANAGER_LINK_4G_CAT1:
        return ui_bt_cfg_pick("4G在线", "4G online");
    case NET_MANAGER_LINK_NONE:
    default:
        return ui_bt_cfg_pick("未就绪", "Not ready");
    }
}

static const char *ui_bt_cfg_get_enabled_text(bool enabled)
{
    if (enabled)
    {
        return ui_bt_cfg_pick("开启", "On");
    }

    return ui_bt_cfg_pick("关闭", "Off");
}

static const char *ui_bt_cfg_get_connected_text(bool connected)
{
    if (connected)
    {
        return ui_bt_cfg_pick("已连接", "Connected");
    }

    return ui_bt_cfg_pick("未连接", "Disconnected");
}

static const char *ui_bt_cfg_get_4g_text(bool enabled)
{
    if (enabled)
    {
        return ui_bt_cfg_pick("启用", "Enabled");
    }

    return ui_bt_cfg_pick("关闭", "Disabled");
}

static const char *ui_bt_cfg_get_selected_name(void)
{
    if (s_selected_name_index < 0 || s_selected_name_index >= (int)(sizeof(s_name_presets) / sizeof(s_name_presets[0])))
    {
        s_selected_name_index = 0;
    }

    return s_name_presets[s_selected_name_index];
}

static void ui_bt_cfg_set_button_selected(lv_obj_t *button, bool selected)
{
    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(button, selected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(button, selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
}

static void ui_bt_cfg_refresh(void)
{
    char text[64];
    const char *selected_name = ui_bt_cfg_get_selected_name();

    if (s_refs.bt_enabled_value != NULL)
    {
        lv_label_set_text(s_refs.bt_enabled_value, ui_bt_cfg_get_enabled_text(net_manager_bt_enabled()));
    }

    if (s_refs.bt_connected_value != NULL)
    {
        lv_label_set_text(s_refs.bt_connected_value, ui_bt_cfg_get_connected_text(net_manager_bt_connected()));
    }

    if (s_refs.link_value != NULL)
    {
        lv_label_set_text(s_refs.link_value, ui_bt_cfg_get_link_text());
    }

    if (s_refs.fourg_value != NULL)
    {
        lv_label_set_text(s_refs.fourg_value, ui_bt_cfg_get_4g_text(net_manager_4g_enabled()));
    }

    if (s_refs.device_name_value != NULL)
    {
        rt_snprintf(text, sizeof(text), "%s%s", ui_bt_cfg_pick("当前：", "Current: "), selected_name);
        lv_label_set_text(s_refs.device_name_value, text);
    }

    ui_bt_cfg_set_button_selected(s_refs.toggle_bt_button, net_manager_bt_enabled());
    ui_bt_cfg_set_button_selected(s_refs.switch_4g_button, net_manager_4g_enabled());
    for (int i = 0; i < 3; ++i)
    {
        ui_bt_cfg_set_button_selected(s_refs.preset_buttons[i], i == s_selected_name_index);
    }
}

static void ui_bt_cfg_apply_preset(int index)
{
    const char *name;

    if (index < 0 || index >= (int)(sizeof(s_name_presets) / sizeof(s_name_presets[0])))
    {
        return;
    }

    s_selected_name_index = index;
    name = s_name_presets[index];
    bt_interface_set_local_name((int)strlen(name), (void *)name);
    ui_bt_cfg_refresh();
}

static void ui_bt_cfg_action_event_cb(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);

    if (action == 0)
    {
        if (net_manager_bt_enabled())
        {
            net_manager_request_4g_mode();
        }
        else
        {
            net_manager_request_bt_mode();
        }
    }
    else if (action == 1)
    {
        net_manager_request_4g_mode();
    }

    ui_bt_cfg_refresh();
}

static void ui_bt_cfg_preset_event_cb(lv_event_t *e)
{
    intptr_t index = (intptr_t)lv_event_get_user_data(e);

    ui_bt_cfg_apply_preset((int)index);
}

static void ui_bt_cfg_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_bt_cfg_refresh();
}

static void ui_bt_cfg_add_row(lv_obj_t *card,
                              int y,
                              const char *title,
                              lv_obj_t **value_ref)
{
    ui_create_label(card,
                    title,
                    24,
                    y,
                    190,
                    24,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    *value_ref = ui_create_label(card,
                                 "",
                                 206,
                                 y,
                                 250,
                                 24,
                                 20,
                                 LV_TEXT_ALIGN_RIGHT,
                                 false,
                                 false);
}

void ui_Bluetooth_Config_screen_init(void)
{
    lv_obj_t *status_card;
    lv_obj_t *action_card;
    lv_obj_t *name_card;
    lv_obj_t *button;
    lv_obj_t *title;
    ui_screen_scaffold_t scaffold;

    if (ui_Bluetooth_Config != NULL)
    {
        return;
    }

    ui_Bluetooth_Config = ui_create_screen_base();
    ui_build_standard_screen(&scaffold,
                             ui_Bluetooth_Config,
                             ui_bt_cfg_pick("蓝牙配置", "Bluetooth Config"),
                             UI_SCREEN_SETTINGS);
    s_scaffold = scaffold;

    status_card = ui_create_card(scaffold.content, 24, 20, 480, 156, UI_SCREEN_NONE, false, 0);
    title = ui_create_label(status_card,
                            ui_bt_cfg_pick("基础状态", "Status"),
                            20,
                            16,
                            280,
                            28,
                            24,
                            LV_TEXT_ALIGN_LEFT,
                            false,
                            false);
    LV_UNUSED(title);
    ui_bt_cfg_add_row(status_card,
                      54,
                      ui_bt_cfg_pick("蓝牙开关", "Bluetooth"),
                      &s_refs.bt_enabled_value);
    ui_bt_cfg_add_row(status_card,
                      82,
                      ui_bt_cfg_pick("当前连接", "Connection"),
                      &s_refs.bt_connected_value);
    ui_bt_cfg_add_row(status_card,
                      110,
                      ui_bt_cfg_pick("业务链路", "Active link"),
                      &s_refs.link_value);
    ui_bt_cfg_add_row(status_card,
                      138,
                      ui_bt_cfg_pick("4G状态", "4G"),
                      &s_refs.fourg_value);

    action_card = ui_create_card(scaffold.content, 24, 188, 480, 136, UI_SCREEN_NONE, false, 0);
    ui_create_label(action_card,
                    ui_bt_cfg_pick("互斥切换", "Mutual switch"),
                    20,
                    16,
                    260,
                    28,
                    24,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_refs.toggle_bt_button = ui_create_button(action_card,
                                               20,
                                               54,
                                               210,
                                               46,
                                               ui_bt_cfg_pick("切换蓝牙", "Toggle Bluetooth"),
                                               21,
                                               UI_SCREEN_NONE,
                                               true);
    lv_obj_add_event_cb(s_refs.toggle_bt_button, ui_bt_cfg_action_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    s_refs.switch_4g_button = ui_create_button(action_card,
                                               250,
                                               54,
                                               210,
                                               46,
                                               ui_bt_cfg_pick("切到4G", "Switch to 4G"),
                                               21,
                                               UI_SCREEN_NONE,
                                               false);
    lv_obj_add_event_cb(s_refs.switch_4g_button, ui_bt_cfg_action_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    ui_create_label(action_card,
                    ui_bt_cfg_pick("开启蓝牙会关闭4G，切到4G会关闭蓝牙。", "Turning on Bluetooth disables 4G; switching to 4G disables Bluetooth."),
                    20,
                    106,
                    440,
                    24,
                    17,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);

    name_card = ui_create_card(scaffold.content, 24, 340, 480, 214, UI_SCREEN_NONE, false, 0);
    ui_create_label(name_card,
                    ui_bt_cfg_pick("设备名预设", "Device name presets"),
                    20,
                    16,
                    260,
                    28,
                    24,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    s_refs.device_name_value = ui_create_label(name_card,
                                                "",
                                                20,
                                                48,
                                                440,
                                                28,
                                                20,
                                                LV_TEXT_ALIGN_LEFT,
                                                false,
                                                false);
    button = ui_create_button(name_card, 20, 92, 140, 48, s_name_presets[0], 20, UI_SCREEN_NONE, false);
    s_refs.preset_buttons[0] = button;
    lv_obj_add_event_cb(button, ui_bt_cfg_preset_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    button = ui_create_button(name_card, 170, 92, 140, 48, s_name_presets[1], 20, UI_SCREEN_NONE, false);
    s_refs.preset_buttons[1] = button;
    lv_obj_add_event_cb(button, ui_bt_cfg_preset_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    button = ui_create_button(name_card, 320, 92, 140, 48, s_name_presets[2], 20, UI_SCREEN_NONE, false);
    s_refs.preset_buttons[2] = button;
    lv_obj_add_event_cb(button, ui_bt_cfg_preset_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)2);
    ui_create_label(name_card,
                    ui_bt_cfg_pick("点击即可写入蓝牙设备名。", "Tap a preset to write the Bluetooth name."),
                    20,
                    158,
                    440,
                    24,
                    17,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);

    if (s_refresh_timer != NULL)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    s_refresh_timer = lv_timer_create(ui_bt_cfg_timer_cb, 1000, NULL);

    ui_bt_cfg_refresh();
}

void ui_Bluetooth_Config_screen_destroy(void)
{
    if (s_refresh_timer != NULL)
    {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    memset(&s_refs, 0, sizeof(s_refs));

    if (ui_Bluetooth_Config != NULL)
    {
        lv_obj_delete(ui_Bluetooth_Config);
        ui_Bluetooth_Config = NULL;
    }
}
