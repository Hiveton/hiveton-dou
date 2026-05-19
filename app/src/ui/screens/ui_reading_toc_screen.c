#include <stdint.h>
#include <string.h>

#include "rtthread.h"

#include "ui.h"
#include "ui_components.h"
#include "ui_dispatch.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "ui_runtime_adapter.h"

#define UI_READING_TOC_VISIBLE_COUNT 7U
#define UI_READING_TOC_ROW_TOP       92
#define UI_READING_TOC_ROW_HEIGHT    89

lv_obj_t *ui_Reading_Toc = NULL;

static lv_obj_t *s_toc_rows[UI_READING_TOC_VISIBLE_COUNT];
static lv_obj_t *s_toc_titles[UI_READING_TOC_VISIBLE_COUNT];
static lv_obj_t *s_toc_radios[UI_READING_TOC_VISIBLE_COUNT];
static lv_obj_t *s_toc_radio_dots[UI_READING_TOC_VISIBLE_COUNT];
static lv_obj_t *s_toc_empty_label = NULL;
static uint16_t s_toc_page_start = 0U;

static void ui_reading_toc_set_page_for_current(void)
{
    uint16_t current = ui_reading_detail_get_current_chapter_index();

    s_toc_page_start = (uint16_t)((current / UI_READING_TOC_VISIBLE_COUNT) * UI_READING_TOC_VISIBLE_COUNT);
}

static lv_obj_t *ui_reading_toc_plain_obj(lv_obj_t *parent,
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

static void ui_reading_toc_refresh(void)
{
    uint16_t chapter_count = ui_reading_detail_get_chapter_count();
    uint16_t current = ui_reading_detail_get_current_chapter_index();
    char title[96];

    if (chapter_count == 0U)
    {
        if (s_toc_empty_label != NULL)
        {
            lv_obj_clear_flag(s_toc_empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if (s_toc_empty_label != NULL)
    {
        lv_obj_add_flag(s_toc_empty_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_toc_page_start >= chapter_count)
    {
        ui_reading_toc_set_page_for_current();
    }

    for (uint16_t i = 0U; i < UI_READING_TOC_VISIBLE_COUNT; ++i)
    {
        uint16_t chapter_index = (uint16_t)(s_toc_page_start + i);
        bool visible = chapter_index < chapter_count;
        bool selected = visible && chapter_index == current;

        if (s_toc_rows[i] == NULL)
        {
            continue;
        }

        if (!visible)
        {
            lv_obj_add_flag(s_toc_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(s_toc_rows[i], LV_OBJ_FLAG_HIDDEN);
        title[0] = '\0';
        (void)ui_reading_detail_get_chapter_title(chapter_index, title, sizeof(title));
        lv_label_set_text(s_toc_titles[i], title[0] != '\0' ? title : ui_i18n_pick("未命名章节", "Untitled"));
        lv_obj_set_style_border_width(s_toc_radios[i], 3, 0);
        if (selected)
        {
            lv_obj_clear_flag(s_toc_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_toc_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_reading_toc_row_event_cb(lv_event_t *e)
{
    uint16_t chapter_index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    chapter_index = (uint16_t)(s_toc_page_start + (uint16_t)(uintptr_t)lv_event_get_user_data(e));
    if (ui_reading_detail_jump_to_chapter(chapter_index))
    {
        ui_dispatch_request_screen_switch(UI_SCREEN_READING_DETAIL);
    }
}

static void ui_reading_toc_create_rows(void)
{
    for (uint16_t i = 0U; i < UI_READING_TOC_VISIBLE_COUNT; ++i)
    {
        int y = UI_READING_TOC_ROW_TOP + ((int)i * UI_READING_TOC_ROW_HEIGHT);

        s_toc_rows[i] = ui_reading_toc_plain_obj(ui_Reading_Toc,
                                                 0,
                                                 y,
                                                 528,
                                                 88,
                                                 0,
                                                 LV_OPA_COVER,
                                                 0xffffff,
                                                 0);
        lv_obj_add_flag(s_toc_rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_toc_rows[i],
                            ui_reading_toc_row_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        s_toc_titles[i] = ui_create_label(s_toc_rows[i],
                                          "",
                                          32,
                                          28,
                                          390,
                                          36,
                                          26,
                                          LV_TEXT_ALIGN_LEFT,
                                          false,
                                          false);
        lv_obj_set_style_text_color(s_toc_titles[i], lv_color_hex(0x343434), 0);
        lv_label_set_long_mode(s_toc_titles[i], LV_LABEL_LONG_DOT);

        s_toc_radios[i] = ui_reading_toc_plain_obj(s_toc_rows[i],
                                                   462,
                                                   28,
                                                   30,
                                                   30,
                                                   15,
                                                   LV_OPA_TRANSP,
                                                   0xffffff,
                                                   3);
        lv_obj_clear_flag(s_toc_radios[i], LV_OBJ_FLAG_CLICKABLE);

        s_toc_radio_dots[i] = ui_reading_toc_plain_obj(s_toc_rows[i],
                                                       471,
                                                       37,
                                                       12,
                                                       12,
                                                       6,
                                                       LV_OPA_COVER,
                                                       0x343434,
                                                       0);
        lv_obj_clear_flag(s_toc_radio_dots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_toc_radio_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_reading_toc_create_bottom_menu(void)
{
    static const char *names[] = {"目录", "字体", "大小", "间距", "进度"};
    static const int x_positions[] = {27, 126, 230, 334, 438};
    lv_obj_t *divider;

    divider = ui_reading_toc_plain_obj(ui_Reading_Toc, 0, 713, 528, 1, 0, LV_OPA_20, 0x343434, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

    for (uint16_t i = 0U; i < 5U; ++i)
    {
        if (i == 0U)
        {
            lv_obj_t *selected_bg = ui_reading_toc_plain_obj(ui_Reading_Toc,
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

        lv_obj_t *label = ui_create_label(ui_Reading_Toc,
                                          names[i],
                                          x_positions[i],
                                          740,
                                          64,
                                          28,
                                          24,
                                          LV_TEXT_ALIGN_CENTER,
                                          false,
                                          false);
        lv_obj_set_style_text_color(label, i == 0U ? lv_color_white() : lv_color_hex(0x343434), 0);
    }
}

void ui_reading_toc_hardware_prev_page(void)
{
    if (s_toc_page_start >= UI_READING_TOC_VISIBLE_COUNT)
    {
        s_toc_page_start = (uint16_t)(s_toc_page_start - UI_READING_TOC_VISIBLE_COUNT);
    }
    else
    {
        s_toc_page_start = 0U;
    }
    ui_reading_toc_refresh();
}

void ui_reading_toc_hardware_next_page(void)
{
    uint16_t chapter_count = ui_reading_detail_get_chapter_count();

    if ((uint16_t)(s_toc_page_start + UI_READING_TOC_VISIBLE_COUNT) < chapter_count)
    {
        s_toc_page_start = (uint16_t)(s_toc_page_start + UI_READING_TOC_VISIBLE_COUNT);
    }
    ui_reading_toc_refresh();
}

void ui_Reading_Toc_screen_init(void)
{
    if (ui_Reading_Toc != NULL)
    {
        return;
    }

    memset(s_toc_rows, 0, sizeof(s_toc_rows));
    memset(s_toc_titles, 0, sizeof(s_toc_titles));
    memset(s_toc_radios, 0, sizeof(s_toc_radios));
    memset(s_toc_radio_dots, 0, sizeof(s_toc_radio_dots));

    ui_Reading_Toc = ui_create_screen_base();
    ui_secondary_top_nav_create(ui_Reading_Toc, ui_i18n_pick("目录", "Contents"), UI_SCREEN_READING_DETAIL);
    ui_reading_toc_create_rows();
    s_toc_empty_label = ui_create_label(ui_Reading_Toc,
                                        ui_i18n_pick("暂无目录", "No contents"),
                                        0,
                                        332,
                                        528,
                                        40,
                                        26,
                                        LV_TEXT_ALIGN_CENTER,
                                        false,
                                        false);
    lv_obj_set_style_text_color(s_toc_empty_label, lv_color_hex(0x343434), 0);
    ui_reading_toc_create_bottom_menu();
    ui_reading_toc_set_page_for_current();
    ui_reading_toc_refresh();
}

void ui_Reading_Toc_screen_destroy(void)
{
    if (ui_Reading_Toc != NULL)
    {
        lv_obj_del(ui_Reading_Toc);
        ui_Reading_Toc = NULL;
    }

    memset(s_toc_rows, 0, sizeof(s_toc_rows));
    memset(s_toc_titles, 0, sizeof(s_toc_titles));
    memset(s_toc_radios, 0, sizeof(s_toc_radios));
    memset(s_toc_radio_dots, 0, sizeof(s_toc_radio_dots));
    s_toc_empty_label = NULL;
    s_toc_page_start = 0U;
}
