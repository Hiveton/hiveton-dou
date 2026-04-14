#include <stdint.h>
#include <string.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "music_service.h"

lv_obj_t *ui_Music_List = NULL;

#define UI_MUSIC_LIST_VISIBLE_COUNT 4U

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
} ui_music_list_card_refs_t;

static ui_music_list_card_refs_t s_music_cards[UI_MUSIC_LIST_VISIBLE_COUNT];
static lv_obj_t *s_music_page_label = NULL;
static lv_obj_t *s_music_prev_button = NULL;
static lv_obj_t *s_music_next_button = NULL;
static uint16_t s_music_page_offset = 0U;

static void ui_music_list_set_button_enabled(lv_obj_t *button, bool enabled)
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

static void ui_music_list_open_selected(uint16_t index)
{
    if (!music_service_select(index))
    {
        return;
    }

    (void)music_service_play_selected();
    ui_runtime_switch_to(UI_SCREEN_MUSIC_PLAYER);
}

static void ui_music_list_card_event_cb(lv_event_t *e)
{
    uint16_t index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    index = (uint16_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    ui_music_list_open_selected(index);
}

static void ui_music_list_show_card(uint16_t slot_index, uint16_t item_index)
{
    ui_music_list_card_refs_t *refs;
    char subtitle[64];

    if (slot_index >= UI_MUSIC_LIST_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_music_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    rt_snprintf(subtitle,
                sizeof(subtitle),
                "TF卡 /mp3 · 第 %u 首",
                (unsigned int)(item_index + 1U));

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(refs->title_label, music_service_get_title(item_index));
    lv_label_set_text(refs->subtitle_label, subtitle);
    lv_obj_set_user_data(refs->card, (void *)(uintptr_t)item_index);
}

static void ui_music_list_hide_card(uint16_t slot_index)
{
    ui_music_list_card_refs_t *refs;

    if (slot_index >= UI_MUSIC_LIST_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_music_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(refs->card, LV_STATE_DISABLED);
  }

static void ui_music_list_render(void)
{
    uint16_t item_count = music_service_count();
    uint16_t total_pages = item_count == 0U ? 0U : (uint16_t)((item_count + UI_MUSIC_LIST_VISIBLE_COUNT - 1U) / UI_MUSIC_LIST_VISIBLE_COUNT);
    uint16_t current_page = total_pages == 0U ? 0U : (uint16_t)(s_music_page_offset / UI_MUSIC_LIST_VISIBLE_COUNT + 1U);
    uint16_t visible_index;
    char page_text[16];
    bool can_prev = s_music_page_offset > 0U;
    bool can_next = (uint16_t)(s_music_page_offset + UI_MUSIC_LIST_VISIBLE_COUNT) < item_count;

    ui_music_list_set_button_enabled(s_music_prev_button, can_prev);
    ui_music_list_set_button_enabled(s_music_next_button, can_next);

    if (s_music_page_label != NULL)
    {
        rt_snprintf(page_text, sizeof(page_text), "%u / %u", (unsigned int)current_page, (unsigned int)total_pages);
        lv_label_set_text(s_music_page_label, page_text);
    }

    for (visible_index = 0; visible_index < UI_MUSIC_LIST_VISIBLE_COUNT; ++visible_index)
    {
        uint16_t item_index = (uint16_t)(s_music_page_offset + visible_index);

        if (item_index >= item_count)
        {
            ui_music_list_hide_card(visible_index);
            continue;
        }

        ui_music_list_show_card(visible_index, item_index);
    }
}

static void ui_music_list_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_music_page_offset >= UI_MUSIC_LIST_VISIBLE_COUNT)
    {
        s_music_page_offset = (uint16_t)(s_music_page_offset - UI_MUSIC_LIST_VISIBLE_COUNT);
    }
    else
    {
        s_music_page_offset = 0U;
    }

    ui_music_list_render();
}

static void ui_music_list_next_event_cb(lv_event_t *e)
{
    uint16_t item_count = music_service_count();

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_music_page_offset + UI_MUSIC_LIST_VISIBLE_COUNT) < item_count)
    {
        s_music_page_offset = (uint16_t)(s_music_page_offset + UI_MUSIC_LIST_VISIBLE_COUNT);
        ui_music_list_render();
    }
}

static void ui_music_list_create_card(lv_obj_t *parent, uint16_t slot_index, int y)
{
    ui_music_list_card_refs_t *refs = &s_music_cards[slot_index];
    lv_obj_t *divider;

    refs->card = ui_create_card(parent, 0, y, 528, 126, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_radius(refs->card, 0, 0);
    lv_obj_set_style_border_width(refs->card, 0, 0);
    lv_obj_set_style_outline_width(refs->card, 0, 0);
    lv_obj_set_style_shadow_width(refs->card, 0, 0);
    lv_obj_set_style_pad_all(refs->card, 0, 0);
    lv_obj_set_style_bg_color(refs->card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(refs->card, LV_OPA_TRANSP, 0);

    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        20,
                                        28,
                                        476,
                                        36,
                                        30,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    refs->subtitle_label = ui_create_label(refs->card,
                                           "",
                                           20,
                                           76,
                                           476,
                                           24,
                                           19,
                                           LV_TEXT_ALIGN_LEFT,
                                           false,
                                           false);

    divider = lv_obj_create(refs->card);
    lv_obj_set_pos(divider, 0, 125);
    lv_obj_set_size(divider, 528, 1);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(refs->card, ui_music_list_card_event_cb, LV_EVENT_CLICKED, NULL);
}

void ui_music_list_hardware_prev_page(void)
{
    if (s_music_prev_button != NULL)
    {
        lv_obj_send_event(s_music_prev_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_music_list_hardware_next_page(void)
{
    if (s_music_next_button != NULL)
    {
        lv_obj_send_event(s_music_next_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_Music_List_screen_init(void)
{
    ui_screen_scaffold_t page;
    uint16_t i;

    if (ui_Music_List != NULL)
    {
        return;
    }

    (void)music_service_refresh();
    s_music_page_offset = 0U;
    for (i = 0; i < UI_MUSIC_LIST_VISIBLE_COUNT; ++i)
    {
        s_music_cards[i].card = NULL;
        s_music_cards[i].title_label = NULL;
        s_music_cards[i].subtitle_label = NULL;
    }

    ui_Music_List = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Music_List, ui_i18n_pick("本地音乐", "Music"), UI_SCREEN_HOME);

    for (i = 0; i < UI_MUSIC_LIST_VISIBLE_COUNT; ++i)
    {
        ui_music_list_create_card(page.content, i, (int)(i * 126));
    }

    s_music_page_label = ui_create_label(page.content,
                                         "1 / 1",
                                         24,
                                         598,
                                         120,
                                         24,
                                         18,
                                         LV_TEXT_ALIGN_LEFT,
                                         false,
                                         false);
    s_music_prev_button = ui_create_button(page.content, 304, 585, 96, 46, ui_i18n_pick("上翻", "Prev"), 20, UI_SCREEN_NONE, false);
    s_music_next_button = ui_create_button(page.content, 408, 585, 96, 46, ui_i18n_pick("下翻", "Next"), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_music_prev_button, ui_music_list_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_music_next_button, ui_music_list_next_event_cb, LV_EVENT_CLICKED, NULL);

    ui_music_list_render();
}

void ui_Music_List_screen_destroy(void)
{
    uint16_t i;

    s_music_page_label = NULL;
    s_music_prev_button = NULL;
    s_music_next_button = NULL;
    s_music_page_offset = 0U;
    for (i = 0; i < UI_MUSIC_LIST_VISIBLE_COUNT; ++i)
    {
        s_music_cards[i].card = NULL;
        s_music_cards[i].title_label = NULL;
        s_music_cards[i].subtitle_label = NULL;
    }

    if (ui_Music_List != NULL)
    {
        lv_obj_delete(ui_Music_List);
        ui_Music_List = NULL;
    }
}
