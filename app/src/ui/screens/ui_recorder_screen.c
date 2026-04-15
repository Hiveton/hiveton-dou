#include <string.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "drv_lcd.h"
#include "../../xiaozhi/recorder_service.h"

extern const lv_image_dsc_t 录音开启;
extern const lv_image_dsc_t 录音未开启;

lv_obj_t *ui_Recorder = NULL;

typedef struct
{
    lv_obj_t *timer_label;
    lv_obj_t *action_card;
    lv_obj_t *action_image;
    lv_obj_t *records_card;
    lv_timer_t *timer;
} ui_recorder_refs_t;

static ui_recorder_refs_t s_recorder_refs;

static void ui_recorder_update_action_style(bool recording)
{
    if (s_recorder_refs.action_card == NULL)
    {
        return;
    }

    if (s_recorder_refs.action_image != NULL)
    {
        ui_img_set_src(s_recorder_refs.action_image, recording ? &录音开启 : &录音未开启);
    }
}

static void ui_recorder_refresh_view(void)
{
    char timer_text[16];
    bool recording;
    bool storage_ready;
    uint32_t elapsed_ms;
    uint32_t total_seconds;
    uint32_t minutes;
    uint32_t seconds;

    recording = recorder_service_is_recording();
    storage_ready = recorder_service_storage_ready();
    elapsed_ms = recorder_service_get_record_elapsed_ms();
    total_seconds = elapsed_ms / 1000U;
    minutes = total_seconds / 60U;
    seconds = total_seconds % 60U;

    if (s_recorder_refs.timer_label != NULL)
    {
        rt_snprintf(timer_text, sizeof(timer_text), "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
        lv_label_set_text(s_recorder_refs.timer_label, timer_text);
    }

    if (s_recorder_refs.action_image != NULL)
    {
        if (!storage_ready)
        {
                ui_img_set_src(s_recorder_refs.action_image, &录音未开启);
        }
        else
        {
                ui_img_set_src(s_recorder_refs.action_image, recording ? &录音开启 : &录音未开启);
        }
    }

    if (s_recorder_refs.action_card != NULL)
    {
        if (!storage_ready)
        {
            lv_obj_clear_flag(s_recorder_refs.action_card, LV_OBJ_FLAG_CLICKABLE);
        }
        else
        {
            lv_obj_add_flag(s_recorder_refs.action_card, LV_OBJ_FLAG_CLICKABLE);
            ui_recorder_update_action_style(recording);
        }
    }
}

static void ui_recorder_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    ui_recorder_refresh_view();
}

static void ui_recorder_action_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);

    if (!recorder_service_storage_ready())
    {
        ui_recorder_refresh_view();
        return;
    }

    if (recorder_service_is_recording())
    {
        recorder_service_stop_record();
    }
    else
    {
        recorder_service_start_record();
    }

    ui_recorder_refresh_view();
}

void ui_Recorder_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *action_button;
    lv_obj_t *records_label;
    lv_obj_t *records_arrow;

    if (ui_Recorder != NULL)
    {
        return;
    }

    recorder_service_init();

    ui_Recorder = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Recorder, ui_i18n_pick("录音", "Recorder"), UI_SCREEN_HOME);

    memset(&s_recorder_refs, 0, sizeof(s_recorder_refs));

    s_recorder_refs.timer_label = ui_create_label(page.content,
                                                  "00:00",
                                                  24,
                                                  92,
                                                  480,
                                                  96,
                                                  74,
                                                  LV_TEXT_ALIGN_CENTER,
                                                  false,
                                                  false);

    action_button = ui_create_image_slot(page.content, 136, 196, 256, 256);
    s_recorder_refs.action_card = action_button;
    lv_obj_add_flag(action_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(action_button, ui_recorder_action_event_cb, LV_EVENT_CLICKED, NULL);
    s_recorder_refs.action_image = action_button;
    ui_img_set_src(s_recorder_refs.action_image, &录音未开启);

    s_recorder_refs.records_card = ui_create_card(page.content, 24, 560, 480, 82, UI_SCREEN_RECORD_LIST, true, 20);
    lv_obj_add_flag(s_recorder_refs.records_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_recorder_refs.records_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_recorder_refs.records_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_recorder_refs.records_card, ui_px_w(2), 0);
    lv_obj_set_style_border_color(s_recorder_refs.records_card, lv_color_hex(0x000000), 0);
    records_label = ui_create_label(s_recorder_refs.records_card,
                                    ui_i18n_pick("录音记录", "Recordings"),
                                    24,
                                    24,
                                    360,
                                    34,
                                    28,
                                    LV_TEXT_ALIGN_LEFT,
                                    false,
                                    false);
    lv_obj_set_style_text_color(records_label, lv_color_hex(0x000000), 0);
    LV_UNUSED(records_label);
    records_arrow = ui_create_label(s_recorder_refs.records_card,
                                    ">",
                                    424,
                                    24,
                                    32,
                                    34,
                                    30,
                                    LV_TEXT_ALIGN_CENTER,
                                    false,
                                    false);
    lv_obj_set_style_text_color(records_arrow, lv_color_hex(0x000000), 0);
    LV_UNUSED(records_arrow);

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

    memset(&s_recorder_refs, 0, sizeof(s_recorder_refs));

    if (ui_Recorder != NULL)
    {
        lv_obj_delete(ui_Recorder);
        ui_Recorder = NULL;
    }
}
