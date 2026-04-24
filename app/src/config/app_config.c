#include "app_config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_CONFIG_SERIALIZED_MAX      4096U
#define APP_CONFIG_STORAGE_PATH_MAX     256U

static struct rt_mutex s_mutex;
static volatile rt_uint8_t s_mutex_ready = 0U;
static app_config_t s_config;
static bool s_config_ready = false;
static bool s_loaded_from_file = false;
static bool s_dirty = false;
static char s_storage_path[APP_CONFIG_STORAGE_PATH_MAX];
static char s_load_buffer[APP_CONFIG_SERIALIZED_MAX];
static char s_save_buffer[APP_CONFIG_SERIALIZED_MAX];
static char s_storage_temp_path[APP_CONFIG_STORAGE_PATH_MAX];

rt_err_t app_config_storage_load(const char *preferred_path,
                                 char *out_path,
                                 size_t out_path_size,
                                 char *data,
                                 size_t data_size,
                                 size_t *out_len,
                                 bool *found);
rt_err_t app_config_storage_save(const char *preferred_path,
                                 const char *data,
                                 size_t len,
                                 char *saved_path,
                                 size_t saved_path_size);
void app_config_storage_cleanup_legacy_files(void);

static rt_err_t app_config_ensure_mutex(void)
{
    if (s_mutex_ready != 0U)
    {
        return RT_EOK;
    }

    rt_enter_critical();
    if (s_mutex_ready == 0U)
    {
        if (rt_mutex_init(&s_mutex, "cfg", RT_IPC_FLAG_PRIO) == RT_EOK)
        {
            s_mutex_ready = 1U;
        }
        else
        {
            rt_exit_critical();
            return -RT_ERROR;
        }
    }
    rt_exit_critical();

    return RT_EOK;
}

static rt_err_t app_config_lock(void)
{
    rt_err_t result = app_config_ensure_mutex();
    if (result != RT_EOK)
    {
        return result;
    }

    return rt_mutex_take(&s_mutex, RT_WAITING_FOREVER);
}

static void app_config_unlock(void)
{
    if (s_mutex_ready != 0U)
    {
        (void)rt_mutex_release(&s_mutex);
    }
}

static void app_config_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0U)
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

static int app_config_ascii_casecmp(const char *lhs, const char *rhs)
{
    unsigned char cl;
    unsigned char cr;

    if (lhs == NULL || rhs == NULL)
    {
        return (lhs == rhs) ? 0 : 1;
    }

    while (*lhs != '\0' && *rhs != '\0')
    {
        cl = (unsigned char)tolower((unsigned char)*lhs);
        cr = (unsigned char)tolower((unsigned char)*rhs);
        if (cl != cr)
        {
            return (int)cl - (int)cr;
        }
        ++lhs;
        ++rhs;
    }

    return (int)(unsigned char)tolower((unsigned char)*lhs) - (int)(unsigned char)tolower((unsigned char)*rhs);
}

static void app_config_trim(char *text)
{
    char *start;
    char *end;
    size_t len;

    if (text == NULL)
    {
        return;
    }

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start))
    {
        ++start;
    }

    if (start != text)
    {
        len = strlen(start);
        memmove(text, start, len + 1U);
    }

    len = strlen(text);
    if (len == 0U)
    {
        return;
    }

    end = text + len - 1U;
    while (end >= text && isspace((unsigned char)*end))
    {
        *end = '\0';
        if (end == text)
        {
            break;
        }
        --end;
    }
}

static bool app_config_parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || out == NULL)
    {
        return false;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX)
    {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

static bool app_config_parse_bool(const char *text, uint8_t *out)
{
    uint32_t value;

    if (text == NULL || out == NULL)
    {
        return false;
    }

    if (app_config_ascii_casecmp(text, "1") == 0 || app_config_ascii_casecmp(text, "true") == 0 ||
        app_config_ascii_casecmp(text, "yes") == 0 || app_config_ascii_casecmp(text, "on") == 0)
    {
        *out = 1U;
        return true;
    }

    if (app_config_ascii_casecmp(text, "0") == 0 || app_config_ascii_casecmp(text, "false") == 0 ||
        app_config_ascii_casecmp(text, "no") == 0 || app_config_ascii_casecmp(text, "off") == 0)
    {
        *out = 0U;
        return true;
    }

    if (!app_config_parse_u32(text, &value))
    {
        return false;
    }

    *out = (value != 0U) ? 1U : 0U;
    return true;
}

static app_config_network_mode_t app_config_parse_network_mode(const char *text, bool *ok)
{
    if (text == NULL)
    {
        if (ok != NULL)
        {
            *ok = false;
        }
        return APP_CONFIG_NETWORK_MODE_4G;
    }

    if (app_config_ascii_casecmp(text, "4g") == 0 || app_config_ascii_casecmp(text, "lte") == 0)
    {
        if (ok != NULL)
        {
            *ok = true;
        }
        return APP_CONFIG_NETWORK_MODE_4G;
    }

    if (app_config_ascii_casecmp(text, "bt") == 0 || app_config_ascii_casecmp(text, "bluetooth") == 0)
    {
        if (ok != NULL)
        {
            *ok = true;
        }
        return APP_CONFIG_NETWORK_MODE_BT;
    }

    if (ok != NULL)
    {
        *ok = false;
    }
    return APP_CONFIG_NETWORK_MODE_4G;
}

static app_config_language_t app_config_parse_language(const char *text, bool *ok)
{
    if (text == NULL)
    {
        if (ok != NULL)
        {
            *ok = false;
        }
        return APP_CONFIG_LANGUAGE_ZH_CN;
    }

    if (app_config_ascii_casecmp(text, "zh-CN") == 0 || app_config_ascii_casecmp(text, "zh_CN") == 0 ||
        app_config_ascii_casecmp(text, "zh") == 0)
    {
        if (ok != NULL)
        {
            *ok = true;
        }
        return APP_CONFIG_LANGUAGE_ZH_CN;
    }

    if (app_config_ascii_casecmp(text, "en-US") == 0 || app_config_ascii_casecmp(text, "en_US") == 0 ||
        app_config_ascii_casecmp(text, "en") == 0)
    {
        if (ok != NULL)
        {
            *ok = true;
        }
        return APP_CONFIG_LANGUAGE_EN_US;
    }

    if (ok != NULL)
    {
        *ok = false;
    }
    return APP_CONFIG_LANGUAGE_ZH_CN;
}

static void app_config_set_defaults(app_config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->version = APP_CONFIG_VERSION;
    cfg->boot.network_mode = APP_CONFIG_NETWORK_MODE_4G;
    cfg->boot.auto_connect = 1U;
    cfg->display.brightness = 50U;
    cfg->display.standby_timeout_sec = 60U;
    cfg->ui.language = APP_CONFIG_LANGUAGE_ZH_CN;
    cfg->reading.use_system_font = 1U;
    cfg->reading.font_path[0] = '\0';
    cfg->reading.font_size = 22U;
    cfg->reading.line_space = 2U;
    cfg->audio.music_volume = 12U;
    cfg->audio.ai_volume = 8U;
    cfg->wallpaper.path[0] = '\0';
    cfg->ai.auto_resume = 1U;
    cfg->weather.auto_refresh = 1U;
}

static void app_config_sanitize(app_config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    cfg->version = APP_CONFIG_VERSION;

    if (cfg->boot.network_mode != APP_CONFIG_NETWORK_MODE_4G &&
        cfg->boot.network_mode != APP_CONFIG_NETWORK_MODE_BT)
    {
        cfg->boot.network_mode = APP_CONFIG_NETWORK_MODE_4G;
    }
    cfg->boot.auto_connect = cfg->boot.auto_connect ? 1U : 0U;

    if (cfg->display.brightness > 100U)
    {
        cfg->display.brightness = 100U;
    }
    if (cfg->display.standby_timeout_sec < 10U)
    {
        cfg->display.standby_timeout_sec = 10U;
    }
    if (cfg->display.standby_timeout_sec > 3600U)
    {
        cfg->display.standby_timeout_sec = 3600U;
    }

    if (cfg->ui.language != APP_CONFIG_LANGUAGE_ZH_CN && cfg->ui.language != APP_CONFIG_LANGUAGE_EN_US)
    {
        cfg->ui.language = APP_CONFIG_LANGUAGE_ZH_CN;
    }

    cfg->reading.use_system_font = cfg->reading.use_system_font ? 1U : 0U;
    if (cfg->reading.font_size < 18U)
    {
        cfg->reading.font_size = 18U;
    }
    if (cfg->reading.font_size > 30U)
    {
        cfg->reading.font_size = 30U;
    }
    if (cfg->reading.line_space > 12U)
    {
        cfg->reading.line_space = 12U;
    }
    cfg->ai.auto_resume = cfg->ai.auto_resume ? 1U : 0U;
    cfg->weather.auto_refresh = cfg->weather.auto_refresh ? 1U : 0U;

    cfg->wallpaper.path[sizeof(cfg->wallpaper.path) - 1U] = '\0';
    cfg->reading.font_path[sizeof(cfg->reading.font_path) - 1U] = '\0';
}

static void app_config_apply_kv(app_config_t *cfg, const char *key, const char *value)
{
    uint32_t number;
    uint8_t flag;
    bool ok = false;

    if (cfg == NULL || key == NULL || value == NULL)
    {
        return;
    }

    if (strcmp(key, "version") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->version = number;
        }
        return;
    }

    if (strcmp(key, "boot.network_mode") == 0)
    {
        app_config_network_mode_t mode = app_config_parse_network_mode(value, &ok);
        if (ok)
        {
            cfg->boot.network_mode = mode;
        }
        return;
    }

    if (strcmp(key, "boot.auto_connect") == 0)
    {
        if (app_config_parse_bool(value, &flag))
        {
            cfg->boot.auto_connect = flag;
        }
        return;
    }

    if (strcmp(key, "display.brightness") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->display.brightness = number;
        }
        return;
    }

    if (strcmp(key, "display.standby_timeout_sec") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->display.standby_timeout_sec = number;
        }
        return;
    }

    if (strcmp(key, "ui.language") == 0)
    {
        app_config_language_t language = app_config_parse_language(value, &ok);
        if (ok)
        {
            cfg->ui.language = language;
        }
        return;
    }

    if (strcmp(key, "reading.use_system_font") == 0)
    {
        if (app_config_parse_bool(value, &flag))
        {
            cfg->reading.use_system_font = flag;
        }
        return;
    }

    if (strcmp(key, "reading.font_path") == 0)
    {
        app_config_copy_string(cfg->reading.font_path, sizeof(cfg->reading.font_path), value);
        return;
    }

    if (strcmp(key, "reading.font_size") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->reading.font_size = number;
        }
        return;
    }

    if (strcmp(key, "reading.line_space") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->reading.line_space = number;
        }
        return;
    }

    if (strcmp(key, "audio.music_volume") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->audio.music_volume = number;
        }
        return;
    }

    if (strcmp(key, "audio.ai_volume") == 0)
    {
        if (app_config_parse_u32(value, &number))
        {
            cfg->audio.ai_volume = number;
        }
        return;
    }

    if (strcmp(key, "wallpaper.path") == 0)
    {
        app_config_copy_string(cfg->wallpaper.path, sizeof(cfg->wallpaper.path), value);
        return;
    }

    if (strcmp(key, "ai.auto_resume") == 0)
    {
        if (app_config_parse_bool(value, &flag))
        {
            cfg->ai.auto_resume = flag;
        }
        return;
    }

    if (strcmp(key, "weather.auto_refresh") == 0)
    {
        if (app_config_parse_bool(value, &flag))
        {
            cfg->weather.auto_refresh = flag;
        }
        return;
    }
}

static void app_config_parse_text(app_config_t *cfg, char *text)
{
    char *cursor;

    if (cfg == NULL || text == NULL)
    {
        return;
    }

    cursor = text;
    while (cursor != NULL && *cursor != '\0')
    {
        char *line = cursor;
        char *next = strchr(cursor, '\n');
        char *equal;

        if (next != NULL)
        {
            *next = '\0';
            cursor = next + 1U;
        }
        else
        {
            cursor = NULL;
        }

        if (line[0] == '\r')
        {
            continue;
        }

        app_config_trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
        {
            continue;
        }

        equal = strchr(line, '=');
        if (equal == NULL)
        {
            continue;
        }

        *equal = '\0';
        ++equal;
        app_config_trim(line);
        app_config_trim(equal);
        if (line[0] == '\0')
        {
            continue;
        }

        app_config_apply_kv(cfg, line, equal);
    }
}

static void app_config_assign_locked(const app_config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    s_config = *cfg;
    app_config_sanitize(&s_config);
    s_config_ready = true;
}

static void app_config_touch_dirty_locked(void)
{
    s_dirty = true;
}

rt_err_t app_config_init(void)
{
    rt_err_t result;

    result = app_config_ensure_mutex();
    if (result != RT_EOK)
    {
        return result;
    }

    result = app_config_lock();
    if (result != RT_EOK)
    {
        return result;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
        s_loaded_from_file = false;
        s_dirty = false;
        s_storage_path[0] = '\0';
    }

    app_config_unlock();
    return RT_EOK;
}

rt_err_t app_config_load(void)
{
    app_config_t loaded;
    app_config_t defaults;
    size_t length = 0U;
    bool found = false;
    rt_err_t result;

    result = app_config_storage_load((s_storage_path[0] != '\0') ? s_storage_path : NULL,
                                     s_storage_temp_path,
                                     sizeof(s_storage_temp_path),
                                     s_load_buffer,
                                     sizeof(s_load_buffer),
                                     &length,
                                     &found);
    if (result != RT_EOK)
    {
        return result;
    }

    app_config_set_defaults(&defaults);
    loaded = defaults;
    if (found)
    {
        app_config_parse_text(&loaded, s_load_buffer);
    }
    app_config_sanitize(&loaded);

    result = app_config_lock();
    if (result != RT_EOK)
    {
        return result;
    }

    app_config_assign_locked(&loaded);
    s_loaded_from_file = found;
    s_dirty = false;
    if (found)
    {
        app_config_copy_string(s_storage_path, sizeof(s_storage_path), s_storage_temp_path);
    }
    else
    {
        s_storage_path[0] = '\0';
    }

    app_config_unlock();
    app_config_storage_cleanup_legacy_files();
    return RT_EOK;
}

static rt_err_t app_config_serialize_locked(char *buffer, size_t buffer_size)
{
    int written;
    size_t used = 0U;

    if (buffer == NULL || buffer_size == 0U)
    {
        return -RT_EINVAL;
    }

    written = snprintf(buffer + used, buffer_size - used, "version=%u\n", (unsigned)s_config.version);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "boot.network_mode=%s\n",
                       (s_config.boot.network_mode == APP_CONFIG_NETWORK_MODE_BT) ? "bt" : "4g");
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "boot.auto_connect=%u\n",
                       (unsigned)s_config.boot.auto_connect);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "display.brightness=%u\n",
                       (unsigned)s_config.display.brightness);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "display.standby_timeout_sec=%u\n",
                       (unsigned)s_config.display.standby_timeout_sec);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "ui.language=%s\n",
                       (s_config.ui.language == APP_CONFIG_LANGUAGE_EN_US) ? "en-US" : "zh-CN");
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "reading.use_system_font=%u\n",
                       (unsigned)s_config.reading.use_system_font);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "reading.font_path=%s\n", s_config.reading.font_path);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "reading.font_size=%u\n",
                       (unsigned)s_config.reading.font_size);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "reading.line_space=%u\n",
                       (unsigned)s_config.reading.line_space);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "audio.music_volume=%u\n",
                       (unsigned)s_config.audio.music_volume);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "audio.ai_volume=%u\n",
                       (unsigned)s_config.audio.ai_volume);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "wallpaper.path=%s\n", s_config.wallpaper.path);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "ai.auto_resume=%u\n",
                       (unsigned)s_config.ai.auto_resume);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    written = snprintf(buffer + used, buffer_size - used, "weather.auto_refresh=%u\n",
                       (unsigned)s_config.weather.auto_refresh);
    if (written < 0 || (size_t)written >= buffer_size - used)
    {
        return -RT_EFULL;
    }
    used += (size_t)written;

    return RT_EOK;
}

rt_err_t app_config_save(void)
{
    rt_err_t result;

    result = app_config_lock();
    if (result != RT_EOK)
    {
        return result;
    }

    result = app_config_serialize_locked(s_save_buffer, sizeof(s_save_buffer));
    if (result != RT_EOK)
    {
        app_config_unlock();
        return result;
    }

    app_config_unlock();

    result = app_config_storage_save((s_storage_path[0] != '\0') ? s_storage_path : NULL,
                                     s_save_buffer,
                                     strlen(s_save_buffer),
                                     s_storage_temp_path,
                                     sizeof(s_storage_temp_path));
    if (result != RT_EOK)
    {
        return result;
    }

    result = app_config_lock();
    if (result != RT_EOK)
    {
        return result;
    }

    app_config_copy_string(s_storage_path, sizeof(s_storage_path), s_storage_temp_path);
    s_loaded_from_file = true;
    s_dirty = false;
    app_config_unlock();
    app_config_storage_cleanup_legacy_files();
    return RT_EOK;
}

void app_config_reset_to_defaults(void)
{
    rt_err_t result = app_config_lock();
    if (result != RT_EOK)
    {
        return;
    }

    app_config_set_defaults(&s_config);
    s_config_ready = true;
    s_loaded_from_file = false;
    s_dirty = true;

    app_config_unlock();
}

const app_config_t *app_config_get(void)
{
    if (app_config_ensure_mutex() != RT_EOK)
    {
        return &s_config;
    }

    if (!s_config_ready)
    {
        app_config_reset_to_defaults();
    }

    return &s_config;
}

void app_config_get_snapshot(app_config_t *out)
{
    if (out == NULL)
    {
        return;
    }

    if (app_config_lock() != RT_EOK)
    {
        memset(out, 0, sizeof(*out));
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    *out = s_config;
    app_config_unlock();
}

bool app_config_is_loaded_from_file(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return false;
    }

    value = s_loaded_from_file;
    app_config_unlock();
    return value;
}

bool app_config_is_dirty(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return false;
    }

    value = s_dirty;
    app_config_unlock();
    return value;
}

app_config_network_mode_t app_config_get_boot_network_mode(void)
{
    app_config_network_mode_t value;

    if (app_config_lock() != RT_EOK)
    {
        return APP_CONFIG_NETWORK_MODE_4G;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.boot.network_mode;
    app_config_unlock();
    return value;
}

void app_config_set_boot_network_mode(app_config_network_mode_t mode)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.boot.network_mode = mode;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

bool app_config_get_boot_auto_connect(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return true;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.boot.auto_connect != 0U;
    app_config_unlock();
    return value;
}

void app_config_set_boot_auto_connect(bool enabled)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.boot.auto_connect = enabled ? 1U : 0U;
    app_config_touch_dirty_locked();
    app_config_unlock();
}

uint32_t app_config_get_display_brightness(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 50U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.display.brightness;
    app_config_unlock();
    return value;
}

void app_config_set_display_brightness(uint32_t brightness)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.display.brightness = brightness;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

uint32_t app_config_get_display_standby_timeout_sec(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 60U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.display.standby_timeout_sec;
    app_config_unlock();
    return value;
}

void app_config_set_display_standby_timeout_sec(uint32_t timeout_sec)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.display.standby_timeout_sec = timeout_sec;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

app_config_language_t app_config_get_ui_language(void)
{
    app_config_language_t value;

    if (app_config_lock() != RT_EOK)
    {
        return APP_CONFIG_LANGUAGE_ZH_CN;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.ui.language;
    app_config_unlock();
    return value;
}

void app_config_set_ui_language(app_config_language_t language)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.ui.language = language;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

bool app_config_get_reading_use_system_font(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return true;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.reading.use_system_font != 0U;
    app_config_unlock();
    return value;
}

void app_config_set_reading_use_system_font(bool enabled)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.reading.use_system_font = enabled ? 1U : 0U;
    app_config_touch_dirty_locked();
    app_config_unlock();
}

void app_config_get_reading_font_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return;
    }

    if (app_config_lock() != RT_EOK)
    {
        out[0] = '\0';
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    app_config_copy_string(out, out_size, s_config.reading.font_path);
    app_config_unlock();
}

rt_err_t app_config_set_reading_font_path(const char *path)
{
    if (app_config_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    app_config_copy_string(s_config.reading.font_path, sizeof(s_config.reading.font_path), path);
    app_config_touch_dirty_locked();
    app_config_unlock();
    return RT_EOK;
}

uint32_t app_config_get_reading_font_size(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 22U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.reading.font_size;
    app_config_unlock();
    return value;
}

void app_config_set_reading_font_size(uint32_t font_size)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.reading.font_size = font_size;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

uint32_t app_config_get_reading_line_space(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 2U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.reading.line_space;
    app_config_unlock();
    return value;
}

void app_config_set_reading_line_space(uint32_t line_space)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.reading.line_space = line_space;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

uint32_t app_config_get_audio_music_volume(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 12U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.audio.music_volume;
    app_config_unlock();
    return value;
}

void app_config_set_audio_music_volume(uint32_t volume)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.audio.music_volume = volume;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

uint32_t app_config_get_audio_ai_volume(void)
{
    uint32_t value;

    if (app_config_lock() != RT_EOK)
    {
        return 8U;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = s_config.audio.ai_volume;
    app_config_unlock();
    return value;
}

void app_config_set_audio_ai_volume(uint32_t volume)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.audio.ai_volume = volume;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

void app_config_get_wallpaper_path(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return;
    }

    if (app_config_lock() != RT_EOK)
    {
        out[0] = '\0';
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    app_config_copy_string(out, out_size, s_config.wallpaper.path);
    app_config_unlock();
}

rt_err_t app_config_set_wallpaper_path(const char *path)
{
    if (app_config_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    app_config_copy_string(s_config.wallpaper.path, sizeof(s_config.wallpaper.path), path);
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
    return RT_EOK;
}

bool app_config_get_ai_auto_resume(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return true;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = (s_config.ai.auto_resume != 0U);
    app_config_unlock();
    return value;
}

void app_config_set_ai_auto_resume(bool enabled)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.ai.auto_resume = enabled ? 1U : 0U;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}

bool app_config_get_weather_auto_refresh(void)
{
    bool value;

    if (app_config_lock() != RT_EOK)
    {
        return true;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    value = (s_config.weather.auto_refresh != 0U);
    app_config_unlock();
    return value;
}

void app_config_set_weather_auto_refresh(bool enabled)
{
    if (app_config_lock() != RT_EOK)
    {
        return;
    }

    if (!s_config_ready)
    {
        app_config_set_defaults(&s_config);
        s_config_ready = true;
    }
    s_config.weather.auto_refresh = enabled ? 1U : 0U;
    app_config_sanitize(&s_config);
    app_config_touch_dirty_locked();
    app_config_unlock();
}
