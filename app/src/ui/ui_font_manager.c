#include "ui_font_manager.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "ui.h"
#include "ui_dispatch.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

#define UI_FONT_MANAGER_DIR_NAME "font"
#define UI_FONT_MANAGER_CONFIG_DIR_NAME "config"
#define UI_FONT_MANAGER_CONFIG_FILE_NAME "system.cfg"
#define UI_FONT_MANAGER_CFG_KEY "font="

static const char *const s_font_manager_device_candidates[] = {"sd0", "sd1", "sd2", "sdio0"};
static const char *const s_font_manager_mount_candidates[] = {"/", "/tf", "/sd", "/sd0"};
static const char *const s_font_manager_font_dir_candidates[] = {
    "/font",
    "/tf/font",
    "/sd/font",
    "/sd0/font",
    "font",
};
static const char *const s_font_manager_config_dir_candidates[] = {
    "/config",
    "/tf/config",
    "/sd/config",
    "/sd0/config",
    "config",
};
static const char *const s_font_manager_root_candidates[] = {
    "/",
    "/tf",
    "/sd",
    "/sd0",
};
static const char *const s_font_manager_known_subdirs[] = {
    "books",
    "record",
    "pic",
    "config",
    "font",
};
typedef struct
{
    bool initialized;
    bool use_system_font;
    bool tf_ready;
    char configured_path[UI_FONT_MANAGER_PATH_MAX];
    char active_path[UI_FONT_MANAGER_PATH_MAX];
    char active_name[UI_FONT_MANAGER_NAME_MAX];
} ui_font_manager_state_t;

static ui_font_manager_state_t s_font_manager = {
    false,
    true,
    false,
    {0},
    {0},
    "系统字体",
};
static bool s_font_manager_async_refresh_pending = false;
static char s_font_manager_list_signature[UI_FONT_MANAGER_PATH_MAX] = {0};
static bool s_font_manager_list_signature_valid = false;

static bool ui_font_manager_has_hdfont_suffix(const char *name)
{
    const char *ext;

    if (name == NULL)
    {
        return false;
    }

    ext = strrchr(name, '.');
    if (ext == NULL)
    {
        return false;
    }

    return strcasecmp(ext, ".hdfont") == 0;
}

static bool ui_font_manager_is_hidden_entry(const char *name)
{
    return (name != NULL && name[0] == '.');
}

static const char *ui_font_manager_basename(const char *path)
{
    const char *base;

    if (path == NULL)
    {
        return "";
    }

    base = strrchr(path, '/');
    return (base != NULL) ? (base + 1) : path;
}

static void ui_font_manager_set_system_active(void)
{
    s_font_manager.use_system_font = true;
    s_font_manager.active_path[0] = '\0';
    rt_strncpy(s_font_manager.active_name, "系统字体", sizeof(s_font_manager.active_name) - 1U);
    s_font_manager.active_name[sizeof(s_font_manager.active_name) - 1U] = '\0';
}

static void ui_font_manager_set_file_active(const char *path)
{
    s_font_manager.use_system_font = false;
    rt_strncpy(s_font_manager.active_path, path, sizeof(s_font_manager.active_path) - 1U);
    s_font_manager.active_path[sizeof(s_font_manager.active_path) - 1U] = '\0';
    rt_strncpy(s_font_manager.active_name,
               ui_font_manager_basename(path),
               sizeof(s_font_manager.active_name) - 1U);
    s_font_manager.active_name[sizeof(s_font_manager.active_name) - 1U] = '\0';
}

static bool ui_font_manager_file_exists(const char *path)
{
    int fd;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return false;
    }

    close(fd);
    return true;
}

static bool ui_font_manager_dir_exists(const char *path)
{
    DIR *dir;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    dir = opendir(path);
    if (dir == NULL)
    {
        return false;
    }

    closedir(dir);
    return true;
}

static void ui_font_manager_copy_path(char *buffer, size_t buffer_size, const char *path)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (path == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    rt_strncpy(buffer, path, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
}

static bool ui_font_manager_try_mount_device(const char *device_name,
                                             char *mounted_path,
                                             size_t mounted_path_size)
{
    rt_device_t device;
    const char *mounted;
    size_t i;

    if (device_name == NULL || mounted_path == NULL || mounted_path_size == 0U)
    {
        return false;
    }

    device = rt_device_find(device_name);
    if (device == RT_NULL)
    {
        return false;
    }

    mounted = dfs_filesystem_get_mounted_path(device);
    if (mounted != RT_NULL && mounted[0] != '\0')
    {
        ui_font_manager_copy_path(mounted_path, mounted_path_size, mounted);
        return true;
    }

    for (i = 0; i < sizeof(s_font_manager_mount_candidates) / sizeof(s_font_manager_mount_candidates[0]); ++i)
    {
        const char *candidate = s_font_manager_mount_candidates[i];

        if (strcmp(candidate, "/") != 0)
        {
            mkdir(candidate, 0);
        }

        if (dfs_mount(device_name, candidate, "elm", 0, 0) == RT_EOK)
        {
            ui_font_manager_copy_path(mounted_path, mounted_path_size, candidate);
            return true;
        }
    }

    return false;
}

static bool ui_font_manager_resolve_storage_root(char *mounted_path, size_t mounted_path_size)
{
    size_t i;

    if (mounted_path == NULL || mounted_path_size == 0U)
    {
        return false;
    }

    mounted_path[0] = '\0';
    for (i = 0; i < sizeof(s_font_manager_device_candidates) / sizeof(s_font_manager_device_candidates[0]); ++i)
    {
        if (ui_font_manager_try_mount_device(s_font_manager_device_candidates[i],
                                             mounted_path,
                                             mounted_path_size))
        {
            return true;
        }
    }

    return false;
}

static bool ui_font_manager_build_subdir_path(const char *subdir, char *buffer, size_t buffer_size)
{
    char mounted_path[64];
    char probe_path[UI_FONT_MANAGER_PATH_MAX];
    size_t i;
    size_t j;
    const char *const *candidates = RT_NULL;
    size_t candidate_count = 0U;

    if (subdir == NULL || buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (ui_font_manager_resolve_storage_root(mounted_path, sizeof(mounted_path)))
    {
        if (strcmp(mounted_path, "/") == 0)
        {
            rt_snprintf(buffer, buffer_size, "/%s", subdir);
        }
        else
        {
            rt_snprintf(buffer, buffer_size, "%s/%s", mounted_path, subdir);
        }

        return true;
    }

    for (i = 0; i < sizeof(s_font_manager_root_candidates) / sizeof(s_font_manager_root_candidates[0]); ++i)
    {
        const char *root = s_font_manager_root_candidates[i];

        for (j = 0; j < sizeof(s_font_manager_known_subdirs) / sizeof(s_font_manager_known_subdirs[0]); ++j)
        {
            if (strcmp(root, "/") == 0)
            {
                rt_snprintf(probe_path,
                            sizeof(probe_path),
                            "/%s",
                            s_font_manager_known_subdirs[j]);
            }
            else
            {
                rt_snprintf(probe_path,
                            sizeof(probe_path),
                            "%s/%s",
                            root,
                            s_font_manager_known_subdirs[j]);
            }

            if (ui_font_manager_dir_exists(probe_path))
            {
                if (strcmp(root, "/") == 0)
                {
                    rt_snprintf(buffer, buffer_size, "/%s", subdir);
                }
                else
                {
                    rt_snprintf(buffer, buffer_size, "%s/%s", root, subdir);
                }

                return true;
            }
        }
    }

    if (strcmp(subdir, UI_FONT_MANAGER_DIR_NAME) == 0)
    {
        candidates = s_font_manager_font_dir_candidates;
        candidate_count = sizeof(s_font_manager_font_dir_candidates) / sizeof(s_font_manager_font_dir_candidates[0]);
    }
    else if (strcmp(subdir, UI_FONT_MANAGER_CONFIG_DIR_NAME) == 0)
    {
        candidates = s_font_manager_config_dir_candidates;
        candidate_count = sizeof(s_font_manager_config_dir_candidates) / sizeof(s_font_manager_config_dir_candidates[0]);
    }

    for (i = 0; candidates != RT_NULL && i < candidate_count; ++i)
    {
        if (ui_font_manager_dir_exists(candidates[i]))
        {
            ui_font_manager_copy_path(buffer, buffer_size, candidates[i]);
            return true;
        }
    }

    buffer[0] = '\0';
    return false;
}

static bool ui_font_manager_build_config_file_path(char *buffer, size_t buffer_size)
{
    char config_dir[UI_FONT_MANAGER_PATH_MAX];

    if (!ui_font_manager_build_subdir_path(UI_FONT_MANAGER_CONFIG_DIR_NAME,
                                           config_dir,
                                           sizeof(config_dir)))
    {
        if (buffer != NULL && buffer_size > 0U)
        {
            buffer[0] = '\0';
        }
        return false;
    }

    rt_snprintf(buffer, buffer_size, "%s/%s", config_dir, UI_FONT_MANAGER_CONFIG_FILE_NAME);
    return true;
}

static bool ui_font_manager_font_file_acceptable(const char *path, bool verbose)
{
    struct stat st;

    (void)verbose;

    if (!ui_font_manager_file_exists(path))
    {
        return false;
    }

    if (stat(path, &st) != 0)
    {
        return false;
    }

    if (st.st_size <= 0)
    {
        return false;
    }

    return true;
}

static bool ui_font_manager_can_open_font(const char *path)
{
    if (!ui_font_manager_font_file_acceptable(path, true))
    {
        return false;
    }

    return ui_font_manager_has_hdfont_suffix(path);
}

static uint32_t ui_font_manager_hash_list_item(const char *name, off_t file_size)
{
    uint32_t hash = 2166136261UL;
    const unsigned char *cursor = (const unsigned char *)name;
    uint32_t size_value = (uint32_t)file_size;

    if (name == NULL)
    {
        return 0U;
    }

    while (*cursor != '\0')
    {
        hash ^= (uint32_t)(*cursor++);
        hash *= 16777619UL;
    }

    hash ^= size_value;
    hash *= 16777619UL;
    return hash;
}

static bool ui_font_manager_capture_list_signature(char *signature, size_t signature_size)
{
    char scan_dir[UI_FONT_MANAGER_PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    uint32_t count = 0U;
    uint32_t hash_sum = 0U;
    uint32_t hash_xor = 0U;

    if (signature == NULL || signature_size == 0U)
    {
        return false;
    }

    signature[0] = '\0';
    if (!ui_font_manager_build_subdir_path(UI_FONT_MANAGER_DIR_NAME, scan_dir, sizeof(scan_dir)))
    {
        return true;
    }

    dir = opendir(scan_dir);
    if (dir == NULL)
    {
        return true;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char path[UI_FONT_MANAGER_PATH_MAX];
        struct stat st;

        if (ui_font_manager_is_hidden_entry(entry->d_name) ||
            !ui_font_manager_has_hdfont_suffix(entry->d_name))
        {
            continue;
        }

        rt_snprintf(path, sizeof(path), "%s/%s", scan_dir, entry->d_name);
        if (stat(path, &st) != 0 || st.st_size <= 0)
        {
            continue;
        }

        count++;
        hash_sum += ui_font_manager_hash_list_item(entry->d_name, st.st_size);
        hash_xor ^= ui_font_manager_hash_list_item(entry->d_name, st.st_size);
    }

    closedir(dir);
    rt_snprintf(signature, signature_size, "%lu:%lu:%lu",
                (unsigned long)count,
                (unsigned long)hash_sum,
                (unsigned long)hash_xor);
    return true;
}

static bool ui_font_manager_list_changed(void)
{
    char signature[UI_FONT_MANAGER_PATH_MAX];
    bool changed;

    if (!ui_font_manager_capture_list_signature(signature, sizeof(signature)))
    {
        signature[0] = '\0';
    }

    changed = !s_font_manager_list_signature_valid ||
              strcmp(s_font_manager_list_signature, signature) != 0;
    rt_strncpy(s_font_manager_list_signature, signature, sizeof(s_font_manager_list_signature) - 1U);
    s_font_manager_list_signature[sizeof(s_font_manager_list_signature) - 1U] = '\0';
    s_font_manager_list_signature_valid = true;
    return changed;
}

static void ui_font_manager_trim_line(char *text)
{
    size_t len;

    if (text == NULL)
    {
        return;
    }

    len = strlen(text);
    while (len > 0U &&
           (text[len - 1U] == '\r' || text[len - 1U] == '\n' ||
            text[len - 1U] == ' ' || text[len - 1U] == '\t'))
    {
        text[--len] = '\0';
    }
}

static void ui_font_manager_load_config(void)
{
    int fd;
    char buffer[UI_FONT_MANAGER_PATH_MAX + 32];
    char config_path[UI_FONT_MANAGER_PATH_MAX];
    ssize_t read_len;
    char *value;

    s_font_manager.configured_path[0] = '\0';
    if (!ui_font_manager_build_config_file_path(config_path, sizeof(config_path)))
    {
        return;
    }

    fd = open(config_path, O_RDONLY, 0);
    if (fd < 0)
    {
        return;
    }

    read_len = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (read_len <= 0)
    {
        return;
    }

    buffer[read_len] = '\0';
    ui_font_manager_trim_line(buffer);
    value = strstr(buffer, UI_FONT_MANAGER_CFG_KEY);
    if (value == NULL)
    {
        return;
    }

    value += strlen(UI_FONT_MANAGER_CFG_KEY);
    if (strcmp(value, "system") == 0)
    {
        s_font_manager.configured_path[0] = '\0';
        return;
    }

    rt_strncpy(s_font_manager.configured_path, value, sizeof(s_font_manager.configured_path) - 1U);
    s_font_manager.configured_path[sizeof(s_font_manager.configured_path) - 1U] = '\0';
}

static void ui_font_manager_save_config(void)
{
    int fd;
    char buffer[UI_FONT_MANAGER_PATH_MAX + 32];
    char config_dir[UI_FONT_MANAGER_PATH_MAX];
    char config_path[UI_FONT_MANAGER_PATH_MAX];

    if (!s_font_manager.tf_ready)
    {
        return;
    }

    if (!ui_font_manager_build_subdir_path(UI_FONT_MANAGER_CONFIG_DIR_NAME,
                                           config_dir,
                                           sizeof(config_dir)))
    {
        return;
    }

    rt_snprintf(config_path, sizeof(config_path), "%s/%s", config_dir, UI_FONT_MANAGER_CONFIG_FILE_NAME);
    mkdir(config_dir, 0);
    fd = open(config_path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return;
    }

    if (s_font_manager.use_system_font)
    {
        rt_snprintf(buffer, sizeof(buffer), "%ssystem\n", UI_FONT_MANAGER_CFG_KEY);
    }
    else
    {
        rt_snprintf(buffer, sizeof(buffer), "%s%s\n", UI_FONT_MANAGER_CFG_KEY, s_font_manager.active_path);
    }

    write(fd, buffer, strlen(buffer));
    close(fd);
}

static void ui_font_manager_apply_configured_font(bool *changed)
{
    bool previous_system = s_font_manager.use_system_font;
    char previous_path[UI_FONT_MANAGER_PATH_MAX];

    if (changed != NULL)
    {
        *changed = false;
    }

    rt_strncpy(previous_path, s_font_manager.active_path, sizeof(previous_path) - 1U);
    previous_path[sizeof(previous_path) - 1U] = '\0';

    if (!s_font_manager.tf_ready || s_font_manager.configured_path[0] == '\0')
    {
        ui_font_manager_set_system_active();
    }
    else if (ui_font_manager_can_open_font(s_font_manager.configured_path))
    {
        ui_font_manager_set_file_active(s_font_manager.configured_path);
    }
    else
    {
        s_font_manager.configured_path[0] = '\0';
        ui_font_manager_set_system_active();
        ui_font_manager_save_config();
    }

    if (changed != NULL)
    {
        *changed = (previous_system != s_font_manager.use_system_font) ||
                   (strcmp(previous_path, s_font_manager.active_path) != 0);
    }
}

static void ui_font_manager_request_refresh_if_changed(bool changed)
{
    if (changed)
    {
        ui_dispatch_request_font_refresh();
    }
}

static void ui_font_manager_async_rebuild_cb(void *user_data)
{
    LV_UNUSED(user_data);
    s_font_manager_async_refresh_pending = false;
    ui_font_manager_rebuild_ui();
}

static void ui_font_manager_request_async_refresh_if_changed(bool changed)
{
    if (!changed)
    {
        return;
    }

    if (s_font_manager_async_refresh_pending)
    {
        return;
    }

    s_font_manager_async_refresh_pending = true;
    if (lv_async_call(ui_font_manager_async_rebuild_cb, NULL) != LV_RESULT_OK)
    {
        s_font_manager_async_refresh_pending = false;
        ui_dispatch_request_font_refresh();
    }
}

void ui_font_manager_init(void)
{
    bool changed = false;
    char config_path[UI_FONT_MANAGER_PATH_MAX];
    char font_dir[UI_FONT_MANAGER_PATH_MAX];

    if (s_font_manager.initialized)
    {
        return;
    }

    if ((ui_font_manager_build_config_file_path(config_path, sizeof(config_path)) &&
         ui_font_manager_file_exists(config_path)) ||
        (ui_font_manager_build_subdir_path(UI_FONT_MANAGER_DIR_NAME, font_dir, sizeof(font_dir)) &&
         ui_font_manager_dir_exists(font_dir)))
    {
        s_font_manager.tf_ready = true;
        ui_font_manager_load_config();
    }

    ui_font_manager_apply_configured_font(&changed);
    ui_font_manager_list_changed();
    s_font_manager.initialized = true;
    LV_UNUSED(changed);
}

void ui_font_manager_deinit(void)
{
    s_font_manager.initialized = false;
    s_font_manager_async_refresh_pending = false;
    s_font_manager_list_signature_valid = false;
}

void ui_font_manager_notify_storage_ready(void)
{
    bool changed = false;
    char config_path[UI_FONT_MANAGER_PATH_MAX];
    char font_dir[UI_FONT_MANAGER_PATH_MAX];
    bool storage_ready;
    bool list_changed;

    ui_font_manager_init();
    storage_ready = (ui_font_manager_build_config_file_path(config_path, sizeof(config_path)) &&
                     ui_font_manager_file_exists(config_path)) ||
                    (ui_font_manager_build_subdir_path(UI_FONT_MANAGER_DIR_NAME, font_dir, sizeof(font_dir)) &&
                     ui_font_manager_dir_exists(font_dir));
    s_font_manager.tf_ready = storage_ready;
    list_changed = ui_font_manager_list_changed();
    if (!storage_ready)
    {
        ui_font_manager_apply_configured_font(&changed);
        ui_font_manager_request_refresh_if_changed(changed || list_changed);
        return;
    }

    ui_font_manager_load_config();
    ui_font_manager_apply_configured_font(&changed);
    ui_font_manager_request_refresh_if_changed(changed || list_changed);
}

void ui_font_manager_notify_storage_removed(void)
{
    bool changed = false;

    ui_font_manager_init();
    s_font_manager.tf_ready = false;
    s_font_manager_list_signature_valid = false;
    ui_font_manager_apply_configured_font(&changed);
    ui_font_manager_request_refresh_if_changed(changed);
}

bool ui_font_manager_using_system_font(void)
{
    ui_font_manager_init();
    return s_font_manager.use_system_font;
}

bool ui_font_manager_get_active_font_path(char *buffer, size_t buffer_size)
{
    ui_font_manager_init();
    if (buffer == NULL || buffer_size == 0U || s_font_manager.use_system_font)
    {
        return false;
    }

    rt_strncpy(buffer, s_font_manager.active_path, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
    return buffer[0] != '\0';
}

const char *ui_font_manager_get_active_font_name(void)
{
    ui_font_manager_init();
    return s_font_manager.active_name;
}

bool ui_font_manager_select_system_font(void)
{
    bool changed;

    ui_font_manager_init();
    changed = !s_font_manager.use_system_font || s_font_manager.active_path[0] != '\0';
    if (!changed)
    {
        return true;
    }

    s_font_manager.configured_path[0] = '\0';
    ui_font_manager_set_system_active();
    ui_font_manager_save_config();
    ui_font_manager_request_async_refresh_if_changed(changed);
    return true;
}

bool ui_font_manager_select_font_file(const char *path)
{
    bool changed;

    ui_font_manager_init();
    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    changed = s_font_manager.use_system_font || strcmp(s_font_manager.active_path, path) != 0;
    if (!changed)
    {
        return true;
    }

    if (!ui_font_manager_can_open_font(path))
    {
        return false;
    }

    rt_strncpy(s_font_manager.configured_path, path, sizeof(s_font_manager.configured_path) - 1U);
    s_font_manager.configured_path[sizeof(s_font_manager.configured_path) - 1U] = '\0';
    ui_font_manager_set_file_active(path);
    ui_font_manager_save_config();
    ui_font_manager_request_async_refresh_if_changed(changed);
    return true;
}

uint16_t ui_font_manager_list_items(ui_font_manager_item_t *items, uint16_t max_items)
{
    char scan_dir[UI_FONT_MANAGER_PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    uint16_t count = 0U;

    ui_font_manager_init();

    if (items != NULL && count < max_items)
    {
        memset(&items[count], 0, sizeof(items[count]));
        rt_strncpy(items[count].name, "系统字体", sizeof(items[count].name) - 1U);
        items[count].selected = s_font_manager.use_system_font;
        items[count].system = true;
        count++;
    }

    if (!ui_font_manager_build_subdir_path(UI_FONT_MANAGER_DIR_NAME, scan_dir, sizeof(scan_dir)))
    {
        return count;
    }

    dir = opendir(scan_dir);
    if (dir == NULL)
    {
        return count;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (ui_font_manager_is_hidden_entry(entry->d_name) ||
            !ui_font_manager_has_hdfont_suffix(entry->d_name))
        {
            continue;
        }

        if (count >= max_items)
        {
            break;
        }

        if (items != NULL)
        {
            memset(&items[count], 0, sizeof(items[count]));
            rt_snprintf(items[count].path, sizeof(items[count].path), "%s/%s", scan_dir, entry->d_name);
            rt_strncpy(items[count].name, entry->d_name, sizeof(items[count].name) - 1U);
            items[count].name[sizeof(items[count].name) - 1U] = '\0';
            if (!ui_font_manager_font_file_acceptable(items[count].path, false))
            {
                continue;
            }

            items[count].selected = (!s_font_manager.use_system_font &&
                                     strcmp(items[count].path, s_font_manager.active_path) == 0);
            items[count].system = false;
        }

        count++;
    }

    closedir(dir);
    return count;
}

void ui_font_manager_rebuild_ui(void)
{
    ui_screen_id_t active = ui_dispatch_get_active_screen();

    if (active == UI_SCREEN_NONE)
    {
        active = ui_runtime_get_active_screen_id();
    }

    ui_Home_screen_destroy();
    ui_Standby_screen_destroy();
    ui_Reading_List_screen_destroy();
    ui_Reading_Detail_screen_destroy();
    ui_Pet_screen_destroy();
    ui_Pet_Rules_screen_destroy();
    ui_AI_Dou_screen_destroy();
    ui_Time_Manage_screen_destroy();
    ui_Pomodoro_screen_destroy();
    ui_Datetime_screen_destroy();
    ui_Weather_screen_destroy();
    ui_Calendar_screen_destroy();
    ui_Status_Detail_screen_destroy();
    ui_Recorder_screen_destroy();
    ui_Record_List_screen_destroy();
    ui_Music_List_screen_destroy();
    ui_Music_Player_screen_destroy();
    ui_Settings_screen_destroy();
    ui_Brightness_screen_destroy();
    ui_Language_screen_destroy();
    ui_Bluetooth_Config_screen_destroy();
    ui_Wallpaper_screen_destroy();
    ui_helpers_reset_font_cache();

    if (active == UI_SCREEN_NONE || active == UI_SCREEN_STANDBY)
    {
        active = UI_SCREEN_HOME;
    }

    ui_runtime_reload(active);
}
