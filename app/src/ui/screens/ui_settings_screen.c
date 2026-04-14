#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "rtthread.h"

#include <stdint.h>

lv_obj_t *ui_Settings = NULL;

#define UI_SETTINGS_VISIBLE_COUNT 4U

typedef struct
{
    const char *(*title_fn)(void);
    const char *(*summary_fn)(void);
    ui_screen_id_t target;
} ui_settings_item_t;

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
} ui_settings_card_refs_t;

static ui_settings_language_t s_language = UI_SETTINGS_LANGUAGE_ZH_CN;
static ui_settings_card_refs_t s_settings_cards[UI_SETTINGS_VISIBLE_COUNT];
static lv_obj_t *s_settings_prev_button = NULL;
static lv_obj_t *s_settings_next_button = NULL;
static lv_obj_t *s_settings_page_label = NULL;
static uint16_t s_settings_page_offset = 0U;

extern void app_set_panel_brightness(rt_uint8_t brightness);
extern rt_uint8_t app_get_panel_brightness(void);

static const char *ui_settings_title_text(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Settings";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "设置";
    }
}

static const char *ui_settings_brightness_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Brightness";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "屏幕亮度";
    }
}

static const char *ui_settings_language_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Language";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "语言";
    }
}

static const char *ui_settings_bluetooth_config_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Bluetooth Config";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "蓝牙配置";
    }
}

static const char *ui_settings_bluetooth_config_card_summary(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Bluetooth status, connection state and device name presets";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "查看蓝牙开关、连接状态与设备名预设";
    }
}

static const char *ui_settings_wallpaper_card_title(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Wallpaper";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "壁纸";
    }
}

static const char *ui_settings_wallpaper_card_summary(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "Open the TF picture preview page";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "进入 TF 卡图片预览测试页";
    }
}

static void ui_settings_format_brightness_summary(char *buffer, size_t buffer_size)
{
    rt_uint8_t brightness;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    brightness = app_get_panel_brightness();
    if (brightness == 0U)
    {
        switch (s_language)
        {
        case UI_SETTINGS_LANGUAGE_EN_US:
            rt_snprintf(buffer, buffer_size, "Currently off");
            break;
        case UI_SETTINGS_LANGUAGE_ZH_CN:
        default:
            rt_snprintf(buffer, buffer_size, "当前已关闭");
            break;
        }
    }
    else
    {
        switch (s_language)
        {
        case UI_SETTINGS_LANGUAGE_EN_US:
            rt_snprintf(buffer, buffer_size, "Current brightness %u%%", (unsigned int)brightness);
            break;
        case UI_SETTINGS_LANGUAGE_ZH_CN:
        default:
            rt_snprintf(buffer, buffer_size, "当前亮度 %u%%", (unsigned int)brightness);
            break;
        }
    }
}

static const char *ui_settings_brightness_card_summary(void)
{
    static char buffer[32];

    ui_settings_format_brightness_summary(buffer, sizeof(buffer));
    return buffer;
}

static const char *ui_settings_language_card_summary(void)
{
    return ui_settings_get_language_label();
}

static const ui_settings_item_t s_settings_items[] = {
    {ui_settings_brightness_card_title, ui_settings_brightness_card_summary, UI_SCREEN_BRIGHTNESS},
    {ui_settings_language_card_title, ui_settings_language_card_summary, UI_SCREEN_LANGUAGE},
    {ui_settings_wallpaper_card_title, ui_settings_wallpaper_card_summary, UI_SCREEN_WALLPAPER},
    {ui_settings_bluetooth_config_card_title, ui_settings_bluetooth_config_card_summary, UI_SCREEN_BLUETOOTH_CONFIG},
};

static const int s_settings_card_y_positions[UI_SETTINGS_VISIBLE_COUNT] = {0, 108, 216, 324};

static void ui_settings_set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL)
    {
        return;
    }

    if (enabled)
    {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
    }
    else
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_50, 0);
    }
}

static void ui_settings_show_card(uint16_t slot_index, const ui_settings_item_t *item)
{
    ui_settings_card_refs_t *refs;

    if (slot_index >= UI_SETTINGS_VISIBLE_COUNT || item == NULL)
    {
        return;
    }

    refs = &s_settings_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(refs->title_label, item->title_fn());
    lv_label_set_text(refs->subtitle_label, item->summary_fn());
    lv_obj_set_user_data(refs->card, (void *)(uintptr_t)item->target);
}

static void ui_settings_hide_card(uint16_t slot_index)
{
    ui_settings_card_refs_t *refs;

    if (slot_index >= UI_SETTINGS_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_settings_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(refs->card, LV_STATE_DISABLED);
}

static void ui_settings_card_event_cb(lv_event_t *e)
{
    ui_screen_id_t target;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    target = (ui_screen_id_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (target != UI_SCREEN_NONE)
    {
        ui_runtime_switch_to(target);
    }
}

static void ui_settings_render(void)
{
    uint16_t visible_index;
    uint16_t item_count = (uint16_t)(sizeof(s_settings_items) / sizeof(s_settings_items[0]));
    uint16_t total_pages = item_count == 0U ? 0U : (uint16_t)((item_count + UI_SETTINGS_VISIBLE_COUNT - 1U) / UI_SETTINGS_VISIBLE_COUNT);
    uint16_t current_page = total_pages == 0U ? 0U : (uint16_t)(s_settings_page_offset / UI_SETTINGS_VISIBLE_COUNT + 1U);
    char page_text[16];
    bool can_prev = s_settings_page_offset > 0U;
    bool can_next = (uint16_t)(s_settings_page_offset + UI_SETTINGS_VISIBLE_COUNT) < item_count;

    ui_settings_set_button_enabled(s_settings_prev_button, can_prev);
    ui_settings_set_button_enabled(s_settings_next_button, can_next);
    if (s_settings_page_label != NULL)
    {
        rt_snprintf(page_text, sizeof(page_text), "%u / %u", (unsigned int)current_page, (unsigned int)total_pages);
        lv_label_set_text(s_settings_page_label, page_text);
    }

    for (visible_index = 0; visible_index < UI_SETTINGS_VISIBLE_COUNT; ++visible_index)
    {
        uint16_t item_index = (uint16_t)(s_settings_page_offset + visible_index);

        if (item_index >= item_count)
        {
            ui_settings_hide_card(visible_index);
            continue;
        }

        ui_settings_show_card(visible_index, &s_settings_items[item_index]);
    }
}

static void ui_settings_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_settings_page_offset >= UI_SETTINGS_VISIBLE_COUNT)
    {
        s_settings_page_offset = (uint16_t)(s_settings_page_offset - UI_SETTINGS_VISIBLE_COUNT);
    }
    else
    {
        s_settings_page_offset = 0U;
    }

    ui_settings_render();
}

static void ui_settings_next_event_cb(lv_event_t *e)
{
    uint16_t item_count = (uint16_t)(sizeof(s_settings_items) / sizeof(s_settings_items[0]));

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_settings_page_offset + UI_SETTINGS_VISIBLE_COUNT) < item_count)
    {
        s_settings_page_offset = (uint16_t)(s_settings_page_offset + UI_SETTINGS_VISIBLE_COUNT);
        ui_settings_render();
    }
}

static void ui_settings_create_card(lv_obj_t *parent, uint16_t slot_index, int y)
{
    ui_settings_card_refs_t *refs = &s_settings_cards[slot_index];
    lv_obj_t *divider;

    refs->card = ui_create_card(parent, 0, y, 528, 108, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_radius(refs->card, 0, 0);
    lv_obj_set_style_border_width(refs->card, 0, 0);
    lv_obj_set_style_outline_width(refs->card, 0, 0);
    lv_obj_set_style_shadow_width(refs->card, 0, 0);
    lv_obj_set_style_pad_all(refs->card, 0, 0);
    lv_obj_set_style_bg_color(refs->card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(refs->card, LV_OPA_TRANSP, 0);
    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        24,
                                        24,
                                        448,
                                        34,
                                        28,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    refs->subtitle_label = ui_create_label(refs->card,
                                           "",
                                           24,
                                           66,
                                           448,
                                           22,
                                           19,
                                           LV_TEXT_ALIGN_LEFT,
                                           false,
                                           false);
    divider = lv_obj_create(refs->card);
    lv_obj_set_pos(divider, 0, 107);
    lv_obj_set_size(divider, 528, 1);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(refs->card, ui_settings_card_event_cb, LV_EVENT_CLICKED, NULL);
}

ui_settings_language_t ui_settings_get_language(void)
{
    return s_language;
}

void ui_settings_set_language(ui_settings_language_t language)
{
    if (language >= UI_SETTINGS_LANGUAGE_COUNT)
    {
        return;
    }

    s_language = language;
}

const char *ui_settings_get_language_label(void)
{
    switch (s_language)
    {
    case UI_SETTINGS_LANGUAGE_EN_US:
        return "English";
    case UI_SETTINGS_LANGUAGE_ZH_CN:
    default:
        return "简体中文";
    }
}

void ui_settings_hardware_prev_page(void)
{
    if (s_settings_prev_button != NULL)
    {
        lv_obj_send_event(s_settings_prev_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_settings_hardware_next_page(void)
{
    if (s_settings_next_button != NULL)
    {
        lv_obj_send_event(s_settings_next_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;
    uint16_t i;

    if (ui_Settings != NULL)
    {
        return;
    }

    s_settings_page_offset = 0U;
    for (i = 0; i < UI_SETTINGS_VISIBLE_COUNT; ++i)
    {
        s_settings_cards[i].card = NULL;
        s_settings_cards[i].title_label = NULL;
        s_settings_cards[i].subtitle_label = NULL;
    }

    ui_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Settings, ui_settings_title_text(), UI_SCREEN_HOME);

    for (i = 0; i < UI_SETTINGS_VISIBLE_COUNT; ++i)
    {
        ui_settings_create_card(page.content, i, s_settings_card_y_positions[i]);
    }

    s_settings_page_label = ui_create_label(page.content,
                                            "1 / 1",
                                            24,
                                            598,
                                            120,
                                            24,
                                            18,
                                            LV_TEXT_ALIGN_LEFT,
                                            false,
                                            false);
    s_settings_prev_button = ui_create_button(page.content, 304, 585, 96, 46, ui_i18n_pick("上翻", "Prev"), 20, UI_SCREEN_NONE, false);
    s_settings_next_button = ui_create_button(page.content, 408, 585, 96, 46, ui_i18n_pick("下翻", "Next"), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_settings_prev_button, ui_settings_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_settings_next_button, ui_settings_next_event_cb, LV_EVENT_CLICKED, NULL);

    ui_settings_render();
}

void ui_Settings_screen_destroy(void)
{
    uint16_t i;

    s_settings_prev_button = NULL;
    s_settings_next_button = NULL;
    s_settings_page_label = NULL;
    s_settings_page_offset = 0U;
    for (i = 0; i < UI_SETTINGS_VISIBLE_COUNT; ++i)
    {
        s_settings_cards[i].card = NULL;
        s_settings_cards[i].title_label = NULL;
        s_settings_cards[i].subtitle_label = NULL;
    }

    if (ui_Settings != NULL)
    {
        lv_obj_delete(ui_Settings);
        ui_Settings = NULL;
    }
}
