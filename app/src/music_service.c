#include "music_service.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <rtthread.h>

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
    memset(s_tracks, 0, sizeof(s_tracks));
    s_track_count = 0;
    s_selected_index = 0;
}

static void music_service_close_player_only(void)
{
    if (s_player != RT_NULL)
    {
        mp3ctrl_close(s_player);
        s_player = RT_NULL;
    }
    s_is_playing = false;
}

static int music_service_ensure_dir(void)
{
    int ret;
    struct stat st;

    ret = stat(MUSIC_SERVICE_DIR, &st);
    if (ret == 0)
    {
        return 0;
    }

    ret = mkdir(MUSIC_SERVICE_DIR, 0);
    if (ret == 0)
    {
        rt_kprintf("music: created dir %s\n", MUSIC_SERVICE_DIR);
        return 0;
    }

    rt_kprintf("music: mkdir failed dir=%s ret=%d\n", MUSIC_SERVICE_DIR, ret);
    return ret;
}

int music_service_refresh(void)
{
    DIR *dirp;
    struct dirent *entry;

    if (music_service_ensure_dir() != 0)
    {
        music_service_reset_tracks();
        return -1;
    }

    dirp = opendir(MUSIC_SERVICE_DIR);
    if (dirp == RT_NULL)
    {
        music_service_reset_tracks();
        rt_kprintf("music: opendir failed dir=%s\n", MUSIC_SERVICE_DIR);
        return -1;
    }

    music_service_reset_tracks();

    while (((entry = readdir(dirp)) != RT_NULL) && (s_track_count < MUSIC_SERVICE_MAX_TRACKS))
    {
        if ((entry->d_name[0] == '.') || (!music_service_has_mp3_ext(entry->d_name)))
        {
            continue;
        }

        music_service_filename_to_title(entry->d_name,
                                        s_tracks[s_track_count].title,
                                        sizeof(s_tracks[s_track_count].title));
        rt_snprintf(s_tracks[s_track_count].path,
                    sizeof(s_tracks[s_track_count].path),
                    "%s/%s",
                    MUSIC_SERVICE_DIR,
                    entry->d_name);
        s_track_count++;
    }

    closedir(dirp);

    if ((s_track_count > 0) && (s_selected_index >= s_track_count))
    {
        s_selected_index = 0;
    }

    rt_kprintf("music: scanned %u track(s) from %s\n", s_track_count, MUSIC_SERVICE_DIR);
    return (int)s_track_count;
}

uint16_t music_service_count(void)
{
    return s_track_count;
}

uint16_t music_service_selected_index(void)
{
    return s_selected_index;
}

bool music_service_select(uint16_t index)
{
    if (index >= s_track_count)
    {
        return false;
    }

    s_selected_index = index;
    return true;
}

bool music_service_select_prev(void)
{
    if (s_track_count == 0)
    {
        return false;
    }

    if (s_selected_index == 0)
    {
        s_selected_index = s_track_count - 1;
    }
    else
    {
        s_selected_index--;
    }

    return true;
}

bool music_service_select_next(void)
{
    if (s_track_count == 0)
    {
        return false;
    }

    s_selected_index = (uint16_t)((s_selected_index + 1) % s_track_count);
    return true;
}

const char *music_service_get_title(uint16_t index)
{
    if (index >= s_track_count)
    {
        return "未找到音乐";
    }

    return s_tracks[index].title;
}

const char *music_service_get_selected_title(void)
{
    return music_service_get_title(s_selected_index);
}

const char *music_service_get_selected_path(void)
{
    if ((s_track_count == 0) || (s_selected_index >= s_track_count))
    {
        return "";
    }

    return s_tracks[s_selected_index].path;
}

bool music_service_play_selected(void)
{
    const char *path;

    if (s_track_count == 0)
    {
        return false;
    }

    if (!audio_acquire(AUDIO_OWNER_MUSIC, AUDIO_REQ_NONBLOCKING))
    {
        rt_kprintf("music: audio acquire failed\n");
        return false;
    }

    music_service_close_player_only();

    path = music_service_get_selected_path();
    s_player = mp3ctrl_open(AUDIO_TYPE_LOCAL_MUSIC, path, RT_NULL, RT_NULL);
    if (s_player == RT_NULL)
    {
        rt_kprintf("music: open failed path=%s\n", path);
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    mp3ctrl_ioctl(s_player, 0, MUSIC_SERVICE_VOLUME);
    if (mp3ctrl_play(s_player) != 0)
    {
        rt_kprintf("music: play failed path=%s\n", path);
        music_service_close_player_only();
        audio_release(AUDIO_OWNER_MUSIC);
        return false;
    }

    s_is_playing = true;
    rt_kprintf("music: playing %s\n", path);
    return true;
}

bool music_service_toggle_playback(void)
{
    if (s_track_count == 0)
    {
        return false;
    }

    if (s_player == RT_NULL)
    {
        return music_service_play_selected();
    }

    if (s_is_playing)
    {
        if (mp3ctrl_pause(s_player) != 0)
        {
            return false;
        }
        s_is_playing = false;
        return true;
    }

    if (mp3ctrl_resume(s_player) != 0)
    {
        return false;
    }

    s_is_playing = true;
    return true;
}

void music_service_stop(void)
{
    music_service_close_player_only();
    if (audio_get_current_owner() == AUDIO_OWNER_MUSIC)
    {
        audio_release(AUDIO_OWNER_MUSIC);
    }
}

bool music_service_is_playing(void)
{
    return s_is_playing;
}
