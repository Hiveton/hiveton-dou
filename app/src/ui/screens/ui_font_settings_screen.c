#include "ui.h"
#include "ui_font_manager.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

#include <stdint.h>
#include <string.h>

lv_obj_t *ui_Font_Settings = NULL;

#define UI_FONT_SETTINGS_VISIBLE_COUNT 4U
#define UI_FONT_SETTINGS_MAX_ITEMS 48U

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *title_label;
} ui_font_settings_card_refs_t;

static ui_font_settings_card_refs_t s_font_settings_cards[UI_FONT_SETTINGS_VISIBLE_COUNT];
static lv_obj_t *s_font_settings_prev_button = NULL;
static lv_obj_t *s_font_settings_next_button = NULL;
static lv_obj_t *s_font_settings_page_label = NULL;
static uint16_t s_font_settings_page_offset = 0U;
static uint16_t s_font_settings_total_count = 0U;
static ui_font_manager_item_t s_font_settings_items[UI_FONT_SETTINGS_MAX_ITEMS];
static const int s_font_settings_card_y_positions[UI_FONT_SETTINGS_VISIBLE_COUNT] = {0, 108, 216, 324};

static void ui_font_settings_render(void);

static const char *ui_font_settings_title_text(void)
{
    return ui_i18n_pick("字体设置", "Font");
}

static void ui_font_settings_set_button_enabled(lv_obj_t *button, bool enabled)
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

static void ui_font_settings_refresh_items(void)
{
    memset(s_font_settings_items, 0, sizeof(s_font_settings_items));
    s_font_settings_total_count = ui_font_manager_list_items(s_font_settings_items,
                                                             UI_FONT_SETTINGS_MAX_ITEMS);
    if (s_font_settings_page_offset >= s_font_settings_total_count)
    {
        s_font_settings_page_offset = 0U;
    }
}

static void ui_font_settings_card_event_cb(lv_event_t *e)
{
    ui_font_manager_item_t *item;
    bool changed = false;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    item = (ui_font_manager_item_t *)lv_event_get_user_data(e);
    if (item == NULL)
    {
        return;
    }

    if (item->system)
    {
        changed = ui_font_manager_select_system_font();
    }
    else
    {
        changed = ui_font_manager_select_font_file(item->path);
    }

    LV_UNUSED(changed);
}

static void ui_font_settings_show_card(uint16_t slot_index, ui_font_manager_item_t *item)
{
    ui_font_settings_card_refs_t *refs;

    if (slot_index >= UI_FONT_SETTINGS_VISIBLE_COUNT || item == NULL)
    {
        return;
    }

    refs = &s_font_settings_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(refs->title_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(refs->title_label, item->name);

    if (item->selected)
    {
        lv_obj_set_style_bg_color(refs->card, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(refs->card, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(refs->title_label, lv_color_hex(0xFFFFFF), 0);
    }
    else
    {
        lv_obj_set_style_bg_color(refs->card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(refs->card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(refs->title_label, lv_color_hex(0x000000), 0);
    }

    lv_obj_remove_event_cb(refs->card, ui_font_settings_card_event_cb);
    lv_obj_add_event_cb(refs->card, ui_font_settings_card_event_cb, LV_EVENT_CLICKED, item);
    lv_obj_remove_event_cb(refs->title_label, ui_font_settings_card_event_cb);
    lv_obj_add_event_cb(refs->title_label, ui_font_settings_card_event_cb, LV_EVENT_CLICKED, item);
}

static void ui_font_settings_hide_card(uint16_t slot_index)
{
    if (slot_index >= UI_FONT_SETTINGS_VISIBLE_COUNT || s_font_settings_cards[slot_index].card == NULL)
    {
        return;
    }

    lv_obj_add_flag(s_font_settings_cards[slot_index].card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(s_font_settings_cards[slot_index].card, LV_STATE_DISABLED);
}

static void ui_font_settings_render(void)
{
    uint16_t visible_index;
    uint16_t total_pages;
    uint16_t current_page;
    char page_text[16];
    bool can_prev;
    bool can_next;

    ui_font_settings_refresh_items();
    total_pages = s_font_settings_total_count == 0U ? 0U :
                  (uint16_t)((s_font_settings_total_count + UI_FONT_SETTINGS_VISIBLE_COUNT - 1U) /
                             UI_FONT_SETTINGS_VISIBLE_COUNT);
    current_page = total_pages == 0U ? 0U :
                   (uint16_t)(s_font_settings_page_offset / UI_FONT_SETTINGS_VISIBLE_COUNT + 1U);
    can_prev = s_font_settings_page_offset > 0U;
    can_next = (uint16_t)(s_font_settings_page_offset + UI_FONT_SETTINGS_VISIBLE_COUNT) < s_font_settings_total_count;

    ui_font_settings_set_button_enabled(s_font_settings_prev_button, can_prev);
    ui_font_settings_set_button_enabled(s_font_settings_next_button, can_next);
    if (s_font_settings_page_label != NULL)
    {
        rt_snprintf(page_text, sizeof(page_text), "%u / %u",
                    (unsigned int)current_page,
                    (unsigned int)total_pages);
        lv_label_set_text(s_font_settings_page_label, page_text);
    }

    for (visible_index = 0; visible_index < UI_FONT_SETTINGS_VISIBLE_COUNT; ++visible_index)
    {
        uint16_t item_index = (uint16_t)(s_font_settings_page_offset + visible_index);

        if (item_index >= s_font_settings_total_count)
        {
            ui_font_settings_hide_card(visible_index);
            continue;
        }

        ui_font_settings_show_card(visible_index, &s_font_settings_items[item_index]);
    }
}

static void ui_font_settings_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_font_settings_page_offset >= UI_FONT_SETTINGS_VISIBLE_COUNT)
    {
        s_font_settings_page_offset = (uint16_t)(s_font_settings_page_offset - UI_FONT_SETTINGS_VISIBLE_COUNT);
    }
    else
    {
        s_font_settings_page_offset = 0U;
    }

    ui_font_settings_render();
}

static void ui_font_settings_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_font_settings_page_offset + UI_FONT_SETTINGS_VISIBLE_COUNT) < s_font_settings_total_count)
    {
        s_font_settings_page_offset = (uint16_t)(s_font_settings_page_offset + UI_FONT_SETTINGS_VISIBLE_COUNT);
        ui_font_settings_render();
    }
}

static void ui_font_settings_create_card(lv_obj_t *parent, uint16_t slot_index, int y)
{
    lv_obj_t *divider;

    s_font_settings_cards[slot_index].card = ui_create_card(parent, 0, y, 528, 108, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_radius(s_font_settings_cards[slot_index].card, 0, 0);
    lv_obj_set_style_border_width(s_font_settings_cards[slot_index].card, 0, 0);
    lv_obj_set_style_outline_width(s_font_settings_cards[slot_index].card, 0, 0);
    lv_obj_set_style_shadow_width(s_font_settings_cards[slot_index].card, 0, 0);
    lv_obj_set_style_pad_all(s_font_settings_cards[slot_index].card, 0, 0);
    lv_obj_set_style_bg_color(s_font_settings_cards[slot_index].card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_font_settings_cards[slot_index].card, LV_OPA_TRANSP, 0);

    s_font_settings_cards[slot_index].title_label = ui_create_label(s_font_settings_cards[slot_index].card,
                                                                    "",
                                                                    24,
                                                                    32,
                                                                    480,
                                                                    34,
                                                                    28,
                                                                    LV_TEXT_ALIGN_LEFT,
                                                                    false,
                                                                    false);

    divider = lv_obj_create(s_font_settings_cards[slot_index].card);
    lv_obj_set_pos(divider, 0, 107);
    lv_obj_set_size(divider, 528, 1);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
}

void ui_font_settings_hardware_prev_page(void)
{
    if (s_font_settings_prev_button != NULL)
    {
        lv_obj_send_event(s_font_settings_prev_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_font_settings_hardware_next_page(void)
{
    if (s_font_settings_next_button != NULL)
    {
        lv_obj_send_event(s_font_settings_next_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_Font_Settings_screen_init(void)
{
    ui_screen_scaffold_t page;
    uint16_t i;

    if (ui_Font_Settings != NULL)
    {
        ui_font_settings_render();
        return;
    }

    s_font_settings_page_offset = 0U;
    memset(s_font_settings_cards, 0, sizeof(s_font_settings_cards));

    ui_Font_Settings = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Font_Settings, ui_font_settings_title_text(), UI_SCREEN_SETTINGS);

    for (i = 0; i < UI_FONT_SETTINGS_VISIBLE_COUNT; ++i)
    {
        ui_font_settings_create_card(page.content, i, s_font_settings_card_y_positions[i]);
    }

    s_font_settings_page_label = ui_create_label(page.content,
                                                 "1 / 1",
                                                 24,
                                                 598,
                                                 120,
                                                 24,
                                                 18,
                                                 LV_TEXT_ALIGN_LEFT,
                                                 false,
                                                 false);
    s_font_settings_prev_button = ui_create_button(page.content, 304, 585, 96, 46, ui_i18n_pick("上翻", "Prev"), 20, UI_SCREEN_NONE, false);
    s_font_settings_next_button = ui_create_button(page.content, 408, 585, 96, 46, ui_i18n_pick("下翻", "Next"), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_font_settings_prev_button, ui_font_settings_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_font_settings_next_button, ui_font_settings_next_event_cb, LV_EVENT_CLICKED, NULL);
    ui_font_settings_render();
}

void ui_Font_Settings_screen_destroy(void)
{
    memset(s_font_settings_cards, 0, sizeof(s_font_settings_cards));
    s_font_settings_prev_button = NULL;
    s_font_settings_next_button = NULL;
    s_font_settings_page_label = NULL;
    s_font_settings_page_offset = 0U;
    s_font_settings_total_count = 0U;

    if (ui_Font_Settings != NULL)
    {
        lv_obj_delete(ui_Font_Settings);
        ui_Font_Settings = NULL;
    }
}
