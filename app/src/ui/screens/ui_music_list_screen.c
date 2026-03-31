#include <stddef.h>

#include "ui.h"
#include "ui_helpers.h"

lv_obj_t *ui_Music_List = NULL;

typedef struct
{
    const char *title;
    const char *meta;
} ui_music_item_t;

static const ui_music_item_t s_music_items[] = {
    {"雨巷留声", "木心朗读集 · 03:28"},
    {"窗下小夜曲", "白描钢琴 · 04:06"},
    {"湖边清晨", "自然采样 · 05:12"},
    {"安静地走路", "散文配乐 · 02:54"},
};

static void create_music_card(lv_obj_t *parent,
                              int y,
                              const ui_music_item_t *item)
{
    lv_obj_t *card = ui_create_card(parent, 24, y, 534, 121, UI_SCREEN_MUSIC_PLAYER, false, 0);

    ui_create_label(card,
                    item->title,
                    20,
                    29,
                    490,
                    31,
                    24,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
    ui_create_label(card,
                    item->meta,
                    20,
                    69,
                    490,
                    19,
                    15,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);
}

void ui_Music_List_screen_init(void)
{
    ui_screen_scaffold_t page;
    size_t i;

    if (ui_Music_List != NULL)
    {
        return;
    }

    ui_Music_List = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Music_List, "音乐", UI_SCREEN_HOME);

    ui_create_label(page.content,
                    "全部音乐",
                    24,
                    14,
                    76,
                    19,
                    15,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    for (i = 0; i < sizeof(s_music_items) / sizeof(s_music_items[0]); ++i)
    {
        create_music_card(page.content, 40 + (int)(i * 139), &s_music_items[i]);
    }

    ui_create_button(page.content, 334, 585, 108, 46, "上翻", 26, UI_SCREEN_NONE, false);
    ui_create_button(page.content, 450, 585, 108, 46, "下翻", 26, UI_SCREEN_NONE, false);
}

void ui_Music_List_screen_destroy(void)
{
    if (ui_Music_List != NULL)
    {
        lv_obj_delete(ui_Music_List);
        ui_Music_List = NULL;
    }
}
