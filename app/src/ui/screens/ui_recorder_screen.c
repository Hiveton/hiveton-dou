#include <string.h>

#include "ui.h"
#include "ui_components.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "drv_lcd.h"
#include "../../xiaozhi/recorder_service.h"

extern const lv_image_dsc_t recorder_mic_idle;
extern const lv_image_dsc_t recorder_mic_recording;

lv_obj_t *ui_Recorder = NULL;

typedef struct
{
    lv_obj_t *timer_label;
    lv_obj_t *mic_image;
    lv_obj_t *start_button;
    lv_obj_t *start_label;
    lv_obj_t *stop_button;
    lv_obj_t *stop_label;
    lv_obj_t *convert_button;
    lv_obj_t *convert_label;
    lv_obj_t *records_button;
    lv_obj_t *records_label;
    lv_obj_t *toast_overlay;
    lv_timer_t *toast_timer;
    lv_timer_t *timer;
} ui_recorder_refs_t;

static ui_recorder_refs_t s_recorder_refs;

static void ui_recorder_hide_toast(void)
{
    if (s_recorder_refs.toast_timer != NULL)
    {
        lv_timer_delete(s_recorder_refs.toast_timer);
        s_recorder_refs.toast_timer = NULL;
    }

    if (s_recorder_refs.toast_overlay != NULL)
    {
        lv_obj_delete(s_recorder_refs.toast_overlay);
        s_recorder_refs.toast_overlay = NULL;
    }
}

static void ui_recorder_toast_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_recorder_hide_toast();
}

static void ui_recorder_toast_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_recorder_hide_toast();
}

static void ui_recorder_show_toast(const char *text)
{
    lv_obj_t *panel;

    ui_recorder_hide_toast();

    s_recorder_refs.toast_overlay = lv_obj_create(lv_layer_top());
    if (s_recorder_refs.toast_overlay == NULL)
    {
        return;
    }

    lv_obj_remove_style_all(s_recorder_refs.toast_overlay);
    lv_obj_set_size(s_recorder_refs.toast_overlay, 528, 792);
    lv_obj_set_pos(s_recorder_refs.toast_overlay, 0, 0);
    lv_obj_set_style_bg_opa(s_recorder_refs.toast_overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_recorder_refs.toast_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_recorder_refs.toast_overlay,
                        ui_recorder_toast_click_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    panel = ui_create_card(s_recorder_refs.toast_overlay, 114, 330, 300, 104, UI_SCREEN_NONE, false, 12);
    if (panel != NULL)
    {
        lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(panel, lv_color_black(), 0);
        lv_obj_set_style_border_width(panel, 2, 0);
        ui_create_label(panel,
                        text != NULL ? text : "",
                        0,
                        30,
                        300,
                        44,
                        26,
                        LV_TEXT_ALIGN_CENTER,
                        false,
                        false);
    }

    s_recorder_refs.toast_timer = lv_timer_create(ui_recorder_toast_timer_cb, 1400, NULL);
    if (s_recorder_refs.toast_timer != NULL)
    {
        lv_timer_set_repeat_count(s_recorder_refs.toast_timer, 1);
    }
}

static void ui_recorder_apply_button_style(lv_obj_t *button,
                                           lv_obj_t *label,
                                           bool filled)
{
    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(button, filled ? lv_color_hex(0x343434) : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(button, filled ? 0 : 1, 0);

    if (label != NULL)
    {
        lv_obj_set_style_text_color(label, filled ? lv_color_white() : lv_color_hex(0x343434), 0);
    }
}

static void ui_recorder_update_action_style(bool recording)
{
    if (s_recorder_refs.mic_image != NULL)
    {
        ui_img_set_src(s_recorder_refs.mic_image,
                       recording ? &recorder_mic_recording : &recorder_mic_idle);
    }

    ui_recorder_apply_button_style(s_recorder_refs.start_button,
                                   s_recorder_refs.start_label,
                                   recording);
    ui_recorder_apply_button_style(s_recorder_refs.stop_button,
                                   s_recorder_refs.stop_label,
                                   false);
    ui_recorder_apply_button_style(s_recorder_refs.convert_button,
                                   s_recorder_refs.convert_label,
                                   false);
    ui_recorder_apply_button_style(s_recorder_refs.records_button,
                                   s_recorder_refs.records_label,
                                   false);
}

static void ui_recorder_set_clickable(lv_obj_t *obj, bool clickable)
{
    if (obj == NULL)
    {
        return;
    }

    if (clickable)
    {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    else
    {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void ui_recorder_refresh_view(void)
{
    char timer_text[16];
    bool recording;
    bool storage_ready;
    uint32_t elapsed_ms;
    uint32_t total_seconds;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;

    recording = recorder_service_is_recording();
    storage_ready = recorder_service_storage_ready();
    elapsed_ms = recorder_service_get_record_elapsed_ms();
    total_seconds = elapsed_ms / 1000U;
    hours = total_seconds / 3600U;
    minutes = (total_seconds / 60U) % 60U;
    seconds = total_seconds % 60U;

    if (s_recorder_refs.timer_label != NULL)
    {
        rt_snprintf(timer_text,
                    sizeof(timer_text),
                    "%02lu:%02lu:%02lu",
                    (unsigned long)hours,
                    (unsigned long)minutes,
                    (unsigned long)seconds);
        lv_label_set_text(s_recorder_refs.timer_label, timer_text);
    }

    ui_recorder_update_action_style(recording);
    ui_recorder_set_clickable(s_recorder_refs.start_button, storage_ready && !recording);
    ui_recorder_set_clickable(s_recorder_refs.stop_button, storage_ready && recording);
}

static void ui_recorder_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_recorder_refresh_view();
}

static void ui_recorder_start_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (recorder_service_storage_ready() && !recorder_service_is_recording())
    {
        recorder_service_start_record();
    }

    ui_recorder_refresh_view();
}

static void ui_recorder_stop_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (recorder_service_is_recording())
    {
        recorder_service_stop_record();
    }

    ui_recorder_refresh_view();
}

static void ui_recorder_convert_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_recorder_show_toast(ui_i18n_pick("功能待开放", "Coming soon"));
}

static lv_obj_t *ui_recorder_create_text_button(lv_obj_t *parent,
                                                int32_t x,
                                                int32_t y,
                                                const char *text,
                                                lv_obj_t **label_out)
{
    lv_obj_t *button;
    lv_obj_t *label;

    button = ui_create_card(parent, x, y, 194, 53, UI_SCREEN_NONE, false, 8);
    if (button == NULL)
    {
        if (label_out != NULL)
        {
            *label_out = NULL;
        }
        return NULL;
    }

    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_pad_all(button, 0, 0);

    label = ui_create_label(button,
                            text,
                            0,
                            0,
                            194,
                            0,
                            24,
                            LV_TEXT_ALIGN_CENTER,
                            false,
                            false);
    if (label != NULL)
    {
        lv_obj_set_style_text_color(label, lv_color_hex(0x343434), 0);
        lv_obj_center(label);
    }

    if (label_out != NULL)
    {
        *label_out = label;
    }

    return button;
}

void ui_Recorder_screen_init(void)
{
    lv_obj_t *content;

    if (ui_Recorder != NULL)
    {
        return;
    }

    recorder_service_init();
    memset(&s_recorder_refs, 0, sizeof(s_recorder_refs));

    ui_Recorder = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Recorder, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ui_Recorder, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Recorder, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_Recorder,
                                ui_i18n_pick("录音", "Recorder"),
                                UI_SCREEN_MORE);

    content = lv_obj_create(ui_Recorder);
    lv_obj_remove_style_all(content);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(content, 0, 90);
    lv_obj_set_size(content, 528, 702);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    s_recorder_refs.mic_image = ui_create_image_slot(content, 189, 64, 150, 150);
    if (s_recorder_refs.mic_image != NULL)
    {
        ui_img_set_src(s_recorder_refs.mic_image, &recorder_mic_idle);
    }

    s_recorder_refs.timer_label = ui_create_label(content,
                                                  "00:00:00",
                                                  120,
                                                  252,
                                                  288,
                                                  74,
                                                  48,
                                                  LV_TEXT_ALIGN_CENTER,
                                                  false,
                                                  false);
    if (s_recorder_refs.timer_label != NULL)
    {
        lv_obj_set_style_text_color(s_recorder_refs.timer_label, lv_color_black(), 0);
    }

    s_recorder_refs.start_button = ui_recorder_create_text_button(content,
                                                                  51,
                                                                  395,
                                                                  ui_i18n_pick("开始录音", "Start"),
                                                                  &s_recorder_refs.start_label);
    if (s_recorder_refs.start_button != NULL)
    {
        lv_obj_add_event_cb(s_recorder_refs.start_button,
                            ui_recorder_start_event_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }

    s_recorder_refs.stop_button = ui_recorder_create_text_button(content,
                                                                 282,
                                                                 395,
                                                                 ui_i18n_pick("停止录音", "Stop"),
                                                                 &s_recorder_refs.stop_label);
    if (s_recorder_refs.stop_button != NULL)
    {
        lv_obj_add_event_cb(s_recorder_refs.stop_button,
                            ui_recorder_stop_event_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }

    s_recorder_refs.convert_button = ui_recorder_create_text_button(content,
                                                                    51,
                                                                    488,
                                                                    ui_i18n_pick("文档转换", "Convert"),
                                                                    &s_recorder_refs.convert_label);
    if (s_recorder_refs.convert_button != NULL)
    {
        lv_obj_add_event_cb(s_recorder_refs.convert_button,
                            ui_recorder_convert_event_cb,
                            LV_EVENT_CLICKED,
                            NULL);
    }

    s_recorder_refs.records_button = ui_recorder_create_text_button(content,
                                                                    282,
                                                                    488,
                                                                    ui_i18n_pick("录音管理", "Records"),
                                                                    &s_recorder_refs.records_label);
    if (s_recorder_refs.records_button != NULL)
    {
        ui_attach_nav_event(s_recorder_refs.records_button, UI_SCREEN_RECORD_LIST);
    }

    ui_recorder_refresh_view();
    s_recorder_refs.timer = lv_timer_create(ui_recorder_timer_cb, 1000, NULL);
}

void ui_Recorder_screen_destroy(void)
{
    if (s_recorder_refs.timer != NULL)
    {
        lv_timer_delete(s_recorder_refs.timer);
        s_recorder_refs.timer = NULL;
    }

    ui_recorder_hide_toast();

    memset(&s_recorder_refs, 0, sizeof(s_recorder_refs));

    if (ui_Recorder != NULL)
    {
        lv_obj_delete(ui_Recorder);
        ui_Recorder = NULL;
    }
}
