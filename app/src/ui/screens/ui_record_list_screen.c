#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "../../xiaozhi/recorder_service.h"

lv_obj_t *ui_Record_List = NULL;

typedef struct
{
    lv_obj_t *summary_card;
    lv_obj_t *summary_label;
    lv_obj_t *hint_label;
    lv_obj_t *list_panel;
} ui_record_list_refs_t;

typedef struct
{
    recorder_service_file_t files[48];
    size_t file_count;
    size_t selected_index;
} ui_record_list_state_t;

static ui_record_list_refs_t s_record_list_refs;
static ui_record_list_state_t s_record_list_state;

static void ui_record_list_format_time(time_t mtime, char *buffer, size_t buffer_size)
{
    struct tm tm_now;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (mtime <= 0)
    {
        rt_snprintf(buffer, buffer_size, "--");
        return;
    }

    localtime_r(&mtime, &tm_now);
    rt_snprintf(buffer,
                buffer_size,
                "%04d-%02d-%02d %02d:%02d",
                tm_now.tm_year + 1900,
                tm_now.tm_mon + 1,
                tm_now.tm_mday,
                tm_now.tm_hour,
                tm_now.tm_min);
}

static void ui_record_list_format_duration(uint32_t duration_ms, char *buffer, size_t buffer_size)
{
    uint32_t total_seconds;
    uint32_t minutes;
    uint32_t seconds;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    total_seconds = duration_ms / 1000U;
    minutes = total_seconds / 60U;
    seconds = total_seconds % 60U;
    rt_snprintf(buffer, buffer_size, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

static void ui_record_list_set_row_style(lv_obj_t *card,
                                         lv_obj_t *title,
                                         lv_obj_t *meta,
                                         lv_obj_t *badge,
                                         bool selected)
{
    lv_color_t bg = selected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
    lv_color_t fg = selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000);

    lv_obj_set_style_bg_color(card, bg, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(title, fg, 0);
    lv_obj_set_style_text_color(meta, selected ? lv_color_hex(0xE5E5E5) : lv_color_hex(0x444444), 0);
    lv_obj_set_style_text_color(badge, fg, 0);
}

static void ui_record_list_clear_panel(void)
{
    if (s_record_list_refs.list_panel != NULL)
    {
        lv_obj_clean(s_record_list_refs.list_panel);
    }
}

static void ui_record_list_render(void);

static void ui_record_list_select_index(size_t index)
{
    if (index < s_record_list_state.file_count)
    {
        const char *path = s_record_list_state.files[index].path;
        const char *playing_path = recorder_service_get_playing_path();
        bool is_same_playing = (playing_path != NULL &&
                                playing_path[0] != '\0' &&
                                strcmp(playing_path, path) == 0 &&
                                recorder_service_is_playing());

        if (is_same_playing)
        {
            recorder_service_stop_playback();
            s_record_list_state.selected_index = (size_t)-1;
        }
        else if (recorder_service_play_file(path))
        {
            s_record_list_state.selected_index = index;
        }
        else
        {
            s_record_list_state.selected_index = (size_t)-1;
        }
    }
    else
    {
        s_record_list_state.selected_index = (size_t)-1;
        recorder_service_stop_playback();
    }

    ui_record_list_render();
}

static void ui_record_list_item_event_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    ui_record_list_select_index(index);
}

static void ui_record_list_render_empty(void)
{
    lv_obj_t *card;

    ui_record_list_clear_panel();
    if (s_record_list_refs.list_panel == NULL)
    {
        return;
    }

    card = ui_create_card(s_record_list_refs.list_panel, 0, 0, 442, 92, UI_SCREEN_NONE, false, 18);
    ui_create_label(card,
                    ui_i18n_pick("没有找到录音文件", "No recordings found"),
                    20,
                    18,
                    402,
                    28,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(card,
                    ui_i18n_pick("先去录音页录一段，再回来播放", "Record one first, then come back to play it"),
                    18,
                    50,
                    406,
                    22,
                    17,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

static void ui_record_list_render_items(void)
{
    size_t i;
    char status_text[128];
    char duration_text[16];
    char time_text[32];
    char count_text[64];
    const char *playing_path = recorder_service_get_playing_path();
    const char *playing_name = recorder_service_get_playing_name();

    ui_record_list_clear_panel();
    s_record_list_state.file_count = recorder_service_scan_files(s_record_list_state.files,
                                                                 sizeof(s_record_list_state.files) / sizeof(s_record_list_state.files[0]));

    if (s_record_list_refs.summary_label != NULL)
    {
        rt_snprintf(count_text,
                    sizeof(count_text),
                    "%s%lu",
                    ui_i18n_pick("共 ", "Total "),
                    (unsigned long)s_record_list_state.file_count);
        lv_label_set_text(s_record_list_refs.summary_label, count_text);
    }

    if (s_record_list_state.file_count == 0U)
    {
        ui_record_list_render_empty();
        return;
    }

    for (i = 0U; i < s_record_list_state.file_count; ++i)
    {
        lv_obj_t *card;
        lv_obj_t *title;
        lv_obj_t *meta;
        lv_obj_t *badge;
        bool selected;

        card = ui_create_card(s_record_list_refs.list_panel, 0, (int)(i * 96U), 442, 88, UI_SCREEN_NONE, false, 18);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, ui_record_list_item_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        title = ui_create_label(card,
                                s_record_list_state.files[i].name,
                                18,
                                12,
                                284,
                                28,
                                24,
                                LV_TEXT_ALIGN_LEFT,
                                false,
                                false);
        meta = ui_create_label(card,
                               "",
                               18,
                               46,
                               290,
                               20,
                               17,
                               LV_TEXT_ALIGN_LEFT,
                               false,
                               false);
        badge = ui_create_icon_badge(card, 330, 26, 94, 28, ui_i18n_pick("点击播放", "Tap to play"));

        selected = false;
        if (playing_path != NULL &&
            playing_path[0] != '\0' &&
            strcmp(playing_path, s_record_list_state.files[i].path) == 0 &&
            recorder_service_is_playing())
        {
            selected = true;
            s_record_list_state.selected_index = i;
            lv_label_set_text(badge, ui_i18n_pick("播放中", "Playing"));
        }
        else if (s_record_list_state.selected_index == i)
        {
            selected = recorder_service_is_playing();
            if (selected)
            {
                lv_label_set_text(badge, ui_i18n_pick("播放中", "Playing"));
            }
        }

        ui_record_list_set_row_style(card, title, meta, badge, selected);

        ui_record_list_format_duration(s_record_list_state.files[i].duration_ms, duration_text, sizeof(duration_text));
        ui_record_list_format_time(s_record_list_state.files[i].mtime, time_text, sizeof(time_text));
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s · %s · %lu KB",
                    duration_text,
                    time_text,
                    (unsigned long)((s_record_list_state.files[i].size_bytes + 1023U) / 1024U));
        lv_label_set_text(meta, status_text);
    }
}

static void ui_record_list_render(void)
{
    char hint_text[160];

    if (s_record_list_refs.hint_label != NULL)
    {
        if (recorder_service_is_playing())
        {
            rt_snprintf(hint_text,
                        sizeof(hint_text),
                        "%s%s",
                        ui_i18n_pick("当前播放：", "Playing: "),
                        recorder_service_get_playing_name());
        }
        else
        {
            rt_snprintf(hint_text,
                        sizeof(hint_text),
                        "%s",
                        ui_i18n_pick("点击同一条可停止播放", "Tap the same row to stop"));
        }
        lv_label_set_text(s_record_list_refs.hint_label, hint_text);
    }

    if (s_record_list_state.selected_index >= s_record_list_state.file_count)
    {
        s_record_list_state.selected_index = (size_t)-1;
    }

    if (s_record_list_state.file_count == 0U)
    {
        ui_record_list_render_empty();
    }
    else
    {
        ui_record_list_render_items();
    }
}

void ui_Record_List_screen_init(void)
{
    ui_screen_scaffold_t page;

    if (ui_Record_List != NULL)
    {
        return;
    }

    recorder_service_init();

    ui_Record_List = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Record_List, ui_i18n_pick("录音记录", "Recordings"), UI_SCREEN_RECORDER);

    memset(&s_record_list_refs, 0, sizeof(s_record_list_refs));
    memset(&s_record_list_state, 0, sizeof(s_record_list_state));
    s_record_list_state.selected_index = (size_t)-1;

    s_record_list_refs.summary_card = ui_create_card(page.content, 24, 24, 480, 92, UI_SCREEN_NONE, false, 22);
    s_record_list_refs.summary_label = ui_create_label(s_record_list_refs.summary_card,
                                                       ui_i18n_pick("共 0 条", "Total 0"),
                                                       24,
                                                       16,
                                                       280,
                                                       24,
                                                       26,
                                                       LV_TEXT_ALIGN_LEFT,
                                                       false,
                                                       false);
    s_record_list_refs.hint_label = ui_create_label(s_record_list_refs.summary_card,
                                                    ui_i18n_pick("点击同一条可停止播放", "Tap the same row to stop"),
                                                    24,
                                                    46,
                                                    400,
                                                    22,
                                                    18,
                                                    LV_TEXT_ALIGN_LEFT,
                                                    false,
                                                    false);

    s_record_list_refs.list_panel = lv_obj_create(page.content);
    lv_obj_remove_flag(s_record_list_refs.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_record_list_refs.list_panel, ui_px_x(24), ui_px_y(136));
    lv_obj_set_size(s_record_list_refs.list_panel, ui_px_w(480), ui_px_h(500));
    lv_obj_set_style_bg_opa(s_record_list_refs.list_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_record_list_refs.list_panel, 0, 0);
    lv_obj_set_style_pad_all(s_record_list_refs.list_panel, 0, 0);
    lv_obj_set_scroll_dir(s_record_list_refs.list_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_record_list_refs.list_panel, LV_SCROLLBAR_MODE_AUTO);

    ui_record_list_render();
}

void ui_Record_List_screen_destroy(void)
{
    memset(&s_record_list_refs, 0, sizeof(s_record_list_refs));
    memset(&s_record_list_state, 0, sizeof(s_record_list_state));

    if (ui_Record_List != NULL)
    {
        lv_obj_delete(ui_Record_List);
        ui_Record_List = NULL;
    }
}
