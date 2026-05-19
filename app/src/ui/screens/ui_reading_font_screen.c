#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "rtthread.h"

#include "ui.h"
#include "ui_components.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"
#include "ui_i18n.h"

#define UI_READING_FONT_VISIBLE_COUNT 6U
#define UI_READING_FONT_ROW_TOP       92
#define UI_READING_FONT_ROW_HEIGHT    89
#define UI_READING_FONT_NAME_MAX      96

lv_obj_t *ui_Reading_Font = NULL;

static lv_obj_t *s_font_rows[UI_READING_FONT_VISIBLE_COUNT];
static lv_obj_t *s_font_names[UI_READING_FONT_VISIBLE_COUNT];
static lv_obj_t *s_font_radios[UI_READING_FONT_VISIBLE_COUNT];
static lv_obj_t *s_font_radio_dots[UI_READING_FONT_VISIBLE_COUNT];
static lv_obj_t *s_font_empty_label = NULL;
static uint16_t s_font_page_start = 0U;

static lv_obj_t *ui_reading_font_plain_obj(lv_obj_t *parent,
                                           int x,
                                           int y,
                                           int w,
                                           int h,
                                           int radius,
                                           lv_opa_t opa,
                                           uint32_t bg,
                                           int border_width)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_reading_font_display_name(const char *name, char *buffer, size_t buffer_size)
{
    size_t len;
    static const char suffix[] = ".hdfont";

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    buffer[0] = '\0';
    if (name == NULL || name[0] == '\0')
    {
        return;
    }

    rt_strncpy(buffer, name, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
    len = strlen(buffer);
    if (len > sizeof(suffix) - 1U &&
        strcasecmp(buffer + len - (sizeof(suffix) - 1U), suffix) == 0)
    {
        buffer[len - (sizeof(suffix) - 1U)] = '\0';
    }
}

static void ui_reading_font_set_page_for_current(void)
{
    uint16_t current = ui_reading_detail_get_current_font_item_index();

    s_font_page_start = (uint16_t)((current / UI_READING_FONT_VISIBLE_COUNT) *
                                   UI_READING_FONT_VISIBLE_COUNT);
}

static void ui_reading_font_refresh(void)
{
    uint16_t font_count = ui_reading_detail_get_font_item_count();
    uint16_t current = ui_reading_detail_get_current_font_item_index();
    char raw_name[UI_READING_FONT_NAME_MAX];
    char display_name[UI_READING_FONT_NAME_MAX];
    char label_text[UI_READING_FONT_NAME_MAX + 8U];

    if (font_count == 0U)
    {
        if (s_font_empty_label != NULL)
        {
            lv_obj_clear_flag(s_font_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if (s_font_empty_label != NULL)
    {
        lv_obj_add_flag(s_font_empty_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_font_page_start >= font_count)
    {
        ui_reading_font_set_page_for_current();
    }

    for (uint16_t i = 0U; i < UI_READING_FONT_VISIBLE_COUNT; ++i)
    {
        uint16_t font_index = (uint16_t)(s_font_page_start + i);
        bool visible = font_index < font_count;
        bool selected = visible && font_index == current;

        if (s_font_rows[i] == NULL)
        {
            continue;
        }

        if (!visible)
        {
            lv_obj_add_flag(s_font_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(s_font_rows[i], LV_OBJ_FLAG_HIDDEN);
        raw_name[0] = '\0';
        display_name[0] = '\0';
        (void)ui_reading_detail_get_font_item_name(font_index, raw_name, sizeof(raw_name));
        ui_reading_font_display_name(raw_name, display_name, sizeof(display_name));
        rt_snprintf(label_text,
                    sizeof(label_text),
                    "Aa   %s",
                    display_name[0] != '\0' ? display_name : ui_i18n_pick("未知字体", "Unknown Font"));
        lv_label_set_text(s_font_names[i], label_text);
        lv_obj_set_style_border_width(s_font_radios[i], 3, 0);
        if (selected)
        {
            lv_obj_clear_flag(s_font_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_font_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_reading_font_row_event_cb(lv_event_t *e)
{
    uint16_t font_index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    font_index = (uint16_t)(s_font_page_start + (uint16_t)(uintptr_t)lv_event_get_user_data(e));
    if (ui_reading_detail_select_font_item(font_index))
    {
        ui_dispatch_request_screen_switch(UI_SCREEN_READING_DETAIL);
    }
    else
    {
        ui_reading_font_refresh();
    }
}

static void ui_reading_font_create_rows(void)
{
    for (uint16_t i = 0U; i < UI_READING_FONT_VISIBLE_COUNT; ++i)
    {
        int y = UI_READING_FONT_ROW_TOP + ((int)i * UI_READING_FONT_ROW_HEIGHT);

        s_font_rows[i] = ui_reading_font_plain_obj(ui_Reading_Font,
                                                   0,
                                                   y,
                                                   528,
                                                   88,
                                                   0,
                                                   LV_OPA_COVER,
                                                   0xffffff,
                                                   0);
        lv_obj_add_flag(s_font_rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_font_rows[i],
                            ui_reading_font_row_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        s_font_names[i] = ui_create_label(s_font_rows[i],
                                          "",
                                          32,
                                          28,
                                          390,
                                          36,
                                          26,
                                          LV_TEXT_ALIGN_LEFT,
                                          false,
                                          false);
        lv_obj_set_style_text_color(s_font_names[i], lv_color_hex(0x343434), 0);
        lv_label_set_long_mode(s_font_names[i], LV_LABEL_LONG_DOT);

        s_font_radios[i] = ui_reading_font_plain_obj(s_font_rows[i],
                                                     462,
                                                     28,
                                                     30,
                                                     30,
                                                     15,
                                                     LV_OPA_TRANSP,
                                                     0xffffff,
                                                     3);
        lv_obj_clear_flag(s_font_radios[i], LV_OBJ_FLAG_CLICKABLE);

        s_font_radio_dots[i] = ui_reading_font_plain_obj(s_font_rows[i],
                                                         471,
                                                         37,
                                                         12,
                                                         12,
                                                         6,
                                                         LV_OPA_COVER,
                                                         0x343434,
                                                         0);
        lv_obj_clear_flag(s_font_radio_dots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_font_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_reading_font_bottom_menu_event_cb(lv_event_t *e)
{
    intptr_t index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    index = (intptr_t)lv_event_get_user_data(e);
    if (index == 0)
    {
        ui_dispatch_request_screen_switch(UI_SCREEN_READING_TOC);
    }
}

static void ui_reading_font_create_bottom_menu(void)
{
    static const char *names[] = {"目录", "字体", "大小", "间距", "进度"};
    static const int x_positions[] = {27, 126, 230, 334, 438};
    lv_obj_t *divider;

    divider = ui_reading_font_plain_obj(ui_Reading_Font, 0, 713, 528, 1, 0, LV_OPA_20, 0x343434, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    for (uint16_t i = 0U; i < 5U; ++i)
    {
        if (i == 1U)
        {
            lv_obj_t *selected_bg = ui_reading_font_plain_obj(ui_Reading_Font,
                                                              x_positions[i],
                                                              734,
                                                              64,
                                                              40,
                                                              7,
                                                              LV_OPA_COVER,
                                                              0x343434,
                                                              0);
            lv_obj_clear_flag(selected_bg, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t *label = ui_create_label(ui_Reading_Font,
                                          names[i],
                                          x_positions[i],
                                          740,
                                          64,
                                          28,
                                          24,
                                          LV_TEXT_ALIGN_CENTER,
                                          false,
                                          false);
        lv_obj_set_style_text_color(label, i == 1U ? lv_color_white() : lv_color_hex(0x343434), 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(label,
                            ui_reading_font_bottom_menu_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

void ui_reading_font_hardware_prev_page(void)
{
    if (s_font_page_start >= UI_READING_FONT_VISIBLE_COUNT)
    {
        s_font_page_start = (uint16_t)(s_font_page_start - UI_READING_FONT_VISIBLE_COUNT);
    }
    else
    {
        s_font_page_start = 0U;
    }
    ui_reading_font_refresh();
}

void ui_reading_font_hardware_next_page(void)
{
    uint16_t font_count = ui_reading_detail_get_font_item_count();

    if ((uint16_t)(s_font_page_start + UI_READING_FONT_VISIBLE_COUNT) < font_count)
    {
        s_font_page_start = (uint16_t)(s_font_page_start + UI_READING_FONT_VISIBLE_COUNT);
    }
    ui_reading_font_refresh();
}

void ui_Reading_Font_screen_init(void)
{
    if (ui_Reading_Font != NULL)
    {
        return;
    }

    memset(s_font_rows, 0, sizeof(s_font_rows));
    memset(s_font_names, 0, sizeof(s_font_names));
    memset(s_font_radios, 0, sizeof(s_font_radios));
    memset(s_font_radio_dots, 0, sizeof(s_font_radio_dots));

    ui_Reading_Font = ui_create_screen_base();
    ui_secondary_top_nav_create(ui_Reading_Font, ui_i18n_pick("字体文件", "Font Files"), UI_SCREEN_READING_DETAIL);
    ui_reading_font_create_rows();
    s_font_empty_label = ui_create_label(ui_Reading_Font,
                                         ui_i18n_pick("未找到字体文件", "No font files"),
                                         0,
                                         332,
                                         528,
                                         40,
                                         26,
                                         LV_TEXT_ALIGN_CENTER,
                                         false,
                                         false);
    lv_obj_set_style_text_color(s_font_empty_label, lv_color_hex(0x343434), 0);
    ui_reading_font_create_bottom_menu();
    ui_reading_font_set_page_for_current();
    ui_reading_font_refresh();
}

void ui_Reading_Font_screen_destroy(void)
{
    if (ui_Reading_Font != NULL)
    {
        lv_obj_del(ui_Reading_Font);
        ui_Reading_Font = NULL;
    }

    memset(s_font_rows, 0, sizeof(s_font_rows));
    memset(s_font_names, 0, sizeof(s_font_names));
    memset(s_font_radios, 0, sizeof(s_font_radios));
    memset(s_font_radio_dots, 0, sizeof(s_font_radio_dots));
    s_font_empty_label = NULL;
    s_font_page_start = 0U;
}
