#include "ui_runtime_adapter.h"

#include <stddef.h>
#include <stdint.h>
#include "rtthread.h"

#include "audio_server.h"
#include "ui.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"
#include "../sleep_manager.h"

typedef void (*ui_runtime_screen_destroy_cb_t)(void);

typedef struct
{
    ui_screen_id_t id;
    lv_obj_t **screen;
    ui_runtime_screen_destroy_cb_t destroy;
    ui_runtime_screen_init_cb_t init;
} ui_runtime_screen_entry_t;

static const ui_runtime_screen_entry_t s_ui_runtime_screens[] = {
    {UI_SCREEN_HOME, &ui_Home, ui_Home_screen_destroy, ui_Home_screen_init},
    {UI_SCREEN_STANDBY, &ui_Standby, ui_Standby_screen_destroy, ui_Standby_screen_init},
    {UI_SCREEN_READING_LIST, &ui_Reading_List, ui_Reading_List_screen_destroy, ui_Reading_List_screen_init},
    {UI_SCREEN_READING_DETAIL, &ui_Reading_Detail, ui_Reading_Detail_screen_destroy, ui_Reading_Detail_screen_init},
    {UI_SCREEN_PET, &ui_Pet, ui_Pet_screen_destroy, ui_Pet_screen_init},
    {UI_SCREEN_PET_RULES, &ui_Pet_Rules, ui_Pet_Rules_screen_destroy, ui_Pet_Rules_screen_init},
    {UI_SCREEN_AI_DOU, &ui_AI_Dou, ui_AI_Dou_screen_destroy, ui_AI_Dou_screen_init},
    {UI_SCREEN_TIME_MANAGE, &ui_Time_Manage, ui_Time_Manage_screen_destroy, ui_Time_Manage_screen_init},
    {UI_SCREEN_POMODORO, &ui_Pomodoro, ui_Pomodoro_screen_destroy, ui_Pomodoro_screen_init},
    {UI_SCREEN_DATETIME, &ui_Datetime, ui_Datetime_screen_destroy, ui_Datetime_screen_init},
    {UI_SCREEN_WEATHER, &ui_Weather, ui_Weather_screen_destroy, ui_Weather_screen_init},
    {UI_SCREEN_CALENDAR, &ui_Calendar, ui_Calendar_screen_destroy, ui_Calendar_screen_init},
    {UI_SCREEN_STATUS_DETAIL, &ui_Status_Detail, ui_Status_Detail_screen_destroy, ui_Status_Detail_screen_init},
    {UI_SCREEN_RECORDER, &ui_Recorder, ui_Recorder_screen_destroy, ui_Recorder_screen_init},
    {UI_SCREEN_RECORD_LIST, &ui_Record_List, ui_Record_List_screen_destroy, ui_Record_List_screen_init},
    {UI_SCREEN_MUSIC_LIST, &ui_Music_List, ui_Music_List_screen_destroy, ui_Music_List_screen_init},
    {UI_SCREEN_MUSIC_PLAYER, &ui_Music_Player, ui_Music_Player_screen_destroy, ui_Music_Player_screen_init},
    {UI_SCREEN_SETTINGS, &ui_Settings, ui_Settings_screen_destroy, ui_Settings_screen_init},
    {UI_SCREEN_BRIGHTNESS, &ui_Brightness, ui_Brightness_screen_destroy, ui_Brightness_screen_init},
    {UI_SCREEN_LANGUAGE, &ui_Language, ui_Language_screen_destroy, ui_Language_screen_init},
    {UI_SCREEN_FONT_SETTINGS, &ui_Font_Settings, ui_Font_Settings_screen_destroy, ui_Font_Settings_screen_init},
    {UI_SCREEN_BLUETOOTH_CONFIG, &ui_Bluetooth_Config, ui_Bluetooth_Config_screen_destroy, ui_Bluetooth_Config_screen_init},
    {UI_SCREEN_NETWORK_MODE, &ui_Network_Mode, ui_Network_Mode_screen_destroy, ui_Network_Mode_screen_init},
    {UI_SCREEN_WALLPAPER, &ui_Wallpaper, ui_Wallpaper_screen_destroy, ui_Wallpaper_screen_init},
};
static bool s_ui_runtime_first_present_done = false;
static lv_timer_t *s_ui_runtime_idle_timer = NULL;
static ui_screen_id_t s_ui_runtime_resume_screen = UI_SCREEN_HOME;
static ui_screen_id_t s_ui_runtime_back_stack[16];
static rt_uint8_t s_ui_runtime_back_count = 0;
static bool s_ui_runtime_back_suppress = false;

#define UI_RUNTIME_IDLE_POLL_MS    1000U
#define UI_RUNTIME_VOLUME_MIN      0
#define UI_RUNTIME_VOLUME_MAX      15

#ifndef UI_RUNTIME_HEAP_LOG_VERBOSE
#define UI_RUNTIME_HEAP_LOG_VERBOSE 0
#endif

#if UI_RUNTIME_HEAP_LOG_VERBOSE
#define UI_RUNTIME_HEAP_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define UI_RUNTIME_HEAP_LOG(...) do { } while (0)
#endif

#ifndef UI_EPD_REFRESH_LOG_ENABLED
#define UI_EPD_REFRESH_LOG_ENABLED 0
#endif

#if UI_EPD_REFRESH_LOG_ENABLED
#define UI_EPD_REFRESH_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define UI_EPD_REFRESH_LOG(...) do { } while (0)
#endif

extern void xiaozhi_ui_update_volume(int volume);
extern void ui_reading_detail_hardware_prev_page(void);
extern void ui_reading_detail_hardware_next_page(void);
extern void ui_reading_detail_request_leave_refresh(void);
extern void ui_reading_list_hardware_prev_page(void);
extern void ui_reading_list_hardware_next_page(void);
extern void ui_record_list_hardware_prev_page(void);
extern void ui_record_list_hardware_next_page(void);
extern void ui_music_list_hardware_prev_page(void);
extern void ui_music_list_hardware_next_page(void);
extern void ui_time_manage_hardware_prev_page(void);
extern void ui_time_manage_hardware_next_page(void);
extern void ui_settings_hardware_prev_page(void);
extern void ui_settings_hardware_next_page(void);
extern void ui_font_settings_hardware_prev_page(void);
extern void ui_font_settings_hardware_next_page(void);
extern void ui_network_mode_hardware_prev_option(void);
extern void ui_network_mode_hardware_next_option(void);
extern void ui_calendar_hardware_prev_month(void);
extern void ui_calendar_hardware_next_month(void);
extern void ui_datetime_hardware_adjust(int direction);

static void ui_runtime_log_heap(const char *tag,
                                ui_screen_id_t active_id,
                                ui_screen_id_t target_id)
{
#if UI_RUNTIME_HEAP_LOG_VERBOSE
    rt_uint32_t total = 0U;
    rt_uint32_t used = 0U;
    rt_uint32_t max_used = 0U;

    rt_memory_info(&total, &used, &max_used);
    UI_RUNTIME_HEAP_LOG("ui_heap[%s]: active=%d target=%d total=%lu used=%lu free=%lu max=%lu\n",
                        tag != NULL ? tag : "na",
                        active_id,
                        target_id,
                        (unsigned long)total,
                        (unsigned long)used,
                        (unsigned long)(total >= used ? (total - used) : 0U),
                        (unsigned long)max_used);
#else
    (void)tag;
    (void)active_id;
    (void)target_id;
#endif
}

static const ui_runtime_screen_entry_t *ui_runtime_find_entry_by_screen(lv_obj_t *screen)
{
    size_t i;

    for (i = 0; i < sizeof(s_ui_runtime_screens) / sizeof(s_ui_runtime_screens[0]); ++i)
    {
        if (s_ui_runtime_screens[i].screen != NULL &&
            *(s_ui_runtime_screens[i].screen) == screen)
        {
            return &s_ui_runtime_screens[i];
        }
    }

    return NULL;
}

static void ui_runtime_screen_delete_event_cb(lv_event_t *e)
{
    lv_obj_t **screen_ref = (lv_obj_t **)lv_event_get_user_data(e);

    if (screen_ref != NULL)
    {
        ui_screen_refs_unregister(*screen_ref);
    }

    if (screen_ref != NULL)
    {
        *screen_ref = NULL;
    }
}

static void ui_runtime_attach_delete_hook(lv_obj_t **screen_ref)
{
    if (screen_ref == NULL || *screen_ref == NULL)
    {
        return;
    }

    lv_obj_add_event_cb(*screen_ref,
                        ui_runtime_screen_delete_event_cb,
                        LV_EVENT_DELETE,
                        screen_ref);
}

static void ui_runtime_notify_screen_leave(ui_screen_id_t active_id, ui_screen_id_t target_id)
{
    if (active_id == UI_SCREEN_READING_DETAIL && target_id != UI_SCREEN_READING_DETAIL)
    {
        ui_reading_detail_request_leave_refresh();
    }
}

static const ui_runtime_screen_entry_t *ui_runtime_find_entry_by_id(ui_screen_id_t id)
{
    size_t i;

    for (i = 0; i < sizeof(s_ui_runtime_screens) / sizeof(s_ui_runtime_screens[0]); ++i)
    {
        if (s_ui_runtime_screens[i].id == id)
        {
            return &s_ui_runtime_screens[i];
        }
    }

    return NULL;
}

static const ui_runtime_screen_entry_t *ui_runtime_find_entry_by_ref(lv_obj_t **target)
{
    size_t i;

    for (i = 0; i < sizeof(s_ui_runtime_screens) / sizeof(s_ui_runtime_screens[0]); ++i)
    {
        if (s_ui_runtime_screens[i].screen == target)
        {
            return &s_ui_runtime_screens[i];
        }
    }

    return NULL;
}

static bool ui_runtime_screen_is_transient(ui_screen_id_t id)
{
    return id == UI_SCREEN_WALLPAPER;
}

static void ui_runtime_back_push(ui_screen_id_t screen_id)
{
    rt_uint8_t i;

    if (screen_id == UI_SCREEN_NONE || screen_id == UI_SCREEN_STANDBY)
    {
        return;
    }

    if (s_ui_runtime_back_count > 0 &&
        s_ui_runtime_back_stack[s_ui_runtime_back_count - 1] == screen_id)
    {
        return;
    }

    if (s_ui_runtime_back_count >= sizeof(s_ui_runtime_back_stack) / sizeof(s_ui_runtime_back_stack[0]))
    {
        for (i = 1; i < s_ui_runtime_back_count; ++i)
        {
            s_ui_runtime_back_stack[i - 1] = s_ui_runtime_back_stack[i];
        }
        s_ui_runtime_back_count--;
    }

    s_ui_runtime_back_stack[s_ui_runtime_back_count++] = screen_id;
}

static ui_screen_id_t ui_runtime_back_pop(void)
{
    while (s_ui_runtime_back_count > 0)
    {
        ui_screen_id_t target = s_ui_runtime_back_stack[--s_ui_runtime_back_count];
        if (target != UI_SCREEN_NONE && target != UI_SCREEN_STANDBY)
        {
            return target;
        }
    }

    return UI_SCREEN_HOME;
}

static void ui_runtime_adjust_volume(int delta)
{
    int volume = audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);

    volume += delta;
    if (volume < UI_RUNTIME_VOLUME_MIN)
    {
        volume = UI_RUNTIME_VOLUME_MIN;
    }
    if (volume > UI_RUNTIME_VOLUME_MAX)
    {
        volume = UI_RUNTIME_VOLUME_MAX;
    }

    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, volume);
    xiaozhi_ui_update_volume(volume);
}

static void ui_runtime_async_back_cb(void *param)
{
    LV_UNUSED(param);
    ui_runtime_go_back();
}

static void ui_runtime_async_hardkey_cb(void *param)
{
    int direction = (int)(intptr_t)param;
    ui_runtime_handle_hardkey_nav(direction);
}

static lv_obj_t *ui_runtime_get_scroll_target(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *section;
    lv_obj_t *content;
    int32_t child_index;

    if (screen == NULL)
    {
        return NULL;
    }

    section = lv_obj_get_child(screen, 1);
    if (section == NULL)
    {
        return screen;
    }

    content = lv_obj_get_child(section, 1);
    if (content == NULL)
    {
        return screen;
    }

    for (child_index = 0; ; ++child_index)
    {
        lv_obj_t *child = lv_obj_get_child(content, child_index);

        if (child == NULL)
        {
            break;
        }

        if (lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE))
        {
            return child;
        }
    }

    return content;
}

static void ui_runtime_idle_timer_cb(lv_timer_t *timer)
{
    ui_screen_id_t active_id;

    LV_UNUSED(timer);

    active_id = ui_runtime_get_active_screen_id();
    if (!sleep_manager_should_enter_standby(active_id,
                                            lv_display_get_inactive_time(NULL)))
    {
        return;
    }

    ui_runtime_switch_to(UI_SCREEN_STANDBY);
}

static void ui_runtime_prepare_target(const ui_runtime_screen_entry_t *entry)
{
    if (s_ui_runtime_idle_timer == NULL)
    {
        s_ui_runtime_idle_timer = lv_timer_create(ui_runtime_idle_timer_cb,
                                                  UI_RUNTIME_IDLE_POLL_MS,
                                                  NULL);
    }

    if (entry == NULL || entry->screen == NULL || entry->init == NULL)
    {
        return;
    }

    if (ui_runtime_screen_is_transient(entry->id) &&
        *(entry->screen) != NULL &&
        entry->destroy != NULL)
    {
        entry->destroy();
    }

    if (*(entry->screen) == NULL)
    {
        entry->init();
    }
    ui_runtime_attach_delete_hook(entry->screen);
}

lv_obj_t *ui_runtime_get_home_screen(void)
{
    return ui_Home;
}

void ui_runtime_ensure_home_screen(void)
{
    ui_runtime_prepare_target(ui_runtime_find_entry_by_id(UI_SCREEN_HOME));
}

ui_screen_id_t ui_runtime_get_active_screen_id(void)
{
    const ui_runtime_screen_entry_t *entry = ui_runtime_find_entry_by_screen(lv_screen_active());

    return entry != NULL ? entry->id : UI_SCREEN_NONE;
}

void ui_runtime_exit_standby(void)
{
    ui_screen_id_t target = s_ui_runtime_resume_screen;

    if (ui_runtime_get_active_screen_id() != UI_SCREEN_STANDBY)
    {
        return;
    }

    if (target == UI_SCREEN_NONE || target == UI_SCREEN_STANDBY)
    {
        target = UI_SCREEN_HOME;
    }

    sleep_manager_on_exit_standby(target);
    lv_display_trigger_activity(NULL);
    ui_runtime_switch_to(target);
}

void ui_runtime_switch_to(ui_screen_id_t target)
{
    const ui_runtime_screen_entry_t *entry = ui_runtime_find_entry_by_id(target);
    lv_obj_t *active_screen = lv_screen_active();
    const ui_runtime_screen_entry_t *active_entry = ui_runtime_find_entry_by_screen(active_screen);
    lv_obj_t *target_screen;

    if (entry == NULL)
    {
        rt_kprintf("ui_switch: invalid target=%d active=%p\n", target, active_screen);
        return;
    }

    if (target != UI_SCREEN_STANDBY)
    {
        sleep_manager_report_activity();
    }

    if (target == UI_SCREEN_STANDBY)
    {
        if (active_entry != NULL &&
            active_entry->id != UI_SCREEN_NONE &&
            active_entry->id != UI_SCREEN_STANDBY)
        {
            s_ui_runtime_resume_screen = active_entry->id;
        }
        else
        {
            s_ui_runtime_resume_screen = UI_SCREEN_HOME;
        }

        sleep_manager_on_enter_standby(active_entry != NULL ? active_entry->id : UI_SCREEN_HOME);
    }

    ui_runtime_log_heap("switch_begin",
                        active_entry != NULL ? active_entry->id : UI_SCREEN_NONE,
                        target);

    if (!s_ui_runtime_back_suppress &&
        active_entry != NULL &&
        active_entry->id != target)
    {
        ui_runtime_back_push(active_entry->id);
    }

    if (active_entry != NULL && active_entry->id == target && entry->screen != NULL && *(entry->screen) != NULL)
    {
        if (!s_ui_runtime_first_present_done)
        {
            UI_EPD_REFRESH_LOG("ui_switch: refreshing already-active first screen target=%d\n", entry->id);
            lv_obj_update_layout(*(entry->screen));
            lv_obj_invalidate(*(entry->screen));
            lv_refr_now(NULL);
            s_ui_runtime_first_present_done = true;
        }
        return;
    }

    ui_runtime_prepare_target(entry);
    target_screen = entry->screen != NULL ? *(entry->screen) : NULL;
    if (target_screen == NULL)
    {
        rt_kprintf("ui_switch: target=%d init produced null screen\n", target);
        return;
    }

    LV_UNUSED(ui_screen_refs_get(target_screen));
    if (active_screen != target_screen)
    {
        ui_runtime_notify_screen_leave(active_entry != NULL ? active_entry->id : UI_SCREEN_NONE,
                                       entry->id);
        UI_EPD_REFRESH_LOG("ui_switch: %d(%p) -> %d(%p) auto_del=%d\n",
                           active_entry != NULL ? active_entry->id : UI_SCREEN_NONE,
                           active_screen,
                           entry->id,
                           target_screen,
                           0);
        lv_screen_load_anim(target_screen,
                            LV_SCR_LOAD_ANIM_NONE,
                            0,
                            0,
                            false);
        ui_dispatch_set_active_screen(entry->id);
        ui_force_refresh_global_status_bar();
        lv_obj_update_layout(target_screen);
        lv_obj_invalidate(target_screen);

        if (active_entry != NULL &&
            active_entry->screen != NULL &&
            *(active_entry->screen) == active_screen &&
            ui_runtime_screen_is_transient(active_entry->id) &&
            active_entry->destroy != NULL)
        {
            active_entry->destroy();
        }

        if (!s_ui_runtime_first_present_done)
        {
            UI_EPD_REFRESH_LOG("ui_switch: forcing first screen refresh target=%d\n", entry->id);
            lv_refr_now(NULL);
            s_ui_runtime_first_present_done = true;
        }

        ui_runtime_log_heap("switch_end",
                            entry->id,
                            target);
    }
    else if (!s_ui_runtime_first_present_done)
    {
        UI_EPD_REFRESH_LOG("ui_switch: refreshing already-active first screen target=%d\n", entry->id);
        lv_obj_update_layout(target_screen);
        lv_obj_invalidate(target_screen);
        lv_refr_now(NULL);
        s_ui_runtime_first_present_done = true;
        ui_runtime_log_heap("switch_end",
                            entry->id,
                            target);
    }

    s_ui_runtime_back_suppress = false;
}

void ui_runtime_reload(ui_screen_id_t target)
{
    const ui_runtime_screen_entry_t *entry = ui_runtime_find_entry_by_id(target);
    lv_obj_t *active_screen = lv_screen_active();
    const ui_runtime_screen_entry_t *active_entry = ui_runtime_find_entry_by_screen(active_screen);
    lv_obj_t *target_screen;

    if (entry == NULL)
    {
        rt_kprintf("ui_reload: invalid target=%d active=%p\n", target, active_screen);
        return;
    }

    ui_runtime_log_heap("reload_begin",
                        active_entry != NULL ? active_entry->id : UI_SCREEN_NONE,
                        target);

    if (active_entry == NULL || active_entry->id != target)
    {
        ui_runtime_switch_to(target);
        return;
    }

    if (active_entry->destroy != NULL)
    {
        active_entry->destroy();
    }

    ui_runtime_prepare_target(entry);
    target_screen = entry->screen != NULL ? *(entry->screen) : NULL;
    if (target_screen == NULL)
    {
        rt_kprintf("ui_reload: target=%d init produced null screen\n", target);
        return;
    }

    UI_EPD_REFRESH_LOG("ui_reload: %d(%p) -> %d(%p)\n",
                       active_entry->id,
                       active_screen,
                       entry->id,
                       target_screen);
    lv_screen_load_anim(target_screen,
                        LV_SCR_LOAD_ANIM_NONE,
                        0,
                        0,
                        false);
    ui_dispatch_set_active_screen(entry->id);
    lv_obj_update_layout(target_screen);
    lv_obj_invalidate(target_screen);
    ui_runtime_log_heap("reload_end",
                        entry->id,
                        target);
}

void ui_runtime_go_back(void)
{
    ui_screen_id_t active = ui_runtime_get_active_screen_id();
    ui_screen_id_t target;

    if (active == UI_SCREEN_STANDBY)
    {
        ui_runtime_exit_standby();
        return;
    }

    target = ui_runtime_back_pop();
    while (target == active && target != UI_SCREEN_HOME)
    {
        target = ui_runtime_back_pop();
    }

    if (target == active)
    {
        target = UI_SCREEN_HOME;
    }

    UI_EPD_REFRESH_LOG("ui_back: active=%d target=%d\n", (int)active, (int)target);
    s_ui_runtime_back_suppress = true;
    ui_runtime_switch_to(target);
}

void ui_runtime_request_back(void)
{
    lv_async_call(ui_runtime_async_back_cb, NULL);
}

void ui_runtime_handle_hardkey_nav(int direction)
{
    ui_screen_id_t active = ui_dispatch_get_active_screen();

    if (active == UI_SCREEN_NONE)
    {
        active = ui_runtime_get_active_screen_id();
    }

    if (direction == 0)
    {
        return;
    }

    if (active == UI_SCREEN_HOME ||
        active == UI_SCREEN_AI_DOU ||
        active == UI_SCREEN_MUSIC_PLAYER)
    {
        ui_runtime_adjust_volume(direction < 0 ? 1 : -1);
        return;
    }

    if (active == UI_SCREEN_READING_DETAIL)
    {
        if (direction < 0)
        {
            ui_reading_detail_hardware_prev_page();
        }
        else
        {
            ui_reading_detail_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_READING_LIST)
    {
        if (direction < 0)
        {
            ui_reading_list_hardware_prev_page();
        }
        else
        {
            ui_reading_list_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_RECORD_LIST)
    {
        if (direction < 0)
        {
            ui_record_list_hardware_prev_page();
        }
        else
        {
            ui_record_list_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_MUSIC_LIST)
    {
        if (direction < 0)
        {
            ui_music_list_hardware_prev_page();
        }
        else
        {
            ui_music_list_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_TIME_MANAGE)
    {
        if (direction < 0)
        {
            ui_time_manage_hardware_prev_page();
        }
        else
        {
            ui_time_manage_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_SETTINGS)
    {
        if (direction < 0)
        {
            ui_settings_hardware_prev_page();
        }
        else
        {
            ui_settings_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_FONT_SETTINGS)
    {
        if (direction < 0)
        {
            ui_font_settings_hardware_prev_page();
        }
        else
        {
            ui_font_settings_hardware_next_page();
        }
        return;
    }

    if (active == UI_SCREEN_NETWORK_MODE)
    {
        if (direction < 0)
        {
            ui_network_mode_hardware_prev_option();
        }
        else
        {
            ui_network_mode_hardware_next_option();
        }
        return;
    }

    if (active == UI_SCREEN_CALENDAR)
    {
        if (direction < 0)
        {
            ui_calendar_hardware_prev_month();
        }
        else
        {
            ui_calendar_hardware_next_month();
        }
        return;
    }

    if (active == UI_SCREEN_DATETIME)
    {
        ui_datetime_hardware_adjust(direction);
        return;
    }

    if (lv_screen_active() != NULL)
    {
        lv_obj_t *scroll_target = ui_runtime_get_scroll_target();

        if (scroll_target != NULL)
        {
            lv_coord_t page_step;

            lv_obj_add_flag(scroll_target, LV_OBJ_FLAG_SCROLLABLE);
            page_step = lv_obj_get_height(scroll_target);
            if (page_step <= 0)
            {
                page_step = 220;
            }
            lv_obj_scroll_by(scroll_target,
                             0,
                             direction < 0 ? -page_step : page_step,
                             LV_ANIM_OFF);
        }
    }
}

void ui_runtime_request_hardkey_nav(int direction)
{
    ui_screen_id_t active;

    if (direction == 0)
    {
        return;
    }

    active = ui_dispatch_get_active_screen();
    if (active == UI_SCREEN_NONE)
    {
        active = ui_runtime_get_active_screen_id();
    }
    if (active == UI_SCREEN_HOME ||
        active == UI_SCREEN_AI_DOU ||
        active == UI_SCREEN_MUSIC_PLAYER)
    {
        ui_runtime_adjust_volume(direction < 0 ? 1 : -1);
        return;
    }

    lv_async_call(ui_runtime_async_hardkey_cb, (void *)(intptr_t)direction);
}

void ui_runtime_screen_change(lv_obj_t **target,
                              ui_runtime_screen_init_cb_t target_init,
                              uint32_t delay_ms)
{
    const ui_runtime_screen_entry_t *entry;

    LV_UNUSED(delay_ms);

    if (target == NULL)
    {
        return;
    }

    entry = ui_runtime_find_entry_by_ref(target);
    if (entry != NULL)
    {
        ui_runtime_switch_to(entry->id);
        return;
    }

    if (*target == NULL && target_init != NULL)
    {
        target_init();
    }

    if (*target != NULL && *target != lv_screen_active())
    {
        lv_screen_load_anim(*target, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        if (entry != NULL)
        {
            ui_dispatch_set_active_screen(entry->id);
        }
    }
}
