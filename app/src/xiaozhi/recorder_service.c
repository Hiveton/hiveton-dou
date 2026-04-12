#include "recorder_service.h"

#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "audio_server.h"
#include "dfs_fs.h"
#include "dfs_posix.h"
#include "rtdevice.h"
#include "rtthread.h"

#define DBG_TAG "recorder"
#define DBG_LVL DBG_INFO
#include "log.h"

#undef printf
#undef LOG_I

#define printf rt_kprintf
#define LOG_I  rt_kprintf

#define RECORDER_SAMPLE_RATE          16000U
#define RECORDER_CHANNELS             1U
#define RECORDER_BITS_PER_SAMPLE      16U
#define RECORDER_BYTES_PER_SAMPLE     (RECORDER_BITS_PER_SAMPLE / 8U)
#define RECORDER_HEADER_BYTES         44U
#define RECORDER_MAX_FILE_NAME        96U
#define RECORDER_MAX_PATH             192U
#define RECORDER_PLAY_CHUNK_BYTES     960U
#define RECORDER_THREAD_STACK_SIZE    4096U
#define RECORDER_GAIN_SHIFT           3U
#define RECORDER_GAIN_CHUNK_SAMPLES   256U

#define RECORDER_EVT_PLAY_REQUEST     (1U << 0)
#define RECORDER_EVT_EXIT             (1U << 1)

typedef struct
{
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt_[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} recorder_wav_header_t;

typedef struct
{
    audio_client_t record_client;
    int record_fd;
    rt_mutex_t lock;
    rt_event_t event;
    bool service_inited;
    bool record_active;
    bool record_stop_requested;
    bool playback_active;
    bool playback_stop_requested;
    char record_dir[RECORDER_MAX_PATH];
    char record_file_path[RECORDER_MAX_PATH];
    char recording_file_name[RECORDER_MAX_FILE_NAME];
    char playing_path[RECORDER_MAX_PATH];
    char playing_name[RECORDER_MAX_FILE_NAME];
    char status_text[96];
    uint32_t record_start_tick;
    uint32_t record_bytes;
} recorder_service_t;

static recorder_service_t s_recorder;
static struct rt_thread s_playback_thread;
static uint32_t s_playback_thread_stack[RECORDER_THREAD_STACK_SIZE / sizeof(uint32_t)];
static const char *const s_record_device_candidates[] = {"sd0", "sd1", "sd2", "sdio0"};

static void recorder_service_playback_thread_entry(void *parameter);

static void recorder_service_set_status_locked(const char *text)
{
    if (text == NULL)
    {
        s_recorder.status_text[0] = '\0';
        return;
    }

    rt_snprintf(s_recorder.status_text, sizeof(s_recorder.status_text), "%s", text);
}

static void recorder_service_set_status(const char *text)
{
    if (!s_recorder.service_inited)
    {
        return;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) == RT_EOK)
    {
        recorder_service_set_status_locked(text);
        rt_mutex_release(s_recorder.lock);
    }
}

static const char *recorder_service_resolve_mount_root(void)
{
    size_t i;
    const char *mounted;

    for (i = 0U; i < sizeof(s_record_device_candidates) / sizeof(s_record_device_candidates[0]); ++i)
    {
        rt_device_t device = rt_device_find(s_record_device_candidates[i]);

        if (device == RT_NULL)
        {
            continue;
        }

        mounted = dfs_filesystem_get_mounted_path(device);
        if (mounted != NULL && mounted[0] != '\0')
        {
            return mounted;
        }
    }

    return "/";
}

bool recorder_service_storage_ready(void)
{
    size_t i;

    for (i = 0U; i < sizeof(s_record_device_candidates) / sizeof(s_record_device_candidates[0]); ++i)
    {
        rt_device_t device = rt_device_find(s_record_device_candidates[i]);
        const char *mounted;

        if (device == RT_NULL)
        {
            continue;
        }

        mounted = dfs_filesystem_get_mounted_path(device);
        if (mounted != RT_NULL && mounted[0] != '\0')
        {
            return true;
        }
    }

    return false;
}

static bool recorder_service_build_record_dir(char *buffer, size_t buffer_size)
{
    const char *mount_root = recorder_service_resolve_mount_root();

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (!recorder_service_storage_ready())
    {
        buffer[0] = '\0';
        return false;
    }

    if (mount_root == NULL || mount_root[0] == '\0' || strcmp(mount_root, "/") == 0)
    {
        rt_snprintf(buffer, buffer_size, "/record");
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s/record", mount_root);
    }

    mkdir(buffer, 0);
    return true;
}

static bool recorder_service_join_path(char *buffer, size_t buffer_size, const char *dir, const char *name)
{
    size_t dir_len;

    if (buffer == NULL || buffer_size == 0U || dir == NULL || name == NULL)
    {
        return false;
    }

    dir_len = strlen(dir);
    if (dir_len == 0U)
    {
        return false;
    }

    if (strcmp(dir, "/") == 0)
    {
        rt_snprintf(buffer, buffer_size, "/%s", name);
    }
    else if (dir[dir_len - 1] == '/')
    {
        rt_snprintf(buffer, buffer_size, "%s%s", dir, name);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s/%s", dir, name);
    }

    return true;
}

static void recorder_service_build_timestamp_name(char *buffer, size_t buffer_size)
{
    time_t now = time(RT_NULL);
    struct tm tm_now;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (now < 1700000000)
    {
        rt_snprintf(buffer, buffer_size, "rec_%lu.wav", (unsigned long)rt_tick_get());
        return;
    }

    localtime_r(&now, &tm_now);
    rt_snprintf(buffer,
                buffer_size,
                "rec_%04d%02d%02d_%02d%02d%02d.wav",
                tm_now.tm_year + 1900,
                tm_now.tm_mon + 1,
                tm_now.tm_mday,
                tm_now.tm_hour,
                tm_now.tm_min,
                tm_now.tm_sec);
}

static bool recorder_service_write_all(int fd, const void *buffer, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)buffer;
    size_t total = 0U;

    if (fd < 0 || buffer == NULL)
    {
        return false;
    }

    while (total < size)
    {
        int written = write(fd, cursor + total, size - total);
        if (written <= 0)
        {
            return false;
        }
        total += (size_t)written;
    }

    return true;
}

static void recorder_service_write_wav_header(int fd, uint32_t data_size)
{
    recorder_wav_header_t header;

    if (fd < 0)
    {
        return;
    }

    memcpy(header.riff, "RIFF", 4);
    header.file_size = 36U + data_size;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt_, "fmt ", 4);
    header.fmt_size = 16U;
    header.audio_format = 1U;
    header.channels = RECORDER_CHANNELS;
    header.sample_rate = RECORDER_SAMPLE_RATE;
    header.byte_rate = RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * RECORDER_BYTES_PER_SAMPLE;
    header.block_align = RECORDER_CHANNELS * RECORDER_BYTES_PER_SAMPLE;
    header.bits_per_sample = RECORDER_BITS_PER_SAMPLE;
    memcpy(header.data, "data", 4);
    header.data_size = data_size;

    lseek(fd, 0, SEEK_SET);
    recorder_service_write_all(fd, &header, sizeof(header));
}

static void recorder_service_fix_wav_header(int fd, uint32_t data_size)
{
    recorder_service_write_wav_header(fd, data_size);
}

static int16_t recorder_service_apply_gain_sample(int16_t sample)
{
    int32_t scaled = ((int32_t)sample) << RECORDER_GAIN_SHIFT;

    if (scaled > 32767)
    {
        return 32767;
    }
    if (scaled < -32768)
    {
        return -32768;
    }

    return (int16_t)scaled;
}

static uint32_t recorder_service_write_gain_pcm(int fd, const uint8_t *data, uint32_t data_len)
{
    int16_t temp[RECORDER_GAIN_CHUNK_SAMPLES];
    uint32_t total_written = 0U;
    uint32_t offset = 0U;

    if (fd < 0 || data == NULL || data_len == 0U)
    {
        return 0U;
    }

    while ((offset + 1U) < data_len)
    {
        uint32_t chunk_bytes = data_len - offset;
        uint32_t sample_count;
        uint32_t i;

        if (chunk_bytes > (RECORDER_GAIN_CHUNK_SAMPLES * sizeof(int16_t)))
        {
            chunk_bytes = RECORDER_GAIN_CHUNK_SAMPLES * sizeof(int16_t);
        }

        sample_count = chunk_bytes / sizeof(int16_t);
        for (i = 0U; i < sample_count; ++i)
        {
            int16_t sample;

            memcpy(&sample, data + offset + (i * sizeof(int16_t)), sizeof(sample));
            temp[i] = recorder_service_apply_gain_sample(sample);
        }

        if (recorder_service_write_all(fd, temp, sample_count * sizeof(int16_t)))
        {
            uint32_t written = sample_count * sizeof(int16_t);
            total_written += written;
            offset += written;
        }
        else
        {
            break;
        }
    }

    if (offset < data_len)
    {
        uint32_t remain = data_len - offset;

        if (recorder_service_write_all(fd, data + offset, remain))
        {
            total_written += remain;
        }
    }

    return total_written;
}

static bool recorder_service_ensure_inited(void)
{
    if (s_recorder.service_inited)
    {
        return true;
    }

    memset(&s_recorder, 0, sizeof(s_recorder));
    s_recorder.record_fd = -1;
    s_recorder.lock = rt_mutex_create("rsvc", RT_IPC_FLAG_PRIO);
    if (s_recorder.lock == RT_NULL)
    {
        return false;
    }
    s_recorder.event = rt_event_create("rsv", RT_IPC_FLAG_FIFO);
    if (s_recorder.event == RT_NULL)
    {
        rt_mutex_delete(s_recorder.lock);
        s_recorder.lock = RT_NULL;
        return false;
    }

    if (rt_thread_init(&s_playback_thread,
                       "rplay",
                       recorder_service_playback_thread_entry,
                       RT_NULL,
                       &s_playback_thread_stack[0],
                       sizeof(s_playback_thread_stack),
                       RT_THREAD_PRIORITY_LOW,
                       RT_THREAD_TICK_DEFAULT) != RT_EOK)
    {
        rt_event_delete(s_recorder.event);
        s_recorder.event = RT_NULL;
        rt_mutex_delete(s_recorder.lock);
        s_recorder.lock = RT_NULL;
        return false;
    }

    rt_thread_startup(&s_playback_thread);
    s_recorder.service_inited = true;
    recorder_service_set_status_locked("待机");
    return true;
}

static int recorder_service_record_callback(audio_server_callback_cmt_t cmd, void *callback_userdata, uint32_t reserved)
{
    int fd;
    audio_server_coming_data_t *data;

    (void)callback_userdata;

    if (cmd != as_callback_cmd_data_coming)
    {
        return 0;
    }

    data = (audio_server_coming_data_t *)reserved;
    if (data == NULL || data->data == NULL || data->data_len == 0U)
    {
        return 0;
    }

    if (!s_recorder.record_active || s_recorder.record_stop_requested)
    {
        return 0;
    }

    if (rt_mutex_take(s_recorder.lock, 0) != RT_EOK)
    {
        return 0;
    }

    fd = s_recorder.record_fd;
    if (fd >= 0)
    {
        uint32_t written = recorder_service_write_gain_pcm(fd, data->data, data->data_len);
        if (written > 0U)
        {
            s_recorder.record_bytes += written;
        }
    }
    rt_mutex_release(s_recorder.lock);

    return 0;
}

static void recorder_service_wait_playback_stop(uint32_t timeout_ms)
{
    uint32_t elapsed = 0U;

    while (s_recorder.playback_active && elapsed < timeout_ms)
    {
        rt_thread_mdelay(10);
        elapsed += 10U;
    }
}

static bool recorder_service_prepare_record_dir(void)
{
    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) != RT_EOK)
    {
        return false;
    }

    if (!recorder_service_build_record_dir(s_recorder.record_dir, sizeof(s_recorder.record_dir)))
    {
        s_recorder.record_dir[0] = '\0';
    }
    rt_mutex_release(s_recorder.lock);
    return s_recorder.record_dir[0] != '\0';
}

static bool recorder_service_open_record_file(void)
{
    char file_name[RECORDER_MAX_FILE_NAME];
    char file_path[RECORDER_MAX_PATH];
    int fd;

    if (!recorder_service_prepare_record_dir())
    {
        recorder_service_set_status("未找到SD卡");
        return false;
    }

    recorder_service_build_timestamp_name(file_name, sizeof(file_name));
    if (!recorder_service_join_path(file_path, sizeof(file_path), s_recorder.record_dir, file_name))
    {
        recorder_service_set_status("文件路径失败");
        return false;
    }

    fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        recorder_service_set_status("打开录音文件失败");
        return false;
    }

    recorder_service_write_wav_header(fd, 0U);

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) != RT_EOK)
    {
        close(fd);
        return false;
    }

    rt_snprintf(s_recorder.record_file_path, sizeof(s_recorder.record_file_path), "%s", file_path);
    rt_snprintf(s_recorder.recording_file_name, sizeof(s_recorder.recording_file_name), "%s", file_name);
    s_recorder.record_fd = fd;
    s_recorder.record_bytes = 0U;
    s_recorder.record_start_tick = rt_tick_get();
    s_recorder.record_stop_requested = false;
    s_recorder.record_active = true;
    recorder_service_set_status_locked("录音中");
    rt_mutex_release(s_recorder.lock);
    return true;
}

static void recorder_service_close_record_file(void)
{
    int fd = -1;
    uint32_t data_size = 0U;

    if (!s_recorder.service_inited)
    {
        return;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) != RT_EOK)
    {
        return;
    }

    fd = s_recorder.record_fd;
    s_recorder.record_fd = -1;
    data_size = s_recorder.record_bytes;
    s_recorder.record_active = false;
    s_recorder.record_start_tick = 0U;
    s_recorder.record_bytes = 0U;
    recorder_service_set_status_locked("保存中");
    rt_mutex_release(s_recorder.lock);

    if (fd >= 0)
    {
        recorder_service_fix_wav_header(fd, data_size);
        close(fd);
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) == RT_EOK)
    {
        recorder_service_set_status_locked("已保存");
        rt_mutex_release(s_recorder.lock);
    }
}

static void recorder_service_playback_cleanup(audio_client_t client)
{
    if (client != RT_NULL)
    {
        audio_close(client);
    }
}

static void recorder_service_playback_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        rt_uint32_t evt = 0U;

        if (s_recorder.event == RT_NULL)
        {
            rt_thread_mdelay(100);
            continue;
        }

        rt_event_recv(s_recorder.event,
                      RECORDER_EVT_PLAY_REQUEST | RECORDER_EVT_EXIT,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER,
                      &evt);

        if (evt & RECORDER_EVT_EXIT)
        {
            break;
        }

        if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) != RT_EOK)
        {
            continue;
        }

        if (s_recorder.playing_path[0] == '\0')
        {
            rt_mutex_release(s_recorder.lock);
            continue;
        }

        s_recorder.playback_stop_requested = false;
        s_recorder.playback_active = true;
        recorder_service_set_status_locked("播放中");
        rt_mutex_release(s_recorder.lock);

        {
            int fd = open(s_recorder.playing_path, O_RDONLY, 0);
            audio_client_t speaker = RT_NULL;
            audio_parameter_t pa = {0};

            if (fd < 0)
            {
                recorder_service_set_status("打开播放文件失败");
                goto playback_done;
            }

            lseek(fd, RECORDER_HEADER_BYTES, SEEK_SET);
            audio_server_select_private_audio_device(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_DEVICE_SPEAKER);
            pa.write_bits_per_sample = 16;
            pa.write_channnel_num = 1;
            pa.write_samplerate = RECORDER_SAMPLE_RATE;
            pa.read_bits_per_sample = 16;
            pa.read_channnel_num = 1;
            pa.read_samplerate = RECORDER_SAMPLE_RATE;
            pa.read_cache_size = 0;
            pa.write_cache_size = 4096;
            speaker = audio_open2(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_TX, &pa, RT_NULL, RT_NULL, AUDIO_DEVICE_SPEAKER);
            if (speaker == RT_NULL)
            {
                close(fd);
                recorder_service_set_status("打开扬声器失败");
                goto playback_done;
            }

            {
                uint8_t buffer[RECORDER_PLAY_CHUNK_BYTES];
                while (!s_recorder.playback_stop_requested)
                {
                    int read_len = read(fd, buffer, sizeof(buffer));
                    if (read_len <= 0)
                    {
                        break;
                    }

                    if (audio_write(speaker, buffer, read_len) <= 0)
                    {
                        break;
                    }
                }
            }

            recorder_service_playback_cleanup(speaker);
            close(fd);
        }

    playback_done:
        if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) == RT_EOK)
        {
            s_recorder.playback_active = false;
            s_recorder.playback_stop_requested = false;
            s_recorder.playing_path[0] = '\0';
            s_recorder.playing_name[0] = '\0';
            if (s_recorder.record_active)
            {
                recorder_service_set_status_locked("录音中");
            }
            else
            {
                recorder_service_set_status_locked("待机");
            }
            rt_mutex_release(s_recorder.lock);
        }
    }
}

void recorder_service_init(void)
{
    if (!recorder_service_ensure_inited())
    {
        return;
    }

    if (s_recorder.record_dir[0] == '\0')
    {
        recorder_service_prepare_record_dir();
    }
}

bool recorder_service_start_record(void)
{
    audio_parameter_t pa = {0};

    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (recorder_service_is_recording())
    {
        return true;
    }

    if (recorder_service_is_playing())
    {
        recorder_service_stop_playback();
        recorder_service_wait_playback_stop(1000U);
    }

    if (!recorder_service_open_record_file())
    {
        return false;
    }

    pa.write_bits_per_sample = 16;
    pa.write_channnel_num = 1;
    pa.write_samplerate = RECORDER_SAMPLE_RATE;
    pa.read_bits_per_sample = 16;
    pa.read_channnel_num = 1;
    pa.read_samplerate = RECORDER_SAMPLE_RATE;
    pa.read_cache_size = 0;
    pa.write_cache_size = 0;
    pa.is_need_3a = 0;
    pa.disable_uplink_agc = 1;
    audio_server_set_public_mic_mute(0);
    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_RECORD, 15);
    audio_server_select_private_audio_device(AUDIO_TYPE_LOCAL_RECORD, AUDIO_DEVICE_SPEAKER);
    s_recorder.record_client = audio_open(AUDIO_TYPE_LOCAL_RECORD,
                                          AUDIO_RX,
                                          &pa,
                                          recorder_service_record_callback,
                                          RT_NULL);
    if (s_recorder.record_client == RT_NULL)
    {
        recorder_service_close_record_file();
        recorder_service_set_status("打开麦克风失败");
        return false;
    }

    recorder_service_set_status("录音中");
    return true;
}

bool recorder_service_stop_record(void)
{
    audio_client_t client = RT_NULL;

    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (!recorder_service_is_recording())
    {
        return true;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(200)) != RT_EOK)
    {
        return false;
    }

    s_recorder.record_stop_requested = true;
    client = s_recorder.record_client;
    s_recorder.record_client = RT_NULL;
    rt_mutex_release(s_recorder.lock);

    if (client != RT_NULL)
    {
        audio_close(client);
    }

    recorder_service_close_record_file();
    recorder_service_set_status("已保存");
    return true;
}

bool recorder_service_is_recording(void)
{
    bool active = false;

    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) == RT_EOK)
    {
        active = s_recorder.record_active;
        rt_mutex_release(s_recorder.lock);
    }

    return active;
}

uint32_t recorder_service_get_record_elapsed_ms(void)
{
    rt_tick_t start_tick = 0U;
    rt_tick_t elapsed_tick = 0U;

    if (!recorder_service_ensure_inited())
    {
        return 0U;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) == RT_EOK)
    {
        start_tick = s_recorder.record_start_tick;
        rt_mutex_release(s_recorder.lock);
    }

    if (start_tick == 0U)
    {
        return 0U;
    }

    elapsed_tick = rt_tick_get() - start_tick;
    return (uint32_t)((elapsed_tick * 1000U) / RT_TICK_PER_SECOND);
}

void recorder_service_get_record_status_text(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (!recorder_service_ensure_inited())
    {
        rt_snprintf(buffer, buffer_size, "未初始化");
        return;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) == RT_EOK)
    {
        rt_snprintf(buffer, buffer_size, "%s", s_recorder.status_text);
        rt_mutex_release(s_recorder.lock);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "忙碌中");
    }
}

const char *recorder_service_get_record_dir(void)
{
    if (!recorder_service_ensure_inited())
    {
        return "/record";
    }

    if (s_recorder.record_dir[0] == '\0')
    {
        recorder_service_prepare_record_dir();
    }

    return s_recorder.record_dir[0] != '\0' ? s_recorder.record_dir : "/record";
}

const char *recorder_service_get_recording_file_name(void)
{
    return s_recorder.recording_file_name[0] != '\0' ? s_recorder.recording_file_name : "";
}

static int recorder_service_file_compare(const void *lhs, const void *rhs)
{
    const recorder_service_file_t *left = (const recorder_service_file_t *)lhs;
    const recorder_service_file_t *right = (const recorder_service_file_t *)rhs;

    if (left->mtime < right->mtime)
    {
        return 1;
    }
    if (left->mtime > right->mtime)
    {
        return -1;
    }
    return strcmp(left->name, right->name);
}

size_t recorder_service_scan_files(recorder_service_file_t *files, size_t max_files)
{
    DIR *dir = RT_NULL;
    struct dirent *entry = RT_NULL;
    size_t count = 0U;
    char dir_path[RECORDER_MAX_PATH];

    if (files == NULL || max_files == 0U)
    {
        return 0U;
    }

    if (!recorder_service_ensure_inited())
    {
        return 0U;
    }

    rt_snprintf(dir_path, sizeof(dir_path), "%s", recorder_service_get_record_dir());
    dir = opendir(dir_path);
    if (dir == RT_NULL)
    {
        recorder_service_set_status("录音目录不可用");
        return 0U;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        char path[RECORDER_MAX_PATH];
        struct stat st;
        const char *dot;

        if (count >= max_files)
        {
            break;
        }

        if (entry->d_name[0] == '.')
        {
            continue;
        }

        dot = strrchr(entry->d_name, '.');
        if (dot == NULL || strcasecmp(dot, ".wav") != 0)
        {
            continue;
        }

        if (!recorder_service_join_path(path, sizeof(path), dir_path, entry->d_name))
        {
            continue;
        }

        if (stat(path, &st) != 0)
        {
            continue;
        }

        rt_snprintf(files[count].name, sizeof(files[count].name), "%s", entry->d_name);
        rt_snprintf(files[count].path, sizeof(files[count].path), "%s", path);
        files[count].size_bytes = (uint32_t)st.st_size;
        files[count].mtime = st.st_mtime;
        if (st.st_size > RECORDER_HEADER_BYTES)
        {
            uint32_t pcm_bytes = (uint32_t)(st.st_size - RECORDER_HEADER_BYTES);
            files[count].duration_ms = (pcm_bytes * 1000U) /
                                       (RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * RECORDER_BYTES_PER_SAMPLE);
        }
        else
        {
            files[count].duration_ms = 0U;
        }
        ++count;
    }

    closedir(dir);
    qsort(files, count, sizeof(files[0]), recorder_service_file_compare);
    return count;
}

bool recorder_service_play_file(const char *path)
{
    const char *name;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (recorder_service_is_recording())
    {
        recorder_service_stop_record();
    }

    if (recorder_service_is_playing())
    {
        recorder_service_stop_playback();
        recorder_service_wait_playback_stop(1000U);
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) != RT_EOK)
    {
        return false;
    }

    rt_snprintf(s_recorder.playing_path, sizeof(s_recorder.playing_path), "%s", path);
    name = strrchr(path, '/');
    name = (name != NULL) ? (name + 1) : path;
    rt_snprintf(s_recorder.playing_name, sizeof(s_recorder.playing_name), "%s", name);
    s_recorder.playback_stop_requested = false;
    rt_mutex_release(s_recorder.lock);

    if (s_recorder.event != RT_NULL)
    {
        rt_event_send(s_recorder.event, RECORDER_EVT_PLAY_REQUEST);
    }

    return true;
}

bool recorder_service_stop_playback(void)
{
    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) == RT_EOK)
    {
        s_recorder.playback_stop_requested = true;
        rt_mutex_release(s_recorder.lock);
    }

    if (s_recorder.event != RT_NULL)
    {
        rt_event_send(s_recorder.event, RECORDER_EVT_PLAY_REQUEST);
    }

    return true;
}

bool recorder_service_is_playing(void)
{
    bool active = false;

    if (!recorder_service_ensure_inited())
    {
        return false;
    }

    if (rt_mutex_take(s_recorder.lock, rt_tick_from_millisecond(100)) == RT_EOK)
    {
        active = s_recorder.playback_active;
        rt_mutex_release(s_recorder.lock);
    }

    return active;
}

const char *recorder_service_get_playing_path(void)
{
    if (!recorder_service_ensure_inited())
    {
        return "";
    }

    return s_recorder.playing_path;
}

const char *recorder_service_get_playing_name(void)
{
    if (!recorder_service_ensure_inited())
    {
        return "";
    }

    return s_recorder.playing_name;
}

void recorder_service_get_play_status_text(char *buffer, size_t buffer_size)
{
    recorder_service_get_record_status_text(buffer, buffer_size);
}
