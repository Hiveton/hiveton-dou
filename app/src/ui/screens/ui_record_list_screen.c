#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "ui_components.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "../../xiaozhi/recorder_service.h"

extern const lv_image_dsc_t record_file_icon;
extern const lv_image_dsc_t record_check_on;
extern const lv_image_dsc_t record_check_off;
extern const lv_image_dsc_t record_delete;

lv_obj_t *ui_Record_List = NULL;

#define UI_RECORD_LIST_MAX_FILES 48U
#define UI_RECORD_LIST_VISIBLE_COUNT 7U
#define UI_RECORD_LIST_ROW_HEIGHT 70U
#define UI_RECORD_LIST_FOOTER_Y 490

typedef struct
{
    lv_obj_t *list_panel;
    lv_obj_t *footer_panel;
    lv_obj_t *confirm_overlay;
} ui_record_list_refs_t;

typedef struct
{
    recorder_service_file_t files[UI_RECORD_LIST_MAX_FILES];
    bool checked[UI_RECORD_LIST_MAX_FILES];
    size_t file_count;
    size_t playing_index;
    size_t page_start;
    char suppressed_path[192];
} ui_record_list_state_t;

static ui_record_list_refs_t s_record_list_refs;
static ui_record_list_state_t s_record_list_state;

static void ui_record_list_hide_confirm(void)
{
    if (s_record_list_refs.confirm_overlay != NULL)
    {
        lv_obj_delete(s_record_list_refs.confirm_overlay);
        s_record_list_refs.confirm_overlay = NULL;
    }
}

static bool ui_record_list_copy_path(char *buffer, size_t buffer_size, const char *path)
{
    size_t path_len;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (path == NULL)
    {
        buffer[0] = '\0';
        return true;
    }

    path_len = strlen(path);
    if (path_len >= buffer_size)
    {
        buffer[0] = '\0';
        return false;
    }

    memcpy(buffer, path, path_len + 1U);
    return true;
}

static void ui_record_list_format_time(time_t mtime, char *buffer, size_t buffer_size)
{
    struct tm tm_now;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (mtime <= 0)
    {
        rt_snprintf(buffer, buffer_size, "-- --:--");
        return;
    }

    localtime_r(&mtime, &tm_now);
    rt_snprintf(buffer,
                buffer_size,
                "%02d-%02d  %02d:%02d",
                tm_now.tm_mon + 1,
                tm_now.tm_mday,
                tm_now.tm_hour,
                tm_now.tm_min);
}

static size_t ui_record_list_checked_count(void)
{
    size_t i;
    size_t count = 0U;

    for (i = 0U; i < s_record_list_state.file_count && i < UI_RECORD_LIST_MAX_FILES; ++i)
    {
        if (s_record_list_state.checked[i])
        {
            ++count;
        }
    }

    return count;
}

static void ui_record_list_clear_panel(lv_obj_t *panel)
{
    if (panel != NULL)
    {
        lv_obj_clean(panel);
    }
}

static void ui_record_list_render(void);

static bool ui_record_list_is_path_playing(size_t index)
{
    char playing_path[192];

    if (index >= s_record_list_state.file_count || !recorder_service_is_playing())
    {
        return false;
    }

    recorder_service_get_playing_path_copy(playing_path, sizeof(playing_path));
    if (playing_path[0] == '\0')
    {
        return false;
    }

    return strcmp(playing_path, s_record_list_state.files[index].path) == 0;
}

static void ui_record_list_play_index(size_t index)
{
    const char *path;

    if (index >= s_record_list_state.file_count)
    {
        recorder_service_stop_playback();
        s_record_list_state.playing_index = (size_t)-1;
        s_record_list_state.suppressed_path[0] = '\0';
        ui_record_list_render();
        return;
    }

    path = s_record_list_state.files[index].path;
    if (ui_record_list_is_path_playing(index))
    {
        recorder_service_stop_playback();
        s_record_list_state.playing_index = (size_t)-1;
        (void)ui_record_list_copy_path(s_record_list_state.suppressed_path,
                                       sizeof(s_record_list_state.suppressed_path),
                                       path);
    }
    else if (recorder_service_play_file(path))
    {
        s_record_list_state.playing_index = index;
        s_record_list_state.suppressed_path[0] = '\0';
    }
    else
    {
        s_record_list_state.playing_index = (size_t)-1;
        s_record_list_state.suppressed_path[0] = '\0';
    }

    ui_record_list_render();
}

static void ui_record_list_item_event_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    ui_record_list_play_index(index);
}

static void ui_record_list_check_event_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);

    if (index < s_record_list_state.file_count && index < UI_RECORD_LIST_MAX_FILES)
    {
        s_record_list_state.checked[index] = !s_record_list_state.checked[index];
    }

    ui_record_list_render();
}

static void ui_record_list_select_all_event_cb(lv_event_t *e)
{
    size_t i;
    bool all_checked;

    LV_UNUSED(e);
    all_checked = s_record_list_state.file_count > 0U &&
                  ui_record_list_checked_count() == s_record_list_state.file_count;

    for (i = 0U; i < s_record_list_state.file_count && i < UI_RECORD_LIST_MAX_FILES; ++i)
    {
        s_record_list_state.checked[i] = !all_checked;
    }

    ui_record_list_render();
}

static void ui_record_list_delete_checked_files(void)
{
    size_t i;
    bool deleted_playing = false;

    for (i = 0U; i < s_record_list_state.file_count && i < UI_RECORD_LIST_MAX_FILES; ++i)
    {
        if (!s_record_list_state.checked[i])
        {
            continue;
        }

        if (ui_record_list_is_path_playing(i))
        {
            deleted_playing = true;
        }
        (void)remove(s_record_list_state.files[i].path);
        s_record_list_state.checked[i] = false;
    }

    if (deleted_playing)
    {
        recorder_service_stop_playback();
    }

    s_record_list_state.playing_index = (size_t)-1;
    s_record_list_state.suppressed_path[0] = '\0';
    ui_record_list_render();
}

static void ui_record_list_confirm_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_record_list_hide_confirm();
}

static void ui_record_list_confirm_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_record_list_hide_confirm();
    ui_record_list_delete_checked_files();
}

static void ui_record_list_delete_event_cb(lv_event_t *e)
{
    lv_obj_t *panel;
    lv_obj_t *cancel_button;
    lv_obj_t *ok_button;
    size_t checked_count;

    LV_UNUSED(e);
    checked_count = ui_record_list_checked_count();
    if (checked_count == 0U)
    {
        return;
    }

    ui_record_list_hide_confirm();
    s_record_list_refs.confirm_overlay = lv_obj_create(lv_layer_top());
    if (s_record_list_refs.confirm_overlay == NULL)
    {
        return;
    }

    lv_obj_remove_style_all(s_record_list_refs.confirm_overlay);
    lv_obj_set_pos(s_record_list_refs.confirm_overlay, 0, 0);
    lv_obj_set_size(s_record_list_refs.confirm_overlay, 528, 792);
    lv_obj_set_style_bg_color(s_record_list_refs.confirm_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_record_list_refs.confirm_overlay, LV_OPA_70, 0);
    lv_obj_add_flag(s_record_list_refs.confirm_overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = ui_create_card(s_record_list_refs.confirm_overlay, 64, 278, 400, 210, UI_SCREEN_NONE, false, 16);
    if (panel == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(panel, 2, 0);

    ui_create_label(panel,
                    "确认删除录音？",
                    0,
                    34,
                    400,
                    34,
                    28,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(panel,
                    "删除后不可恢复",
                    0,
                    78,
                    400,
                    26,
                    20,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    cancel_button = ui_create_card(panel, 42, 134, 138, 48, UI_SCREEN_NONE, false, 8);
    if (cancel_button != NULL)
    {
        lv_obj_set_style_border_width(cancel_button, 2, 0);
        lv_obj_set_style_border_color(cancel_button, lv_color_black(), 0);
        lv_obj_add_flag(cancel_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cancel_button, ui_record_list_confirm_cancel_cb, LV_EVENT_CLICKED, NULL);
        ui_create_label(cancel_button, "取消", 0, 9, 138, 28, 22, LV_TEXT_ALIGN_CENTER, false, false);
    }

    ok_button = ui_create_card(panel, 220, 134, 138, 48, UI_SCREEN_NONE, true, 8);
    if (ok_button != NULL)
    {
        lv_obj_t *ok_label;

        lv_obj_set_style_bg_color(ok_button, lv_color_black(), 0);
        lv_obj_set_style_border_width(ok_button, 0, 0);
        lv_obj_add_flag(ok_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ok_button, ui_record_list_confirm_delete_cb, LV_EVENT_CLICKED, NULL);
        ok_label = ui_create_label(ok_button, "删除", 0, 9, 138, 28, 22, LV_TEXT_ALIGN_CENTER, true, false);
        if (ok_label != NULL)
        {
            lv_obj_set_style_text_color(ok_label, lv_color_white(), 0);
        }
    }
}

static void ui_record_list_prev_page(void)
{
    if (s_record_list_state.page_start >= UI_RECORD_LIST_VISIBLE_COUNT)
    {
        s_record_list_state.page_start -= UI_RECORD_LIST_VISIBLE_COUNT;
    }
    else
    {
        s_record_list_state.page_start = 0U;
    }

    ui_record_list_render();
}

static void ui_record_list_next_page(void)
{
    if ((s_record_list_state.page_start + UI_RECORD_LIST_VISIBLE_COUNT) < s_record_list_state.file_count)
    {
        s_record_list_state.page_start += UI_RECORD_LIST_VISIBLE_COUNT;
    }

    ui_record_list_render();
}

static void ui_record_list_render_empty(void)
{
    ui_record_list_clear_panel(s_record_list_refs.list_panel);
    if (s_record_list_refs.list_panel == NULL)
    {
        return;
    }

    ui_create_label(s_record_list_refs.list_panel,
                    ui_i18n_pick("没有找到录音文件", "No recordings found"),
                    24,
                    160,
                    480,
                    36,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

static lv_obj_t *ui_record_list_create_check(lv_obj_t *parent,
                                             int32_t x,
                                             int32_t y,
                                             bool checked,
                                             bool inverted,
                                             size_t index,
                                             bool attach_index)
{
    lv_obj_t *hit;
    lv_obj_t *circle;
    lv_obj_t *mark;
    lv_color_t fg = inverted ? lv_color_white() : lv_color_hex(0x343434);
    lv_color_t bg = inverted ? lv_color_black() : lv_color_white();
    lv_color_t fill = checked ? fg : bg;
    lv_color_t mark_color = checked ? bg : fg;

    hit = ui_create_card(parent, x, y, 40, 40, UI_SCREEN_NONE, false, 20);
    if (hit == NULL)
    {
        return NULL;
    }

    lv_obj_set_style_border_width(hit, 0, 0);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    if (attach_index)
    {
        lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(hit, ui_record_list_check_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    }

    circle = lv_obj_create(hit);
    lv_obj_set_pos(circle, 2, 2);
    lv_obj_set_size(circle, 36, 36);
    lv_obj_set_style_radius(circle, 18, 0);
    lv_obj_set_style_bg_color(circle, fill, 0);
    lv_obj_set_style_bg_opa(circle, checked ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle, fg, 0);
    lv_obj_set_style_border_width(circle, 3, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

    if (checked)
    {
        mark = ui_create_label(hit, "✓", 0, 0, 40, 40, 30, LV_TEXT_ALIGN_CENTER, false, false);
        if (mark != NULL)
        {
            lv_obj_set_style_text_color(mark, mark_color, 0);
            lv_obj_center(mark);
            lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    return hit;
}

static void ui_record_list_set_row_visual(lv_obj_t *card,
                                          lv_obj_t *title,
                                          lv_obj_t *meta,
                                          lv_obj_t *fallback_icon,
                                          lv_obj_t *divider,
                                          bool playing)
{
    lv_color_t bg = playing ? lv_color_black() : lv_color_white();
    lv_color_t fg = playing ? lv_color_white() : lv_color_black();

    lv_obj_set_style_bg_color(card, bg, 0);
    lv_obj_set_style_bg_opa(card, playing ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_text_color(title, fg, 0);
    lv_obj_set_style_text_color(meta, fg, 0);
    if (fallback_icon != NULL)
    {
        lv_obj_set_style_text_color(fallback_icon, fg, 0);
    }
    lv_obj_set_style_bg_color(divider, playing ? lv_color_white() : lv_color_black(), 0);
}

static void ui_record_list_render_items(void)
{
    size_t i;
    size_t visible_index;
    size_t end_index;
    char meta_text[80];
    char time_text[32];
    bool playing = recorder_service_is_playing();

    ui_record_list_clear_panel(s_record_list_refs.list_panel);
    s_record_list_state.file_count = recorder_service_scan_files(s_record_list_state.files,
                                                                 UI_RECORD_LIST_MAX_FILES);
    if (s_record_list_state.file_count == 0U)
    {
        ui_record_list_render_empty();
        return;
    }

    if (s_record_list_state.page_start >= s_record_list_state.file_count)
    {
        s_record_list_state.page_start = 0U;
    }

    if (!playing)
    {
        s_record_list_state.suppressed_path[0] = '\0';
    }

    end_index = s_record_list_state.page_start + UI_RECORD_LIST_VISIBLE_COUNT;
    if (end_index > s_record_list_state.file_count)
    {
        end_index = s_record_list_state.file_count;
    }

    visible_index = 0U;
    for (i = s_record_list_state.page_start; i < end_index; ++i, ++visible_index)
    {
        lv_obj_t *card;
        lv_obj_t *title;
        lv_obj_t *meta;
        lv_obj_t *file_icon;
        lv_obj_t *fallback_icon = NULL;
        lv_obj_t *divider;
        bool row_playing;
        int32_t row_y = (int32_t)(visible_index * UI_RECORD_LIST_ROW_HEIGHT);

        row_playing = ui_record_list_is_path_playing(i) &&
                      s_record_list_state.suppressed_path[0] == '\0';
        if (row_playing)
        {
            s_record_list_state.playing_index = i;
        }

        card = ui_create_card(s_record_list_refs.list_panel,
                              0,
                              row_y,
                              528,
                              UI_RECORD_LIST_ROW_HEIGHT,
                              UI_SCREEN_NONE,
                              false,
                              0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, ui_record_list_item_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        if (row_playing)
        {
            fallback_icon = ui_create_label(card, ">", 36, 17, 40, 40, 34, LV_TEXT_ALIGN_CENTER, false, false);
        }
        else
        {
            file_icon = ui_create_image_slot(card, 36, 15, 40, 40);
            if (file_icon != NULL)
            {
                ui_img_set_src(file_icon, &record_file_icon);
            }
        }

        title = ui_create_label(card,
                                s_record_list_state.files[i].name,
                                89,
                                10,
                                300,
                                26,
                                20,
                                LV_TEXT_ALIGN_LEFT,
                                false,
                                false);
        meta = ui_create_label(card,
                               "",
                               89,
                               35,
                               300,
                               18,
                               10,
                               LV_TEXT_ALIGN_LEFT,
                               false,
                               false);
        ui_record_list_format_time(s_record_list_state.files[i].mtime, time_text, sizeof(time_text));
        rt_snprintf(meta_text,
                    sizeof(meta_text),
                    "%lu.%lu MB  %s",
                    (unsigned long)(s_record_list_state.files[i].size_bytes / (1024U * 1024U)),
                    (unsigned long)(((s_record_list_state.files[i].size_bytes % (1024U * 1024U)) * 10U) / (1024U * 1024U)),
                    time_text);
        lv_label_set_text(meta, meta_text);

        ui_record_list_create_check(card,
                                    445,
                                    15,
                                    s_record_list_state.checked[i],
                                    row_playing,
                                    i,
                                    true);

        divider = lv_obj_create(card);
        lv_obj_set_pos(divider, 43, 69);
        lv_obj_set_size(divider, 442, 1);
        lv_obj_set_style_radius(divider, 0, 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(divider, 0, 0);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);

        ui_record_list_set_row_visual(card, title, meta, fallback_icon, divider, row_playing);
    }
}

static void ui_record_list_render_footer(void)
{
    char text[32];
    size_t checked_count;
    size_t total_pages;
    size_t current_page;
    lv_obj_t *line;
    lv_obj_t *select_all_hit;

    ui_record_list_clear_panel(s_record_list_refs.footer_panel);
    if (s_record_list_refs.footer_panel == NULL)
    {
        return;
    }

    checked_count = ui_record_list_checked_count();
    total_pages = s_record_list_state.file_count == 0U ? 0U :
                  (s_record_list_state.file_count + UI_RECORD_LIST_VISIBLE_COUNT - 1U) / UI_RECORD_LIST_VISIBLE_COUNT;
    current_page = total_pages == 0U ? 0U :
                   (s_record_list_state.page_start / UI_RECORD_LIST_VISIBLE_COUNT) + 1U;

    line = lv_obj_create(s_record_list_refs.footer_panel);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_size(line, 528, 1);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);

    rt_snprintf(text, sizeof(text), "总计：%lu", (unsigned long)s_record_list_state.file_count);
    ui_create_label(s_record_list_refs.footer_panel, text, 43, 27, 100, 24, 20, LV_TEXT_ALIGN_LEFT, false, false);

    rt_snprintf(text, sizeof(text), "选中：%lu", (unsigned long)checked_count);
    ui_create_label(s_record_list_refs.footer_panel, text, 146, 27, 110, 24, 20, LV_TEXT_ALIGN_LEFT, false, false);

    rt_snprintf(text, sizeof(text), "%lu/%lu", (unsigned long)current_page, (unsigned long)total_pages);
    ui_create_label(s_record_list_refs.footer_panel, text, 246, 27, 90, 24, 20, LV_TEXT_ALIGN_CENTER, false, false);

    select_all_hit = ui_create_card(s_record_list_refs.footer_panel, 370, 12, 120, 56, UI_SCREEN_NONE, false, 0);
    if (select_all_hit != NULL)
    {
        lv_obj_set_style_border_width(select_all_hit, 0, 0);
        lv_obj_set_style_bg_opa(select_all_hit, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(select_all_hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(select_all_hit, ui_record_list_select_all_event_cb, LV_EVENT_CLICKED, NULL);
        ui_create_label(select_all_hit, "全选", 0, 17, 58, 24, 20, LV_TEXT_ALIGN_RIGHT, false, false);
        ui_record_list_create_check(select_all_hit,
                                    75,
                                    8,
                                    (s_record_list_state.file_count > 0U &&
                                     checked_count == s_record_list_state.file_count),
                                    false,
                                    0U,
                                    false);
    }
}

static void ui_record_list_render(void)
{
    ui_record_list_render_items();
    ui_record_list_render_footer();
}

void ui_Record_List_screen_init(void)
{
    lv_obj_t *content;

    if (ui_Record_List != NULL)
    {
        return;
    }

    recorder_service_init();
    memset(&s_record_list_refs, 0, sizeof(s_record_list_refs));
    memset(&s_record_list_state, 0, sizeof(s_record_list_state));
    s_record_list_state.playing_index = (size_t)-1;
    s_record_list_state.page_start = 0U;

    ui_Record_List = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Record_List, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ui_Record_List, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Record_List, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_action_create(ui_Record_List,
                                       ui_i18n_pick("录音管理", "Recordings"),
                                       UI_SCREEN_RECORDER,
                                       &record_delete,
                                       ui_record_list_delete_event_cb,
                                       NULL);

    content = lv_obj_create(ui_Record_List);
    lv_obj_remove_style_all(content);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(content, 0, 90);
    lv_obj_set_size(content, 528, 702);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    s_record_list_refs.list_panel = lv_obj_create(content);
    lv_obj_remove_flag(s_record_list_refs.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_record_list_refs.list_panel, 0, 0);
    lv_obj_set_size(s_record_list_refs.list_panel, 528, UI_RECORD_LIST_FOOTER_Y);
    lv_obj_set_style_bg_opa(s_record_list_refs.list_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_record_list_refs.list_panel, 0, 0);
    lv_obj_set_style_pad_all(s_record_list_refs.list_panel, 0, 0);
    lv_obj_set_scrollbar_mode(s_record_list_refs.list_panel, LV_SCROLLBAR_MODE_OFF);

    s_record_list_refs.footer_panel = lv_obj_create(content);
    lv_obj_remove_flag(s_record_list_refs.footer_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_record_list_refs.footer_panel, 0, UI_RECORD_LIST_FOOTER_Y);
    lv_obj_set_size(s_record_list_refs.footer_panel, 528, 90);
    lv_obj_set_style_bg_opa(s_record_list_refs.footer_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_record_list_refs.footer_panel, 0, 0);
    lv_obj_set_style_pad_all(s_record_list_refs.footer_panel, 0, 0);

    ui_record_list_render();
}

void ui_Record_List_screen_destroy(void)
{
    ui_record_list_hide_confirm();
    memset(&s_record_list_refs, 0, sizeof(s_record_list_refs));
    memset(&s_record_list_state, 0, sizeof(s_record_list_state));

    if (ui_Record_List != NULL)
    {
        lv_obj_delete(ui_Record_List);
        ui_Record_List = NULL;
    }
}

void ui_record_list_hardware_prev_page(void)
{
    ui_record_list_prev_page();
}

void ui_record_list_hardware_next_page(void)
{
    ui_record_list_next_page();
}
