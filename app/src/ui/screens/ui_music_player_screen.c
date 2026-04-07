#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Music_Player = NULL;

static lv_obj_t *create_bar(lv_obj_t *parent,
                            int x,
                            int y,
                            int w,
                            int h,
                            bool filled)
{
    lv_obj_t *bar = lv_obj_create(parent);

    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(bar, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(bar, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_outline_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar,
                              filled ? lv_color_hex(0x000000) : lv_color_hex(0xD3D3D3),
                              0);
    return bar;
}

void ui_Music_Player_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *disc;
    lv_obj_t *inner_disc;

    if (ui_Music_Player != NULL)
    {
        return;
    }

    ui_Music_Player = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Music_Player, ui_i18n_pick("音乐播放器", "Music Player"), UI_SCREEN_MUSIC_LIST);

    disc = ui_create_card(page.content, 144, 56, 240, 240, UI_SCREEN_NONE, false, 120);
    inner_disc = ui_create_card(disc, 89, 89, 62, 62, UI_SCREEN_NONE, false, 31);
    lv_obj_set_style_border_width(inner_disc, 2, 0);

    ui_create_label(page.content,
                    "雨巷留声",
                    0,
                    324,
                    528,
                    42,
                    32,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(page.content,
                    "木心朗读集",
                    0,
                    370,
                    528,
                    23,
                    18,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    ui_create_label(page.content, "00:36", 48, 448, 62, 19, 15, LV_TEXT_ALIGN_LEFT, false, false);
    ui_create_label(page.content, "03:28", 418, 448, 62, 19, 15, LV_TEXT_ALIGN_RIGHT, false, false);
    create_bar(page.content, 110, 455, 362, 8, false);
    create_bar(page.content, 110, 455, 138, 8, true);

    ui_create_button(page.content, 55, 528, 126, 52, ui_i18n_pick("上一首", "Previous"), 20, UI_SCREEN_NONE, false);
    ui_create_button(page.content, 201, 528, 126, 52, ui_i18n_pick("播放", "Play"), 20, UI_SCREEN_NONE, true);
    ui_create_button(page.content, 347, 528, 126, 52, ui_i18n_pick("下一首", "Next"), 20, UI_SCREEN_NONE, false);
}

void ui_Music_Player_screen_destroy(void)
{
    if (ui_Music_Player != NULL)
    {
        lv_obj_delete(ui_Music_Player);
        ui_Music_Player = NULL;
    }
}
