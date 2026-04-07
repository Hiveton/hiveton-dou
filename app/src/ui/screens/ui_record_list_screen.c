#include <stddef.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Record_List = NULL;

typedef struct
{
    const char *title;
    const char *meta_zh;
    const char *meta_en;
} ui_record_item_t;

static const ui_record_item_t s_record_items[] = {
    {"晨读摘录_0318_01.wav", "00:46 · 点击播放", "00:46 · Tap to play"},
    {"会议备忘_0317_02.wav", "01:12 · 点击播放", "01:12 · Tap to play"},
    {"临时灵感_0315_03.wav", "00:28 · 点击播放", "00:28 · Tap to play"},
};

static void create_record_card(lv_obj_t *parent,
                               int y,
                               const ui_record_item_t *item)
{
    lv_obj_t *card = ui_create_card(parent, 24, y, 470, 96, UI_SCREEN_NONE, false, 0);

    ui_create_label(card,
                    item->title,
                    18,
                    17,
                    430,
                    31,
                    24,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    ui_i18n_pick(item->meta_zh, item->meta_en),
                    18,
                    57,
                    430,
                    19,
                    15,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
}

void ui_Record_List_screen_init(void)
{
    ui_screen_scaffold_t page;
    size_t i;

    if (ui_Record_List != NULL)
    {
        return;
    }

    ui_Record_List = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Record_List, ui_i18n_pick("录音记录", "Recordings"), UI_SCREEN_RECORDER);

    for (i = 0; i < sizeof(s_record_items) / sizeof(s_record_items[0]); ++i)
    {
        create_record_card(page.content, 40 + (int)(i * 108), &s_record_items[i]);
    }
}

void ui_Record_List_screen_destroy(void)
{
    if (ui_Record_List != NULL)
    {
        lv_obj_delete(ui_Record_List);
        ui_Record_List = NULL;
    }
}
