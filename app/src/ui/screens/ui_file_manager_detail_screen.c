#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "../../app_tf_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "app_watchdog.h"
#include "dfs_posix.h"
#include "rtthread.h"

lv_obj_t *ui_File_Manager_Detail = NULL;

#define UI_FILE_MANAGER_DETAIL_MAX_FILES 48U
#define UI_FILE_MANAGER_DETAIL_MAX_NAME  96U
#define UI_FILE_MANAGER_DETAIL_MAX_PATH  160U
#define UI_FILE_MANAGER_DETAIL_VISIBLE   6U
#define UI_FILE_MANAGER_DETAIL_NAV_GAP_MS 1200U

typedef struct
{
    char name[UI_FILE_MANAGER_DETAIL_MAX_NAME];
} ui_file_manager_detail_file_t;

typedef struct
{
    const char *title_zh;
    const char *title_en;
    const char *empty_zh;
    const char *empty_en;
    const char *subdir;
} ui_file_manager_detail_category_t;

typedef struct
{
    ui_file_manager_detail_file_t files[UI_FILE_MANAGER_DETAIL_MAX_FILES];
    size_t file_count;
} ui_file_manager_detail_state_t;

typedef struct
{
    lv_obj_t *list_panel;
    lv_obj_t *page_label;
    lv_obj_t *rows[UI_FILE_MANAGER_DETAIL_VISIBLE];
    lv_obj_t *labels[UI_FILE_MANAGER_DETAIL_VISIBLE];
    lv_obj_t *icons[UI_FILE_MANAGER_DETAIL_VISIBLE];
} ui_file_manager_detail_refs_t;

static const ui_file_manager_detail_category_t s_detail_categories[] = {
    {"字体文件", "Font Files", "未找到字体文件", "No font files", "font"},
    {"音乐文件", "Music Files", "未找到音乐文件", "No music files", "mp3"},
    {"录音文件", "Record Files", "未找到录音文件", "No record files", "record"},
    {"阅读文件", "Book Files", "未找到阅读文件", "No book files", "books"},
};

static ui_file_manager_category_t s_detail_category = UI_FILE_MANAGER_CATEGORY_FONT;
static ui_file_manager_detail_state_t s_detail_state;
static ui_file_manager_detail_refs_t s_detail_refs;
static size_t s_visible_indices[UI_FILE_MANAGER_DETAIL_VISIBLE];
static size_t s_page_start[UI_FILE_MANAGER_CATEGORY_COUNT];
static bool s_selected_files[UI_FILE_MANAGER_CATEGORY_COUNT][UI_FILE_MANAGER_DETAIL_MAX_FILES];
static rt_tick_t s_last_page_nav_tick = 0;

static lv_obj_t *ui_file_manager_detail_plain_obj(lv_obj_t *parent,
                                                  int x,
                                                  int y,
                                                  int w,
                                                  int h,
                                                  int radius,
                                                  lv_opa_t opa,
                                                  uint32_t bg,
                                                  int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static bool ui_file_manager_detail_join_path(char *buffer,
                                             size_t buffer_size,
                                             const char *dir,
                                             const char *name)
{
    int written;

    if (buffer == NULL || buffer_size == 0U || dir == NULL || name == NULL)
    {
        return false;
    }

    if (strcmp(dir, "/") == 0)
    {
        written = rt_snprintf(buffer, buffer_size, "/%s", name);
    }
    else
    {
        written = rt_snprintf(buffer, buffer_size, "%s/%s", dir, name);
    }

    if (written < 0 || (size_t)written >= buffer_size)
    {
        buffer[0] = '\0';
        return false;
    }

    return true;
}

static bool ui_file_manager_detail_is_hidden(const char *name)
{
    return name == NULL || name[0] == '\0' || name[0] == '.';
}

static bool ui_file_manager_detail_scan_dir(const char *dir)
{
    DIR *handle;
    struct dirent *entry;
    uint32_t scan_tick = 0U;

    if (dir == NULL)
    {
        return false;
    }

    handle = opendir(dir);
    if (handle == NULL)
    {
        return false;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    while ((entry = readdir(handle)) != NULL &&
           s_detail_state.file_count < UI_FILE_MANAGER_DETAIL_MAX_FILES)
    {
        ui_file_manager_detail_file_t *file;
        struct stat st;
        char path[UI_FILE_MANAGER_DETAIL_MAX_PATH];

        if ((scan_tick++ & 0x0FU) == 0U)
        {
            app_watchdog_progress(APP_WDT_MODULE_UI);
        }

        if (ui_file_manager_detail_is_hidden(entry->d_name) ||
            !ui_file_manager_detail_join_path(path, sizeof(path), dir, entry->d_name) ||
            stat(path, &st) != 0 ||
            !S_ISREG(st.st_mode))
        {
            continue;
        }

        file = &s_detail_state.files[s_detail_state.file_count];
        rt_snprintf(file->name, sizeof(file->name), "%s", entry->d_name);
        s_detail_state.file_count++;
        app_watchdog_progress(APP_WDT_MODULE_UI);
    }

    closedir(handle);
    app_watchdog_progress(APP_WDT_MODULE_UI);
    return true;
}

static void ui_file_manager_detail_scan_files(const ui_file_manager_detail_category_t *category)
{
    char dir[UI_FILE_MANAGER_DETAIL_MAX_PATH];

    memset(&s_detail_state, 0, sizeof(s_detail_state));
    if (category == NULL)
    {
        return;
    }

    if (!app_tf_storage_ready() ||
        !app_tf_build_path(category->subdir, dir, sizeof(dir)))
    {
        return;
    }

    (void)ui_file_manager_detail_scan_dir(dir);
}

static void ui_file_manager_detail_format_name(char *buffer,
                                               size_t buffer_size,
                                               const char *name)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (name == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    if (s_detail_category == UI_FILE_MANAGER_CATEGORY_FONT)
    {
        rt_snprintf(buffer, buffer_size, "Aa   %s", name);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s", name);
    }
}

static void ui_file_manager_detail_set_icon_selected(lv_obj_t *icon, bool selected)
{
    if (icon == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(icon, lv_color_hex(0x343434), 0);
    lv_obj_set_style_bg_opa(icon, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(icon, lv_color_hex(0x343434), 0);
    lv_obj_set_style_border_width(icon, 3, 0);
}

static void ui_file_manager_detail_refresh_selected_icons(void)
{
    size_t i;

    for (i = 0; i < UI_FILE_MANAGER_DETAIL_VISIBLE; ++i)
    {
        bool selected = false;
        if (s_detail_category < UI_FILE_MANAGER_CATEGORY_COUNT &&
            s_visible_indices[i] < UI_FILE_MANAGER_DETAIL_MAX_FILES)
        {
            selected = s_selected_files[s_detail_category][s_visible_indices[i]];
        }
        ui_file_manager_detail_set_icon_selected(s_detail_refs.icons[i],
                                                 selected);
    }
}

static void ui_file_manager_detail_row_event_cb(lv_event_t *e)
{
    uintptr_t index;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    index = (uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (index >= s_detail_state.file_count || s_detail_category >= UI_FILE_MANAGER_CATEGORY_COUNT)
    {
        return;
    }

    s_selected_files[s_detail_category][index] = !s_selected_files[s_detail_category][index];
    ui_file_manager_detail_refresh_selected_icons();
}

static lv_obj_t *ui_file_manager_detail_create_select_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *circle = ui_file_manager_detail_plain_obj(parent, x, y, 30, 30, 15, LV_OPA_TRANSP, 0xffffff, 3);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    return circle;
}

static void ui_file_manager_detail_create_row_slot(lv_obj_t *parent, size_t visible_index, int y)
{
    lv_obj_t *row;
    lv_obj_t *label;

    row = ui_file_manager_detail_plain_obj(parent, 0, y, 528, 88, 0, LV_OPA_TRANSP, 0xffffff, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, ui_file_manager_detail_row_event_cb, LV_EVENT_CLICKED, NULL);

    label = ui_create_label(row,
                            "",
                            32,
                            28,
                            390,
                            31,
                            26,
                            LV_TEXT_ALIGN_LEFT,
                            false,
                            false);
    lv_obj_set_style_text_color(label, lv_color_hex(0x343434), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    s_detail_refs.rows[visible_index] = row;
    s_detail_refs.labels[visible_index] = label;
    s_detail_refs.icons[visible_index] = ui_file_manager_detail_create_select_icon(row, 462, 28);
}

static void ui_file_manager_detail_create_row_slots(lv_obj_t *parent)
{
    size_t i;
    static const int row_y[] = {2, 91, 180, 268, 357, 446};

    for (i = 0; i < UI_FILE_MANAGER_DETAIL_VISIBLE; ++i)
    {
        ui_file_manager_detail_create_row_slot(parent, i, row_y[i]);
    }
}

static size_t ui_file_manager_detail_total_pages(void)
{
    if (s_detail_state.file_count == 0U)
    {
        return 0U;
    }

    return (s_detail_state.file_count + UI_FILE_MANAGER_DETAIL_VISIBLE - 1U) / UI_FILE_MANAGER_DETAIL_VISIBLE;
}

static void ui_file_manager_detail_update_page_label(void)
{
    char text[32];
    size_t total_pages = ui_file_manager_detail_total_pages();

    if (s_detail_refs.page_label == NULL)
    {
        return;
    }

    rt_snprintf(text,
                sizeof(text),
                ui_i18n_pick("总计：%u页", "Total: %u pages"),
                (unsigned int)total_pages);
    lv_label_set_text(s_detail_refs.page_label, text);
}

static void ui_file_manager_detail_render_list(void)
{
    size_t i;
    size_t start;
    size_t end;
    char name[UI_FILE_MANAGER_DETAIL_MAX_NAME + 8U];

    if (s_detail_refs.list_panel == NULL)
    {
        return;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    memset(s_visible_indices, 0xff, sizeof(s_visible_indices));

    if (s_detail_category >= UI_FILE_MANAGER_CATEGORY_COUNT)
    {
        s_detail_category = UI_FILE_MANAGER_CATEGORY_FONT;
    }

    start = s_page_start[s_detail_category];
    if (start >= s_detail_state.file_count)
    {
        start = 0U;
        s_page_start[s_detail_category] = 0U;
    }
    end = start + UI_FILE_MANAGER_DETAIL_VISIBLE;
    if (end > s_detail_state.file_count)
    {
        end = s_detail_state.file_count;
    }

    if (s_detail_state.file_count == 0U)
    {
        const ui_file_manager_detail_category_t *category = &s_detail_categories[s_detail_category];
        for (i = 1; i < UI_FILE_MANAGER_DETAIL_VISIBLE; ++i)
        {
            lv_obj_add_flag(s_detail_refs.rows[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(s_detail_refs.rows[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_refs.labels[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail_refs.icons[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_refs.rows[0], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(s_detail_refs.rows[0], (void *)(uintptr_t)UI_FILE_MANAGER_DETAIL_MAX_FILES);
        lv_label_set_text(s_detail_refs.labels[0], ui_i18n_pick(category->empty_zh, category->empty_en));
        ui_file_manager_detail_update_page_label();
        return;
    }

    for (i = 0; i < UI_FILE_MANAGER_DETAIL_VISIBLE; ++i)
    {
        size_t file_index = start + i;

        if (file_index >= end)
        {
            lv_obj_add_flag(s_detail_refs.rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        ui_file_manager_detail_format_name(name,
                                           sizeof(name),
                                           s_detail_state.files[file_index].name);
        s_visible_indices[i] = file_index;
        lv_obj_clear_flag(s_detail_refs.rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail_refs.rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_detail_refs.labels[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_detail_refs.icons[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(s_detail_refs.rows[i], (void *)(uintptr_t)file_index);
        lv_label_set_text(s_detail_refs.labels[i], name);
    }

    ui_file_manager_detail_refresh_selected_icons();
    ui_file_manager_detail_update_page_label();
    app_watchdog_progress(APP_WDT_MODULE_UI);
}

static bool ui_file_manager_detail_nav_allowed(void)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t min_gap = rt_tick_from_millisecond(UI_FILE_MANAGER_DETAIL_NAV_GAP_MS);

    if (s_last_page_nav_tick != 0 && (rt_tick_t)(now - s_last_page_nav_tick) < min_gap)
    {
        return false;
    }

    s_last_page_nav_tick = now;
    return true;
}

static void ui_file_manager_detail_prev_page(void)
{
    if (s_detail_category >= UI_FILE_MANAGER_CATEGORY_COUNT ||
        s_page_start[s_detail_category] < UI_FILE_MANAGER_DETAIL_VISIBLE)
    {
        return;
    }
    if (!ui_file_manager_detail_nav_allowed())
    {
        return;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    s_page_start[s_detail_category] -= UI_FILE_MANAGER_DETAIL_VISIBLE;
    ui_file_manager_detail_render_list();
}

static void ui_file_manager_detail_next_page(void)
{
    if (s_detail_category >= UI_FILE_MANAGER_CATEGORY_COUNT ||
        (s_page_start[s_detail_category] + UI_FILE_MANAGER_DETAIL_VISIBLE) >= s_detail_state.file_count)
    {
        return;
    }
    if (!ui_file_manager_detail_nav_allowed())
    {
        return;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    s_page_start[s_detail_category] += UI_FILE_MANAGER_DETAIL_VISIBLE;
    ui_file_manager_detail_render_list();
}

static void ui_file_manager_detail_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_file_manager_detail_prev_page();
    }
}

static void ui_file_manager_detail_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_file_manager_detail_next_page();
    }
}

static void ui_file_manager_detail_create_footer(lv_obj_t *parent)
{
    lv_obj_t *prev_button;
    lv_obj_t *next_button;

    s_detail_refs.page_label = ui_create_label(parent,
                                               "总计：0页",
                                               37,
                                               702,
                                               170,
                                               31,
                                               24,
                                               LV_TEXT_ALIGN_LEFT,
                                               false,
                                               false);
    lv_obj_set_style_text_color(s_detail_refs.page_label, lv_color_hex(0x000000), 0);

    prev_button = ui_file_manager_detail_plain_obj(parent, 235, 686, 120, 64, 16, LV_OPA_COVER, 0xffffff, 1);
    next_button = ui_file_manager_detail_plain_obj(parent, 371, 686, 120, 64, 16, LV_OPA_COVER, 0xffffff, 1);
    lv_obj_add_flag(prev_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(next_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(prev_button, ui_file_manager_detail_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(next_button, ui_file_manager_detail_next_event_cb, LV_EVENT_CLICKED, NULL);

    ui_create_label(prev_button,
                    ui_i18n_pick("上一页", "Prev"),
                    0,
                    19,
                    120,
                    31,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
    ui_create_label(next_button,
                    ui_i18n_pick("下一页", "Next"),
                    0,
                    19,
                    120,
                    31,
                    24,
                    LV_TEXT_ALIGN_CENTER,
                    false,
                    false);
}

void ui_file_manager_detail_set_category(ui_file_manager_category_t category)
{
    if (category >= UI_FILE_MANAGER_CATEGORY_COUNT)
    {
        category = UI_FILE_MANAGER_CATEGORY_FONT;
    }

    s_detail_category = category;
    s_last_page_nav_tick = 0;
    if (ui_File_Manager_Detail != NULL)
    {
        lv_obj_delete(ui_File_Manager_Detail);
        ui_File_Manager_Detail = NULL;
    }
}

void ui_File_Manager_Detail_screen_init(void)
{
    const ui_file_manager_detail_category_t *category;

    if (ui_File_Manager_Detail != NULL)
    {
        return;
    }

    if (s_detail_category >= UI_FILE_MANAGER_CATEGORY_COUNT)
    {
        s_detail_category = UI_FILE_MANAGER_CATEGORY_FONT;
    }
    category = &s_detail_categories[s_detail_category];
    memset(&s_detail_refs, 0, sizeof(s_detail_refs));
    memset(s_visible_indices, 0xff, sizeof(s_visible_indices));
    ui_file_manager_detail_scan_files(category);

    ui_File_Manager_Detail = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_File_Manager_Detail, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_File_Manager_Detail, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_File_Manager_Detail, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_File_Manager_Detail,
                                ui_i18n_pick(category->title_zh, category->title_en),
                                UI_SCREEN_FILE_MANAGER);

    s_detail_refs.list_panel = ui_file_manager_detail_plain_obj(ui_File_Manager_Detail,
                                                                0,
                                                                90,
                                                                528,
                                                                560,
                                                                0,
                                                                LV_OPA_TRANSP,
                                                                0xffffff,
                                                                0);
    ui_file_manager_detail_create_row_slots(s_detail_refs.list_panel);
    ui_file_manager_detail_create_footer(ui_File_Manager_Detail);
    ui_file_manager_detail_render_list();
}

void ui_File_Manager_Detail_screen_destroy(void)
{
    if (ui_File_Manager_Detail != NULL)
    {
        lv_obj_delete(ui_File_Manager_Detail);
        ui_File_Manager_Detail = NULL;
    }
    memset(s_visible_indices, 0xff, sizeof(s_visible_indices));
    memset(&s_detail_refs, 0, sizeof(s_detail_refs));
    memset(&s_detail_state, 0, sizeof(s_detail_state));
}

void ui_file_manager_detail_hardware_prev_page(void)
{
    ui_file_manager_detail_prev_page();
}

void ui_file_manager_detail_hardware_next_page(void)
{
    ui_file_manager_detail_next_page();
}
