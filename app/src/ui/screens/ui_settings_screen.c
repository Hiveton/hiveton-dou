#include "ui.h"
#include "ui_helpers.h"
#include "rtthread.h"

lv_obj_t *ui_Settings = NULL;

static ui_settings_language_t s_language = UI_SETTINGS_LANGUAGE_ZH_CN;

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

static void create_settings_card(lv_obj_t *parent,
                                 int y,
                                 const char *title,
                                 const char *subtitle,
                                 ui_screen_id_t target)
{
    lv_obj_t *card = ui_create_card(parent, 24, y, 480, 112, target, false, 0);

    ui_create_label(card,
                    title,
                    24,
                    34,
                    432,
                    34,
                    28,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    subtitle,
                    24,
                    79,
                    432,
                    21,
                    19,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    if (target == UI_SCREEN_WALLPAPER)
    {
        create_settings_card(parent,
                             y + 116,
                             ui_settings_bluetooth_config_card_title(),
                             ui_settings_bluetooth_config_card_summary(),
                             UI_SCREEN_BLUETOOTH_CONFIG);
    }
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

void ui_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;
    char brightness_summary[32];

    if (ui_Settings != NULL)
    {
        return;
    }

    ui_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Settings, ui_settings_title_text(), UI_SCREEN_HOME);

    ui_settings_format_brightness_summary(brightness_summary, sizeof(brightness_summary));

    create_settings_card(page.content, 56, ui_settings_brightness_card_title(), brightness_summary, UI_SCREEN_BRIGHTNESS);
    create_settings_card(page.content, 172, ui_settings_language_card_title(), ui_settings_get_language_label(), UI_SCREEN_LANGUAGE);
    create_settings_card(page.content, 288, ui_settings_wallpaper_card_title(), ui_settings_wallpaper_card_summary(), UI_SCREEN_WALLPAPER);
}

void ui_Settings_screen_destroy(void)
{
    if (ui_Settings != NULL)
    {
        lv_obj_delete(ui_Settings);
        ui_Settings = NULL;
    }
}
