#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "drv_lcd.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "reading/reading_cover_cache.h"
#include "reading/reading_state.h"
#include "reading/reading_epub.h"
#include "ui.h"
#include "ui_components.h"
#include "ui_epd_refresh_policy.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

#define UI_READING_VISIBLE_COUNT 9U
#define UI_READING_FIRST_PAGE_COUNT 6U
#define UI_READING_NEXT_PAGE_COUNT 9U
#define UI_READING_MAX_FILES 48U
#define UI_READING_MAX_NAME_LEN 96U
#define UI_READING_MAX_PATH_LEN 192U
#define UI_READING_BOOKS_DIRECTORY "books"
#define UI_READING_TAB_X 24
#define UI_READING_TAB_Y 14
#define UI_READING_TAB_WIDTH 160
#define UI_READING_TAB_HEIGHT 48
#define UI_READING_GRID_COVER_WIDTH 124
#define UI_READING_GRID_COVER_HEIGHT 126
#define UI_READING_CONTINUE_CARD_X 23
#define UI_READING_CONTINUE_CARD_Y 128
#define UI_READING_CONTINUE_CARD_WIDTH 482
#define UI_READING_CONTINUE_CARD_HEIGHT 176
#define UI_READING_FIRST_PAGE_TITLE_Y 306
#define UI_READING_FIRST_PAGE_NAV_Y 314
#define UI_READING_NEXT_PAGE_NAV_Y 123
#define UI_READING_EPUB_COVER_TEXT_BUFFER 256U
#define UI_READING_EPUB_COVER_BLOCK_COUNT 4U
#define UI_READING_EPUB_COVER_IMAGE_COUNT 4U

typedef enum
{
    UI_READING_SCAN_OK = 0,
    UI_READING_SCAN_LOADING,
    UI_READING_SCAN_NO_CARD,
    UI_READING_SCAN_MOUNT_FAILED,
    UI_READING_SCAN_NO_FILES,
} ui_reading_scan_state_t;

typedef enum
{
    UI_READING_TAB_ALL = 0,
    UI_READING_TAB_RECENT,
    UI_READING_TAB_FAVORITES,
    UI_READING_TAB_COUNT,
} ui_reading_tab_t;

typedef struct
{
    char name[UI_READING_MAX_NAME_LEN];
    char path[UI_READING_MAX_PATH_LEN];
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
    lv_obj_t *cover_box;
    lv_obj_t *cover_img;
    lv_obj_t *cover_title_label;
    lv_obj_t *title_label;
    lv_obj_t *meta_label;
    lv_image_dsc_t cover_dsc;
    bool cover_loaded;
    char cover_path[UI_READING_MAX_PATH_LEN];
    char cover_title_text[UI_READING_MAX_NAME_LEN];
    char title_text[UI_READING_MAX_NAME_LEN];
    char meta_text[128];
} ui_reading_card_refs_t;

lv_obj_t *ui_Reading_List = NULL;

static const int s_grid_x_positions[UI_READING_VISIBLE_COUNT] = {40, 203, 362, 40, 203, 362, 40, 203, 362};
static const int s_grid_first_page_y_positions[UI_READING_VISIBLE_COUNT] = {348, 348, 348, 530, 530, 530, 530, 530, 530};
static const int s_grid_next_page_y_positions[UI_READING_VISIBLE_COUNT] = {128, 128, 128, 322, 322, 322, 516, 516, 516};
static const char *const s_reading_device_candidates[] = {"sd0", "sd1", "sd2", "sdio0"};
static const char *const s_reading_mount_candidates[] = {"/", "/tf", "/sd", "/sd0"};

static ui_reading_file_entry_t s_reading_files[UI_READING_MAX_FILES];
static ui_reading_card_refs_t s_reading_cards[UI_READING_VISIBLE_COUNT];
static ui_reading_card_refs_t s_reading_continue_card;
static lv_obj_t *s_reading_tab_buttons[UI_READING_TAB_COUNT];
static lv_obj_t *s_reading_status_label = NULL;
static lv_obj_t *s_reading_prev_button = NULL;
static lv_obj_t *s_reading_next_button = NULL;
static lv_obj_t *s_reading_page_label = NULL;
static lv_timer_t *s_reading_enter_refresh_timer = NULL;
static lv_timer_t *s_reading_cover_prompt_timer = NULL;
static lv_timer_t *s_reading_refresh_timer = NULL;
static lv_timer_t *s_reading_open_timer = NULL;
static uint16_t s_reading_file_count = 0;
static uint16_t s_reading_page_offset = 0;
static uint16_t s_reading_selected_index = 0;
static bool s_reading_has_selection = false;
static bool s_reading_open_detail_pending = false;
static bool s_reading_enter_refresh_requested = false;
static bool s_reading_cover_prompt_shown = false;
static volatile bool s_reading_cover_worker_running = false;
static volatile bool s_reading_cover_worker_done = false;
static volatile bool s_reading_cover_worker_cancel = false;
static volatile uint16_t s_reading_cover_worker_total = 0;
static volatile uint16_t s_reading_cover_worker_done_count = 0;
static ui_reading_scan_state_t s_reading_scan_state = UI_READING_SCAN_NO_CARD;
static ui_reading_tab_t s_reading_active_tab = UI_READING_TAB_ALL;
static lv_obj_t *s_reading_cover_prompt_overlay = NULL;
static lv_obj_t *s_reading_cover_progress_label = NULL;
static lv_obj_t *s_reading_cover_progress_fill = NULL;
static lv_timer_t *s_reading_cover_progress_timer = NULL;
static rt_thread_t s_reading_cover_thread = RT_NULL;
static uint16_t s_reading_cover_missing_count = 0;
static uint16_t s_reading_cover_last_done_count = 0;
static uint8_t s_reading_cover_finish_ticks = 0;
static char s_reading_mount_path[32];
static char s_reading_books_path[UI_READING_MAX_PATH_LEN];
static char s_reading_selected_name[UI_READING_MAX_NAME_LEN];
static char s_reading_selected_path[UI_READING_MAX_PATH_LEN];
static char s_reading_status_text[128];
static uint16_t s_reading_cover_missing_indices[UI_READING_MAX_FILES];
static char s_reading_cover_worker_paths[UI_READING_MAX_FILES][UI_READING_MAX_PATH_LEN];

static void ui_reading_list_render(void);
static void ui_reading_open_selected_detail(void);
static void ui_reading_cover_maybe_prompt(void);
static void ui_reading_cover_request_prompt_deferred(void);
static void ui_reading_cover_show_progress(uint16_t total);
static bool ui_reading_copy_path(char *buffer, size_t buffer_size, const char *path);

static uint16_t ui_reading_page_capacity(uint16_t page_offset)
{
    return page_offset == 0U ? UI_READING_FIRST_PAGE_COUNT : UI_READING_NEXT_PAGE_COUNT;
}

static void ui_reading_list_request_full_refresh(const char *reason)
{
    ui_epd_refresh_policy_request_image_refresh(reason);
}

static void ui_reading_enter_full_refresh_async_cb(void *user_data)
{
    LV_UNUSED(user_data);

    s_reading_enter_refresh_requested = false;
    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_list_request_full_refresh("enter_async");
    lv_obj_update_layout(ui_Reading_List);
    lv_obj_invalidate(ui_Reading_List);
}

static void ui_reading_request_enter_full_refresh_deferred(void)
{
    if (s_reading_enter_refresh_requested)
    {
        return;
    }

    s_reading_enter_refresh_requested = true;
    lv_async_call(ui_reading_enter_full_refresh_async_cb, NULL);
}

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

static lv_obj_t *ui_reading_plain_obj(lv_obj_t *parent,
                                      int x,
                                      int y,
                                      int w,
                                      int h,
                                      int radius,
                                      lv_opa_t bg_opa,
                                      uint32_t bg_color,
                                      int border_width)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, bg_opa, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, border_width, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    return obj;
}

static void ui_reading_make_touch_passthrough(lv_obj_t *obj)
{
    if (obj == NULL)
    {
        return;
    }

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
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
    ui_reading_tab_t tab;
    uint16_t file_count;
    char mount_path[sizeof(s_reading_mount_path)];
} ui_reading_snapshot_t;

static ui_reading_snapshot_t ui_reading_capture_snapshot(void)
{
    ui_reading_snapshot_t snapshot;

    snapshot.state = s_reading_scan_state;
    snapshot.tab = s_reading_active_tab;
    snapshot.file_count = s_reading_file_count;
    (void)ui_reading_copy_path(snapshot.mount_path, sizeof(snapshot.mount_path), s_reading_mount_path);
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
           before->tab != after->tab ||
           before->file_count != after->file_count ||
           strcmp(before->mount_path, after->mount_path) != 0;
}

static const char *ui_reading_tab_title(ui_reading_tab_t tab)
{
    switch (tab)
    {
    case UI_READING_TAB_RECENT:
        return ui_i18n_pick("最近阅读", "Recent");
    case UI_READING_TAB_FAVORITES:
        return ui_i18n_pick("我的收藏", "Favorites");
    case UI_READING_TAB_ALL:
    default:
        return ui_i18n_pick("全部图书", "All Books");
    }
}

static const char *ui_reading_path_basename(const char *path)
{
    const char *slash;

    if (path == NULL || path[0] == '\0')
    {
        return "";
    }

    slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static void ui_reading_update_tab_styles(void)
{
    uint16_t i;

    for (i = 0; i < UI_READING_TAB_COUNT; ++i)
    {
        lv_obj_t *button = s_reading_tab_buttons[i];
        lv_obj_t *label = NULL;
        bool active = (ui_reading_tab_t)i == s_reading_active_tab;

        if (button == NULL)
        {
            continue;
        }

        lv_obj_set_style_bg_color(button, lv_color_hex(active ? 0x000000 : 0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(button, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(button, lv_color_hex(active ? 0xFFFFFF : 0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        label = lv_obj_get_child(button, 0);
        if (label != NULL)
        {
            lv_obj_set_style_text_color(label, lv_color_hex(active ? 0xFFFFFF : 0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(label, lv_color_hex(active ? 0xFFFFFF : 0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }
}

static bool ui_reading_copy_path(char *buffer, size_t buffer_size, const char *path)
{
    size_t path_len;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (path == NULL)
    {
        buffer[0] = '\0';
        return true;
    }

    path_len = strlen(path);
    if (path_len >= buffer_size)
    {
        buffer[0] = '\0';
        return false;
    }

    memcpy(buffer, path, path_len + 1U);
    return true;
}

static bool ui_reading_join_path(char *buffer,
                                 size_t buffer_size,
                                 const char *base_path,
                                 const char *child_path)
{
    int written;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    if (base_path == NULL || base_path[0] == '\0' || child_path == NULL || child_path[0] == '\0')
    {
        written = rt_snprintf(buffer,
                              buffer_size,
                              "%s%s",
                              base_path != NULL ? base_path : "",
                              child_path != NULL ? child_path : "");
        if (written < 0 || (size_t)written >= buffer_size)
        {
            buffer[0] = '\0';
            return false;
        }
        return true;
    }

    written = rt_snprintf(buffer,
                          buffer_size,
                          "%s%s%s",
                          base_path,
                          strcmp(base_path, "/") == 0 ? "" : "/",
                          child_path);
    if (written < 0 || (size_t)written >= buffer_size)
    {
        buffer[0] = '\0';
        return false;
    }

    return true;
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

static void ui_reading_update_selected_cache(void)
{
    if (!s_reading_has_selection ||
        s_reading_selected_index >= s_reading_file_count)
    {
        s_reading_selected_name[0] = '\0';
        s_reading_selected_path[0] = '\0';
        return;
    }

    rt_snprintf(s_reading_selected_name,
                sizeof(s_reading_selected_name),
                "%s",
                s_reading_files[s_reading_selected_index].name);
    if (s_reading_files[s_reading_selected_index].path[0] != '\0')
    {
        (void)ui_reading_copy_path(s_reading_selected_path,
                                   sizeof(s_reading_selected_path),
                                   s_reading_files[s_reading_selected_index].path);
        return;
    }

    if (s_reading_books_path[0] == '\0')
    {
        s_reading_selected_path[0] = '\0';
        return;
    }

    (void)ui_reading_join_path(s_reading_selected_path,
                               sizeof(s_reading_selected_path),
                               s_reading_books_path,
                               s_reading_files[s_reading_selected_index].name);
}

static bool ui_reading_get_file_path(uint16_t file_index, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U || file_index >= s_reading_file_count)
    {
        return false;
    }

    if (s_reading_files[file_index].path[0] != '\0')
    {
        return ui_reading_copy_path(buffer, buffer_size, s_reading_files[file_index].path);
    }

    if (s_reading_books_path[0] == '\0')
    {
        buffer[0] = '\0';
        return false;
    }

    return ui_reading_join_path(buffer,
                                buffer_size,
                                s_reading_books_path,
                                s_reading_files[file_index].name);
}

static void ui_reading_format_progress(uint16_t file_index, char *buffer, size_t buffer_size)
{
    char path[UI_READING_MAX_PATH_LEN];
    reading_book_state_t state;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    buffer[0] = '\0';
    if (!ui_reading_get_file_path(file_index, path, sizeof(path)) ||
        !reading_state_get(path, &state) ||
        state.open_count == 0U)
    {
        rt_snprintf(buffer, buffer_size, "%s", ui_i18n_pick("未阅读", "Unread"));
        return;
    }

    if (s_reading_files[file_index].file_type == UI_READING_FILE_TYPE_EPUB ||
        state.type == READING_BOOK_TYPE_EPUB)
    {
        rt_snprintf(buffer,
                    buffer_size,
                    ui_i18n_pick("第%u章 第%u页", "Chapter %u Page %u"),
                    (unsigned int)state.chapter_index + 1U,
                    (unsigned int)state.page_index + 1U);
        return;
    }

    rt_snprintf(buffer,
                buffer_size,
                ui_i18n_pick("第%u页", "Page %u"),
                (unsigned int)state.page_index + 1U);
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
        return ui_reading_copy_path(mounted_path, mounted_path_size, mounted);
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
            return ui_reading_copy_path(mounted_path, mounted_path_size, candidate);
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
        int written;

        if (!ui_reading_is_listable_file(entry->d_name))
        {
            continue;
        }

        written = rt_snprintf(full_path,
                              sizeof(full_path),
                              "%s%s%s",
                              directory_path,
                              strcmp(directory_path, "/") == 0 ? "" : "/",
                              entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(full_path))
        {
            continue;
        }

        if (stat(full_path, &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode))
        {
            continue;
        }

        rt_snprintf(s_reading_files[s_reading_file_count].name,
                    sizeof(s_reading_files[s_reading_file_count].name),
                    "%s",
                    entry->d_name);
        if (!ui_reading_copy_path(s_reading_files[s_reading_file_count].path,
                                  sizeof(s_reading_files[s_reading_file_count].path),
                                  full_path))
        {
            continue;
        }
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

static void ui_reading_restore_selection(bool had_selection,
                                         const char *previous_path,
                                         const char *previous_name)
{
    uint16_t i;

    s_reading_has_selection = false;
    s_reading_selected_index = 0U;

    if (s_reading_file_count == 0U)
    {
        ui_reading_update_selected_cache();
        return;
    }

    if (had_selection && previous_path != NULL && previous_path[0] != '\0')
    {
        for (i = 0; i < s_reading_file_count; ++i)
        {
            if (strcmp(previous_path, s_reading_files[i].path) == 0)
            {
                s_reading_selected_index = i;
                s_reading_has_selection = true;
                ui_reading_update_selected_cache();
                return;
            }
        }
    }

    if (had_selection && previous_name != NULL && previous_name[0] != '\0')
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

    s_reading_selected_index = 0U;
    s_reading_has_selection = true;
    ui_reading_update_selected_cache();
}

static void ui_reading_refresh_all_files(void)
{
    bool had_device = false;

    if (!ui_reading_resolve_storage_root(s_reading_mount_path, sizeof(s_reading_mount_path), &had_device))
    {
        s_reading_scan_state = had_device ? UI_READING_SCAN_MOUNT_FAILED : UI_READING_SCAN_NO_CARD;
        return;
    }

    if (!ui_reading_join_path(s_reading_books_path,
                              sizeof(s_reading_books_path),
                              s_reading_mount_path,
                              UI_READING_BOOKS_DIRECTORY))
    {
        s_reading_scan_state = UI_READING_SCAN_MOUNT_FAILED;
        return;
    }

    if (!ui_reading_scan_directory(s_reading_books_path))
    {
        s_reading_scan_state = UI_READING_SCAN_MOUNT_FAILED;
        return;
    }

    s_reading_scan_state = s_reading_file_count > 0U ? UI_READING_SCAN_OK : UI_READING_SCAN_NO_FILES;
}

static bool ui_reading_add_state_record(const reading_book_state_t *record)
{
    const char *path;
    const char *name;
    struct stat stat_buffer;

    if (record == NULL || s_reading_file_count >= UI_READING_MAX_FILES)
    {
        return false;
    }

    path = record->path;
    name = ui_reading_path_basename(path);
    if (path == NULL || path[0] == '\0' || !ui_reading_is_listable_file(name))
    {
        return false;
    }

    if (stat(path, &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode))
    {
        return false;
    }

    rt_snprintf(s_reading_files[s_reading_file_count].name,
                sizeof(s_reading_files[s_reading_file_count].name),
                "%s",
                name);
    if (!ui_reading_copy_path(s_reading_files[s_reading_file_count].path,
                              sizeof(s_reading_files[s_reading_file_count].path),
                              path))
    {
        return false;
    }
    s_reading_files[s_reading_file_count].size_bytes = (uint32_t)stat_buffer.st_size;
    s_reading_files[s_reading_file_count].file_type = ui_reading_detect_file_type(name);
    ++s_reading_file_count;
    return true;
}

static void ui_reading_refresh_state_files(ui_reading_tab_t tab)
{
    reading_book_state_t records[UI_READING_MAX_FILES];
    size_t record_count;
    size_t i;

    memset(records, 0, sizeof(records));
    if (tab == UI_READING_TAB_FAVORITES)
    {
        record_count = reading_state_collect_favorites(records, UI_READING_MAX_FILES);
    }
    else
    {
        record_count = reading_state_collect_recent(records, UI_READING_MAX_FILES);
    }
    if (record_count > UI_READING_MAX_FILES)
    {
        record_count = UI_READING_MAX_FILES;
    }

    for (i = 0; i < record_count && s_reading_file_count < UI_READING_MAX_FILES; ++i)
    {
        ui_reading_add_state_record(&records[i]);
    }

    s_reading_scan_state = s_reading_file_count > 0U ? UI_READING_SCAN_OK : UI_READING_SCAN_NO_FILES;
}

static void ui_reading_refresh_files(bool reset_offset)
{
    char previous_name[UI_READING_MAX_NAME_LEN];
    char previous_path[UI_READING_MAX_PATH_LEN];
    bool had_selection;

    had_selection = s_reading_has_selection &&
                    (s_reading_selected_name[0] != '\0' || s_reading_selected_path[0] != '\0');
    rt_snprintf(previous_name, sizeof(previous_name), "%s", s_reading_selected_name);
    (void)ui_reading_copy_path(previous_path, sizeof(previous_path), s_reading_selected_path);

    memset(s_reading_files, 0, sizeof(s_reading_files));
    s_reading_file_count = 0;
    if (reset_offset)
    {
        s_reading_page_offset = 0;
    }
    s_reading_mount_path[0] = '\0';
    s_reading_books_path[0] = '\0';

    if (s_reading_active_tab == UI_READING_TAB_ALL)
    {
        ui_reading_refresh_all_files();
    }
    else
    {
        ui_reading_refresh_state_files(s_reading_active_tab);
    }

    if (s_reading_page_offset >= s_reading_file_count)
    {
        s_reading_page_offset = 0;
    }

    ui_reading_restore_selection(had_selection, previous_path, previous_name);
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
    ui_reading_refresh_files(false);
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

static void ui_reading_enter_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_reading_enter_refresh_timer != NULL)
    {
        lv_timer_delete(s_reading_enter_refresh_timer);
        s_reading_enter_refresh_timer = NULL;
    }

    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_refresh_files(true);
    ui_reading_list_render();
    ui_reading_request_enter_full_refresh_deferred();
    ui_reading_cover_request_prompt_deferred();
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

static uint16_t ui_reading_cover_collect_missing(void)
{
    uint16_t count = 0U;
    uint16_t i;

    memset(s_reading_cover_missing_indices, 0, sizeof(s_reading_cover_missing_indices));
    if (s_reading_scan_state != UI_READING_SCAN_OK)
    {
        return 0U;
    }

    for (i = 0U; i < s_reading_file_count && count < UI_READING_MAX_FILES; ++i)
    {
        reading_cover_cache_state_t state;

        if (s_reading_files[i].file_type != UI_READING_FILE_TYPE_EPUB ||
            s_reading_files[i].path[0] == '\0')
        {
            continue;
        }

        state = reading_cover_cache_get_state(s_reading_files[i].path,
                                              UI_READING_GRID_COVER_WIDTH,
                                              UI_READING_GRID_COVER_HEIGHT);
        if (state != READING_COVER_CACHE_UNKNOWN)
        {
            continue;
        }

        s_reading_cover_missing_indices[count] = i;
        ++count;
    }

    return count;
}

static void ui_reading_cover_prompt_close(void)
{
    if (s_reading_cover_prompt_overlay != NULL)
    {
        lv_obj_delete(s_reading_cover_prompt_overlay);
        s_reading_cover_prompt_overlay = NULL;
    }
    s_reading_cover_progress_label = NULL;
    s_reading_cover_progress_fill = NULL;
    s_reading_cover_finish_ticks = 0U;
}

static void ui_reading_cover_progress_update(void)
{
    uint16_t total = s_reading_cover_worker_total;
    uint16_t done = s_reading_cover_worker_done_count;
    char text[64];
    int width = 0;

    if (total == 0U)
    {
        total = s_reading_cover_missing_count;
    }
    if (total == 0U)
    {
        total = 1U;
    }
    if (done > total)
    {
        done = total;
    }

    if (s_reading_cover_progress_label != NULL)
    {
        if (s_reading_cover_worker_done && !s_reading_cover_worker_running)
        {
            rt_snprintf(text,
                        sizeof(text),
                        "封面生成完成 %u/%u",
                        (unsigned int)done,
                        (unsigned int)total);
        }
        else
        {
            rt_snprintf(text,
                        sizeof(text),
                        "正在生成封面 %u/%u",
                        (unsigned int)done,
                        (unsigned int)total);
        }
        lv_label_set_text(s_reading_cover_progress_label, text);
    }

    if (s_reading_cover_progress_fill != NULL)
    {
        width = (int)((336U * (uint32_t)done) / (uint32_t)total);
        if (done > 0U && width < 6)
        {
            width = 6;
        }
        lv_obj_set_width(s_reading_cover_progress_fill, ui_px_w(width));
    }
}

static void ui_reading_cover_worker_entry(void *parameter)
{
    uint16_t i;

    LV_UNUSED(parameter);
    s_reading_cover_worker_done = false;
    s_reading_cover_worker_done_count = 0U;

    for (i = 0U; i < s_reading_cover_worker_total && !s_reading_cover_worker_cancel; ++i)
    {
        const char *path = s_reading_cover_worker_paths[i];

        if (path[0] != '\0')
        {
            (void)reading_cover_cache_build(path,
                                            UI_READING_GRID_COVER_WIDTH,
                                            UI_READING_GRID_COVER_HEIGHT);
        }
        s_reading_cover_worker_done_count = (uint16_t)(i + 1U);
        rt_thread_mdelay(20);
    }

    s_reading_cover_worker_done = true;
    s_reading_cover_worker_running = false;
    s_reading_cover_worker_cancel = false;
    s_reading_cover_thread = RT_NULL;
}

static void ui_reading_cover_progress_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (ui_Reading_List == NULL)
    {
        return;
    }

    if (s_reading_cover_last_done_count != s_reading_cover_worker_done_count)
    {
        s_reading_cover_last_done_count = s_reading_cover_worker_done_count;
        ui_reading_cover_progress_update();
        ui_reading_list_render();
    }

    if (s_reading_cover_worker_done && !s_reading_cover_worker_running)
    {
        ui_reading_cover_progress_update();
        ui_reading_list_render();
        if (s_reading_cover_finish_ticks++ >= 2U)
        {
            ui_reading_cover_prompt_close();
            if (s_reading_cover_progress_timer != NULL)
            {
                lv_timer_delete(s_reading_cover_progress_timer);
                s_reading_cover_progress_timer = NULL;
            }
        }
    }
}

static void ui_reading_cover_start_worker(void)
{
    uint16_t i;
    uint16_t path_count = 0U;

    if (s_reading_cover_worker_running ||
        s_reading_cover_missing_count == 0U)
    {
        return;
    }

    memset(s_reading_cover_worker_paths, 0, sizeof(s_reading_cover_worker_paths));
    for (i = 0U; i < s_reading_cover_missing_count; ++i)
    {
        uint16_t file_index = s_reading_cover_missing_indices[i];

        if (file_index >= s_reading_file_count ||
            s_reading_files[file_index].file_type != UI_READING_FILE_TYPE_EPUB ||
            s_reading_files[file_index].path[0] == '\0')
        {
            continue;
        }

        if (ui_reading_copy_path(s_reading_cover_worker_paths[path_count],
                                 sizeof(s_reading_cover_worker_paths[path_count]),
                                 s_reading_files[file_index].path))
        {
            ++path_count;
        }
    }

    if (path_count == 0U)
    {
        s_reading_cover_worker_done = true;
        s_reading_cover_worker_done_count = 0U;
        return;
    }

    s_reading_cover_worker_total = path_count;
    s_reading_cover_worker_done_count = 0U;
    s_reading_cover_last_done_count = 0U;
    s_reading_cover_worker_done = false;
    s_reading_cover_worker_cancel = false;
    s_reading_cover_worker_running = true;
    s_reading_cover_thread = rt_thread_create("covgen",
                                              ui_reading_cover_worker_entry,
                                              NULL,
                                              8192,
                                              24,
                                              10);
    if (s_reading_cover_thread == RT_NULL)
    {
        s_reading_cover_worker_running = false;
        s_reading_cover_worker_done = true;
        s_reading_cover_worker_cancel = false;
        return;
    }

    rt_thread_startup(s_reading_cover_thread);
    if (s_reading_cover_progress_timer == NULL)
    {
        s_reading_cover_progress_timer = lv_timer_create(ui_reading_cover_progress_timer_cb, 500, NULL);
    }
}

static void ui_reading_cover_prompt_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    ui_reading_cover_prompt_close();
}

static void ui_reading_cover_prompt_generate_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    ui_reading_cover_prompt_close();
    ui_reading_cover_show_progress(s_reading_cover_missing_count);
    ui_reading_cover_start_worker();
    if (!s_reading_cover_worker_running &&
        s_reading_cover_worker_done_count == 0U)
    {
        ui_reading_cover_prompt_close();
        return;
    }
    ui_reading_cover_progress_update();
}

static void ui_reading_cover_show_progress(uint16_t total)
{
    lv_obj_t *panel;
    lv_obj_t *track;

    if (ui_Reading_List == NULL || total == 0U)
    {
        return;
    }

    ui_reading_cover_prompt_close();

    s_reading_cover_prompt_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_reading_cover_prompt_overlay);
    lv_obj_set_size(s_reading_cover_prompt_overlay, 528, 704);
    lv_obj_set_style_bg_color(s_reading_cover_prompt_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_reading_cover_prompt_overlay, LV_OPA_30, 0);
    lv_obj_add_flag(s_reading_cover_prompt_overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = ui_reading_plain_obj(s_reading_cover_prompt_overlay,
                                 54,
                                 220,
                                 420,
                                 220,
                                 14,
                                 LV_OPA_COVER,
                                 0xffffff,
                                 1);
    ui_create_label(panel,
                    "正在生成书籍封面",
                    0,
                    28,
                    420,
                    34,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    true,
                    false);
    s_reading_cover_progress_label = ui_create_label(panel,
                                                     "正在生成封面 0/0",
                                                     0,
                                                     76,
                                                     420,
                                                     30,
                                                     20,
                                                     LV_TEXT_ALIGN_CENTER,
                                                     false,
                                                     false);
    track = ui_reading_plain_obj(panel,
                                 42,
                                 122,
                                 336,
                                 18,
                                 9,
                                 LV_OPA_COVER,
                                 0xffffff,
                                 1);
    s_reading_cover_progress_fill = ui_reading_plain_obj(track,
                                                         0,
                                                         0,
                                                         0,
                                                         18,
                                                         9,
                                                         LV_OPA_COVER,
                                                         0x000000,
                                                         0);
    ui_create_label(panel,
                    "完成后会自动刷新书架",
                    0,
                    158,
                    420,
                    28,
                    18,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    s_reading_cover_worker_total = total;
    s_reading_cover_worker_done_count = 0U;
    s_reading_cover_finish_ticks = 0U;
    ui_reading_cover_progress_update();
}

static void ui_reading_cover_show_prompt(uint16_t missing_count)
{
    lv_obj_t *panel;
    lv_obj_t *button;
    char message[96];

    if (s_reading_cover_prompt_overlay != NULL ||
        ui_Reading_List == NULL ||
        missing_count == 0U)
    {
        return;
    }

    s_reading_cover_prompt_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_reading_cover_prompt_overlay);
    lv_obj_set_size(s_reading_cover_prompt_overlay, 528, 704);
    lv_obj_set_style_bg_color(s_reading_cover_prompt_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_reading_cover_prompt_overlay, LV_OPA_30, 0);
    lv_obj_add_flag(s_reading_cover_prompt_overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = ui_reading_plain_obj(s_reading_cover_prompt_overlay,
                                 54,
                                 220,
                                 420,
                                 220,
                                 14,
                                 LV_OPA_COVER,
                                 0xffffff,
                                 1);
    ui_create_label(panel,
                    "生成书籍封面",
                    0,
                    26,
                    420,
                    34,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    true,
                    false);
    rt_snprintf(message,
                sizeof(message),
                "发现 %u 本图书还没有封面缓存\n生成后书架加载会更快",
                (unsigned int)missing_count);
    ui_create_label(panel,
                    message,
                    32,
                    74,
                    356,
                    64,
                    19,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);

    button = ui_reading_plain_obj(panel, 42, 156, 138, 44, 10, LV_OPA_COVER, 0xffffff, 1);
    ui_create_label(button, "稍后", 0, 8, 138, 28, 20, LV_TEXT_ALIGN_CENTER, false, false);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, ui_reading_cover_prompt_cancel_cb, LV_EVENT_CLICKED, NULL);

    button = ui_reading_plain_obj(panel, 240, 156, 138, 44, 10, LV_OPA_COVER, 0x000000, 0);
    lv_obj_t *label = ui_create_label(button, "生成", 0, 8, 138, 28, 20, LV_TEXT_ALIGN_CENTER, true, true);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, ui_reading_cover_prompt_generate_cb, LV_EVENT_CLICKED, NULL);
}

static void ui_reading_cover_maybe_prompt(void)
{
    if (s_reading_cover_prompt_shown ||
        s_reading_cover_worker_running ||
        ui_Reading_List == NULL)
    {
        return;
    }

    s_reading_cover_missing_count = ui_reading_cover_collect_missing();
    if (s_reading_cover_missing_count == 0U)
    {
        return;
    }

    s_reading_cover_prompt_shown = true;
    ui_reading_cover_show_prompt(s_reading_cover_missing_count);
}

static void ui_reading_cover_prompt_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_reading_cover_prompt_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_prompt_timer);
        s_reading_cover_prompt_timer = NULL;
    }

    ui_reading_cover_maybe_prompt();
}

static void ui_reading_cover_request_prompt_deferred(void)
{
    if (s_reading_cover_prompt_timer != NULL ||
        s_reading_cover_prompt_shown ||
        s_reading_cover_worker_running ||
        ui_Reading_List == NULL)
    {
        return;
    }

    s_reading_cover_prompt_timer = lv_timer_create(ui_reading_cover_prompt_timer_cb, 300, NULL);
}

static void ui_reading_tab_event_cb(lv_event_t *e)
{
    ui_reading_tab_t tab;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    tab = (ui_reading_tab_t)(uintptr_t)lv_event_get_user_data(e);
    if (tab >= UI_READING_TAB_COUNT || tab == s_reading_active_tab)
    {
        return;
    }

    s_reading_active_tab = tab;
    s_reading_page_offset = 0;
    s_reading_open_detail_pending = false;
    s_reading_cover_worker_cancel = true;
    ui_reading_refresh_files(true);
    ui_reading_list_render();
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

static void ui_reading_release_card_cover(ui_reading_card_refs_t *refs)
{
    if (refs == NULL)
    {
        return;
    }

    if (refs->cover_loaded)
    {
        reading_cover_cache_release_image(&refs->cover_dsc);
        memset(&refs->cover_dsc, 0, sizeof(refs->cover_dsc));
        refs->cover_loaded = false;
    }
    refs->cover_path[0] = '\0';
}

static bool ui_reading_try_load_epub_cover(uint16_t file_index, ui_reading_card_refs_t *refs, int cover_w, int cover_h)
{
    char path[UI_READING_MAX_PATH_LEN];

    if (refs == NULL || file_index >= s_reading_file_count ||
        s_reading_files[file_index].file_type != UI_READING_FILE_TYPE_EPUB)
    {
        return false;
    }

    if (!ui_reading_get_file_path(file_index, path, sizeof(path)))
    {
        return false;
    }

    if (refs->cover_loaded && strcmp(refs->cover_path, path) == 0)
    {
        return true;
    }

    ui_reading_release_card_cover(refs);

    if (!reading_cover_cache_load_image(path,
                                        (uint16_t)cover_w,
                                        (uint16_t)cover_h,
                                        &refs->cover_dsc))
    {
        return false;
    }

    refs->cover_loaded = true;
    if (!ui_reading_copy_path(refs->cover_path, sizeof(refs->cover_path), path))
    {
        ui_reading_release_card_cover(refs);
        return false;
    }

    return true;
}

static void ui_reading_update_cover(ui_reading_card_refs_t *refs, uint16_t file_index, int cover_w, int cover_h)
{
    bool has_epub_cover;

    if (refs == NULL || refs->cover_box == NULL || refs->cover_title_label == NULL)
    {
        return;
    }

    has_epub_cover = ui_reading_try_load_epub_cover(file_index, refs, cover_w, cover_h);
    if (has_epub_cover && refs->cover_img != NULL)
    {
        lv_obj_clear_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(refs->cover_img, &refs->cover_dsc);
        lv_obj_add_flag(refs->cover_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(refs->cover_box, 0, LV_PART_MAIN);
        return;
    }

    if (refs->cover_img != NULL)
    {
        lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(refs->cover_img, NULL);
    }
    lv_obj_clear_flag(refs->cover_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(refs->cover_box, 1, LV_PART_MAIN);
    ui_reading_set_label_text_static(refs->cover_title_label,
                                     refs->cover_title_text,
                                     sizeof(refs->cover_title_text),
                                     s_reading_files[file_index].name);
}

static void ui_reading_show_card(uint16_t slot_index, uint16_t file_index, bool clickable)
{
    ui_reading_card_refs_t *refs;
    char progress_text[64];

    if (slot_index >= UI_READING_VISIBLE_COUNT || file_index >= s_reading_file_count)
    {
        return;
    }

    refs = &s_reading_cards[slot_index];
    if (refs->card == NULL || refs->title_label == NULL || refs->meta_label == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    ui_reading_update_cover(refs, file_index, UI_READING_GRID_COVER_WIDTH, UI_READING_GRID_COVER_HEIGHT);
    ui_reading_format_progress(file_index, progress_text, sizeof(progress_text));
    ui_reading_set_label_text_static(refs->title_label,
                                     refs->title_text,
                                     sizeof(refs->title_text),
                                     s_reading_files[file_index].name);
    ui_reading_set_label_text_static(refs->meta_label,
                                     refs->meta_text,
                                     sizeof(refs->meta_text),
                                     progress_text);

    if (clickable)
    {
        lv_obj_clear_state(refs->card, LV_STATE_DISABLED);
        lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(refs->card, LV_OPA_COVER, LV_PART_MAIN);
    }
    else
    {
        lv_obj_add_state(refs->card, LV_STATE_DISABLED);
        lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
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

static void ui_reading_show_continue_card(uint16_t file_index)
{
    char progress_text[64];

    if (s_reading_continue_card.card == NULL || file_index >= s_reading_file_count)
    {
        return;
    }

    lv_obj_clear_flag(s_reading_continue_card.card, LV_OBJ_FLAG_HIDDEN);
    ui_reading_update_cover(&s_reading_continue_card, file_index, UI_READING_GRID_COVER_WIDTH, UI_READING_GRID_COVER_HEIGHT);
    ui_reading_format_progress(file_index, progress_text, sizeof(progress_text));
    ui_reading_set_label_text_static(s_reading_continue_card.title_label,
                                     s_reading_continue_card.title_text,
                                     sizeof(s_reading_continue_card.title_text),
                                     s_reading_files[file_index].name);
    ui_reading_set_label_text_static(s_reading_continue_card.meta_label,
                                     s_reading_continue_card.meta_text,
                                     sizeof(s_reading_continue_card.meta_text),
                                     progress_text);
}

static void ui_reading_hide_continue_card(void)
{
    if (s_reading_continue_card.card != NULL)
    {
        lv_obj_add_flag(s_reading_continue_card.card, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_reading_prev_event_cb(lv_event_t *e)
{
    uint16_t previous_offset = s_reading_page_offset;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (s_reading_page_offset <= UI_READING_FIRST_PAGE_COUNT)
    {
        s_reading_page_offset = 0;
    }
    else
    {
        s_reading_page_offset = (uint16_t)(s_reading_page_offset - UI_READING_NEXT_PAGE_COUNT);
        if (s_reading_page_offset < UI_READING_FIRST_PAGE_COUNT)
        {
            s_reading_page_offset = UI_READING_FIRST_PAGE_COUNT;
        }
    }

    if (s_reading_page_offset != previous_offset)
    {
        ui_reading_list_request_full_refresh("prev_page");
    }
    ui_reading_list_render();
}

static void ui_reading_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((uint16_t)(s_reading_page_offset + ui_reading_page_capacity(s_reading_page_offset)) < s_reading_file_count)
    {
        s_reading_page_offset = (uint16_t)(s_reading_page_offset + ui_reading_page_capacity(s_reading_page_offset));
        ui_reading_list_request_full_refresh("next_page");
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

    if (s_reading_cover_prompt_overlay != NULL && !s_reading_cover_worker_running)
    {
        ui_reading_cover_prompt_close();
    }
    s_reading_selected_index = file_index;
    s_reading_has_selection = true;
    ui_reading_update_selected_cache();
    lv_display_trigger_activity(NULL);
    s_reading_open_detail_pending = false;
    ui_reading_open_selected_detail();
}

static void ui_reading_continue_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_reading_file_count == 0U)
    {
        return;
    }

    if (s_reading_cover_prompt_overlay != NULL && !s_reading_cover_worker_running)
    {
        ui_reading_cover_prompt_close();
    }
    s_reading_selected_index = 0U;
    s_reading_has_selection = true;
    ui_reading_update_selected_cache();
    lv_display_trigger_activity(NULL);
    s_reading_open_detail_pending = false;
    ui_reading_open_selected_detail();
}

static void ui_reading_create_card(lv_obj_t *parent, uint16_t slot_index, int x, int y)
{
    ui_reading_card_refs_t *refs = &s_reading_cards[slot_index];

    refs->card = ui_reading_plain_obj(parent, x, y, UI_READING_GRID_COVER_WIDTH, 187, 0, LV_OPA_TRANSP, 0xffffff, 0);
    refs->cover_box = ui_reading_plain_obj(refs->card, 0, 0, UI_READING_GRID_COVER_WIDTH, UI_READING_GRID_COVER_HEIGHT, 5, LV_OPA_COVER, 0xffffff, 1);
    ui_reading_make_touch_passthrough(refs->cover_box);
    refs->cover_img = ui_create_image_slot(refs->cover_box, 0, 0, UI_READING_GRID_COVER_WIDTH, UI_READING_GRID_COVER_HEIGHT);
    ui_reading_make_touch_passthrough(refs->cover_img);
    refs->cover_title_label = ui_create_label(refs->cover_box,
                                              "",
                                              8,
                                              16,
                                              UI_READING_GRID_COVER_WIDTH - 16,
                                              UI_READING_GRID_COVER_HEIGHT - 24,
                                              13,
                                              LV_TEXT_ALIGN_CENTER,
                                              false,
                                              false);
    ui_reading_make_touch_passthrough(refs->cover_title_label);
    lv_label_set_long_mode(refs->cover_title_label, LV_LABEL_LONG_WRAP);
    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        0,
                                        UI_READING_GRID_COVER_HEIGHT + 4,
                                        UI_READING_GRID_COVER_WIDTH,
                                        20,
                                        16,
                                        LV_TEXT_ALIGN_LEFT,
                                        false,
                                        false);
    ui_reading_make_touch_passthrough(refs->title_label);
    lv_label_set_long_mode(refs->title_label, LV_LABEL_LONG_DOT);
    refs->meta_label = ui_create_label(refs->card,
                                       "",
                                       0,
                                       UI_READING_GRID_COVER_HEIGHT + 25,
                                       UI_READING_GRID_COVER_WIDTH,
                                       18,
                                       14,
                                       LV_TEXT_ALIGN_LEFT,
                                       false,
                                       false);
    ui_reading_make_touch_passthrough(refs->meta_label);
    lv_label_set_long_mode(refs->meta_label, LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(refs->card,
                        ui_reading_card_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)slot_index);
}

static void ui_reading_create_continue_card(lv_obj_t *parent)
{
    ui_reading_card_refs_t *refs = &s_reading_continue_card;

    refs->card = ui_reading_plain_obj(parent,
                                      UI_READING_CONTINUE_CARD_X,
                                      UI_READING_CONTINUE_CARD_Y,
                                      UI_READING_CONTINUE_CARD_WIDTH,
                                      UI_READING_CONTINUE_CARD_HEIGHT,
                                      8,
                                      LV_OPA_COVER,
                                      0xffffff,
                                      1);
    refs->cover_box = ui_reading_plain_obj(refs->card, 17, 20, 126, 138, 5, LV_OPA_COVER, 0xffffff, 1);
    ui_reading_make_touch_passthrough(refs->cover_box);
    refs->cover_img = ui_create_image_slot(refs->cover_box, 0, 0, 126, 138);
    ui_reading_make_touch_passthrough(refs->cover_img);
    refs->cover_title_label = ui_create_label(refs->cover_box, "", 8, 18, 110, 100, 13, LV_TEXT_ALIGN_CENTER, false, false);
    ui_reading_make_touch_passthrough(refs->cover_title_label);
    lv_label_set_long_mode(refs->cover_title_label, LV_LABEL_LONG_WRAP);
    lv_obj_t *badge = ui_reading_plain_obj(refs->card, 170, 32, 78, 28, 12, LV_OPA_COVER, 0x000000, 0);
    ui_reading_make_touch_passthrough(badge);
    lv_obj_t *badge_label = ui_create_label(badge, "继续阅读", 0, 2, 78, 24, 17, LV_TEXT_ALIGN_CENTER, false, true);
    ui_reading_make_touch_passthrough(badge_label);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    refs->title_label = ui_create_label(refs->card, "", 170, 78, 250, 36, 22, LV_TEXT_ALIGN_LEFT, true, false);
    ui_reading_make_touch_passthrough(refs->title_label);
    lv_label_set_long_mode(refs->title_label, LV_LABEL_LONG_DOT);
    refs->meta_label = ui_create_label(refs->card, "", 170, 124, 250, 24, 18, LV_TEXT_ALIGN_LEFT, false, false);
    ui_reading_make_touch_passthrough(refs->meta_label);
    lv_label_set_long_mode(refs->meta_label, LV_LABEL_LONG_DOT);
    ui_reading_make_touch_passthrough(ui_create_label(refs->card, ">", 438, 65, 30, 55, 36, LV_TEXT_ALIGN_CENTER, true, false));
    lv_obj_add_flag(refs->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(refs->card, ui_reading_continue_event_cb, LV_EVENT_CLICKED, NULL);
}

static void ui_reading_create_tab_button(lv_obj_t *parent, ui_reading_tab_t tab)
{
    s_reading_tab_buttons[tab] = ui_create_button(parent,
                                                  UI_READING_TAB_X + (int)tab * UI_READING_TAB_WIDTH,
                                                  UI_READING_TAB_Y,
                                                  UI_READING_TAB_WIDTH,
                                                  UI_READING_TAB_HEIGHT,
                                                  ui_reading_tab_title(tab),
                                                  22,
                                                  UI_SCREEN_NONE,
                                                  false);
    lv_obj_add_event_cb(s_reading_tab_buttons[tab],
                        ui_reading_tab_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)tab);
}

static void ui_reading_list_render(void)
{
    uint16_t visible_index;
    char status_text[128];
    char count_text[64];
    bool can_prev;
    bool can_next;
    bool first_page;
    uint16_t page_capacity;
    const int *grid_y_positions;

    first_page = (s_reading_page_offset == 0U);
    page_capacity = ui_reading_page_capacity(s_reading_page_offset);
    grid_y_positions = first_page ? s_grid_first_page_y_positions : s_grid_next_page_y_positions;

    can_prev = s_reading_page_offset > 0U;
    can_next = (uint16_t)(s_reading_page_offset + page_capacity) < s_reading_file_count;
    ui_reading_set_button_enabled(s_reading_prev_button, can_prev);
    ui_reading_set_button_enabled(s_reading_next_button, can_next);

    if (s_reading_page_label != NULL)
    {
        if (first_page)
        {
            lv_obj_clear_flag(s_reading_page_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_reading_page_label,
                           ui_px_x(28),
                           ui_px_y(UI_READING_FIRST_PAGE_TITLE_Y));
            rt_snprintf(count_text,
                        sizeof(count_text),
                        ui_i18n_pick("全部书籍（%u）", "All Books (%u)"),
                        (unsigned int)s_reading_file_count);
            lv_label_set_text(s_reading_page_label, count_text);
        }
        else
        {
            lv_obj_add_flag(s_reading_page_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_reading_page_label, "");
        }
    }

    if (s_reading_prev_button != NULL)
    {
        lv_obj_set_pos(s_reading_prev_button,
                       ui_px_x(0),
                       ui_px_y(first_page ? UI_READING_FIRST_PAGE_NAV_Y : UI_READING_NEXT_PAGE_NAV_Y));
    }
    if (s_reading_next_button != NULL)
    {
        lv_obj_set_pos(s_reading_next_button,
                       ui_px_x(405),
                       ui_px_y(first_page ? UI_READING_FIRST_PAGE_NAV_Y : UI_READING_NEXT_PAGE_NAV_Y));
    }

    if (s_reading_scan_state == UI_READING_SCAN_OK)
    {
        ui_reading_set_label_text_static(s_reading_status_label,
                                         s_reading_status_text,
                                         sizeof(s_reading_status_text),
                                         "");
        if (first_page)
        {
            ui_reading_show_continue_card(0U);
        }
        else
        {
            ui_reading_hide_continue_card();
        }

        for (visible_index = 0; visible_index < UI_READING_VISIBLE_COUNT; ++visible_index)
        {
            uint16_t file_index = (uint16_t)(s_reading_page_offset + visible_index);
            if (s_reading_cards[visible_index].card != NULL)
            {
                lv_obj_set_pos(s_reading_cards[visible_index].card,
                               ui_px_x(s_grid_x_positions[visible_index]),
                               ui_px_y(grid_y_positions[visible_index]));
            }
            if (visible_index >= page_capacity || file_index >= s_reading_file_count)
            {
                ui_reading_hide_card(visible_index);
                continue;
            }
            ui_reading_show_card(visible_index, file_index, true);
        }
        return;
    }

    ui_reading_hide_continue_card();
    for (visible_index = 0; visible_index < UI_READING_VISIBLE_COUNT; ++visible_index)
    {
        ui_reading_hide_card(visible_index);
    }

    if (s_reading_scan_state == UI_READING_SCAN_LOADING)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s",
                    ui_i18n_pick("正在加载书架...", "Loading bookshelf..."));
    }
    else if (s_reading_scan_state == UI_READING_SCAN_NO_FILES)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s",
                    ui_i18n_pick("TF 卡 /books 暂无可阅读文件", "No readable files in /books"));
    }
    else if (s_reading_scan_state == UI_READING_SCAN_MOUNT_FAILED)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s",
                    ui_i18n_pick("TF 卡挂载失败", "TF card mount failed"));
    }
    else
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s",
                    ui_i18n_pick("未检测到 TF 卡", "No TF card detected"));
    }

    ui_reading_set_label_text_static(s_reading_status_label,
                                     s_reading_status_text,
                                     sizeof(s_reading_status_text),
                                     status_text);
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

    ui_reading_refresh_files(true);

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

    return ui_reading_copy_path(buffer, buffer_size, s_reading_selected_path);
}

void ui_Reading_List_screen_init(void)
{
    uint16_t i;

    if (ui_Reading_List != NULL)
    {
        return;
    }

    memset(&s_reading_continue_card, 0, sizeof(s_reading_continue_card));
    memset(s_reading_cards, 0, sizeof(s_reading_cards));
    memset(s_reading_tab_buttons, 0, sizeof(s_reading_tab_buttons));
    s_reading_active_tab = UI_READING_TAB_ALL;
    s_reading_scan_state = UI_READING_SCAN_LOADING;
    s_reading_file_count = 0U;
    s_reading_page_offset = 0U;

    ui_Reading_List = ui_create_screen_base();
    ui_reading_list_request_full_refresh("enter");
    ui_top_nav_create(ui_Reading_List, UI_TOP_TAB_NONE);
    ui_bottom_nav_create(ui_Reading_List, UI_BOTTOM_TAB_BOOKS);

    s_reading_status_label = NULL;
    s_reading_status_text[0] = '\0';

    ui_create_label(ui_Reading_List,
                    ui_i18n_pick("我的书架", "My Bookshelf"),
                    28,
                    82,
                    220,
                    48,
                    29,
                    LV_TEXT_ALIGN_LEFT,
                    true,
                    false);
    ui_reading_create_continue_card(ui_Reading_List);

    s_reading_page_label = ui_create_label(ui_Reading_List,
                                           "全部书籍（0）",
                                           28,
                                           319,
                                           260,
                                           36,
                                           25,
                                           LV_TEXT_ALIGN_LEFT,
                                           true,
                                           false);
    lv_label_set_long_mode(s_reading_page_label, LV_LABEL_LONG_DOT);

    for (i = 0; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        ui_reading_create_card(ui_Reading_List, i, s_grid_x_positions[i], s_grid_first_page_y_positions[i]);
    }

    s_reading_prev_button = ui_reading_plain_obj(ui_Reading_List, 0, 314, 95, 48, 0, LV_OPA_TRANSP, 0xffffff, 0);
    s_reading_next_button = ui_reading_plain_obj(ui_Reading_List, 405, 314, 120, 48, 0, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_event_cb(s_reading_prev_button, ui_reading_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_reading_next_button, ui_reading_next_event_cb, LV_EVENT_CLICKED, NULL);

    s_reading_status_label = ui_create_label(ui_Reading_List,
                                             "",
                                             28,
                                             365,
                                             472,
                                             90,
                                             20,
                                             LV_TEXT_ALIGN_CENTER,
                                             false,
                                             false);

    ui_reading_hide_continue_card();
    for (i = 0; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        ui_reading_hide_card(i);
    }
    ui_reading_set_label_text_static(s_reading_status_label,
                                     s_reading_status_text,
                                     sizeof(s_reading_status_text),
                                     ui_i18n_pick("正在加载书架...", "Loading bookshelf..."));
    ui_reading_list_render();
    ui_reading_request_enter_full_refresh_deferred();

    if (s_reading_enter_refresh_timer == NULL)
    {
        s_reading_enter_refresh_timer = lv_timer_create(ui_reading_enter_refresh_timer_cb, 80, NULL);
    }

    if (s_reading_cover_worker_running && s_reading_cover_progress_timer == NULL)
    {
        s_reading_cover_progress_timer = lv_timer_create(ui_reading_cover_progress_timer_cb, 500, NULL);
    }

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
    uint16_t i;

    s_reading_cover_worker_cancel = true;
    s_reading_enter_refresh_requested = false;

    if (s_reading_enter_refresh_timer != NULL)
    {
        lv_timer_delete(s_reading_enter_refresh_timer);
        s_reading_enter_refresh_timer = NULL;
    }

    if (s_reading_cover_prompt_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_prompt_timer);
        s_reading_cover_prompt_timer = NULL;
    }

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
    if (s_reading_cover_progress_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_progress_timer);
        s_reading_cover_progress_timer = NULL;
    }

    ui_reading_cover_prompt_close();

    ui_reading_release_card_cover(&s_reading_continue_card);
    for (i = 0; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        ui_reading_release_card_cover(&s_reading_cards[i]);
    }

    if (ui_Reading_List != NULL)
    {
        lv_obj_delete(ui_Reading_List);
        ui_Reading_List = NULL;
    }

    memset(&s_reading_continue_card, 0, sizeof(s_reading_continue_card));
    memset(s_reading_cards, 0, sizeof(s_reading_cards));
    memset(s_reading_tab_buttons, 0, sizeof(s_reading_tab_buttons));
    s_reading_status_label = NULL;
    s_reading_prev_button = NULL;
    s_reading_next_button = NULL;
    s_reading_page_label = NULL;
    s_reading_open_detail_pending = false;
    s_reading_enter_refresh_requested = false;
}

void ui_reading_list_refresh(void)
{
    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_refresh_files(true);
    ui_reading_list_render();
}

void ui_reading_list_request_enter_refresh(void)
{
    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_request_enter_full_refresh_deferred();
}
