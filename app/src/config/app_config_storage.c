#include "app_config.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dfs_posix.h"

#define APP_CONFIG_STORAGE_FILE_NAME       "device_config.cfg"
#define APP_CONFIG_STORAGE_TEMP_SUFFIX     ".tmp"
#define APP_CONFIG_STORAGE_PATH_MAX        256U
#define APP_CONFIG_STORAGE_BUFFER_MAX      4096U

static const char *const s_config_dirs[] = {
    "/config",
    "/tf/config",
    "/sd/config",
    "/sd0/config",
    "config",
};

static const char *const s_legacy_files[] = {
    "network_mode.cfg",
    "system.cfg",
};

static int app_config_storage_join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int written;

    if (out == NULL || dir == NULL || name == NULL || out_size == 0U)
    {
        return -1;
    }

    written = snprintf(out, out_size, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= out_size)
    {
        return -1;
    }

    return written;
}

static rt_err_t app_config_storage_read_path(const char *path, char *data, size_t data_size, size_t *out_len)
{
    int fd;
    size_t total = 0U;
    bool truncated = false;

    if (path == NULL || data == NULL || data_size == 0U)
    {
        return -RT_EINVAL;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return -RT_ERROR;
    }

    while (total + 1U < data_size)
    {
        size_t space = data_size - total - 1U;
        ssize_t read_len = read(fd, data + total, space);
        if (read_len < 0)
        {
            (void)close(fd);
            return -RT_EIO;
        }
        if (read_len == 0)
        {
            break;
        }
        total += (size_t)read_len;
        if ((size_t)read_len < space)
        {
            break;
        }
        if (total + 1U >= data_size)
        {
            char probe;
            ssize_t probe_len = read(fd, &probe, 1U);
            if (probe_len < 0)
            {
                (void)close(fd);
                return -RT_EIO;
            }
            truncated = (probe_len > 0);
            break;
        }
    }

    (void)close(fd);
    data[total] = '\0';
    if (out_len != NULL)
    {
        *out_len = total;
    }

    if (truncated)
    {
        return -RT_EFULL;
    }

    return RT_EOK;
}

static rt_err_t app_config_storage_write_all(int fd, const char *data, size_t len)
{
    size_t total = 0U;

    while (total < len)
    {
        ssize_t written = write(fd, data + total, len - total);
        if (written < 0)
        {
            return -RT_EIO;
        }
        if (written == 0)
        {
            return -RT_EIO;
        }
        total += (size_t)written;
    }

    return RT_EOK;
}

static rt_err_t app_config_storage_write_single_path(const char *path, const char *data, size_t len)
{
    int fd;
    rt_err_t result;
    char temp_path[APP_CONFIG_STORAGE_PATH_MAX];

    if (path == NULL || data == NULL)
    {
        return -RT_EINVAL;
    }

    if (snprintf(temp_path, sizeof(temp_path), "%s%s", path, APP_CONFIG_STORAGE_TEMP_SUFFIX) >= (int)sizeof(temp_path))
    {
        return -RT_EFULL;
    }

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        return -RT_EIO;
    }

    result = app_config_storage_write_all(fd, data, len);
    if (close(fd) != 0 && result == RT_EOK)
    {
        result = -RT_EIO;
    }
    if (result != RT_EOK)
    {
        (void)unlink(temp_path);
        return result;
    }

    if (rename(temp_path, path) == 0)
    {
        return RT_EOK;
    }

    (void)unlink(path);
    if (rename(temp_path, path) == 0)
    {
        return RT_EOK;
    }

    (void)unlink(temp_path);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        return -RT_EIO;
    }

    result = app_config_storage_write_all(fd, data, len);
    if (close(fd) != 0 && result == RT_EOK)
    {
        result = -RT_EIO;
    }

    return result;
}

rt_err_t app_config_storage_load(const char *preferred_path,
                                 char *out_path,
                                 size_t out_path_size,
                                 char *data,
                                 size_t data_size,
                                 size_t *out_len,
                                 bool *found)
{
    size_t i;
    rt_err_t result = RT_EOK;

    if (found != NULL)
    {
        *found = false;
    }

    if (preferred_path != NULL && preferred_path[0] != '\0')
    {
        result = app_config_storage_read_path(preferred_path, data, data_size, out_len);
        if (result == RT_EOK)
        {
            if (out_path != NULL && out_path_size > 0U)
            {
                if (snprintf(out_path, out_path_size, "%s", preferred_path) >= (int)out_path_size)
                {
                    return -RT_EFULL;
                }
            }
            if (found != NULL)
            {
                *found = true;
            }
            return RT_EOK;
        }
    }

    for (i = 0U; i < (sizeof(s_config_dirs) / sizeof(s_config_dirs[0])); ++i)
    {
        char path[APP_CONFIG_STORAGE_PATH_MAX];

        if (app_config_storage_join_path(path, sizeof(path), s_config_dirs[i], APP_CONFIG_STORAGE_FILE_NAME) < 0)
        {
            continue;
        }

        result = app_config_storage_read_path(path, data, data_size, out_len);
        if (result == RT_EOK)
        {
            if (out_path != NULL && out_path_size > 0U)
            {
                if (snprintf(out_path, out_path_size, "%s", path) >= (int)out_path_size)
                {
                    return -RT_EFULL;
                }
            }
            if (found != NULL)
            {
                *found = true;
            }
            return RT_EOK;
        }
    }

    return RT_EOK;
}

rt_err_t app_config_storage_save(const char *preferred_path,
                                 const char *data,
                                 size_t len,
                                 char *saved_path,
                                 size_t saved_path_size)
{
    size_t i;

    if (preferred_path != NULL && preferred_path[0] != '\0')
    {
        rt_err_t result = app_config_storage_write_single_path(preferred_path, data, len);
        if (result == RT_EOK)
        {
            if (saved_path != NULL && saved_path_size > 0U)
            {
                if (snprintf(saved_path, saved_path_size, "%s", preferred_path) >= (int)saved_path_size)
                {
                    return -RT_EFULL;
                }
            }
            return RT_EOK;
        }
    }

    for (i = 0U; i < (sizeof(s_config_dirs) / sizeof(s_config_dirs[0])); ++i)
    {
        char path[APP_CONFIG_STORAGE_PATH_MAX];
        rt_err_t result;

        if (app_config_storage_join_path(path, sizeof(path), s_config_dirs[i], APP_CONFIG_STORAGE_FILE_NAME) < 0)
        {
            continue;
        }

        result = app_config_storage_write_single_path(path, data, len);
        if (result == RT_EOK)
        {
            if (saved_path != NULL && saved_path_size > 0U)
            {
                if (snprintf(saved_path, saved_path_size, "%s", path) >= (int)saved_path_size)
                {
                    return -RT_EFULL;
                }
            }
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

void app_config_storage_cleanup_legacy_files(void)
{
    size_t i;
    size_t j;

    for (i = 0U; i < (sizeof(s_config_dirs) / sizeof(s_config_dirs[0])); ++i)
    {
        for (j = 0U; j < (sizeof(s_legacy_files) / sizeof(s_legacy_files[0])); ++j)
        {
            char path[APP_CONFIG_STORAGE_PATH_MAX];

            if (app_config_storage_join_path(path, sizeof(path), s_config_dirs[i], s_legacy_files[j]) < 0)
            {
                continue;
            }
            (void)unlink(path);
        }
    }
}
