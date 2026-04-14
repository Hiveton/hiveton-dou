#include <string.h>

#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "music_service.h"

lv_obj_t *ui_Music_Player = NULL;

static lv_obj_t *s_music_player_title = NULL;
static lv_obj_t *s_music_player_subtitle = NULL;
static lv_obj_t *s_music_player_state = NULL;
static lv_obj_t *s_music_player_play_button = NULL;
static lv_obj_t *s_music_player_play_label = NULL;

static void ui_music_player_render(void)
{
    char subtitle[96];
    uint16_t count = music_service_count();
    uint16_t index = music_service_selected_index();
    bool is_playing = music_service_is_playing();

    if (s_music_player_title == NULL)
    {
        return;
    }

    if (count == 0U)
    {
        lv_label_set_text(s_music_player_title, ui_i18n_pick("未找到音乐", "No music found"));
        lv_label_set_text(s_music_player_subtitle, ui_i18n_pick("TF卡 /mp3 目录为空", "TF /mp3 is empty"));
        lv_label_set_text(s_music_player_state, ui_i18n_pick("请先拷贝 MP3 文件", "Copy MP3 files first"));
        if (s_music_player_play_label != NULL)
        {
            lv_label_set_text(s_music_player_play_label, ui_i18n_pick("播放", "Play"));
        }
        return;
    }

    rt_snprintf(subtitle,
                sizeof(subtitle),
                "%s %u/%u",
                ui_i18n_pick("TF卡 /mp3 · 第", "TF /mp3 · Track"),
                (unsigned int)(index + 1U),
                (unsigned int)count);

    lv_label_set_text(s_music_player_title, music_service_get_selected_title());
    lv_label_set_text(s_music_player_subtitle, subtitle);
    lv_label_set_text(s_music_player_state, is_playing ? ui_i18n_pick("播放中", "Playing") : ui_i18n_pick("已暂停", "Paused"));
    if (s_music_player_play_label != NULL)
    {
        lv_label_set_text(s_music_player_play_label, is_playing ? ui_i18n_pick("暂停", "Pause") : ui_i18n_pick("播放", "Play"));
    }
}

static void ui_music_player_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (music_service_select_prev())
    {
        (void)music_service_play_selected();
        ui_music_player_render();
    }
}

static void ui_music_player_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (music_service_select_next())
    {
        (void)music_service_play_selected();
        ui_music_player_render();
    }
}

static void ui_music_player_play_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    (void)music_service_toggle_playback();
    ui_music_player_render();
}

void ui_Music_Player_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;

    if (ui_Music_Player != NULL)
    {
        return;
    }

    (void)music_service_refresh();

    ui_Music_Player = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Music_Player, ui_i18n_pick("音乐播放", "Player"), UI_SCREEN_MUSIC_LIST);

    s_music_player_title = ui_create_label(page.content,
                                           "",
                                           24,
                                           84,
                                           480,
                                           120,
                                           36,
                                           LV_TEXT_ALIGN_CENTER,
                                           false,
                                           true);
    s_music_player_subtitle = ui_create_label(page.content,
                                              "",
                                              44,
                                              214,
                                              440,
                                              30,
                                              20,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);
    s_music_player_state = ui_create_label(page.content,
                                           "",
                                           44,
                                           282,
                                           440,
                                           40,
                                           28,
                                           LV_TEXT_ALIGN_CENTER,
                                           false,
                                           false);

    prev_button = ui_create_button(page.content, 32, 520, 136, 56, ui_i18n_pick("上一首", "Prev"), 22, UI_SCREEN_NONE, false);
    s_music_player_play_button = ui_create_button(page.content, 186, 500, 156, 96, ui_i18n_pick("播放", "Play"), 28, UI_SCREEN_NONE, true);
    next_button = ui_create_button(page.content, 360, 520, 136, 56, ui_i18n_pick("下一首", "Next"), 22, UI_SCREEN_NONE, false);

    lv_obj_add_event_cb(prev_button, ui_music_player_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_music_player_play_button, ui_music_player_play_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next_button, ui_music_player_next_event_cb, LV_EVENT_CLICKED, NULL);

    s_music_player_play_label = lv_obj_get_child(s_music_player_play_button, 0);

    ui_music_player_render();
}

void ui_Music_Player_screen_destroy(void)
{
    s_music_player_title = NULL;
    s_music_player_subtitle = NULL;
    s_music_player_state = NULL;
    s_music_player_play_button = NULL;
    s_music_player_play_label = NULL;

    if (ui_Music_Player != NULL)
    {
        lv_obj_delete(ui_Music_Player);
        ui_Music_Player = NULL;
    }
}
