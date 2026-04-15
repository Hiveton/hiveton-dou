#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"

#include "petgame.h"

#include <stdbool.h>
#include <stdint.h>

#include "rtthread.h"

extern const lv_image_dsc_t 开心;
extern const lv_image_dsc_t 一般;
extern const lv_image_dsc_t 不开心;
extern const lv_image_dsc_t 饥饿;

lv_obj_t *ui_Pet = NULL;

typedef enum
{
    PETGAME_UI_ACTION_FEED_SMALL = 0,
    PETGAME_UI_ACTION_FEED_BIG = 1,
    PETGAME_UI_ACTION_AFFECTION = 2,
} petgame_ui_action_t;

static lv_obj_t *s_face_img = NULL;
static lv_obj_t *s_level_label = NULL;
static lv_obj_t *s_growth_label = NULL;
static lv_obj_t *s_source_label = NULL;
static lv_obj_t *s_balance_label = NULL;
static lv_obj_t *s_hunger_label = NULL;
static lv_obj_t *s_energy_label = NULL;
static lv_obj_t *s_mood_label = NULL;
static lv_obj_t *s_action_button_small = NULL;
static lv_obj_t *s_action_button_big = NULL;
static lv_obj_t *s_action_button_affection = NULL;
static lv_obj_t *s_action_msg_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static rt_tick_t s_last_interaction_tick = 0U;

static const uint32_t s_level_thresholds_ui[] = {0U, 25U, 80U, 180U, 360U, 640U, 1000U};
static const uint8_t s_pet_level_count = (uint8_t)(sizeof(s_level_thresholds_ui) / sizeof(s_level_thresholds_ui[0]));
static const uint32_t PETGAME_UI_INTERACTION_COOLDOWN_MS = 700U;

#define PETGAME_UI_FEED_COST_SMALL 1U
#define PETGAME_UI_FEED_COST_BIG 3U

static void ui_pet_style_monochrome_button(lv_obj_t *button);
static uint32_t ui_pet_get_level_prev_threshold(uint32_t level);
static uint32_t ui_pet_get_level_next_threshold(uint32_t level);
static const lv_image_dsc_t *ui_pet_get_mood_icon(uint32_t mood_level);
static const lv_image_dsc_t *ui_pet_get_primary_icon(const petgame_state_t *state);
static void ui_pet_set_face(const lv_image_dsc_t *src);
static void ui_pet_render_content(void);

static void ui_pet_style_monochrome_button(lv_obj_t *button)
{
    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(button, 2, 0);
}

static uint32_t ui_pet_get_level_prev_threshold(uint32_t level)
{
    if (level == 0U)
    {
        return s_level_thresholds_ui[0U];
    }

    if (level < (uint32_t)s_pet_level_count)
    {
        return s_level_thresholds_ui[level - 1U];
    }

    return s_level_thresholds_ui[s_pet_level_count - 1U];
}

static uint32_t ui_pet_get_level_next_threshold(uint32_t level)
{
    if (level + 1U < (uint32_t)s_pet_level_count)
    {
        return s_level_thresholds_ui[level + 1U];
    }

    return s_level_thresholds_ui[s_pet_level_count - 1U];
}

static const lv_image_dsc_t *ui_pet_get_mood_icon(uint32_t mood_level)
{
    if (mood_level >= 70U)
    {
        return &开心;
    }
    if (mood_level >= 40U)
    {
        return &一般;
    }

    return &不开心;
}

static const lv_image_dsc_t *ui_pet_get_primary_icon(const petgame_state_t *state)
{
    if (state == NULL)
    {
        return &一般;
    }

    if (state->hunger_level <= 40U)
    {
        return &饥饿;
    }

    return ui_pet_get_mood_icon(state->mood_level);
}

static void ui_pet_set_face(const lv_image_dsc_t *src)
{
    if (s_face_img == NULL || src == NULL)
    {
        return;
    }

    ui_img_set_src(s_face_img, src);
    lv_obj_center(s_face_img);
}

static void ui_pet_render_content(void)
{
    const petgame_state_t *state;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t level_prev;
    uint32_t level_next;
    uint32_t level_span;
    uint32_t growth_in_level;

    if (ui_Pet == NULL)
    {
        return;
    }

    state = petgame_get_state();
    if (state == NULL)
    {
        return;
    }

    minutes = state->reading_seconds / 60U;
    seconds = state->reading_seconds % 60U;
    level_prev = ui_pet_get_level_prev_threshold(state->growth_level);
    level_next = ui_pet_get_level_next_threshold(state->growth_level);
    level_span = (level_next > level_prev) ? (level_next - level_prev) : 1U;
    growth_in_level = (state->growth_score > level_prev) ? (state->growth_score - level_prev) : 0U;
    if (growth_in_level > level_span)
    {
        growth_in_level = level_span;
    }

    lv_label_set_text_fmt(s_level_label,
                          ui_i18n_pick("Lv%u", "Lv%u"),
                          (unsigned int)(state->growth_level + 1U));
    lv_label_set_text_fmt(s_growth_label,
                          ui_i18n_pick("成长: %u/%u", "Growth: %u/%u"),
                          (unsigned int)growth_in_level,
                          (unsigned int)level_span);
    lv_label_set_text_fmt(s_source_label,
                          ui_i18n_pick("阅读 %u:%02u   AI %u", "Read %u:%02u   AI %u"),
                          (unsigned int)minutes,
                          (unsigned int)seconds,
                          (unsigned int)state->ai_interaction_count);
    lv_label_set_text_fmt(s_balance_label,
                          ui_i18n_pick("投喂余额: %u", "Feed balance: %u"),
                          (unsigned int)petgame_get_feed_balance());
    lv_label_set_text_fmt(s_hunger_label,
                          ui_i18n_pick("饱食:%u", "Hunger:%u"),
                          (unsigned int)state->hunger_level);
    lv_label_set_text_fmt(s_energy_label,
                          ui_i18n_pick("精力:%u", "Energy:%u"),
                          (unsigned int)state->energy_level);
    lv_label_set_text_fmt(s_mood_label,
                          ui_i18n_pick("心情:%u", "Mood:%u"),
                          (unsigned int)state->mood_level);

    ui_pet_set_face(ui_pet_get_primary_icon(state));

    if ((s_last_interaction_tick != 0U) &&
        ((rt_tick_get() - s_last_interaction_tick) < rt_tick_from_millisecond(PETGAME_UI_INTERACTION_COOLDOWN_MS)))
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("动作太快了，稍等一下再互动", "Too fast. Wait a moment."));
    }
    else if (state->hunger_level >= 100U)
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("宠物已经吃饱了，先抱抱它吧", "Your pet is full. Try a hug."));
    }
    else if (petgame_get_feed_balance() == 0U)
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("阅读和AI互动可获得投喂额度", "Read and chat to earn feed."));
    }
    else if (state->hunger_level <= 20U)
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("宠物有点饿，先喂点东西吧", "Your pet is hungry now."));
    }
    else if (state->mood_level <= 20U)
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("宠物心情不好，抱抱会更有效", "A hug will help more now."));
    }
    else
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("保持陪伴，宠物会慢慢成长", "Keep caring and it will grow."));
    }
}

static void ui_pet_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_pet_render_content();
}

static void ui_pet_action_event_cb(lv_event_t *e)
{
    lv_event_code_t code;
    intptr_t action_id;
    const petgame_state_t *state;
    uint32_t hunger;
    uint32_t feed_balance;
    bool action_ok = false;

    code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((s_last_interaction_tick != 0U) &&
        ((rt_tick_get() - s_last_interaction_tick) < rt_tick_from_millisecond(PETGAME_UI_INTERACTION_COOLDOWN_MS)))
    {
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("动作太快了，稍等一下再互动", "Too fast. Wait a moment."));
        return;
    }

    state = petgame_get_state();
    hunger = (state != NULL) ? state->hunger_level : 100U;
    feed_balance = petgame_get_feed_balance();
    action_id = (intptr_t)lv_event_get_user_data(e);

    if (action_id == (intptr_t)PETGAME_UI_ACTION_FEED_SMALL)
    {
        if (hunger >= 100U)
        {
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("宠物已经吃饱了", "Your pet is already full."));
        }
        else if (feed_balance < PETGAME_UI_FEED_COST_SMALL)
        {
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("投喂余额不足", "Not enough feed balance."));
        }
        else
        {
            action_ok = petgame_record_feed(1U);
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("喂了一份小食", "A small meal was given."));
        }
    }
    else if (action_id == (intptr_t)PETGAME_UI_ACTION_FEED_BIG)
    {
        if (hunger >= 100U)
        {
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("宠物已经吃饱了", "Your pet is already full."));
        }
        else if (feed_balance < PETGAME_UI_FEED_COST_BIG)
        {
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("投喂余额不足", "Not enough feed balance."));
        }
        else
        {
            action_ok = petgame_record_feed(3U);
            lv_label_set_text(s_action_msg_label,
                              ui_i18n_pick("喂了一顿大餐", "A big meal was given."));
        }
    }
    else if (action_id == (intptr_t)PETGAME_UI_ACTION_AFFECTION)
    {
        petgame_record_affection(1U);
        action_ok = true;
        lv_label_set_text(s_action_msg_label,
                          ui_i18n_pick("抱抱让宠物更开心了", "A hug made your pet happier."));
    }

    if (action_ok)
    {
        s_last_interaction_tick = rt_tick_get();
        ui_pet_render_content();
    }
}

void ui_Pet_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *help_button;
    lv_obj_t *help_label;
    lv_obj_t *face;
    const lv_coord_t side_pad = 24;
    const lv_coord_t top_gap = 18;
    const lv_coord_t face_size = 180;
    const lv_coord_t face_y = 68;
    const lv_coord_t stat_y = 304;
    const lv_coord_t stat_gap = 8;
    const lv_coord_t stat_w = 160;
    const lv_coord_t title_font = 28;
    const lv_coord_t meta_font = 20;
    const lv_coord_t hint_font = 17;
    const lv_coord_t btn_font = 24;
    const lv_coord_t button_h = 56;
    const lv_coord_t button_w = 154;
    const lv_coord_t button_gap = 9;
    const lv_coord_t buttons_y = 576;

    if (ui_Pet != NULL)
    {
        return;
    }

    ui_Pet = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Pet, ui_i18n_pick("陪伴成长", "Companion Growth"), UI_SCREEN_HOME);

    lv_obj_set_style_pad_all(page.content, 0, 0);
    lv_obj_set_style_border_width(page.content, 0, 0);
    lv_obj_set_style_radius(page.content, 0, 0);
    lv_obj_set_style_bg_color(page.content, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(page.content, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(page.content, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(page.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(page.content, LV_OBJ_FLAG_SCROLLABLE);

    petgame_init();

    s_level_label = ui_create_label(page.content,
                                    "Lv-",
                                    side_pad,
                                    top_gap,
                                    180,
                                    32,
                                    title_font,
                                    LV_TEXT_ALIGN_LEFT,
                                    false,
                                    false);
    s_growth_label = ui_create_label(page.content,
                                     "成长: 0/0",
                                     side_pad,
                                     top_gap + 34,
                                     220,
                                     24,
                                     meta_font,
                                     LV_TEXT_ALIGN_LEFT,
                                     false,
                                     false);
    s_source_label = ui_create_label(page.content,
                                     "阅读 0:00   AI 0",
                                     side_pad,
                                     top_gap + 60,
                                     240,
                                     24,
                                     meta_font,
                                     LV_TEXT_ALIGN_LEFT,
                                     false,
                                     false);
    s_balance_label = ui_create_label(page.content,
                                      "投喂余额: 0",
                                      side_pad,
                                      top_gap + 86,
                                      220,
                                      24,
                                      meta_font,
                                      LV_TEXT_ALIGN_LEFT,
                                      false,
                                      false);

    help_button = ui_create_button(page.content,
                     410,
                     top_gap,
                     94,
                     40,
                     ui_i18n_pick("说明", "Rules"),
                     meta_font,
                     UI_SCREEN_PET_RULES,
                     true);
    ui_pet_style_monochrome_button(help_button);
    lv_obj_set_style_radius(help_button, 8, 0);
    help_label = lv_obj_get_child(help_button, 0);
    if (help_label != NULL)
    {
        lv_obj_set_style_text_font(help_label, ui_font_get(18), 0);
    }

    face = ui_create_card(page.content,
                          (528 - face_size) / 2,
                          face_y,
                          face_size,
                          face_size,
                          UI_SCREEN_NONE,
                          false,
                          face_size / 2);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    s_face_img = lv_img_create(face);
    ui_pet_set_face(&一般);
    lv_obj_center(s_face_img);

    s_hunger_label = ui_create_label(page.content,
                                     "饱食:100",
                                     side_pad,
                                     stat_y + 14,
                                     stat_w,
                                     24,
                                     meta_font,
                                     LV_TEXT_ALIGN_CENTER,
                                     false,
                                     false);

    s_energy_label = ui_create_label(page.content,
                                     "精力:100",
                                     side_pad + stat_w + stat_gap,
                                     stat_y + 14,
                                     stat_w,
                                     24,
                                     meta_font,
                                     LV_TEXT_ALIGN_CENTER,
                                     false,
                                     false);

    s_mood_label = ui_create_label(page.content,
                                   "心情:100",
                                   side_pad + (stat_w + stat_gap) * 2U,
                                   stat_y + 14,
                                   stat_w,
                                   24,
                                   meta_font,
                                   LV_TEXT_ALIGN_CENTER,
                                   false,
                                   false);

    s_action_msg_label = ui_create_label(page.content,
                                         ui_i18n_pick("阅读和AI互动可获得投喂额度", "Read and chat to earn feed"),
                                         side_pad,
                                         536,
                                         480,
                                         22,
                                         hint_font,
                                         LV_TEXT_ALIGN_CENTER,
                                         false,
                                         false);

    s_action_button_small = ui_create_button(page.content,
                                             side_pad,
                                             buttons_y,
                                             button_w,
                                             button_h,
                                             ui_i18n_pick("喂食", "Feed"),
                                             btn_font,
                                             UI_SCREEN_NONE,
                                             true);
    lv_obj_set_style_radius(s_action_button_small, 8, 0);
    ui_pet_style_monochrome_button(s_action_button_small);
    lv_obj_add_event_cb(s_action_button_small,
                        ui_pet_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)((uintptr_t)PETGAME_UI_ACTION_FEED_SMALL));

    s_action_button_big = ui_create_button(page.content,
                                           side_pad + button_w + button_gap,
                                           buttons_y,
                                           button_w,
                                           button_h,
                                           ui_i18n_pick("喂大餐", "Big Meal"),
                                           btn_font,
                                           UI_SCREEN_NONE,
                                           true);
    lv_obj_set_style_radius(s_action_button_big, 8, 0);
    ui_pet_style_monochrome_button(s_action_button_big);
    lv_obj_add_event_cb(s_action_button_big,
                        ui_pet_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)((uintptr_t)PETGAME_UI_ACTION_FEED_BIG));

    s_action_button_affection = ui_create_button(page.content,
                                                 side_pad + (button_w + button_gap) * 2U,
                                                 buttons_y,
                                                 button_w,
                                                 button_h,
                                                 ui_i18n_pick("抱抱", "Hug"),
                                                 btn_font,
                                                 UI_SCREEN_NONE,
                                                 true);
    lv_obj_set_style_radius(s_action_button_affection, 8, 0);
    ui_pet_style_monochrome_button(s_action_button_affection);
    lv_obj_add_event_cb(s_action_button_affection,
                        ui_pet_action_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)((uintptr_t)PETGAME_UI_ACTION_AFFECTION));

    if (s_refresh_timer == NULL)
    {
        s_refresh_timer = lv_timer_create(ui_pet_refresh_timer_cb, 1000, NULL);
    }

    ui_pet_render_content();
}

void ui_Pet_screen_destroy(void)
{
    if (s_refresh_timer != NULL)
    {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    s_face_img = NULL;
    s_level_label = NULL;
    s_growth_label = NULL;
    s_source_label = NULL;
    s_balance_label = NULL;
    s_hunger_label = NULL;
    s_energy_label = NULL;
    s_mood_label = NULL;
    s_action_button_small = NULL;
    s_action_button_big = NULL;
    s_action_button_affection = NULL;
    s_action_msg_label = NULL;
    if (ui_Pet != NULL)
    {
        lv_obj_delete(ui_Pet);
        ui_Pet = NULL;
    }
}
