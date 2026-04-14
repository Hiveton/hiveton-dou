#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

lv_obj_t *ui_Pet_Rules = NULL;

void ui_Pet_Rules_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Pet_Rules != NULL)
    {
        return;
    }

    ui_Pet_Rules = ui_create_screen_base();
    ui_build_standard_screen(&page,
                             ui_Pet_Rules,
                             ui_i18n_pick("游戏规则", "Rules"),
                             UI_SCREEN_PET);

    lv_obj_set_style_pad_all(page.content, 0, 0);
    lv_obj_set_style_border_width(page.content, 0, 0);
    lv_obj_set_style_radius(page.content, 0, 0);
    lv_obj_set_style_bg_color(page.content, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(page.content, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(page.content, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(page.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(page.content, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_label(page.content,
                    ui_i18n_pick("1. 初始赠送 3 点投喂额度\n"
                                 "2. 阅读每满 1 分钟，增加 1 点投喂额度\n"
                                 "3. 每次 AI 互动，增加 1 点投喂额度\n"
                                 "4. 喂食消耗 1 点，大餐消耗 3 点\n"
                                 "5. 宠物吃饱后，不能继续投喂\n"
                                 "6. 抱抱不消耗额度，可改善状态\n"
                                 "7. 阅读、AI、投喂、抱抱都会带来成长\n"
                                 "8. 成长记录保存在 TF 卡 games 目录",
                                 "1. Start with 3 feed points\n"
                                 "2. Every 1 minute of reading gives 1 feed point\n"
                                 "3. Each AI interaction gives 1 feed point\n"
                                 "4. Feed costs 1, big meal costs 3\n"
                                 "5. You cannot feed a full pet\n"
                                 "6. Hug costs nothing and improves status\n"
                                 "7. Reading, AI, feeding and hugs all add growth\n"
                                 "8. Save data is stored in TF card games directory"),
                    24,
                    24,
                    480,
                    420,
                    22,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    true);
}

void ui_Pet_Rules_screen_destroy(void)
{
    if (ui_Pet_Rules != NULL)
    {
        lv_obj_delete(ui_Pet_Rules);
        ui_Pet_Rules = NULL;
    }
}
