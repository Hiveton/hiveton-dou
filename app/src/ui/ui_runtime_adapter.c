#include "ui_runtime_adapter.h"

#include <stddef.h>
#include "rtthread.h"

#include "ui.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"

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
    {UI_SCREEN_BLUETOOTH_CONFIG, &ui_Bluetooth_Config, ui_Bluetooth_Config_screen_destroy, ui_Bluetooth_Config_screen_init},
    {UI_SCREEN_WALLPAPER, &ui_Wallpaper, ui_Wallpaper_screen_destroy, ui_Wallpaper_screen_init},
};
static bool s_ui_runtime_first_present_done = false;
static lv_timer_t *s_ui_runtime_idle_timer = NULL;
static ui_screen_id_t s_ui_runtime_resume_screen = UI_SCREEN_HOME;

#define UI_RUNTIME_STANDBY_IDLE_MS 20000U
#define UI_RUNTIME_IDLE_POLL_MS    1000U

static void ui_runtime_log_heap(const char *tag,
                                ui_screen_id_t active_id,
                                ui_screen_id_t target_id)
{
    rt_uint32_t total = 0U;
    rt_uint32_t used = 0U;
    rt_uint32_t max_used = 0U;

    rt_memory_info(&total, &used, &max_used);
    rt_kprintf("ui_heap[%s]: active=%d target=%d total=%lu used=%lu free=%lu max=%lu\n",
               tag != NULL ? tag : "na",
               active_id,
               target_id,
               (unsigned long)total,
               (unsigned long)used,
               (unsigned long)(total >= used ? (total - used) : 0U),
               (unsigned long)max_used);
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

static void ui_runtime_idle_timer_cb(lv_timer_t *timer)
{
    ui_screen_id_t active_id;

    LV_UNUSED(timer);

    if (ui_status_panel_is_visible())
    {
        return;
    }

    active_id = ui_runtime_get_active_screen_id();
    if (active_id == UI_SCREEN_NONE || active_id == UI_SCREEN_STANDBY)
    {
        return;
    }

    if (lv_display_get_inactive_time(NULL) < UI_RUNTIME_STANDBY_IDLE_MS)
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
    }

    ui_runtime_log_heap("switch_begin",
                        active_entry != NULL ? active_entry->id : UI_SCREEN_NONE,
                        target);

    if (active_entry != NULL && active_entry->id == target && entry->screen != NULL && *(entry->screen) != NULL)
    {
        if (!s_ui_runtime_first_present_done)
        {
            rt_kprintf("ui_switch: refreshing already-active first screen target=%d\n", entry->id);
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
        rt_kprintf("ui_switch: %d(%p) -> %d(%p) auto_del=%d\n",
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
        lv_obj_update_layout(target_screen);
        lv_obj_invalidate(target_screen);

        if (!s_ui_runtime_first_present_done)
        {
            rt_kprintf("ui_switch: forcing first screen refresh target=%d\n", entry->id);
            lv_refr_now(NULL);
            s_ui_runtime_first_present_done = true;
        }

        ui_runtime_log_heap("switch_end",
                            entry->id,
                            target);
    }
    else if (!s_ui_runtime_first_present_done)
    {
        rt_kprintf("ui_switch: refreshing already-active first screen target=%d\n", entry->id);
        lv_obj_update_layout(target_screen);
        lv_obj_invalidate(target_screen);
        lv_refr_now(NULL);
        s_ui_runtime_first_present_done = true;
        ui_runtime_log_heap("switch_end",
                            entry->id,
                            target);
    }
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

    rt_kprintf("ui_reload: %d(%p) -> %d(%p)\n",
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
