#include "music_service.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <rtthread.h>

#include "app_tf_storage.h"
#include "app_watchdog.h"
#include "audio_manager.h"
#include "audio_mp3ctrl.h"

#define MUSIC_SERVICE_MAX_TRACKS 64
#define MUSIC_SERVICE_NAME_MAX   96
#define MUSIC_SERVICE_PATH_MAX   192
#define MUSIC_SERVICE_VOLUME     0x7FFF

typedef struct
{
    char title[MUSIC_SERVICE_NAME_MAX];
    char path[MUSIC_SERVICE_PATH_MAX];
} music_track_t;

static music_track_t s_tracks[MUSIC_SERVICE_MAX_TRACKS];
static uint16_t s_track_count = 0;
static uint16_t s_selected_index = 0;
static mp3ctrl_handle s_player = RT_NULL;
static bool s_is_playing = false;
static rt_mutex_t s_music_control_lock = RT_NULL;

static rt_mutex_t music_service_control_lock_get(void)
{
    if (s_music_control_lock == RT_NULL)
    {
        s_music_control_lock = rt_mutex_create("musicctl", RT_IPC_FLAG_PRIO);
    }

    return s_music_control_lock;
}

static bool music_service_control_lock_take(void)
{
    rt_mutex_t lock = music_service_control_lock_get();

    if (lock == RT_NULL)
    {
        return false;
    }

    return rt_mutex_take(lock, rt_tick_from_millisecond(1000U)) == RT_EOK;
}

static void music_service_control_lock_release(void)
{
    if (s_music_control_lock != RT_NULL)
    {
        rt_mutex_release(s_music_control_lock);
    }
}

static uint16_t music_service_track_count_snapshot(void)
{
    uint16_t count;

    rt_enter_critical();
    count = s_track_count;
    rt_exit_critical();

    return count;
}

static uint16_t music_service_selected_index_snapshot(void)
{
    uint16_t index;

    rt_enter_critical();
    index = s_selected_index;
    rt_exit_critical();

    return index;
}

static void music_service_set_selected_index(uint16_t index)
{
    rt_enter_critical();
    s_selected_index = index;
    rt_exit_critical();
}

static uint16_t music_service_publish_track(void)
{
    uint16_t index;

    rt_enter_critical();
    index = s_track_count;
    if (s_track_count < MUSIC_SERVICE_MAX_TRACKS)
    {
        s_track_count++;
    }
    rt_exit_critical();

    return index;
}

static bool music_service_playing_snapshot(void)
{
    bool playing;

    rt_enter_critical();
    playing = s_is_playing;
    rt_exit_critical();

    return playing;
}

static void music_service_set_playing(bool playing)
{
    rt_enter_critical();
    s_is_playing = playing;
    rt_exit_critical();
}

static bool music_service_has_mp3_ext(const char *name)
{
    size_t len;

    if (name == RT_NULL)
    {
        return false;
    }

    len = strlen(name);
    if (len < 4)
    {
        return false;
    }

    return (tolower((unsigned char)name[len - 4]) == '.') &&
           (tolower((unsigned char)name[len - 3]) == 'm') &&
           (tolower((unsigned char)name[len - 2]) == 'p') &&
           (tolower((unsigned char)name[len - 1]) == '3');
}

static void music_service_filename_to_title(const char *filename, char *title, size_t title_size)
{
    const char *base = filename;
    size_t len;

    if ((title == RT_NULL) || (title_size == 0))
    {
        return;
    }

    title[0] = '\0';
    if ((filename == RT_NULL) || (filename[0] == '\0'))
    {
        rt_strncpy(title, "未知音频", title_size - 1);
        title[title_size - 1] = '\0';
        return;
    }

    if (strrchr(filename, '/') != RT_NULL)
    {
        base = strrchr(filename, '/') + 1;
    }

    rt_strncpy(title, base, title_size - 1);
    title[title_size - 1] = '\0';

    len = strlen(title);
    if ((len > 4) && music_service_has_mp3_ext(title))
    {
        title[len - 4] = '\0';
    }
}

static void music_service_reset_tracks(void)
{
    rt_enter_critical();
    s_track_count = 0;
    s_selected_index = 0;
    rt_exit_critical();
    memset(s_tracks, 0, sizeof(s_tracks));
}

static void music_service_close_player_only(void)
{
    if (s_player != RT_NULL)
    {
        mp3ctrl_close(s_player);
        s_player = RT_NULL;
    }
    music_service_set_playing(false);
}

static bool music_service_build_dir(char *buffer, size_t buffer_size)
{
    return app_tf_build_path("mp3", buffer, buffer_size);
}

static int music_service_ensure_dir(const char *music_dir)
{
    int ret;
    struct stat st;

    if (music_dir == RT_NULL || music_dir[0] == '\0' || !app_tf_storage_ready())
    {
        return -1;
    }

    ret = stat(music_dir, &st);
    if (ret == 0)
    {
        return 0;
    }

    ret = mkdir(music_dir, 0);
    if (ret == 0)
    {
        rt_kprintf("music: created dir %s\n", music_dir);
        return 0;
    }

    rt_kprintf("music: mkdir failed dir=%s ret=%d\n", music_dir, ret);
    return ret;
}

int music_service_refresh(void)
{
    DIR *dirp;
    struct dirent *entry;
    uint16_t write_index;
    uint32_t scan_tick = 0U;
    int written;
    char music_dir[MUSIC_SERVICE_PATH_MAX];

    app_watchdog_progress(APP_WDT_MODULE_UI);

    if (!music_service_control_lock_take())
    {
        return -1;
    }

    if (!music_service_build_dir(music_dir, sizeof(music_dir)) ||
        music_service_ensure_dir(music_dir) != 0)
    {
        music_service_reset_tracks();
        music_service_control_lock_release();
        return -1;
    }

    dirp = opendir(music_dir);
    if (dirp == RT_NULL)
    {
        music_service_reset_tracks();
        rt_kprintf("music: opendir failed dir=%s\n", music_dir);
        music_service_control_lock_release();
        return -1;
    }

    music_service_reset_tracks();
    app_watchdog_progress(APP_WDT_MODULE_UI);

    while (((entry = readdir(dirp)) != RT_NULL) &&
           (music_service_track_count_snapshot() < MUSIC_SERVICE_MAX_TRACKS))
    {
        if ((scan_tick++ & 0x0FU) == 0U)
        {
            app_watchdog_progress(APP_WDT_MODULE_UI);
        }

        if ((entry->d_name[0] == '.') || (!music_service_has_mp3_ext(entry->d_name)))
        {
            continue;
        }

        write_index = music_service_track_count_snapshot();
        if (write_index >= MUSIC_SERVICE_MAX_TRACKS)
        {
            break;
        }

        music_service_filename_to_title(entry->d_name,
                                        s_tracks[write_index].title,
                                        sizeof(s_tracks[write_index].title));
        written = rt_snprintf(s_tracks[write_index].path,
                              sizeof(s_tracks[write_index].path),
                              "%s/%s",
                              music_dir,
                              entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(s_tracks[write_index].path))
        {
            memset(&s_tracks[write_index], 0, sizeof(s_tracks[write_index]));
            rt_kprintf("music: skip overlong path name=%s\n", entry->d_name);
            continue;
        }
        (void)music_service_publish_track();
        app_watchdog_progress(APP_WDT_MODULE_UI);
    }

    closedir(dirp);
    app_watchdog_progress(APP_WDT_MODULE_UI);

    if ((music_service_track_count_snapshot() > 0) &&
        (music_service_selected_index_snapshot() >= music_service_track_count_snapshot()))
    {
        music_service_set_selected_index(0);
    }

    rt_kprintf("music: scanned %u track(s) from %s\n",
               music_service_track_count_snapshot(),
               music_dir);
    music_service_control_lock_release();
    return (int)music_service_track_count_snapshot();
}

uint16_t music_service_count(void)
{
    return music_service_track_count_snapshot();
}

uint16_t music_service_selected_index(void)
{
    return music_service_selected_index_snapshot();
}

bool music_service_select(uint16_t index)
{
    if (index >= music_service_track_count_snapshot())
    {
        return false;
    }

    music_service_set_selected_index(index);
    return true;
}

bool music_service_select_prev(void)
{
    uint16_t count = music_service_track_count_snapshot();
    uint16_t selected = music_service_selected_index_snapshot();

    if (count == 0)
    {
        return false;
    }

    if (selected == 0)
    {
        selected = count - 1;
    }
    else
    {
        selected--;
    }

    music_service_set_selected_index(selected);
    return true;
}

bool music_service_select_next(void)
{
    uint16_t count = music_service_track_count_snapshot();
    uint16_t selected = music_service_selected_index_snapshot();

    if (count == 0)
    {
        return false;
    }

    selected = (uint16_t)((selected + 1) % count);
    music_service_set_selected_index(selected);
    return true;
}

void music_service_get_title_copy(uint16_t index, char *buffer, size_t buffer_size)
{
    const char *title = "未找到音乐";

    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return;
    }

    rt_enter_critical();
    if (index < s_track_count)
    {
        title = s_tracks[index].title;
    }
    rt_strncpy(buffer, title, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
    rt_exit_critical();
}

void music_service_get_selected_title_copy(char *buffer, size_t buffer_size)
{
    uint16_t selected;

    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return;
    }

    rt_enter_critical();
    selected = s_selected_index;
    if (selected < s_track_count)
    {
        rt_strncpy(buffer, s_tracks[selected].title, buffer_size - 1U);
    }
    else
    {
        rt_strncpy(buffer, "未找到音乐", buffer_size - 1U);
    }
    buffer[buffer_size - 1U] = '\0';
    rt_exit_critical();
}

void music_service_get_selected_path_copy(char *buffer, size_t buffer_size)
{
    uint16_t selected;

    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return;
    }

    rt_enter_critical();
    selected = s_selected_index;
    if (selected < s_track_count)
    {
        rt_strncpy(buffer, s_tracks[selected].path, buffer_size - 1U);
    }
    else
    {
        buffer[0] = '\0';
    }
    buffer[buffer_size - 1U] = '\0';
    rt_exit_critical();
}

bool music_service_play_selected(void)
{
    char path[MUSIC_SERVICE_PATH_MAX];

    if (music_service_track_count_snapshot() == 0)
    {
        return false;
    }

    if (!audio_acquire(AUDIO_OWNER_MUSIC, AUDIO_REQ_NONBLOCKING))
    {
        rt_kprintf("music: audio acquire failed\n");
        return false;
    }

    if (!music_service_control_lock_take())
    {
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    music_service_close_player_only();

    music_service_get_selected_path_copy(path, sizeof(path));
    if (path[0] == '\0')
    {
        music_service_control_lock_release();
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    s_player = mp3ctrl_open(AUDIO_TYPE_LOCAL_MUSIC, path, RT_NULL, RT_NULL);
    if (s_player == RT_NULL)
    {
        rt_kprintf("music: open failed path=%s\n", path);
        music_service_control_lock_release();
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    mp3ctrl_ioctl(s_player, 0, MUSIC_SERVICE_VOLUME);
    if (mp3ctrl_play(s_player) != 0)
    {
        rt_kprintf("music: play failed path=%s\n", path);
        music_service_close_player_only();
        music_service_control_lock_release();
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    music_service_set_playing(true);
    rt_kprintf("music: playing %s\n", path);
    music_service_control_lock_release();
    return true;
}

bool music_service_toggle_playback(void)
{
    if (music_service_track_count_snapshot() == 0)
    {
        return false;
    }

    if (!music_service_control_lock_take())
    {
        return false;
    }

    if (s_player == RT_NULL)
    {
        music_service_control_lock_release();
        return music_service_play_selected();
    }

    if (music_service_playing_snapshot())
    {
        if (mp3ctrl_pause(s_player) != 0)
        {
            music_service_control_lock_release();
            return false;
        }
        music_service_set_playing(false);
        music_service_control_lock_release();
        return true;
    }

    if (mp3ctrl_resume(s_player) != 0)
    {
        music_service_control_lock_release();
        return false;
    }

    music_service_set_playing(true);
    music_service_control_lock_release();
    return true;
}

void music_service_stop(void)
{
    if (!music_service_control_lock_take())
    {
        return;
    }

    music_service_close_player_only();
    music_service_control_lock_release();
    if (audio_get_current_owner() == AUDIO_OWNER_MUSIC)
    {
        audio_release(AUDIO_OWNER_MUSIC);
    }
}

bool music_service_is_playing(void)
{
    return music_service_playing_snapshot();
}
