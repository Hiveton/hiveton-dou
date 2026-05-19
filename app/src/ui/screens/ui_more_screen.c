#include <stddef.h>
#include <stdint.h>

#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"

extern const lv_image_dsc_t more_calendar;
extern const lv_image_dsc_t more_record;
extern const lv_image_dsc_t more_weather;
extern const lv_image_dsc_t more_alarm;
extern const lv_image_dsc_t more_picture;
extern const lv_image_dsc_t more_font;
extern const lv_image_dsc_t more_folder;
extern const lv_image_dsc_t more_pomodoro;
extern const lv_image_dsc_t more_bluetooth;
extern const lv_image_dsc_t more_standby;
extern const lv_image_dsc_t more_reading_bean;
extern const lv_image_dsc_t more_signal;

typedef struct
{
    const char *label;
    const lv_image_dsc_t *icon;
    ui_screen_id_t target;
    int card_x;
    int card_y;
    int icon_x;
    int icon_y;
    int icon_w;
    int icon_h;
    int label_x;
    int label_y;
    int label_w;
} ui_more_item_t;

lv_obj_t *ui_More = NULL;

static const ui_more_item_t s_more_items[] = {
    {"日历", &more_calendar, UI_SCREEN_CALENDAR, 32, 99, 76, 127, 48, 48, 76, 195, 52},
    {"录音", &more_record, UI_SCREEN_RECORDER, 196, 99, 239, 127, 50, 50, 240, 195, 52},
    {"天气", &more_weather, UI_SCREEN_WEATHER, 360, 99, 400, 129, 55, 48, 404, 195, 52},
    {"闹钟", &more_alarm, UI_SCREEN_DATETIME, 32, 245, 73, 270, 55, 55, 76, 341, 52},
    {"图片", &more_picture, UI_SCREEN_WALLPAPER, 196, 245, 239, 276, 50, 44, 240, 341, 52},
    {"字体", &more_font, UI_SCREEN_SETTINGS, 360, 245, 398, 276, 59, 59, 396, 341, 64},
    {"文件管理", &more_folder, UI_SCREEN_FILE_MANAGER, 32, 391, 75, 422, 48, 48, 51, 487, 100},
    {"番茄时间", &more_pomodoro, UI_SCREEN_POMODORO, 196, 391, 223, 404, 80, 80, 217, 487, 100},
    {"蓝牙", &more_bluetooth, UI_SCREEN_SETTINGS, 360, 391, 399, 416, 55, 55, 403, 487, 52},
    {"待机桌面", &more_standby, UI_SCREEN_STANDBY, 32, 537, 72, 562, 59, 59, 51, 632, 100},
    {"阅读豆", &more_reading_bean, UI_SCREEN_AI_DOU, 196, 537, 223, 548, 80, 80, 217, 632, 100},
    {"4G", &more_signal, UI_SCREEN_NONE, 360, 537, 397, 568, 55, 55, 407, 632, 52},
};

static void ui_more_create_item(lv_obj_t *parent, const ui_more_item_t *item)
{
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *icon;

    if (parent == NULL || item == NULL)
    {
        return;
    }

    card = ui_create_card(parent,
                          item->card_x,
                          item->card_y,
                          136,
                          136,
                          item->target,
                          false,
                          32);
    lv_obj_set_style_border_color(card, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(card, 2, 0);

    if (item->icon != NULL)
    {
        icon = ui_create_image_slot(parent,
                                    item->icon_x,
                                    item->icon_y,
                                    item->icon_w,
                                    item->icon_h);
        ui_img_set_src(icon, item->icon);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }
    label = ui_create_label(parent,
                            item->label,
                            item->label_x,
                            item->label_y,
                            item->label_w,
                            30,
                            24,
                            LV_TEXT_ALIGN_CENTER,
                            false,
                            false);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
}

void ui_More_screen_init(void)
{
    size_t i;

    if (ui_More != NULL)
    {
        return;
    }

    ui_More = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_More, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_More, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_More, LV_OBJ_FLAG_SCROLLABLE);

    ui_top_nav_create(ui_More, UI_TOP_TAB_NAV);

    for (i = 0; i < sizeof(s_more_items) / sizeof(s_more_items[0]); ++i)
    {
        ui_more_create_item(ui_More, &s_more_items[i]);
    }
}

void ui_More_screen_destroy(void)
{
    if (ui_More != NULL)
    {
        lv_obj_delete(ui_More);
        ui_More = NULL;
    }
}
