#include "ui.h"
#include "ui_helpers.h"
#include "../../xiaozhi/weather/weather.h"

lv_obj_t *ui_Settings = NULL;

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
                    26,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    subtitle,
                    24,
                    79,
                    432,
                    21,
                    17,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
}

void ui_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Settings != NULL)
    {
        return;
    }

    ui_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Settings, "设置", UI_SCREEN_HOME);

    create_settings_card(page.content, 56, "助眠时间", "22:30 开始柔和提醒", UI_SCREEN_SLEEP_TIME);
    create_settings_card(page.content,
                         188,
                         "天气开关",
                         xiaozhi_weather_is_home_entry_enabled() ? "首页天气卡片已开启" : "首页天气卡片已关闭",
                         UI_SCREEN_WEATHER_TOGGLE);
    create_settings_card(page.content, 320, "屏幕亮度", "墨水屏刷新亮度级别", UI_SCREEN_BRIGHTNESS);
    create_settings_card(page.content, 452, "语言", "简体中文", UI_SCREEN_LANGUAGE);
}

void ui_Settings_screen_destroy(void)
{
    if (ui_Settings != NULL)
    {
        lv_obj_delete(ui_Settings);
        ui_Settings = NULL;
    }
}
