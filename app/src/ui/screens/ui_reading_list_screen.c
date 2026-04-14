#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

#define UI_READING_VISIBLE_COUNT 4U
#define UI_READING_MAX_FILES 48U
#define UI_READING_MAX_NAME_LEN 96U
#define UI_READING_MAX_PATH_LEN 192U
#define UI_READING_BOOKS_DIRECTORY "books"

typedef enum
{
    UI_READING_SCAN_OK = 0,
    UI_READING_SCAN_NO_CARD,
    UI_READING_SCAN_MOUNT_FAILED,
    UI_READING_SCAN_NO_FILES,
} ui_reading_scan_state_t;

typedef struct
{
    char name[UI_READING_MAX_NAME_LEN];
    uint32_t size_bytes;
    uint8_t file_type;
} ui_reading_file_entry_t;

enum
{
    UI_READING_FILE_TYPE_TXT = 0,
    UI_READING_FILE_TYPE_EPUB,
};

typedef struct
{
    lv_obj_t *card;
    lv_obj_t *title_label;
    lv_obj_t *meta_label;
    char title_text[UI_READING_MAX_NAME_LEN];
    char meta_text[80];
} ui_reading_card_refs_t;

lv_obj_t *ui_Reading_List = NULL;

static const int s_card_y_positions[UI_READING_VISIBLE_COUNT] = {40, 179, 318, 457};
static const char *const s_reading_device_candidates[] = {"sd0", "sd1", "sd2", "sdio0"};
static const char *const s_reading_mount_candidates[] = {"/", "/tf", "/sd", "/sd0"};

static ui_reading_file_entry_t s_reading_files[UI_READING_MAX_FILES];
static ui_reading_card_refs_t s_reading_cards[UI_READING_VISIBLE_COUNT];
static lv_obj_t *s_reading_status_label = NULL;
static lv_obj_t *s_reading_prev_button = NULL;
static lv_obj_t *s_reading_next_button = NULL;
static lv_obj_t *s_reading_page_label = NULL;
static lv_timer_t *s_reading_refresh_timer = NULL;
static lv_timer_t *s_reading_open_timer = NULL;
static uint16_t s_reading_file_count = 0;
static uint16_t s_reading_page_offset = 0;
static uint16_t s_reading_selected_index = 0;
static bool s_reading_has_selection = false;
static bool s_reading_open_detail_pending = false;
static ui_reading_scan_state_t s_reading_scan_state = UI_READING_SCAN_NO_CARD;
static char s_reading_mount_path[32];
static char s_reading_books_path[UI_READING_MAX_PATH_LEN];
static char s_reading_selected_name[UI_READING_MAX_NAME_LEN];
static char s_reading_selected_path[UI_READING_MAX_PATH_LEN];
static char s_reading_status_text[128];

static void ui_reading_list_render(void);
static void ui_reading_open_selected_detail(void);

static void ui_reading_set_label_text_static(lv_obj_t *label,
                                             char *buffer,
                                             size_t buffer_size,
                                             const char *text)
{
    if (label == NULL || buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    rt_snprintf(buffer, buffer_size, "%s", text != NULL ? text : "");
    lv_label_set_text_static(label, buffer);
}

static void ui_reading_open_selected_detail(void)
{
    if (ui_Reading_Detail != NULL)
    {
        ui_Reading_Detail_screen_destroy();
    }

    ui_runtime_switch_to(UI_SCREEN_READING_DETAIL);
}

void ui_reading_list_hardware_prev_page(void)
{
    if (s_reading_prev_button != NULL)
    {
        lv_obj_send_event(s_reading_prev_button, LV_EVENT_CLICKED, NULL);
    }
}

void ui_reading_list_hardware_next_page(void)
{
    if (s_reading_next_button != NULL)
    {
        lv_obj_send_event(s_reading_next_button, LV_EVENT_CLICKED, NULL);
    }
}

typedef struct
{
    ui_reading_scan_state_t state;
    uint16_t file_count;
    char mount_path[sizeof(s_reading_mount_path)];
} ui_reading_snapshot_t;

static ui_reading_snapshot_t ui_reading_capture_snapshot(void)
{
    ui_reading_snapshot_t snapshot;

    snapshot.state = s_reading_scan_state;
    snapshot.file_count = s_reading_file_count;
    rt_snprintf(snapshot.mount_path, sizeof(snapshot.mount_path), "%s", s_reading_mount_path);
    return snapshot;
}

static bool ui_reading_snapshot_changed(const ui_reading_snapshot_t *before,
                                        const ui_reading_snapshot_t *after)
{
    if (before == NULL || after == NULL)
    {
        return true;
    }

    return before->state != after->state ||
           before->file_count != after->file_count ||
           strcmp(before->mount_path, after->mount_path) != 0;
}

static void ui_reading_join_path(char *buffer,
                                 size_t buffer_size,
                                 const char *base_path,
                                 const char *child_path)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (base_path == NULL || base_path[0] == '\0' || child_path == NULL || child_path[0] == '\0')
    {
        rt_snprintf(buffer,
                    buffer_size,
                    "%s%s",
                    base_path != NULL ? base_path : "",
                    child_path != NULL ? child_path : "");
        return;
    }

    rt_snprintf(buffer,
                buffer_size,
                "%s%s%s",
                base_path,
                strcmp(base_path, "/") == 0 ? "" : "/",
                child_path);
}

static bool ui_reading_is_listable_file(const char *name)
{
    const char *dot;

    if (name == NULL)
    {
        return false;
    }

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        return false;
    }

    if (name[0] == '.')
    {
        return false;
    }

    dot = strrchr(name, '.');
    if (dot == NULL)
    {
        return false;
    }

    return strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".epub") == 0;
}

static uint8_t ui_reading_detect_file_type(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot != NULL && strcasecmp(dot, ".epub") == 0)
    {
        return UI_READING_FILE_TYPE_EPUB;
    }

    return UI_READING_FILE_TYPE_TXT;
}

static const char *ui_reading_file_type_label(uint8_t file_type)
{
    switch (file_type)
    {
    case UI_READING_FILE_TYPE_EPUB:
        return "EPUB";
    case UI_READING_FILE_TYPE_TXT:
    default:
        return "TXT";
    }
}

static void ui_reading_format_size(uint32_t size_bytes, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (size_bytes >= (1024U * 1024U))
    {
        uint32_t whole = size_bytes / (1024U * 1024U);
        uint32_t tenth = ((size_bytes % (1024U * 1024U)) * 10U) / (1024U * 1024U);

        rt_snprintf(buffer, buffer_size, "%lu.%lu MB", (unsigned long)whole, (unsigned long)tenth);
    }
    else if (size_bytes >= 1024U)
    {
        uint32_t kb = (size_bytes + 1023U) / 1024U;

        rt_snprintf(buffer, buffer_size, "%lu KB", (unsigned long)kb);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%lu B", (unsigned long)size_bytes);
    }
}

static int ui_reading_file_compare(const void *lhs, const void *rhs)
{
    const ui_reading_file_entry_t *left = (const ui_reading_file_entry_t *)lhs;
    const ui_reading_file_entry_t *right = (const ui_reading_file_entry_t *)rhs;

    return strcmp(left->name, right->name);
}

static void ui_reading_copy_path(char *buffer, size_t buffer_size, const char *path)
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

    rt_snprintf(buffer, buffer_size, "%s", path);
}

static void ui_reading_update_selected_cache(void)
{
    if (!s_reading_has_selection ||
        s_reading_selected_index >= s_reading_file_count ||
        s_reading_books_path[0] == '\0')
    {
        s_reading_selected_name[0] = '\0';
        s_reading_selected_path[0] = '\0';
        return;
    }

    rt_snprintf(s_reading_selected_name,
                sizeof(s_reading_selected_name),
                "%s",
                s_reading_files[s_reading_selected_index].name);
    ui_reading_join_path(s_reading_selected_path,
                         sizeof(s_reading_selected_path),
                         s_reading_books_path,
                         s_reading_files[s_reading_selected_index].name);
}

static bool ui_reading_try_mount_device(const char *device_name,
                                        char *mounted_path,
                                        size_t mounted_path_size)
{
    rt_device_t device;
    const char *mounted;
    size_t i;

    device = rt_device_find(device_name);
    if (device == RT_NULL)
    {
        return false;
    }

    mounted = dfs_filesystem_get_mounted_path(device);
    if (mounted != RT_NULL && mounted[0] != '\0')
    {
        ui_reading_copy_path(mounted_path, mounted_path_size, mounted);
        return true;
    }

    for (i = 0; i < sizeof(s_reading_mount_candidates) / sizeof(s_reading_mount_candidates[0]); ++i)
    {
        const char *candidate = s_reading_mount_candidates[i];

        if (strcmp(candidate, "/") != 0)
        {
            mkdir(candidate, 0);
        }

        if (dfs_mount(device_name, candidate, "elm", 0, 0) == RT_EOK)
        {
            rt_kprintf("reading_list: mounted %s at %s\n", device_name, candidate);
            ui_reading_copy_path(mounted_path, mounted_path_size, candidate);
            return true;
        }
    }

    rt_kprintf("reading_list: mount failed for %s\n", device_name);
    return false;
}

static bool ui_reading_resolve_storage_root(char *mounted_path, size_t mounted_path_size, bool *had_device)
{
    size_t i;

    if (mounted_path == NULL || mounted_path_size == 0U)
    {
        return false;
    }

    mounted_path[0] = '\0';
    if (had_device != NULL)
    {
        *had_device = false;
    }

    for (i = 0; i < sizeof(s_reading_device_candidates) / sizeof(s_reading_device_candidates[0]); ++i)
    {
        const char *device_name = s_reading_device_candidates[i];
        rt_device_t device = rt_device_find(device_name);

        if (device == RT_NULL)
        {
            continue;
        }

        if (had_device != NULL)
        {
            *had_device = true;
        }

        if (ui_reading_try_mount_device(device_name, mounted_path, mounted_path_size))
        {
            return true;
        }
    }

    return false;
}

static bool ui_reading_scan_directory(const char *directory_path)
{
    DIR *dir;
    struct dirent *entry;

    if (directory_path == NULL || directory_path[0] == '\0')
    {
        return false;
    }

    dir = opendir(directory_path);
    if (dir == NULL)
    {
        rt_kprintf("reading_list: opendir failed path=%s\n", directory_path);
        return false;
    }

    s_reading_file_count = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[UI_READING_MAX_PATH_LEN];
        struct stat stat_buffer;

        if (!ui_reading_is_listable_file(entry->d_name))
        {
            continue;
        }

        rt_snprintf(full_path,
                    sizeof(full_path),
                    "%s%s%s",
                    directory_path,
                    strcmp(directory_path, "/") == 0 ? "" : "/",
                    entry->d_name);

        if (stat(full_path, &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode))
        {
            continue;
        }

        rt_snprintf(s_reading_files[s_reading_file_count].name,
                    sizeof(s_reading_files[s_reading_file_count].name),
                    "%s",
                    entry->d_name);
        s_reading_files[s_reading_file_count].size_bytes = (uint32_t)stat_buffer.st_size;
        s_reading_files[s_reading_file_count].file_type = ui_reading_detect_file_type(entry->d_name);
        ++s_reading_file_count;

        if (s_reading_file_count >= UI_READING_MAX_FILES)
        {
            break;
        }
    }

    closedir(dir);

    if (s_reading_file_count > 1U)
    {
        qsort(s_reading_files,
              s_reading_file_count,
              sizeof(s_reading_files[0]),
              ui_reading_file_compare);
    }

    return true;
}

static void ui_reading_refresh_files(void)
{
    char previous_name[UI_READING_MAX_NAME_LEN];
    bool had_device = false;
    bool had_selection;
    uint16_t i;

    had_selection = s_reading_has_selection && s_reading_selected_name[0] != '\0';
    rt_snprintf(previous_name, sizeof(previous_name), "%s", s_reading_selected_name);

    memset(s_reading_files, 0, sizeof(s_reading_files));
    s_reading_file_count = 0;
    s_reading_page_offset = 0;
    s_reading_mount_path[0] = '\0';
    s_reading_books_path[0] = '\0';

    if (!ui_reading_resolve_storage_root(s_reading_mount_path, sizeof(s_reading_mount_path), &had_device))
    {
        s_reading_scan_state = had_device ? UI_READING_SCAN_MOUNT_FAILED : UI_READING_SCAN_NO_CARD;
        s_reading_has_selection = false;
        s_reading_selected_index = 0U;
        ui_reading_update_selected_cache();
        return;
    }

    ui_reading_join_path(s_reading_books_path,
                         sizeof(s_reading_books_path),
                         s_reading_mount_path,
                         UI_READING_BOOKS_DIRECTORY);

    if (!ui_reading_scan_directory(s_reading_books_path))
    {
        s_reading_scan_state = UI_READING_SCAN_MOUNT_FAILED;
        s_reading_has_selection = false;
        s_reading_selected_index = 0U;
        ui_reading_update_selected_cache();
        return;
    }

    s_reading_scan_state = s_reading_file_count > 0U ? UI_READING_SCAN_OK : UI_READING_SCAN_NO_FILES;

    if (s_reading_file_count == 0U)
    {
        s_reading_has_selection = false;
        s_reading_selected_index = 0U;
        ui_reading_update_selected_cache();
        return;
    }

    if (had_selection)
    {
        for (i = 0; i < s_reading_file_count; ++i)
        {
            if (strcmp(previous_name, s_reading_files[i].name) == 0)
            {
                s_reading_selected_index = i;
                s_reading_has_selection = true;
                ui_reading_update_selected_cache();
                return;
            }
        }
    }

    if (!s_reading_has_selection || s_reading_selected_index >= s_reading_file_count)
    {
        s_reading_selected_index = 0U;
        s_reading_has_selection = true;
    }

    ui_reading_update_selected_cache();
}

static void ui_reading_refresh_timer_cb(lv_timer_t *timer)
{
    ui_reading_snapshot_t before;
    ui_reading_snapshot_t after;

    LV_UNUSED(timer);

    if (ui_Reading_List == NULL)
    {
        return;
    }

    before = ui_reading_capture_snapshot();
    ui_reading_refresh_files();
    after = ui_reading_capture_snapshot();

    if (ui_reading_snapshot_changed(&before, &after))
    {
        ui_reading_list_render();
    }

    if (s_reading_scan_state == UI_READING_SCAN_OK ||
        s_reading_scan_state == UI_READING_SCAN_NO_FILES)
    {
        lv_timer_delete(s_reading_refresh_timer);
        s_reading_refresh_timer = NULL;
    }
}

static void ui_reading_open_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (!s_reading_open_detail_pending || ui_Reading_List == NULL)
    {
        return;
    }

    if (!ui_reading_detail_is_selected_ready())
    {
        return;
    }

    s_reading_open_detail_pending = false;
    ui_reading_set_label_text_static(s_reading_status_label,
                                     s_reading_status_text,
                                     sizeof(s_reading_status_text),
                                     ui_i18n_pick("正在打开阅读详情...", "Opening reading detail..."));
    ui_reading_open_selected_detail();
}

static void ui_reading_set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL)
    {
        return;
    }

    if (enabled)
    {
        lv_obj_clear_state(button, LV_STATE_DISABLED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
    }
    else
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_50, 0);
    }
}

static void ui_reading_show_card(uint16_t slot_index,
                                 const char *title,
                                 const char *meta,
                                 bool clickable)
{
    ui_reading_card_refs_t *refs;

    if (slot_index >= UI_READING_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_reading_cards[slot_index];
    if (refs->card == NULL || refs->title_label == NULL || refs->meta_label == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    ui_reading_set_label_text_static(refs->title_label,
                                     refs->title_text,
                                     sizeof(refs->title_text),
                                     title);
    ui_reading_set_label_text_static(refs->meta_label,
                                     refs->meta_text,
                                     sizeof(refs->meta_text),
                                     meta);

    if (clickable)
    {
        lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
        lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(refs->card, LV_OPA_COVER, 0);
    }
    else
    {
        lv_obj_add_state(refs->card, LV_STATE_DISABLED);
        lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(refs->card, LV_OPA_COVER, 0);
    }
}

static void ui_reading_hide_card(uint16_t slot_index)
{
    ui_reading_card_refs_t *refs;

    if (slot_index >= UI_READING_VISIBLE_COUNT)
    {
        return;
    }

    refs = &s_reading_cards[slot_index];
    if (refs->card == NULL)
    {
        return;
    }

    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
}

static void ui_reading_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_reading_page_offset >= UI_READING_VISIBLE_COUNT)
    {
        s_reading_page_offset = (uint16_t)(s_reading_page_offset - UI_READING_VISIBLE_COUNT);
    }
    else
    {
        s_reading_page_offset = 0;
    }

    ui_reading_list_render();
}

static void ui_reading_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_reading_page_offset + UI_READING_VISIBLE_COUNT) < s_reading_file_count)
    {
        s_reading_page_offset = (uint16_t)(s_reading_page_offset + UI_READING_VISIBLE_COUNT);
        ui_reading_list_render();
    }
}

static void ui_reading_card_event_cb(lv_event_t *e)
{
    uintptr_t slot_index;
    uint16_t file_index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    slot_index = (uintptr_t)lv_event_get_user_data(e);
    file_index = (uint16_t)(s_reading_page_offset + (uint16_t)slot_index);
    if (file_index >= s_reading_file_count)
    {
        return;
    }

    s_reading_selected_index = file_index;
    s_reading_has_selection = true;
    ui_reading_update_selected_cache();
    lv_display_trigger_activity(NULL);
    s_reading_open_detail_pending = false;
    ui_reading_open_selected_detail();
}

static void ui_reading_create_card(lv_obj_t *parent, uint16_t slot_index, int y)
{
    ui_reading_card_refs_t *refs = &s_reading_cards[slot_index];

    refs->card = ui_create_card(parent, 24, y, 480, 121, UI_SCREEN_NONE, false, 0);
    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        22,
                                        31,
                                        436,
                                        31,
                                        28,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    refs->meta_label = ui_create_label(refs->card,
                                       "",
                                       22,
                                       71,
                                       436,
                                       20,
                                       19,
                                       LV_TEXT_ALIGN_LEFT,
                                       false,
                                       false);
    lv_obj_add_event_cb(refs->card,
                        ui_reading_card_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)slot_index);
}

static void ui_reading_list_render(void)
{
    uint16_t visible_index;
    bool can_prev;
    bool can_next;
    char status_text[80];
    char page_text[16];
    uint16_t total_pages = s_reading_file_count == 0U ? 0U : (uint16_t)((s_reading_file_count + UI_READING_VISIBLE_COUNT - 1U) / UI_READING_VISIBLE_COUNT);
    uint16_t current_page = total_pages == 0U ? 0U : (uint16_t)(s_reading_page_offset / UI_READING_VISIBLE_COUNT + 1U);

    if (s_reading_status_label == NULL)
    {
        return;
    }

    can_prev = s_reading_page_offset > 0U;
    can_next = (uint16_t)(s_reading_page_offset + UI_READING_VISIBLE_COUNT) < s_reading_file_count;
    ui_reading_set_button_enabled(s_reading_prev_button, can_prev);
    ui_reading_set_button_enabled(s_reading_next_button, can_next);
    if (s_reading_page_label != NULL)
    {
        rt_snprintf(page_text, sizeof(page_text), "%u / %u", (unsigned int)current_page, (unsigned int)total_pages);
        lv_label_set_text(s_reading_page_label, page_text);
    }

    if (s_reading_scan_state == UI_READING_SCAN_OK)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    ui_i18n_pick("扫描 %s · 共 %u 个文件", "Scan %s · %u files"),
                    s_reading_books_path,
                    (unsigned int)s_reading_file_count);
        ui_reading_set_label_text_static(s_reading_status_label,
                                         s_reading_status_text,
                                         sizeof(s_reading_status_text),
                                         status_text);

        for (visible_index = 0; visible_index < UI_READING_VISIBLE_COUNT; ++visible_index)
        {
            uint16_t file_index = (uint16_t)(s_reading_page_offset + visible_index);
            char meta_text[48];

            if (file_index >= s_reading_file_count)
            {
                ui_reading_hide_card(visible_index);
                continue;
            }

            ui_reading_format_size(s_reading_files[file_index].size_bytes, meta_text, sizeof(meta_text));
            rt_snprintf(status_text,
                        sizeof(status_text),
                        ui_i18n_pick("%s · %s · 点击查看", "%s · %s · Tap to open"),
                        ui_reading_file_type_label(s_reading_files[file_index].file_type),
                        meta_text);
            ui_reading_show_card(visible_index,
                                 s_reading_files[file_index].name,
                                 status_text,
                                 true);
        }

        return;
    }

    if (s_reading_scan_state == UI_READING_SCAN_NO_FILES)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    ui_i18n_pick("扫描 %s · 当前没有文件", "Scan %s · No files"),
                    s_reading_books_path[0] != '\0' ? s_reading_books_path : "/books");
        ui_reading_set_label_text_static(s_reading_status_label,
                                         s_reading_status_text,
                                         sizeof(s_reading_status_text),
                                         status_text);
        ui_reading_show_card(0,
                             ui_i18n_pick("TF 卡里还没有可显示文件", "No readable files on TF card"),
                             ui_i18n_pick("请将书籍文件复制到 TF 卡的 /books 目录后等待列表自动刷新。",
                                          "Copy book files to the TF card /books directory and wait for the list to refresh."),
                             false);
    }
    else if (s_reading_scan_state == UI_READING_SCAN_MOUNT_FAILED)
    {
        ui_reading_set_label_text_static(s_reading_status_label,
                                         s_reading_status_text,
                                         sizeof(s_reading_status_text),
                                         ui_i18n_pick("已检测到存储设备，但挂载失败", "Storage detected, but mount failed"));
        ui_reading_show_card(0,
                             ui_i18n_pick("TF 卡未挂载成功", "TF card mount failed"),
                             ui_i18n_pick("请确认卡已格式化为 FAT，并重启或重新插拔后再试。",
                                          "Make sure the card is FAT formatted, then reboot or reinsert it."),
                             false);
    }
    else
    {
        ui_reading_set_label_text_static(s_reading_status_label,
                                         s_reading_status_text,
                                         sizeof(s_reading_status_text),
                                         ui_i18n_pick("当前未检测到 TF 卡", "No TF card detected"));
        ui_reading_show_card(0,
                             ui_i18n_pick("未检测到 TF 卡", "TF card not detected"),
                             ui_i18n_pick("系统还没有注册 sd0/sd1 设备，插卡后页面会自动刷新。",
                                          "The system has not registered an sd0/sd1 device yet. This page will refresh after card insertion."),
                             false);
    }

    for (visible_index = 1; visible_index < UI_READING_VISIBLE_COUNT; ++visible_index)
    {
        ui_reading_hide_card(visible_index);
    }
}

const char *ui_reading_list_get_selected_name(void)
{
    if (s_reading_selected_name[0] != '\0')
    {
        return s_reading_selected_name;
    }

    if (!s_reading_has_selection || s_reading_selected_index >= s_reading_file_count)
    {
        return ui_i18n_pick("在线阅读", "Reading");
    }

    return s_reading_files[s_reading_selected_index].name;
}

bool ui_reading_list_prepare_selected_file(void)
{
    if (s_reading_has_selection &&
        s_reading_selected_index < s_reading_file_count &&
        s_reading_selected_path[0] != '\0')
    {
        return true;
    }

    ui_reading_refresh_files();

    if (s_reading_file_count == 0U)
    {
        s_reading_has_selection = false;
        s_reading_selected_index = 0;
        return false;
    }

    if (!s_reading_has_selection || s_reading_selected_index >= s_reading_file_count)
    {
        s_reading_selected_index = 0;
        s_reading_has_selection = true;
    }

    ui_reading_update_selected_cache();
    return s_reading_selected_path[0] != '\0';
}

bool ui_reading_list_get_selected_path(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (!ui_reading_list_prepare_selected_file())
    {
        buffer[0] = '\0';
        return false;
    }

    rt_snprintf(buffer, buffer_size, "%s", s_reading_selected_path);
    return true;
}

void ui_Reading_List_screen_init(void)
{
    ui_screen_scaffold_t page;
    uint16_t i;

    if (ui_Reading_List != NULL)
    {
        return;
    }

    memset(s_reading_cards, 0, sizeof(s_reading_cards));

    ui_Reading_List = ui_create_screen_base();
    ui_build_standard_screen(&page, ui_Reading_List, ui_i18n_pick("在线阅读", "Reading"), UI_SCREEN_HOME);

    s_reading_status_label = ui_create_label(page.content,
                                             ui_i18n_pick("正在扫描 TF 卡...", "Scanning TF card..."),
                                             24,
                                             14,
                                             360,
                                             18,
                                             18,
                                             LV_TEXT_ALIGN_LEFT,
                                             false,
                                             false);
    ui_reading_set_label_text_static(s_reading_status_label,
                                     s_reading_status_text,
                                     sizeof(s_reading_status_text),
                                     ui_i18n_pick("正在扫描 TF 卡...", "Scanning TF card..."));

    for (i = 0; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        ui_reading_create_card(page.content, i, s_card_y_positions[i]);
    }

    s_reading_page_label = ui_create_label(page.content,
                                           "1 / 1",
                                           24,
                                           598,
                                           120,
                                           24,
                                           18,
                                           LV_TEXT_ALIGN_LEFT,
                                           false,
                                           false);
    s_reading_prev_button = ui_create_button(page.content, 304, 585, 96, 46, ui_i18n_pick("上翻", "Prev"), 20, UI_SCREEN_NONE, false);
    s_reading_next_button = ui_create_button(page.content, 408, 585, 96, 46, ui_i18n_pick("下翻", "Next"), 20, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(s_reading_prev_button, ui_reading_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_reading_next_button, ui_reading_next_event_cb, LV_EVENT_CLICKED, NULL);

    ui_reading_refresh_files();
    ui_reading_list_render();

    if (s_reading_refresh_timer == NULL)
    {
        s_reading_refresh_timer = lv_timer_create(ui_reading_refresh_timer_cb, 1500, NULL);
    }

    if (s_reading_open_timer == NULL)
    {
        s_reading_open_timer = lv_timer_create(ui_reading_open_timer_cb, 60, NULL);
    }
}

void ui_Reading_List_screen_destroy(void)
{
    if (s_reading_refresh_timer != NULL)
    {
        lv_timer_delete(s_reading_refresh_timer);
        s_reading_refresh_timer = NULL;
    }

    if (s_reading_open_timer != NULL)
    {
        lv_timer_delete(s_reading_open_timer);
        s_reading_open_timer = NULL;
    }

    if (ui_Reading_List != NULL)
    {
        lv_obj_delete(ui_Reading_List);
        ui_Reading_List = NULL;
    }

    memset(s_reading_cards, 0, sizeof(s_reading_cards));
    s_reading_status_label = NULL;
    s_reading_prev_button = NULL;
    s_reading_next_button = NULL;
    s_reading_page_label = NULL;
    s_reading_open_detail_pending = false;
}

void ui_reading_list_refresh(void)
{
    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_refresh_files();
    ui_reading_list_render();
}
