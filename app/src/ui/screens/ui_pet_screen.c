#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

#include "petgame.h"

#include <stdbool.h>
#include <stdint.h>

#include "rtthread.h"

extern const lv_image_dsc_t pet_face_happy;
extern const lv_image_dsc_t pet_face_growth;
extern const lv_image_dsc_t pet_face_full;
extern const lv_image_dsc_t pet_face_hungry;
extern const lv_image_dsc_t pet_face_tired;
extern const lv_image_dsc_t pet_face_sleep;
extern const lv_image_dsc_t pet_face_sport;
extern const lv_image_dsc_t pet_face_sick;
extern const lv_image_dsc_t pet_face_shy;
extern const lv_image_dsc_t pet_face_surprise;
extern const lv_image_dsc_t pet_face_sad;
extern const lv_image_dsc_t pet_face_cute;
extern const lv_image_dsc_t pet_icon_food;
extern const lv_image_dsc_t pet_icon_energy;
extern const lv_image_dsc_t pet_icon_mood;
extern const lv_image_dsc_t pet_icon_clean;
extern const lv_image_dsc_t pet_icon_notice;
extern const lv_image_dsc_t pet_icon_play_food;
extern const lv_image_dsc_t pet_icon_play;
extern const lv_image_dsc_t pet_icon_bath;

lv_obj_t *ui_Pet = NULL;

typedef enum
{
    PET_UI_ACTION_FEED = 0,
    PET_UI_ACTION_PLAY = 1,
    PET_UI_ACTION_CLEAN = 2,
} pet_ui_action_t;

static lv_obj_t *s_face_img = NULL;
static lv_obj_t *s_level_label = NULL;
static lv_obj_t *s_growth_label = NULL;
static lv_obj_t *s_growth_fill = NULL;
static lv_obj_t *s_mood_label = NULL;
static lv_obj_t *s_energy_label = NULL;
static lv_obj_t *s_days_label = NULL;
static lv_obj_t *s_speech_label = NULL;
static lv_obj_t *s_stat_food_value = NULL;
static lv_obj_t *s_stat_energy_value = NULL;
static lv_obj_t *s_stat_mood_value = NULL;
static lv_obj_t *s_stat_clean_value = NULL;
static lv_obj_t *s_notice_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static rt_tick_t s_last_action_tick = 0U;
static rt_tick_t s_last_face_tick = 0U;
static petgame_activity_type_t s_last_face_event = PETGAME_ACTIVITY_TYPE_NONE;

static const uint32_t s_level_thresholds_ui[] = {0U, 25U, 80U, 180U, 360U, 640U, 1000U};
static const uint32_t PET_UI_ACTION_COOLDOWN_MS = 700U;
static const uint32_t PET_UI_EVENT_FACE_MS = 2500U;

static lv_obj_t *pet_plain_obj(lv_obj_t *parent,
                               int x,
                               int y,
                               int w,
                               int h,
                               int radius,
                               lv_opa_t opa,
                               uint32_t bg,
                               int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *pet_text(lv_obj_t *parent,
                          const char *text,
                          int x,
                          int y,
                          int w,
                          int h,
                          uint16_t size,
                          lv_text_align_t align,
                          bool wrap)
{
    return ui_create_label(parent, text, x, y, w, h, size, align, false, wrap);
}

static void pet_make_clickable(lv_obj_t *obj, ui_screen_id_t target)
{
    if (obj == NULL || target == UI_SCREEN_NONE)
    {
        return;
    }

    ui_attach_nav_event(obj, target);
}

static uint32_t pet_level_next_threshold(uint32_t level)
{
    uint32_t count = (uint32_t)(sizeof(s_level_thresholds_ui) / sizeof(s_level_thresholds_ui[0]));
    uint32_t index = level + 1U;

    if (index >= count)
    {
        return s_level_thresholds_ui[count - 1U];
    }

    return s_level_thresholds_ui[index];
}

static uint32_t pet_level_prev_threshold(uint32_t level)
{
    uint32_t count = (uint32_t)(sizeof(s_level_thresholds_ui) / sizeof(s_level_thresholds_ui[0]));

    if (level == 0U)
    {
        return 0U;
    }
    if (level < count)
    {
        return s_level_thresholds_ui[level];
    }
    return s_level_thresholds_ui[count - 1U];
}

static void pet_progress_set(lv_obj_t *fill, uint32_t current, uint32_t total, int max_w)
{
    int width;

    if (fill == NULL)
    {
        return;
    }

    if (total == 0U)
    {
        total = 1U;
    }
    if (current > total)
    {
        current = total;
    }

    width = (int)((current * (uint32_t)max_w) / total);
    if (width < 0)
    {
        width = 0;
    }
    lv_obj_set_size(fill, ui_px_w(width), ui_px_h(8));
}

static const lv_image_dsc_t *pet_face_for_event(petgame_activity_type_t event)
{
    switch (event)
    {
    case PETGAME_ACTIVITY_TYPE_AI_INTERACTION:
        return &pet_face_surprise;
    case PETGAME_ACTIVITY_TYPE_FEED_SMALL:
    case PETGAME_ACTIVITY_TYPE_FEED_BIG:
        return &pet_face_full;
    case PETGAME_ACTIVITY_TYPE_AFFECTION:
        return &pet_face_cute;
    case PETGAME_ACTIVITY_TYPE_PLAY:
        return &pet_face_sport;
    case PETGAME_ACTIVITY_TYPE_CLEAN:
        return &pet_face_shy;
    case PETGAME_ACTIVITY_TYPE_NONE:
    default:
        return NULL;
    }
}

static const char *pet_speech_for_event(petgame_activity_type_t event)
{
    switch (event)
    {
    case PETGAME_ACTIVITY_TYPE_AI_INTERACTION:
        return ui_i18n_pick("哇！发生什么了？", "Wow! What happened?");
    case PETGAME_ACTIVITY_TYPE_FEED_SMALL:
    case PETGAME_ACTIVITY_TYPE_FEED_BIG:
        return ui_i18n_pick("好满足，吃饱啦！", "So full and happy!");
    case PETGAME_ACTIVITY_TYPE_AFFECTION:
        return ui_i18n_pick("主人最好了~", "You are the best!");
    case PETGAME_ACTIVITY_TYPE_PLAY:
        return ui_i18n_pick("运动让我充满活力！", "Playing gives me energy!");
    case PETGAME_ACTIVITY_TYPE_CLEAN:
        return ui_i18n_pick("洗干净啦，好舒服！", "Clean and comfy!");
    case PETGAME_ACTIVITY_TYPE_NONE:
    default:
        return NULL;
    }
}

static const lv_image_dsc_t *pet_face_for_state(const petgame_state_t *state)
{
    if (state == NULL)
    {
        return &pet_face_happy;
    }
    if (state->hunger_level <= 35U)
    {
        return &pet_face_hungry;
    }
    if (state->energy_level <= 25U)
    {
        return &pet_face_tired;
    }
    if (state->cleanliness_level <= 30U)
    {
        return &pet_face_sick;
    }
    if (state->mood_level <= 35U)
    {
        return &pet_face_sad;
    }
    if (state->mood_level >= 85U)
    {
        return &pet_face_cute;
    }
    return &pet_face_happy;
}

static const char *pet_speech_for_state(const petgame_state_t *state)
{
    if (state == NULL)
    {
        return ui_i18n_pick("主人真好，我很开心！", "I am happy!");
    }
    if (state->hunger_level <= 35U)
    {
        return ui_i18n_pick("我有点饿了...", "I am a little hungry...");
    }
    if (state->energy_level <= 25U)
    {
        return ui_i18n_pick("好困呀，想睡觉了...", "Sleepy now...");
    }
    if (state->cleanliness_level <= 30U)
    {
        return ui_i18n_pick("我需要照顾一下...", "I need some care...");
    }
    if (state->mood_level <= 35U)
    {
        return ui_i18n_pick("呜呜...我好难过...", "I feel sad...");
    }
    return ui_i18n_pick("主人真好，我今天超开心！", "I am super happy today!");
}

static const char *pet_notice_for_state(const petgame_state_t *state)
{
    if (state == NULL)
    {
        return ui_i18n_pick("宠物有点饿了，去喂点食物吧~", "Your pet is hungry.");
    }
    if (state->hunger_level <= 50U)
    {
        return ui_i18n_pick("宠物有点饿了，去喂点食物吧~", "Your pet is hungry.");
    }
    if (state->cleanliness_level <= 45U)
    {
        return ui_i18n_pick("宠物该洗澡了，保持干净更舒服~", "Time for a bath.");
    }
    if (state->energy_level <= 35U)
    {
        return ui_i18n_pick("宠物有点困了，让它休息一下吧~", "Your pet is sleepy.");
    }
    if (state->mood_level <= 45U)
    {
        return ui_i18n_pick("陪宠物玩一会儿，心情会变好~", "Play with your pet.");
    }
    return ui_i18n_pick("宠物状态不错，继续陪伴它吧~", "Your pet is doing well.");
}

static void pet_set_face(const lv_image_dsc_t *face)
{
    if (s_face_img == NULL || face == NULL)
    {
        return;
    }

    ui_img_set_src(s_face_img, face);
}

static void pet_drain_activity_events(void)
{
    petgame_activity_type_t events[4];
    uint8_t count = petgame_drain_activity_events(events, (uint8_t)(sizeof(events) / sizeof(events[0])));

    if (count > 0U)
    {
        s_last_face_event = events[count - 1U];
        s_last_face_tick = rt_tick_get();
    }
}

static void ui_pet_render_content(void)
{
    petgame_state_t state_snapshot;
    const petgame_state_t *state;
    const lv_image_dsc_t *face;
    const char *speech;
    uint32_t prev;
    uint32_t next;
    uint32_t growth_in_level;
    rt_tick_t now;

    if (ui_Pet == NULL)
    {
        return;
    }

    if (!petgame_get_state_copy(&state_snapshot))
    {
        return;
    }
    state = &state_snapshot;

    pet_drain_activity_events();
    now = rt_tick_get();
    face = NULL;
    speech = NULL;
    if (s_last_face_event != PETGAME_ACTIVITY_TYPE_NONE &&
        (now - s_last_face_tick) < rt_tick_from_millisecond(PET_UI_EVENT_FACE_MS))
    {
        face = pet_face_for_event(s_last_face_event);
        speech = pet_speech_for_event(s_last_face_event);
    }
    if (face == NULL)
    {
        face = pet_face_for_state(state);
        speech = pet_speech_for_state(state);
    }

    prev = pet_level_prev_threshold(state->growth_level);
    next = pet_level_next_threshold(state->growth_level);
    growth_in_level = (state->growth_score > prev) ? (state->growth_score - prev) : 0U;
    if (next > prev)
    {
        next -= prev;
    }
    else
    {
        next = 1U;
    }
    if (growth_in_level > next)
    {
        growth_in_level = next;
    }

    lv_label_set_text_fmt(s_level_label, "Lv%u", (unsigned int)(state->growth_level + 1U));
    lv_label_set_text_fmt(s_growth_label,
                          ui_i18n_pick("成长: %u / %u", "Growth: %u / %u"),
                          (unsigned int)growth_in_level,
                          (unsigned int)next);
    pet_progress_set(s_growth_fill, growth_in_level, next, 150);
    lv_label_set_text_fmt(s_mood_label,
                          ui_i18n_pick("快乐: %u / 100", "Mood: %u / 100"),
                          (unsigned int)state->mood_level);
    lv_label_set_text_fmt(s_energy_label,
                          ui_i18n_pick("能量: %u / 100", "Energy: %u / 100"),
                          (unsigned int)state->energy_level);
    lv_label_set_text_fmt(s_days_label,
                          ui_i18n_pick("陪伴天数: %u天", "Days: %u"),
                          (unsigned int)petgame_get_companion_days());
    lv_label_set_text(s_speech_label, speech != NULL ? speech : "");
    lv_label_set_text_fmt(s_stat_food_value, "%u", (unsigned int)state->hunger_level);
    lv_label_set_text_fmt(s_stat_energy_value, "%u", (unsigned int)state->energy_level);
    lv_label_set_text_fmt(s_stat_mood_value, "%u", (unsigned int)state->mood_level);
    lv_label_set_text_fmt(s_stat_clean_value, "%u", (unsigned int)state->cleanliness_level);
    lv_label_set_text(s_notice_label, pet_notice_for_state(state));
    pet_set_face(face);
}

static void ui_pet_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_pet_render_content();
}

static void pet_action_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    intptr_t action_id;

    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((s_last_action_tick != 0U) &&
        ((rt_tick_get() - s_last_action_tick) < rt_tick_from_millisecond(PET_UI_ACTION_COOLDOWN_MS)))
    {
        return;
    }

    action_id = (intptr_t)lv_event_get_user_data(e);
    if (action_id == (intptr_t)PET_UI_ACTION_FEED)
    {
        if (!petgame_record_feed(1U) && s_notice_label != NULL)
        {
            lv_label_set_text(s_notice_label, ui_i18n_pick("投喂额度不足，阅读和AI互动可获得额度", "Not enough feed."));
        }
    }
    else if (action_id == (intptr_t)PET_UI_ACTION_PLAY)
    {
        petgame_record_play();
    }
    else if (action_id == (intptr_t)PET_UI_ACTION_CLEAN)
    {
        petgame_record_clean();
    }

    s_last_action_tick = rt_tick_get();
    ui_pet_render_content();
}

static lv_obj_t *pet_action_button(lv_obj_t *parent,
                                   int x,
                                   const lv_image_dsc_t *icon,
                                   const char *text,
                                   pet_ui_action_t action)
{
    lv_obj_t *button = pet_plain_obj(parent, x, 607, 138, 78, 10, LV_OPA_COVER, 0xffffff, 2);
    lv_obj_t *img = NULL;
    lv_obj_t *label;

    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, pet_action_event_cb, LV_EVENT_CLICKED, (void *)((uintptr_t)action));

    if (icon != NULL)
    {
        img = ui_create_image_slot(button, 41, 5, 56, 46);
        ui_img_set_src(img, icon);
    }

    label = pet_text(button, text, 0, 43, 138, 24, 24, LV_TEXT_ALIGN_CENTER, false);
    LV_UNUSED(label);
    return button;
}

static void pet_create_header(lv_obj_t *parent)
{
    lv_obj_t *back_hit;
    lv_obj_t *home_hit;

    back_hit = pet_plain_obj(parent, 20, 70, 110, 36, 0, LV_OPA_TRANSP, 0xffffff, 0);
    pet_make_clickable(back_hit, UI_SCREEN_HOME);
    (void)pet_text(parent, "< 返回", 24, 73, 110, 34, 25, LV_TEXT_ALIGN_LEFT, false);

    (void)pet_text(parent, ui_i18n_pick("我的宠物", "My Pet"), 0, 72, 528, 40, 30, LV_TEXT_ALIGN_CENTER, false);

    home_hit = pet_plain_obj(parent, 410, 70, 100, 36, 0, LV_OPA_TRANSP, 0xffffff, 0);
    pet_make_clickable(home_hit, UI_SCREEN_HOME);
    (void)pet_text(parent, "主页 >", 410, 73, 100, 34, 25, LV_TEXT_ALIGN_RIGHT, false);
}

static void pet_create_main_card(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *help;
    lv_obj_t *track;
    lv_obj_t *img;

    card = pet_plain_obj(parent, 21, 116, 486, 425, 14, LV_OPA_COVER, 0xffffff, 2);

    s_level_label = pet_text(card, "Lv-", 24, 28, 140, 48, 40, LV_TEXT_ALIGN_LEFT, false);
    s_growth_label = pet_text(card, "成长: 0 / 0", 24, 72, 190, 30, 23, LV_TEXT_ALIGN_LEFT, false);
    track = pet_plain_obj(card, 24, 108, 154, 10, 5, LV_OPA_TRANSP, 0xffffff, 2);
    s_growth_fill = pet_plain_obj(track, 2, 2, 0, 6, 3, LV_OPA_COVER, 0x000000, 0);
    s_mood_label = pet_text(card, "快乐: 0 / 100", 24, 122, 200, 34, 23, LV_TEXT_ALIGN_LEFT, false);
    s_energy_label = pet_text(card, "能量: 0 / 100", 24, 152, 200, 34, 23, LV_TEXT_ALIGN_LEFT, false);
    s_days_label = pet_text(card, "陪伴天数: 1天", 24, 182, 210, 34, 23, LV_TEXT_ALIGN_LEFT, false);

    help = pet_plain_obj(card, 389, 21, 74, 36, 8, LV_OPA_COVER, 0xffffff, 2);
    pet_make_clickable(help, UI_SCREEN_PET_RULES);
    (void)pet_text(help, ui_i18n_pick("说明", "Info"), 0, 4, 74, 28, 22, LV_TEXT_ALIGN_CENTER, false);

    s_face_img = ui_create_image_slot(card, 182, 58, 198, 198);
    ui_img_set_src(s_face_img, &pet_face_happy);

    s_speech_label = pet_text(card,
                              "",
                              112,
                              302,
                              282,
                              28,
                              20,
                              LV_TEXT_ALIGN_CENTER,
                              false);
    lv_label_set_long_mode(s_speech_label, LV_LABEL_LONG_DOT);

    (void)pet_plain_obj(card, 14, 342, 458, 1, 0, LV_OPA_COVER, 0x000000, 0);

    img = ui_create_image_slot(card, 23, 363, 46, 34);
    ui_img_set_src(img, &pet_icon_food);
    (void)pet_text(card, ui_i18n_pick("饱食", "Food"), 77, 352, 54, 28, 21, LV_TEXT_ALIGN_LEFT, false);
    s_stat_food_value = pet_text(card, "0", 77, 382, 54, 36, 31, LV_TEXT_ALIGN_LEFT, false);

    (void)pet_plain_obj(card, 129, 360, 1, 58, 0, LV_OPA_COVER, 0x000000, 0);
    img = ui_create_image_slot(card, 138, 358, 31, 45);
    ui_img_set_src(img, &pet_icon_energy);
    (void)pet_text(card, ui_i18n_pick("精力", "Energy"), 177, 352, 54, 28, 21, LV_TEXT_ALIGN_LEFT, false);
    s_stat_energy_value = pet_text(card, "0", 177, 382, 54, 36, 31, LV_TEXT_ALIGN_LEFT, false);

    (void)pet_plain_obj(card, 244, 360, 1, 58, 0, LV_OPA_COVER, 0x000000, 0);
    img = ui_create_image_slot(card, 253, 360, 41, 40);
    ui_img_set_src(img, &pet_icon_mood);
    (void)pet_text(card, ui_i18n_pick("心情", "Mood"), 302, 352, 54, 28, 21, LV_TEXT_ALIGN_LEFT, false);
    s_stat_mood_value = pet_text(card, "0", 302, 382, 54, 36, 31, LV_TEXT_ALIGN_LEFT, false);

    (void)pet_plain_obj(card, 359, 360, 1, 58, 0, LV_OPA_COVER, 0x000000, 0);
    img = ui_create_image_slot(card, 369, 358, 31, 45);
    ui_img_set_src(img, &pet_icon_clean);
    (void)pet_text(card, ui_i18n_pick("清洁", "Clean"), 408, 352, 54, 28, 21, LV_TEXT_ALIGN_LEFT, false);
    s_stat_clean_value = pet_text(card, "0", 408, 382, 54, 36, 31, LV_TEXT_ALIGN_LEFT, false);
}

static void pet_create_action_card(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *img;

    card = pet_plain_obj(parent, 21, 555, 486, 150, 14, LV_OPA_COVER, 0xffffff, 2);
    img = ui_create_image_slot(card, 31, 15, 42, 38);
    ui_img_set_src(img, &pet_icon_notice);
    s_notice_label = pet_text(card,
                              "",
                              82,
                              16,
                              380,
                              34,
                              22,
                              LV_TEXT_ALIGN_LEFT,
                              false);
    lv_label_set_long_mode(s_notice_label, LV_LABEL_LONG_DOT);

    (void)pet_action_button(parent, 39, &pet_icon_play_food, ui_i18n_pick("喂食", "Feed"), PET_UI_ACTION_FEED);
    (void)pet_action_button(parent, 195, &pet_icon_play, ui_i18n_pick("玩耍", "Play"), PET_UI_ACTION_PLAY);
    (void)pet_action_button(parent, 351, &pet_icon_bath, ui_i18n_pick("洗澡", "Bath"), PET_UI_ACTION_CLEAN);
}

void ui_Pet_screen_init(void)
{
    if (ui_Pet != NULL)
    {
        return;
    }

    ui_Pet = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Pet, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Pet, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Pet, LV_OBJ_FLAG_SCROLLABLE);

    petgame_init();

    ui_top_nav_create(ui_Pet, UI_TOP_TAB_AI);
    pet_create_header(ui_Pet);
    pet_create_main_card(ui_Pet);
    pet_create_action_card(ui_Pet);
    ui_bottom_nav_create(ui_Pet, UI_BOTTOM_TAB_PET);

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
    s_growth_fill = NULL;
    s_mood_label = NULL;
    s_energy_label = NULL;
    s_days_label = NULL;
    s_speech_label = NULL;
    s_stat_food_value = NULL;
    s_stat_energy_value = NULL;
    s_stat_mood_value = NULL;
    s_stat_clean_value = NULL;
    s_notice_label = NULL;
    s_last_face_event = PETGAME_ACTIVITY_TYPE_NONE;
    s_last_face_tick = 0U;

    if (ui_Pet != NULL)
    {
        lv_obj_delete(ui_Pet);
        ui_Pet = NULL;
    }
}
