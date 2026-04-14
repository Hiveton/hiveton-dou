#include "petgame_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "rtdevice.h"

static const char *const s_tf_devices[] = {"sd0", "sd1", "sd2", "sdio0"};

#define PETGAME_PATH_BUF_SIZE 256U
#define PETGAME_FILE_NAME "petgame.dat"
#define PETGAME_GAMES_DIR "games"
#define PETGAME_DATA_DIR "petgame"
#define PETGAME_MAX_FILE_SIZE 2048U

static bool petgame_storage_find_mount_root(char *buffer, size_t buffer_size)
{
    size_t i;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    for (i = 0; i < sizeof(s_tf_devices) / sizeof(s_tf_devices[0]); ++i)
    {
        rt_device_t device = rt_device_find(s_tf_devices[i]);
        const char *mounted = (device != RT_NULL) ? dfs_filesystem_get_mounted_path(device) : RT_NULL;

        if (mounted != NULL && mounted[0] != '\0')
        {
            rt_snprintf(buffer, buffer_size, "%s", mounted);
            return true;
        }
    }

    return false;
}

static bool petgame_storage_join_path(char *buffer,
                                     size_t buffer_size,
                                     const char *base,
                                     const char *suffix)
{
    size_t base_len;

    if (buffer == NULL || buffer_size == 0U || base == NULL || suffix == NULL)
    {
        return false;
    }

    if (suffix[0] == '\0')
    {
        rt_snprintf(buffer, buffer_size, "%s", base);
        return true;
    }

    if ((base[0] == '\0') || strcmp(base, "/") == 0)
    {
        rt_snprintf(buffer, buffer_size, "/%s", suffix);
        return true;
    }

    base_len = strlen(base);
    if (base[base_len - 1U] == '/')
    {
        rt_snprintf(buffer, buffer_size, "%s%s", base, suffix);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s/%s", base, suffix);
    }

    return true;
}

static void petgame_storage_build_paths(char *games_dir,
                                       size_t games_dir_size,
                                       char *data_dir,
                                       size_t data_dir_size,
                                       char *data_file,
                                       size_t data_file_size)
{
    char mount_root[PETGAME_PATH_BUF_SIZE] = {0};
    char games_dir_path[PETGAME_PATH_BUF_SIZE] = {0};
    char data_dir_path[PETGAME_PATH_BUF_SIZE] = {0};

    if (games_dir == NULL || data_dir == NULL || data_file == NULL)
    {
        return;
    }

    games_dir[0] = '\0';
    data_dir[0] = '\0';
    data_file[0] = '\0';
    if (!petgame_storage_find_mount_root(mount_root, sizeof(mount_root)))
    {
        return;
    }

    (void)petgame_storage_join_path(games_dir_path,
                                    games_dir_size,
                                    mount_root,
                                    PETGAME_GAMES_DIR);
    (void)petgame_storage_join_path(data_dir_path,
                                    data_dir_size,
                                    games_dir_path,
                                    PETGAME_DATA_DIR);
    (void)petgame_storage_join_path(data_file,
                                    data_file_size,
                                    data_dir_path,
                                    PETGAME_FILE_NAME);
    rt_snprintf(games_dir, games_dir_size, "%s", games_dir_path);
    rt_snprintf(data_dir, data_dir_size, "%s", data_dir_path);
}

static bool petgame_storage_make_dir(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    mkdir(path, 0);
    return true;
}

static bool petgame_parse_u32_line(const char *line, const char *key, uint32_t *value)
{
    size_t key_len;

    if (line == NULL || key == NULL || value == NULL)
    {
        return false;
    }

    key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0 || line[key_len] != '=')
    {
        return false;
    }

    *value = (uint32_t)strtoul(line + key_len + 1U, NULL, 10);
    return true;
}

static bool petgame_storage_parse_state(const char *text, petgame_state_t *state)
{
    char local_copy[PETGAME_MAX_FILE_SIZE];
    char *token;
    char *save_ptr = NULL;
    bool parsed = false;

    if (text == NULL || state == NULL)
    {
        return false;
    }

    rt_snprintf(local_copy, sizeof(local_copy), "%s", text);
    token = strtok_r(local_copy, "\r\n", &save_ptr);
    while (token != NULL)
    {
        uint32_t value;

        if (petgame_parse_u32_line(token, "version", &value))
        {
            state->version = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "reading_seconds", &value))
        {
            state->reading_seconds = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "ai_interaction_count", &value))
        {
            state->ai_interaction_count = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "manual_feed_count", &value))
        {
            state->manual_feed_count = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "affection_count", &value))
        {
            state->affection_count = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "growth_score", &value))
        {
            state->growth_score = value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "growth_level", &value))
        {
            state->growth_level = (uint8_t)value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "mood_level", &value))
        {
            state->mood_level = (uint8_t)value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "hunger_level", &value))
        {
            state->hunger_level = (uint8_t)value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "energy_level", &value))
        {
            state->energy_level = (uint8_t)value;
            parsed = true;
        }
        else if (petgame_parse_u32_line(token, "feed_balance", &value))
        {
            state->feed_balance = value;
            parsed = true;
        }

        token = strtok_r(NULL, "\r\n", &save_ptr);
    }

    return parsed;
}

static bool petgame_storage_read_file(const char *path, char *buffer, size_t buffer_size)
{
    int fd;
    ssize_t read_size;

    if (path == NULL || buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return false;
    }

    read_size = read(fd, buffer, buffer_size - 1U);
    close(fd);
    if (read_size <= 0)
    {
        return false;
    }

    buffer[read_size] = '\0';
    return true;
}

bool petgame_storage_load(petgame_state_t *state)
{
    char games_dir[PETGAME_PATH_BUF_SIZE] = {0};
    char data_dir[PETGAME_PATH_BUF_SIZE] = {0};
    char data_file[PETGAME_PATH_BUF_SIZE] = {0};
    char file_content[PETGAME_MAX_FILE_SIZE] = {0};

    if (state == NULL)
    {
        return false;
    }

    petgame_storage_build_paths(games_dir,
                               sizeof(games_dir),
                               data_dir,
                               sizeof(data_dir),
                               data_file,
                               sizeof(data_file));
    if (data_file[0] == '\0')
    {
        return false;
    }

    if (!petgame_storage_read_file(data_file, file_content, sizeof(file_content)))
    {
        return false;
    }

    (void)games_dir;
    (void)data_dir;

    return petgame_storage_parse_state(file_content, state);
}

bool petgame_storage_save(const petgame_state_t *state)
{
    char games_dir[PETGAME_PATH_BUF_SIZE] = {0};
    char data_dir[PETGAME_PATH_BUF_SIZE] = {0};
    char data_file[PETGAME_PATH_BUF_SIZE] = {0};
    char content[PETGAME_MAX_FILE_SIZE] = {0};
    int fd;
    int written;

    if (state == NULL)
    {
        return false;
    }

    petgame_storage_build_paths(games_dir,
                               sizeof(games_dir),
                               data_dir,
                               sizeof(data_dir),
                               data_file,
                               sizeof(data_file));
    if (data_file[0] == '\0')
    {
        return false;
    }

    (void)petgame_storage_make_dir(games_dir);
    (void)petgame_storage_make_dir(data_dir);

    rt_snprintf(content,
                sizeof(content),
                "version=%u\n"
                "reading_seconds=%u\n"
                "ai_interaction_count=%u\n"
                "manual_feed_count=%u\n"
                "affection_count=%u\n"
                "growth_score=%u\n"
                "growth_level=%u\n"
                "mood_level=%u\n"
                "hunger_level=%u\n"
                "energy_level=%u\n"
                "feed_balance=%u\n",
                (unsigned int)state->version,
                (unsigned int)state->reading_seconds,
                (unsigned int)state->ai_interaction_count,
                (unsigned int)state->manual_feed_count,
                (unsigned int)state->affection_count,
                (unsigned int)state->growth_score,
                (unsigned int)state->growth_level,
                (unsigned int)state->mood_level,
                (unsigned int)state->hunger_level,
                (unsigned int)state->energy_level,
                (unsigned int)state->feed_balance);

    fd = open(data_file, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return false;
    }

    written = write(fd, content, strlen(content));
    close(fd);
    return (written > 0 && written == (int)strlen(content));
}
