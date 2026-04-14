#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

#include <stdint.h>

lv_obj_t *ui_Time_Manage = NULL;

#define UI_TIME_MANAGE_VISIBLE_COUNT 4U

typedef struct
{
    const char *title;
    const char *subtitle;
    ui_screen_id_t target;
} ui_time_manage_item_t;

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
} ui_time_manage_card_refs_t;

static const ui_time_manage_item_t s_time_manage_items[] = {
    {NULL, NULL, UI_SCREEN_POMODORO},
    {NULL, NULL, UI_SCREEN_DATETIME},
};

static const int s_time_manage_card_y_positions[UI_TIME_MANAGE_VISIBLE_COUNT] = {56, 188, 320, 452};

static ui_time_manage_card_refs_t s_time_manage_cards[UI_TIME_MANAGE_VISIBLE_COUNT];
static lv_obj_t *s_time_manage_prev_button = NULL;
static lv_obj_t *s_time_manage_next_button = NULL;
static lv_obj_t *s_time_manage_page_label = NULL;
static uint16_t s_time_manage_page_offset = 0U;

static const char *ui_time_manage_title_at(size_t index)
{
    switch (index)
    {
    case 0:
        return ui_i18n_pick("番茄时间", "Pomodoro");
    case 1:
    default:
        return ui_i18n_pick("日期与时间", "Date & Time");
    }
}

static const char *ui_time_manage_subtitle_at(size_t index)
{
    switch (index)
    {
    case 0:
        return ui_i18n_pick("专注计时与休息切换", "Focus timer and break cycle");
    case 1:
    default:
        return ui_i18n_pick("手动校准当前时间", "Adjust the device clock");
    }
}

static void ui_time_manage_set_button_enabled(lv_obj_t *button, bool enabled)
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

static void ui_time_manage_show_card(uint16_t slot_index, const ui_time_manage_item_t *item, size_t item_index)
{
    ui_time_manage_card_refs_t *refs;

    if (slot_index >= UI_TIME_MANAGE_VISIBLE_COUNT || item == NULL)
    {
        return;
    }

    refs = &s_time_manage_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(refs->title_label, ui_time_manage_title_at(item_index));
    lv_label_set_text(refs->subtitle_label, ui_time_manage_subtitle_at(item_index));
    lv_obj_set_user_data(refs->card, (void *)(uintptr_t)item->target);
}

static void ui_time_manage_hide_card(uint16_t slot_index)
{
    ui_time_manage_card_refs_t *refs;

    if (slot_index >= UI_TIME_MANAGE_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_time_manage_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(refs->card, LV_STATE_DISABLED);
}

static void ui_time_manage_card_event_cb(lv_event_t *e)
{
    ui_screen_id_t target;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    target = (ui_screen_id_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (target != UI_SCREEN_NONE)
    {
        ui_runtime_switch_to(target);
    }
}

static void ui_time_manage_render(void)
{
    uint16_t visible_index;
    uint16_t item_count = (uint16_t)(sizeof(s_time_manage_items) / sizeof(s_time_manage_items[0]));
    uint16_t total_pages = item_count == 0U ? 0U : (uint16_t)((item_count + UI_TIME_MANAGE_VISIBLE_COUNT - 1U) / UI_TIME_MANAGE_VISIBLE_COUNT);
    uint16_t current_page = total_pages == 0U ? 0U : (uint16_t)(s_time_manage_page_offset / UI_TIME_MANAGE_VISIBLE_COUNT + 1U);
    char page_text[16];
    bool can_prev = s_time_manage_page_offset > 0U;
    bool can_next = (uint16_t)(s_time_manage_page_offset + UI_TIME_MANAGE_VISIBLE_COUNT) < item_count;

    ui_time_manage_set_button_enabled(s_time_manage_prev_button, can_prev);
    ui_time_manage_set_button_enabled(s_time_manage_next_button, can_next);
    if (s_time_manage_page_label != NULL)
    {
        rt_snprintf(page_text, sizeof(page_text), "%u / %u", (unsigned int)current_page, (unsigned int)total_pages);
        lv_label_set_text(s_time_manage_page_label, page_text);
    }

    for (visible_index = 0; visible_index < UI_TIME_MANAGE_VISIBLE_COUNT; ++visible_index)
    {
        uint16_t item_index = (uint16_t)(s_time_manage_page_offset + visible_index);

        if (item_index >= item_count)
        {
            ui_time_manage_hide_card(visible_index);
            continue;
        }

        ui_time_manage_show_card(visible_index, &s_time_manage_items[item_index], item_index);
    }
}

static void ui_time_manage_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_time_manage_page_offset >= UI_TIME_MANAGE_VISIBLE_COUNT)
    {
        s_time_manage_page_offset = (uint16_t)(s_time_manage_page_offset - UI_TIME_MANAGE_VISIBLE_COUNT);
    }
    else
    {
        s_time_manage_page_offset = 0U;
    }

    ui_time_manage_render();
}

static void ui_time_manage_next_event_cb(lv_event_t *e)
{
    uint16_t item_count = (uint16_t)(sizeof(s_time_manage_items) / sizeof(s_time_manage_items[0]));

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_time_manage_page_offset + UI_TIME_MANAGE_VISIBLE_COUNT) < item_count)
    {
        s_time_manage_page_offset = (uint16_t)(s_time_manage_page_offset + UI_TIME_MANAGE_VISIBLE_COUNT);
        ui_time_manage_render();
    }
}

static void ui_time_manage_create_card(lv_obj_t *parent, uint16_t slot_index, int y)
{
    ui_time_manage_card_refs_t *refs = &s_time_manage_cards[slot_index];
    lv_obj_t *divider;

    refs->card = ui_create_card(parent, 0, y, 528, 108, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_radius(refs->card, 0, 0);
    lv_obj_set_style_border_width(refs->card, 0, 0);
    lv_obj_set_style_outline_width(refs->card, 0, 0);
    lv_obj_set_style_shadow_width(refs->card, 0, 0);
    lv_obj_set_style_pad_all(refs->card, 0, 0);
    lv_obj_set_style_bg_color(refs->card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(refs->card, LV_OPA_TRANSP, 0);
    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        24,
                                        24,
                                        448,
                                        34,
                                        28,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    refs->subtitle_label = ui_create_label(refs->card,
                                           "",
                                           24,
                                           66,
                                           448,
                                           22,
                                           19,
                                           LV_TEXT_ALIGN_LEFT,
                                           false,
                                           false);
    divider = lv_obj_create(refs->card);
    lv_obj_set_pos(divider, 0, 107);
    lv_obj_set_size(divider, 528, 1);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(refs->card, ui_time_manage_card_event_cb, LV_EVENT_CLICKED, NULL);
}

void ui_time_manage_hardware_prev_page(void)
{
    if (s_time_manage_prev_button != NULL)
    {
        lv_obj_send_event(s_time_manage_prev_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_time_manage_hardware_next_page(void)
{
    if (s_time_manage_next_button != NULL)
    {
        lv_obj_send_event(s_time_manage_next_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_Time_Manage_screen_init(void)
{
    ui_screen_scaffold_t page;
    uint16_t i;

    if (ui_Time_Manage != NULL)
    {
        return;
    }

    s_time_manage_page_offset = 0U;
    for (i = 0; i < UI_TIME_MANAGE_VISIBLE_COUNT; ++i)
    {
        s_time_manage_cards[i].card = NULL;
        s_time_manage_cards[i].title_label = NULL;
        s_time_manage_cards[i].subtitle_label = NULL;
    }

    ui_Time_Manage = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Time_Manage, ui_i18n_pick("时间管理", "Time"), UI_SCREEN_HOME);

    for (i = 0; i < UI_TIME_MANAGE_VISIBLE_COUNT; ++i)
    {
        ui_time_manage_create_card(page.content, i, s_time_manage_card_y_positions[i]);
    }

    s_time_manage_page_label = ui_create_label(page.content,
                                               "1 / 1",
                                               24,
                                               598,
                                               120,
                                               24,
                                               18,
                                               LV_TEXT_ALIGN_LEFT,
                                               false,
                                               false);
    s_time_manage_prev_button = ui_create_button(page.content, 304, 585, 96, 46, ui_i18n_pick("上翻", "Prev"), 20, UI_SCREEN_NONE, false);
    s_time_manage_next_button = ui_create_button(page.content, 408, 585, 96, 46, ui_i18n_pick("下翻", "Next"), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_time_manage_prev_button, ui_time_manage_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_time_manage_next_button, ui_time_manage_next_event_cb, LV_EVENT_CLICKED, NULL);

    ui_time_manage_render();
}

void ui_Time_Manage_screen_destroy(void)
{
    uint16_t i;

    s_time_manage_prev_button = NULL;
    s_time_manage_next_button = NULL;
    s_time_manage_page_label = NULL;
    s_time_manage_page_offset = 0U;
    for (i = 0; i < UI_TIME_MANAGE_VISIBLE_COUNT; ++i)
    {
        s_time_manage_cards[i].card = NULL;
        s_time_manage_cards[i].title_label = NULL;
        s_time_manage_cards[i].subtitle_label = NULL;
    }

    if (ui_Time_Manage != NULL)
    {
        lv_obj_delete(ui_Time_Manage);
        ui_Time_Manage = NULL;
    }
}
