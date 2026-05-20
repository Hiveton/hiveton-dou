#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#define DIR FATFS_DIR
#include "ff.h"
#undef DIR
#include "drv_lcd.h"
#include "app_watchdog.h"
#include "rthw.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "network/net_manager.h"
#include "reading/reading_cover_cache.h"
#include "reading/reading_state.h"
#include "reading/reading_epub.h"
#include "../../sleep_manager.h"
#include "ui.h"
#include "ui_components.h"
#include "ui_epd_refresh_policy.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#include "../../app_tf_storage.h"

#define UI_READING_VISIBLE_COUNT 9U
#define UI_READING_FIRST_PAGE_COUNT 3U
#define UI_READING_NEXT_PAGE_COUNT 9U
#define UI_READING_MAX_FILES 48U
#define UI_READING_MAX_NAME_LEN 96U
#define UI_READING_MAX_PATH_LEN 192U
#define UI_READING_BOOKS_DIRECTORY "books"
#define UI_READING_TAB_X 24
#define UI_READING_TAB_Y 14
#define UI_READING_TAB_WIDTH 160
#define UI_READING_TAB_HEIGHT 48
#define UI_READING_GRID_COVER_WIDTH 140
#define UI_READING_GRID_COVER_HEIGHT 186
#define UI_READING_COVER_TITLE_MAX_UNITS 11
#define UI_READING_COVER_TITLE_MAX_LINES 3
#define UI_READING_CONTINUE_TITLE_MAX_UNITS 20
#define UI_READING_CONTINUE_TITLE_MAX_LINES 2
#define UI_READING_CONTINUE_CARD_X 32
#define UI_READING_CONTINUE_CARD_Y 122
#define UI_READING_CONTINUE_CARD_WIDTH 464
#define UI_READING_CONTINUE_CARD_HEIGHT 230
#define UI_READING_FIRST_PAGE_TITLE_Y 389
#define UI_READING_FIRST_PAGE_NAV_Y 390
#define UI_READING_NEXT_PAGE_TITLE_Y 110
#define UI_READING_NEXT_PAGE_NAV_Y 111
#define UI_READING_COVER_PROGRESS_INTERVAL_MS 500U
#define UI_READING_COVER_LOAD_INTERVAL_MS 260U
#define UI_READING_COVER_LOAD_INITIAL_DELAY_MS 6000U
#define UI_READING_COVER_LOAD_BATCH_SIZE 1U
#define UI_READING_COVER_LOAD_MAX_PER_RENDER 3U
#define UI_READING_COVER_PROMPT_DELAY_MS 30000U
#define UI_READING_COVER_PROMPT_SCAN_INTERVAL_MS 200U
#define UI_READING_COVER_PROMPT_SCAN_SLICE_MAX 1U
#define UI_READING_COVER_BUILD_TIMEOUT_MS 120000U
#define UI_READING_COVER_BATCH_MAX UI_READING_MAX_FILES
#define UI_READING_SCAN_SLICE_MAX_ENTRIES 6U
#define UI_READING_SCAN_SLICE_INTERVAL_MS 35U
#define UI_READING_PERF_LOG_ENABLE 1
#define UI_READING_DEBUG_AUTO_GENERATE_COVERS 0
#define UI_READING_EPUB_COVER_TEXT_BUFFER 256U
#define UI_READING_EPUB_COVER_BLOCK_COUNT 4U
#define UI_READING_EPUB_COVER_IMAGE_COUNT 4U

typedef enum
{
    UI_READING_SCAN_OK = 0,
    UI_READING_SCAN_LOADING,
    UI_READING_SCAN_NO_CARD,
    UI_READING_SCAN_MOUNT_FAILED,
    UI_READING_SCAN_DIR_FAILED,
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
    uint16_t file_index;
    bool has_file;
    bool cover_pending;
} ui_reading_card_refs_t;

lv_obj_t *ui_Reading_List = NULL;

static const int s_grid_x_positions[UI_READING_VISIBLE_COUNT] = {32, 194, 356, 30, 192, 354, 30, 192, 354};
static const int s_grid_first_page_y_positions[UI_READING_VISIBLE_COUNT] = {446, 446, 446, 446, 446, 446, 446, 446, 446};
static const int s_grid_next_page_y_positions[UI_READING_VISIBLE_COUNT] = {161, 161, 161, 371, 371, 371, 582, 582, 582};
static const uint32_t UI_READING_REFRESH_FAST_MS = 1500U;
static const uint32_t UI_READING_REFRESH_RETRY_MS = 5000U;

static ui_reading_file_entry_t s_reading_files[UI_READING_MAX_FILES];
static ui_reading_card_refs_t s_reading_cards[UI_READING_VISIBLE_COUNT];
static ui_reading_card_refs_t s_reading_continue_card;
static lv_obj_t *s_reading_tab_buttons[UI_READING_TAB_COUNT];
static lv_obj_t *s_reading_status_label = NULL;
static lv_obj_t *s_reading_prev_button = NULL;
static lv_obj_t *s_reading_next_button = NULL;
static lv_obj_t *s_reading_page_label = NULL;
static lv_obj_t *s_reading_bottom_nav = NULL;
static lv_timer_t *s_reading_enter_refresh_timer = NULL;
static lv_timer_t *s_reading_cover_prompt_timer = NULL;
static lv_timer_t *s_reading_cover_load_timer = NULL;
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
static uint16_t s_reading_cover_missing_count = 0;
static uint16_t s_reading_cover_last_done_count = 0;
static uint8_t s_reading_cover_finish_ticks = 0;
static bool s_reading_cover_sleep_blocked = false;
static bool s_reading_cover_network_suspended = false;
static bool s_reading_cover_prompt_dismissed = false;
static uint32_t s_reading_cover_dismissed_signature = 0U;
#if UI_READING_DEBUG_AUTO_GENERATE_COVERS
static bool s_reading_cover_debug_auto_started = false;
#endif
static char s_reading_mount_path[32];
static char s_reading_books_path[UI_READING_MAX_PATH_LEN];
static char s_reading_selected_name[UI_READING_MAX_NAME_LEN];
static char s_reading_selected_path[UI_READING_MAX_PATH_LEN];
static char s_reading_status_text[128];
static uint16_t s_reading_cover_missing_indices[UI_READING_MAX_FILES];
static char s_reading_cover_worker_paths[UI_READING_MAX_FILES][UI_READING_MAX_PATH_LEN];
static volatile uint16_t s_reading_cover_result_ready_count = 0;
static volatile uint16_t s_reading_cover_result_no_cover_count = 0;
static volatile uint16_t s_reading_cover_result_failed_count = 0;
static uint8_t s_reading_cover_loaded_since_refresh = 0U;

typedef struct
{
    bool running;
    bool done;
    bool cancel;
    uint16_t total;
    uint16_t done_count;
} ui_reading_cover_worker_state_t;

typedef struct
{
    uint16_t total_epub;
    uint16_t ready_count;
    uint16_t unknown_count;
    uint16_t failed_count;
    uint16_t no_cover_count;
    uint16_t target_count;
    uint32_t signature;
} ui_reading_cover_scan_stats_t;

typedef struct
{
    uint16_t ready_count;
    uint16_t no_cover_count;
    uint16_t failed_count;
} ui_reading_cover_result_stats_t;

typedef struct
{
    bool active;
    bool dir_open;
    bool reset_offset;
    bool had_selection;
    uint32_t entries_seen;
    rt_tick_t start_tick;
    char previous_name[UI_READING_MAX_NAME_LEN];
    char previous_path[UI_READING_MAX_PATH_LEN];
    char directory_path[UI_READING_MAX_PATH_LEN];
    FATFS_DIR dir;
} ui_reading_scan_job_t;

typedef struct
{
    bool active;
    uint16_t index;
    uint16_t count;
    rt_tick_t start_tick;
    ui_reading_cover_scan_stats_t stats;
} ui_reading_cover_prompt_scan_job_t;

static ui_reading_cover_scan_stats_t s_reading_cover_prompt_stats;
static ui_reading_scan_job_t s_reading_scan_job;
static ui_reading_cover_prompt_scan_job_t s_reading_cover_prompt_scan_job;
static bool s_reading_cover_load_first_tick = false;

typedef enum
{
    UI_READING_COVER_START_OK = 0,
    UI_READING_COVER_START_ALREADY_RUNNING,
    UI_READING_COVER_START_NO_MISSING,
    UI_READING_COVER_START_NO_VALID_PATH,
} ui_reading_cover_start_result_t;

static ui_reading_cover_worker_state_t ui_reading_cover_worker_snapshot(void)
{
    ui_reading_cover_worker_state_t state;
    rt_base_t level = rt_hw_interrupt_disable();
    state.running = s_reading_cover_worker_running;
    state.done = s_reading_cover_worker_done;
    state.cancel = s_reading_cover_worker_cancel;
    state.total = s_reading_cover_worker_total;
    state.done_count = s_reading_cover_worker_done_count;
    rt_hw_interrupt_enable(level);
    return state;
}

static void ui_reading_cover_worker_set_running(bool running)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_running = running;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_worker_set_cancel(bool cancel)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_cancel = cancel;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_worker_set_progress(uint16_t total,
                                                 uint16_t done_count)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_total = total;
    s_reading_cover_worker_done_count = done_count;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_worker_mark_started(uint16_t total)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_total = total;
    s_reading_cover_worker_done_count = 0U;
    s_reading_cover_worker_done = false;
    s_reading_cover_worker_cancel = false;
    s_reading_cover_worker_running = true;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_block_sleep(void)
{
    if (s_reading_cover_sleep_blocked)
    {
        return;
    }

    sleep_manager_block_standby();
    s_reading_cover_sleep_blocked = true;
    net_manager_request_none_mode();
    s_reading_cover_network_suspended = true;
    rt_kprintf("reading_list: cover generation blocked standby\n");
}

static void ui_reading_cover_unblock_sleep(void)
{
    if (!s_reading_cover_sleep_blocked)
    {
        return;
    }

    sleep_manager_unblock_standby();
    s_reading_cover_sleep_blocked = false;
    if (s_reading_cover_network_suspended)
    {
        s_reading_cover_network_suspended = false;
        net_manager_reload_config_mode();
    }
    rt_kprintf("reading_list: cover generation unblocked standby\n");
}

static void ui_reading_cover_worker_mark_done(uint16_t done_count)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_done_count = done_count;
    s_reading_cover_worker_done = true;
    s_reading_cover_worker_running = false;
    s_reading_cover_worker_cancel = false;
    rt_hw_interrupt_enable(level);
    ui_reading_cover_unblock_sleep();
}

static void ui_reading_cover_worker_mark_idle_done(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_worker_done = true;
    s_reading_cover_worker_running = false;
    s_reading_cover_worker_cancel = false;
    s_reading_cover_worker_done_count = 0U;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_result_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_result_ready_count = 0U;
    s_reading_cover_result_no_cover_count = 0U;
    s_reading_cover_result_failed_count = 0U;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_result_add(reading_cover_cache_state_t state)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (state == READING_COVER_CACHE_READY)
    {
        ++s_reading_cover_result_ready_count;
    }
    else if (state == READING_COVER_CACHE_NO_COVER)
    {
        ++s_reading_cover_result_no_cover_count;
    }
    else
    {
        ++s_reading_cover_result_failed_count;
    }

    rt_hw_interrupt_enable(level);
}

static ui_reading_cover_result_stats_t ui_reading_cover_result_snapshot(void)
{
    ui_reading_cover_result_stats_t stats;
    rt_base_t level = rt_hw_interrupt_disable();

    stats.ready_count = s_reading_cover_result_ready_count;
    stats.no_cover_count = s_reading_cover_result_no_cover_count;
    stats.failed_count = s_reading_cover_result_failed_count;

    rt_hw_interrupt_enable(level);
    return stats;
}

static bool ui_reading_enter_refresh_requested(void)
{
    bool requested;
    rt_base_t level = rt_hw_interrupt_disable();
    requested = s_reading_enter_refresh_requested;
    rt_hw_interrupt_enable(level);
    return requested;
}

static void ui_reading_set_enter_refresh_requested(bool requested)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_enter_refresh_requested = requested;
    rt_hw_interrupt_enable(level);
}

static bool ui_reading_cover_prompt_shown(void)
{
    bool shown;
    rt_base_t level = rt_hw_interrupt_disable();
    shown = s_reading_cover_prompt_shown;
    rt_hw_interrupt_enable(level);
    return shown;
}

static void ui_reading_cover_set_prompt_shown(bool shown)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_prompt_shown = shown;
    rt_hw_interrupt_enable(level);
}

static void ui_reading_cover_set_dismissed_signature(bool dismissed,
                                                     uint32_t signature)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_reading_cover_prompt_dismissed = dismissed;
    s_reading_cover_dismissed_signature = signature;
    rt_hw_interrupt_enable(level);
}

static bool ui_reading_cover_dismissed_signature_matches(uint32_t signature)
{
    bool matches;
    rt_base_t level = rt_hw_interrupt_disable();
    matches = s_reading_cover_prompt_dismissed &&
              s_reading_cover_dismissed_signature == signature;
    rt_hw_interrupt_enable(level);
    return matches;
}

static void ui_reading_list_render(void);
static void ui_reading_open_selected_detail(void);
static void ui_reading_cover_maybe_prompt(void);
static void ui_reading_cover_request_prompt_deferred(void);
static void ui_reading_cover_show_progress(uint16_t total);
static void ui_reading_cover_generate_async_cb(void *user_data);
static bool ui_reading_copy_path(char *buffer, size_t buffer_size, const char *path);
static void ui_reading_release_card_cover(ui_reading_card_refs_t *refs);
static void ui_reading_schedule_cover_load(void);

#if UI_READING_PERF_LOG_ENABLE
static unsigned long ui_reading_elapsed_ms(rt_tick_t start_tick)
{
    rt_tick_t elapsed = rt_tick_get() - start_tick;

    return ((unsigned long)elapsed * 1000UL) / (unsigned long)RT_TICK_PER_SECOND;
}

static void ui_reading_perf_log(const char *stage, rt_tick_t start_tick)
{
    rt_kprintf("reading_list: perf %s elapsed=%lu ms\n",
               stage != NULL ? stage : "?",
               ui_reading_elapsed_ms(start_tick));
}
#else
static void ui_reading_perf_log(const char *stage, rt_tick_t start_tick)
{
    LV_UNUSED(stage);
    LV_UNUSED(start_tick);
}
#endif

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

    ui_reading_set_enter_refresh_requested(false);
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
    if (ui_reading_enter_refresh_requested())
    {
        return;
    }

    ui_reading_set_enter_refresh_requested(true);
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

static int ui_reading_centered_text_y(uint16_t font_size, int box_h)
{
    lv_font_t *font = ui_font_get(font_size);
    int line_h = font != NULL && font->line_height > 0 ? (int)font->line_height : (int)font_size;

    if (line_h >= box_h)
    {
        return 0;
    }

    return (box_h - line_h) / 2;
}

static lv_obj_t *ui_reading_prompt_button(lv_obj_t *parent,
                                          int x,
                                          int y,
                                          int w,
                                          int h,
                                          int radius,
                                          uint32_t bg_color,
                                          int border_width)
{
    lv_obj_t *button = lv_button_create(parent);

    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(button, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(button, ui_px_x(radius), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, border_width, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    return button;
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

static size_t ui_reading_utf8_char_len(const char *text)
{
    unsigned char c;

    if (text == NULL || text[0] == '\0')
    {
        return 0U;
    }

    c = (unsigned char)text[0];
    if ((c & 0x80U) == 0U)
    {
        return 1U;
    }
    if ((c & 0xE0U) == 0xC0U)
    {
        return 2U;
    }
    if ((c & 0xF0U) == 0xE0U)
    {
        return 3U;
    }
    if ((c & 0xF8U) == 0xF0U)
    {
        return 4U;
    }
    return 1U;
}

static uint8_t ui_reading_utf8_char_units(const char *text, size_t char_len)
{
    if (text == NULL || char_len == 0U)
    {
        return 0U;
    }

    return ((unsigned char)text[0] < 0x80U) ? 1U : 2U;
}

static void ui_reading_format_cover_title(const char *name, char *buffer, size_t buffer_size)
{
    const char *src = name != NULL ? name : "";
    size_t out = 0U;
    uint8_t line_units = 0U;
    uint8_t line_count = 1U;
    bool truncated = false;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    buffer[0] = '\0';
    while (*src != '\0' && out + 1U < buffer_size)
    {
        size_t char_len = ui_reading_utf8_char_len(src);
        uint8_t units = ui_reading_utf8_char_units(src, char_len);

        if (char_len == 0U || out + char_len + 1U >= buffer_size)
        {
            truncated = true;
            break;
        }

        if (line_units > 0U &&
            (uint8_t)(line_units + units) > UI_READING_COVER_TITLE_MAX_UNITS)
        {
            if (line_count >= UI_READING_COVER_TITLE_MAX_LINES)
            {
                truncated = true;
                break;
            }
            buffer[out++] = '\n';
            buffer[out] = '\0';
            line_units = 0U;
            ++line_count;
        }

        if (line_count >= UI_READING_COVER_TITLE_MAX_LINES &&
            (uint8_t)(line_units + units) > UI_READING_COVER_TITLE_MAX_UNITS)
        {
            truncated = true;
            break;
        }

        memcpy(&buffer[out], src, char_len);
        out += char_len;
        buffer[out] = '\0';
        line_units = (uint8_t)(line_units + units);
        src += char_len;
    }

    if ((*src != '\0' || truncated) && out + 3U < buffer_size)
    {
        if (out > 1U && buffer[out - 1U] != '\n')
        {
            buffer[out++] = '.';
            buffer[out++] = '.';
            buffer[out] = '\0';
        }
    }
}

static void ui_reading_format_continue_title(const char *name, char *buffer, size_t buffer_size)
{
    const char *src = name != NULL ? name : "";
    size_t out = 0U;
    uint8_t line_units = 0U;
    uint8_t line_count = 1U;
    bool truncated = false;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    buffer[0] = '\0';
    while (*src != '\0' && out + 1U < buffer_size)
    {
        size_t char_len = ui_reading_utf8_char_len(src);
        uint8_t units = ui_reading_utf8_char_units(src, char_len);

        if (char_len == 0U || out + char_len + 1U >= buffer_size)
        {
            truncated = true;
            break;
        }

        if (line_units > 0U &&
            (uint8_t)(line_units + units) > UI_READING_CONTINUE_TITLE_MAX_UNITS)
        {
            if (line_count >= UI_READING_CONTINUE_TITLE_MAX_LINES)
            {
                truncated = true;
                break;
            }
            buffer[out++] = '\n';
            buffer[out] = '\0';
            line_units = 0U;
            ++line_count;
        }

        if (line_count >= UI_READING_CONTINUE_TITLE_MAX_LINES &&
            (uint8_t)(line_units + units) > UI_READING_CONTINUE_TITLE_MAX_UNITS)
        {
            truncated = true;
            break;
        }

        memcpy(&buffer[out], src, char_len);
        out += char_len;
        buffer[out] = '\0';
        line_units = (uint8_t)(line_units + units);
        src += char_len;
    }

    if ((*src != '\0' || truncated) && out + 3U < buffer_size)
    {
        if (out > 1U && buffer[out - 1U] != '\n')
        {
            buffer[out++] = '.';
            buffer[out++] = '.';
            buffer[out] = '\0';
        }
    }
}

static uint8_t ui_reading_count_title_lines(const char *text)
{
    uint8_t lines = 1U;

    if (text == NULL || text[0] == '\0')
    {
        return lines;
    }

    while (*text != '\0')
    {
        if (*text == '\n' && lines < UI_READING_COVER_TITLE_MAX_LINES)
        {
            ++lines;
        }
        ++text;
    }

    return lines;
}

static void ui_reading_layout_cover_title(lv_obj_t *label,
                                          int cover_w,
                                          int cover_h,
                                          const char *text)
{
    uint8_t lines;
    int title_h;
    int title_y;

    if (label == NULL)
    {
        return;
    }

    lines = ui_reading_count_title_lines(text);
    title_h = (int)lines * 26;
    title_y = (cover_h - title_h) / 2;
    if (title_y < 0)
    {
        title_y = 0;
    }

    lv_obj_set_pos(label, ui_px_x(17), ui_px_y(title_y));
    lv_obj_set_size(label, ui_px_w(cover_w - 34), ui_px_h(title_h));
}

static bool ui_reading_try_mount_device(const char *device_name,
                                        char *mounted_path,
                                        size_t mounted_path_size)
{
    const char *mounted;
    struct statfs fs_stat;

    (void)device_name;

    mounted = app_tf_mount_root();
    if (mounted == RT_NULL)
    {
        if (dfs_statfs("/", &fs_stat) != 0)
        {
            rt_kprintf("reading_list: storage root unavailable card=%u ready=%u errno=%d\n",
                       (unsigned int)app_tf_card_inserted(),
                       (unsigned int)app_tf_storage_ready(),
                       rt_get_errno());
            return false;
        }

        rt_kprintf("reading_list: storage ready flag is off, but DFS root is accessible\n");
        mounted = "/";
    }

    return ui_reading_copy_path(mounted_path, mounted_path_size, mounted);
}

static bool ui_reading_resolve_storage_root(char *mounted_path, size_t mounted_path_size, bool *had_device)
{
    struct statfs fs_stat;
    bool root_accessible;

    if (mounted_path == NULL || mounted_path_size == 0U)
    {
        return false;
    }

    mounted_path[0] = '\0';
    root_accessible = (dfs_statfs("/", &fs_stat) == 0);
    if (had_device != NULL)
    {
        *had_device = app_tf_card_inserted() || root_accessible;
    }

    return ui_reading_try_mount_device(RT_NULL, mounted_path, mounted_path_size);
}

static bool ui_reading_scan_directory(const char *directory_path)
{
    FATFS_DIR dir;
    FILINFO info;
    FRESULT result;
    uint32_t scan_tick = 0U;

    if (directory_path == NULL || directory_path[0] == '\0')
    {
        return false;
    }

    memset(&dir, 0, sizeof(dir));
    memset(&info, 0, sizeof(info));

    result = f_opendir(&dir, directory_path);
    if (result == FR_NO_PATH || result == FR_NO_FILE)
    {
        result = f_mkdir(directory_path);
        if (result != FR_OK && result != FR_EXIST)
        {
            rt_kprintf("reading_list: f_mkdir failed path=%s result=%d\n", directory_path, result);
            return false;
        }

        result = f_opendir(&dir, directory_path);
    }

    if (result != FR_OK)
    {
        rt_kprintf("reading_list: f_opendir failed path=%s result=%d\n", directory_path, result);
        return false;
    }

    s_reading_file_count = 0;
    app_watchdog_progress(APP_WDT_MODULE_UI);
    while (1)
    {
        char full_path[UI_READING_MAX_PATH_LEN];
        int written;

        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            rt_kprintf("reading_list: f_readdir failed path=%s result=%d\n", directory_path, result);
            break;
        }

        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((scan_tick++ & 0x0FU) == 0U)
        {
            app_watchdog_progress(APP_WDT_MODULE_UI);
        }

        if ((info.fattrib & AM_DIR) != 0U || !ui_reading_is_listable_file(info.fname))
        {
            continue;
        }

        written = rt_snprintf(full_path,
                              sizeof(full_path),
                              "%s%s%s",
                              directory_path,
                              strcmp(directory_path, "/") == 0 ? "" : "/",
                              info.fname);
        if (written < 0 || (size_t)written >= sizeof(full_path))
        {
            continue;
        }

        rt_snprintf(s_reading_files[s_reading_file_count].name,
                    sizeof(s_reading_files[s_reading_file_count].name),
                    "%s",
                    info.fname);
        if (!ui_reading_copy_path(s_reading_files[s_reading_file_count].path,
                                  sizeof(s_reading_files[s_reading_file_count].path),
                                  full_path))
        {
            continue;
        }
        s_reading_files[s_reading_file_count].size_bytes = (uint32_t)info.fsize;
        s_reading_files[s_reading_file_count].file_type = ui_reading_detect_file_type(info.fname);
        ++s_reading_file_count;
        app_watchdog_progress(APP_WDT_MODULE_UI);

        if (s_reading_file_count >= UI_READING_MAX_FILES)
        {
            break;
        }
    }

    (void)f_closedir(&dir);
    app_watchdog_progress(APP_WDT_MODULE_UI);

    if (result != FR_OK)
    {
        return false;
    }

    if (s_reading_file_count > 1U)
    {
        qsort(s_reading_files,
              s_reading_file_count,
              sizeof(s_reading_files[0]),
              ui_reading_file_compare);
    }

    return true;
}

static bool ui_reading_add_file_from_info(const char *directory_path, const FILINFO *info)
{
    char full_path[UI_READING_MAX_PATH_LEN];
    int written;

    if (directory_path == NULL || info == NULL ||
        (info->fattrib & AM_DIR) != 0U ||
        !ui_reading_is_listable_file(info->fname) ||
        s_reading_file_count >= UI_READING_MAX_FILES)
    {
        return false;
    }

    written = rt_snprintf(full_path,
                          sizeof(full_path),
                          "%s%s%s",
                          directory_path,
                          strcmp(directory_path, "/") == 0 ? "" : "/",
                          info->fname);
    if (written < 0 || (size_t)written >= sizeof(full_path))
    {
        return false;
    }

    rt_snprintf(s_reading_files[s_reading_file_count].name,
                sizeof(s_reading_files[s_reading_file_count].name),
                "%s",
                info->fname);
    if (!ui_reading_copy_path(s_reading_files[s_reading_file_count].path,
                              sizeof(s_reading_files[s_reading_file_count].path),
                              full_path))
    {
        return false;
    }

    s_reading_files[s_reading_file_count].size_bytes = (uint32_t)info->fsize;
    s_reading_files[s_reading_file_count].file_type = ui_reading_detect_file_type(info->fname);
    ++s_reading_file_count;
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
        rt_kprintf("reading_list: resolve root failed card=%u ready=%u state=%d\n",
                   (unsigned int)app_tf_card_inserted(),
                   (unsigned int)app_tf_storage_ready(),
                   (int)s_reading_scan_state);
        return;
    }

    if (!ui_reading_join_path(s_reading_books_path,
                              sizeof(s_reading_books_path),
                              s_reading_mount_path,
                              UI_READING_BOOKS_DIRECTORY))
    {
        s_reading_scan_state = UI_READING_SCAN_DIR_FAILED;
        rt_kprintf("reading_list: build books path failed root=%s\n", s_reading_mount_path);
        return;
    }

    rt_kprintf("reading_list: scan start card=%u ready=%u root=%s books=%s\n",
               (unsigned int)app_tf_card_inserted(),
               (unsigned int)app_tf_storage_ready(),
               s_reading_mount_path,
               s_reading_books_path);

    if (!ui_reading_scan_directory(s_reading_books_path))
    {
        s_reading_scan_state = UI_READING_SCAN_DIR_FAILED;
        rt_kprintf("reading_list: scan failed books=%s\n", s_reading_books_path);
        return;
    }

    s_reading_scan_state = s_reading_file_count > 0U ? UI_READING_SCAN_OK : UI_READING_SCAN_NO_FILES;
    rt_kprintf("reading_list: scan done files=%u state=%d\n",
               (unsigned int)s_reading_file_count,
               (int)s_reading_scan_state);
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

static void ui_reading_scan_job_cancel(void)
{
    if (s_reading_scan_job.dir_open)
    {
        (void)f_closedir(&s_reading_scan_job.dir);
    }
    memset(&s_reading_scan_job, 0, sizeof(s_reading_scan_job));
}

static void ui_reading_scan_job_finish(ui_reading_scan_state_t final_state)
{
    if (s_reading_scan_job.dir_open)
    {
        (void)f_closedir(&s_reading_scan_job.dir);
        s_reading_scan_job.dir_open = false;
    }

    if (s_reading_file_count > 1U)
    {
        qsort(s_reading_files,
              s_reading_file_count,
              sizeof(s_reading_files[0]),
              ui_reading_file_compare);
    }

    if (final_state == UI_READING_SCAN_OK)
    {
        s_reading_scan_state = s_reading_file_count > 0U ? UI_READING_SCAN_OK : UI_READING_SCAN_NO_FILES;
    }
    else
    {
        s_reading_scan_state = final_state;
    }

    if (s_reading_scan_job.reset_offset ||
        s_reading_page_offset >= s_reading_file_count)
    {
        s_reading_page_offset = 0U;
    }

    ui_reading_restore_selection(s_reading_scan_job.had_selection,
                                 s_reading_scan_job.previous_path,
                                 s_reading_scan_job.previous_name);

    rt_kprintf("reading_list: async scan done files=%u entries=%lu state=%d\n",
               (unsigned int)s_reading_file_count,
               (unsigned long)s_reading_scan_job.entries_seen,
               (int)s_reading_scan_state);
    ui_reading_perf_log("scan_async_total", s_reading_scan_job.start_tick);
    memset(&s_reading_scan_job, 0, sizeof(s_reading_scan_job));

    ui_reading_list_render();
    if ((s_reading_scan_state == UI_READING_SCAN_OK ||
         s_reading_scan_state == UI_READING_SCAN_NO_FILES) &&
        s_reading_refresh_timer != NULL)
    {
        lv_timer_delete(s_reading_refresh_timer);
        s_reading_refresh_timer = NULL;
    }
    if (s_reading_scan_state == UI_READING_SCAN_OK)
    {
        ui_reading_cover_request_prompt_deferred();
    }
    ui_reading_request_enter_full_refresh_deferred();
}

static bool ui_reading_scan_job_begin(bool reset_offset)
{
    bool had_device = false;
    FRESULT result;
    rt_tick_t start_tick = rt_tick_get();

    ui_reading_scan_job_cancel();

    s_reading_scan_job.reset_offset = reset_offset;
    s_reading_scan_job.had_selection =
        s_reading_has_selection &&
        (s_reading_selected_name[0] != '\0' || s_reading_selected_path[0] != '\0');
    rt_snprintf(s_reading_scan_job.previous_name,
                sizeof(s_reading_scan_job.previous_name),
                "%s",
                s_reading_selected_name);
    (void)ui_reading_copy_path(s_reading_scan_job.previous_path,
                               sizeof(s_reading_scan_job.previous_path),
                               s_reading_selected_path);

    memset(s_reading_files, 0, sizeof(s_reading_files));
    s_reading_file_count = 0U;
    if (reset_offset)
    {
        s_reading_page_offset = 0U;
    }
    s_reading_mount_path[0] = '\0';
    s_reading_books_path[0] = '\0';
    s_reading_scan_state = UI_READING_SCAN_LOADING;

    if (s_reading_active_tab != UI_READING_TAB_ALL)
    {
        ui_reading_refresh_files(reset_offset);
        ui_reading_list_render();
        return false;
    }

    if (!ui_reading_resolve_storage_root(s_reading_mount_path, sizeof(s_reading_mount_path), &had_device))
    {
        s_reading_scan_state = had_device ? UI_READING_SCAN_MOUNT_FAILED : UI_READING_SCAN_NO_CARD;
        rt_kprintf("reading_list: async resolve root failed card=%u ready=%u state=%d\n",
                   (unsigned int)app_tf_card_inserted(),
                   (unsigned int)app_tf_storage_ready(),
                   (int)s_reading_scan_state);
        ui_reading_restore_selection(s_reading_scan_job.had_selection,
                                     s_reading_scan_job.previous_path,
                                     s_reading_scan_job.previous_name);
        memset(&s_reading_scan_job, 0, sizeof(s_reading_scan_job));
        ui_reading_list_render();
        return false;
    }

    if (!ui_reading_join_path(s_reading_books_path,
                              sizeof(s_reading_books_path),
                              s_reading_mount_path,
                              UI_READING_BOOKS_DIRECTORY))
    {
        s_reading_scan_state = UI_READING_SCAN_DIR_FAILED;
        ui_reading_restore_selection(s_reading_scan_job.had_selection,
                                     s_reading_scan_job.previous_path,
                                     s_reading_scan_job.previous_name);
        memset(&s_reading_scan_job, 0, sizeof(s_reading_scan_job));
        ui_reading_list_render();
        return false;
    }

    (void)ui_reading_copy_path(s_reading_scan_job.directory_path,
                               sizeof(s_reading_scan_job.directory_path),
                               s_reading_books_path);
    rt_kprintf("reading_list: async scan start card=%u ready=%u root=%s books=%s\n",
               (unsigned int)app_tf_card_inserted(),
               (unsigned int)app_tf_storage_ready(),
               s_reading_mount_path,
               s_reading_books_path);

    s_reading_scan_job.start_tick = start_tick;
    memset(&s_reading_scan_job.dir, 0, sizeof(s_reading_scan_job.dir));
    result = f_opendir(&s_reading_scan_job.dir, s_reading_scan_job.directory_path);
    if (result == FR_NO_PATH || result == FR_NO_FILE)
    {
        result = f_mkdir(s_reading_scan_job.directory_path);
        if (result == FR_OK || result == FR_EXIST)
        {
            result = f_opendir(&s_reading_scan_job.dir, s_reading_scan_job.directory_path);
        }
    }
    if (result != FR_OK)
    {
        rt_kprintf("reading_list: async f_opendir failed path=%s result=%d\n",
                   s_reading_scan_job.directory_path,
                   (int)result);
        ui_reading_scan_job_finish(UI_READING_SCAN_DIR_FAILED);
        return false;
    }

    s_reading_scan_job.active = true;
    s_reading_scan_job.dir_open = true;
    ui_reading_perf_log("scan_async_begin", start_tick);
    return true;
}

static bool ui_reading_scan_job_step(void)
{
    uint16_t processed = 0U;
    rt_tick_t slice_tick = rt_tick_get();

    if (!s_reading_scan_job.active || !s_reading_scan_job.dir_open)
    {
        return false;
    }

    while (processed < UI_READING_SCAN_SLICE_MAX_ENTRIES &&
           s_reading_file_count < UI_READING_MAX_FILES)
    {
        FILINFO info;
        FRESULT result;

        memset(&info, 0, sizeof(info));
        result = f_readdir(&s_reading_scan_job.dir, &info);
        if (result != FR_OK)
        {
            rt_kprintf("reading_list: async f_readdir failed path=%s result=%d\n",
                       s_reading_scan_job.directory_path,
                       (int)result);
            ui_reading_scan_job_finish(UI_READING_SCAN_DIR_FAILED);
            return false;
        }
        if (info.fname[0] == '\0')
        {
            ui_reading_scan_job_finish(UI_READING_SCAN_OK);
            return false;
        }

        ++processed;
        ++s_reading_scan_job.entries_seen;
        (void)ui_reading_add_file_from_info(s_reading_scan_job.directory_path, &info);
    }

    if (s_reading_file_count >= UI_READING_MAX_FILES)
    {
        ui_reading_scan_job_finish(UI_READING_SCAN_OK);
        return false;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    ui_reading_perf_log("scan_async_slice", slice_tick);
    return true;
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
    if (s_reading_scan_job.active)
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
        if (s_reading_scan_state == UI_READING_SCAN_OK)
        {
            ui_reading_cover_request_prompt_deferred();
        }
        lv_timer_delete(s_reading_refresh_timer);
        s_reading_refresh_timer = NULL;
    }
    else if (s_reading_scan_state == UI_READING_SCAN_NO_CARD ||
             s_reading_scan_state == UI_READING_SCAN_MOUNT_FAILED)
    {
        lv_timer_set_period(timer, UI_READING_REFRESH_RETRY_MS);
    }
    else
    {
        lv_timer_set_period(timer, UI_READING_REFRESH_FAST_MS);
    }
}

static void ui_reading_enter_refresh_timer_cb(lv_timer_t *timer)
{
    if (ui_Reading_List == NULL)
    {
        if (s_reading_enter_refresh_timer != NULL)
        {
            lv_timer_delete(s_reading_enter_refresh_timer);
        }
        s_reading_enter_refresh_timer = NULL;
        return;
    }

    if (!s_reading_scan_job.active)
    {
        if (!ui_reading_scan_job_begin(true))
        {
            if (s_reading_enter_refresh_timer != NULL)
            {
                lv_timer_delete(s_reading_enter_refresh_timer);
                s_reading_enter_refresh_timer = NULL;
            }
            if (s_reading_scan_state == UI_READING_SCAN_OK)
            {
                ui_reading_cover_request_prompt_deferred();
            }
            ui_reading_request_enter_full_refresh_deferred();
            return;
        }
        lv_timer_set_period(timer, UI_READING_SCAN_SLICE_INTERVAL_MS);
    }

    if (ui_reading_scan_job_step())
    {
        return;
    }

    if (s_reading_enter_refresh_timer != NULL)
    {
        lv_timer_delete(s_reading_enter_refresh_timer);
        s_reading_enter_refresh_timer = NULL;
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

static uint32_t ui_reading_cover_signature_mix(uint32_t hash, uint16_t index, reading_cover_cache_state_t state)
{
    hash ^= (uint32_t)index;
    hash *= 16777619UL;
    hash ^= (uint32_t)state;
    hash *= 16777619UL;
    return hash;
}

static uint16_t ui_reading_cover_collect_targets(ui_reading_cover_scan_stats_t *stats)
{
    uint16_t count = 0U;
    uint16_t i;
    ui_reading_cover_scan_stats_t local_stats;
    rt_tick_t start_tick = rt_tick_get();

    memset(s_reading_cover_missing_indices, 0, sizeof(s_reading_cover_missing_indices));
    memset(&local_stats, 0, sizeof(local_stats));
    local_stats.signature = 2166136261UL;

    if (s_reading_scan_state != UI_READING_SCAN_OK)
    {
        if (stats != NULL)
        {
            *stats = local_stats;
        }
        return 0U;
    }

    for (i = 0U; i < s_reading_file_count && count < UI_READING_MAX_FILES; ++i)
    {
        reading_cover_cache_state_t state;

        if ((i & 0x07U) == 0U)
        {
            app_watchdog_progress(APP_WDT_MODULE_UI);
        }

        if (s_reading_files[i].file_type != UI_READING_FILE_TYPE_EPUB ||
            s_reading_files[i].path[0] == '\0')
        {
            continue;
        }

        ++local_stats.total_epub;
        state = reading_cover_cache_get_state(s_reading_files[i].path,
                                              UI_READING_GRID_COVER_WIDTH,
                                              UI_READING_GRID_COVER_HEIGHT);
        local_stats.signature = ui_reading_cover_signature_mix(local_stats.signature, i, state);

        if (state == READING_COVER_CACHE_READY)
        {
            ++local_stats.ready_count;
            continue;
        }
        if (state == READING_COVER_CACHE_NO_COVER)
        {
            ++local_stats.no_cover_count;
            continue;
        }
        if (state == READING_COVER_CACHE_FAILED)
        {
            ++local_stats.failed_count;
        }
        else
        {
            ++local_stats.unknown_count;
        }

        s_reading_cover_missing_indices[count] = i;
        ++count;
    }

    local_stats.target_count = count;
    rt_kprintf("reading_list: cover stats total=%u ready=%u no_cover=%u failed=%u unknown=%u target=%u\n",
               (unsigned int)local_stats.total_epub,
               (unsigned int)local_stats.ready_count,
               (unsigned int)local_stats.no_cover_count,
               (unsigned int)local_stats.failed_count,
               (unsigned int)local_stats.unknown_count,
               (unsigned int)local_stats.target_count);
    if (stats != NULL)
    {
        *stats = local_stats;
    }
    app_watchdog_progress(APP_WDT_MODULE_UI);
    ui_reading_perf_log("cover_collect_targets", start_tick);
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

static void ui_reading_reload_visible_covers(void)
{
    uint16_t i;

    ui_reading_release_card_cover(&s_reading_continue_card);
    for (i = 0U; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        ui_reading_release_card_cover(&s_reading_cards[i]);
    }
    ui_reading_list_request_full_refresh("cover_generated");
    ui_reading_list_render();
}

static void ui_reading_cover_progress_update(void)
{
    ui_reading_cover_worker_state_t state = ui_reading_cover_worker_snapshot();
    ui_reading_cover_result_stats_t result = ui_reading_cover_result_snapshot();
    uint16_t total = state.total;
    uint16_t done = state.done_count;
    char text[96];
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
        if (state.done && !state.running)
        {
            rt_snprintf(text,
                        sizeof(text),
                        "完成 成功%u 无封面%u 失败%u",
                        (unsigned int)result.ready_count,
                        (unsigned int)result.no_cover_count,
                        (unsigned int)result.failed_count);
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

static void ui_reading_cover_process_next_job(void)
{
    ui_reading_cover_worker_state_t worker_state = ui_reading_cover_worker_snapshot();
    uint16_t index;
    reading_cover_cache_state_t state;

    if (!worker_state.running || worker_state.done)
    {
        return;
    }

    if (worker_state.cancel)
    {
        ui_reading_cover_worker_mark_done(worker_state.done_count);
        return;
    }

    if (worker_state.done_count >= worker_state.total)
    {
        ui_reading_cover_worker_mark_done(worker_state.done_count);
        return;
    }

    index = worker_state.done_count;
    if (s_reading_cover_worker_paths[index][0] != '\0')
    {
        app_watchdog_begin_long_task(APP_WDT_MODULE_UI, UI_READING_COVER_BUILD_TIMEOUT_MS);
        state = reading_cover_cache_build(s_reading_cover_worker_paths[index],
                                          UI_READING_GRID_COVER_WIDTH,
                                          UI_READING_GRID_COVER_HEIGHT);
        app_watchdog_progress(APP_WDT_MODULE_UI);
        app_watchdog_end_long_task(APP_WDT_MODULE_UI);
        ui_reading_cover_result_add(state);
        rt_kprintf("reading_list: cover build result=%d path=%s\n",
                   (int)state,
                   s_reading_cover_worker_paths[index]);
    }

    ui_reading_cover_worker_set_progress(worker_state.total, (uint16_t)(index + 1U));
    if ((uint16_t)(index + 1U) >= worker_state.total)
    {
        ui_reading_cover_worker_mark_done((uint16_t)(index + 1U));
    }
}

static void ui_reading_cover_progress_timer_cb(lv_timer_t *timer)
{
    ui_reading_cover_worker_state_t state;

    LV_UNUSED(timer);

    if (ui_Reading_List == NULL)
    {
        return;
    }

    ui_reading_cover_process_next_job();

    state = ui_reading_cover_worker_snapshot();

    if (s_reading_cover_last_done_count != state.done_count)
    {
        s_reading_cover_last_done_count = state.done_count;
        ui_reading_cover_progress_update();
    }

    if (state.done && !state.running)
    {
        ui_reading_cover_progress_update();
        ui_reading_reload_visible_covers();
        if (s_reading_cover_finish_ticks++ >= 2U)
        {
            ui_reading_cover_prompt_close();
            ui_reading_cover_set_prompt_shown(false);
            if (s_reading_cover_progress_timer != NULL)
            {
                lv_timer_delete(s_reading_cover_progress_timer);
                s_reading_cover_progress_timer = NULL;
            }
        }
    }
}

static ui_reading_cover_start_result_t ui_reading_cover_start_worker(void)
{
    uint16_t i;
    uint16_t path_count = 0U;
    ui_reading_cover_worker_state_t state = ui_reading_cover_worker_snapshot();

    if (state.running)
    {
        rt_kprintf("reading_list: cover generation already running total=%u done=%u\n",
                   (unsigned int)state.total,
                   (unsigned int)state.done_count);
        return UI_READING_COVER_START_ALREADY_RUNNING;
    }

    if (s_reading_cover_missing_count == 0U)
    {
        return UI_READING_COVER_START_NO_MISSING;
    }

    memset(s_reading_cover_worker_paths, 0, sizeof(s_reading_cover_worker_paths));
    for (i = 0U; i < s_reading_cover_missing_count && path_count < UI_READING_COVER_BATCH_MAX; ++i)
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
        rt_kprintf("reading_list: cover generation no valid epub paths missing=%u\n",
                   (unsigned int)s_reading_cover_missing_count);
        ui_reading_cover_worker_mark_idle_done();
        return UI_READING_COVER_START_NO_VALID_PATH;
    }

    s_reading_cover_last_done_count = 0U;
    ui_reading_cover_result_reset();
    ui_reading_cover_worker_mark_started(path_count);
    ui_reading_cover_block_sleep();
    rt_kprintf("reading_list: cover generation ui task started paths=%u pending=%u interval=%u\n",
               (unsigned int)path_count,
               (unsigned int)s_reading_cover_missing_count,
               (unsigned int)UI_READING_COVER_PROGRESS_INTERVAL_MS);
    if (s_reading_cover_progress_timer == NULL)
    {
        s_reading_cover_progress_timer = lv_timer_create(ui_reading_cover_progress_timer_cb,
                                                         UI_READING_COVER_PROGRESS_INTERVAL_MS,
                                                         NULL);
    }
    return UI_READING_COVER_START_OK;
}

static void ui_reading_cover_prompt_cancel_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    rt_kprintf("reading_list: cover prompt cancel clicked\n");
    ui_reading_cover_set_dismissed_signature(true, s_reading_cover_prompt_stats.signature);
    ui_reading_cover_prompt_close();
    ui_reading_cover_set_prompt_shown(false);
}

static void ui_reading_cover_prompt_generate_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    rt_kprintf("reading_list: cover prompt generate clicked\n");
    if (lv_async_call(ui_reading_cover_generate_async_cb, NULL) != LV_RESULT_OK)
    {
        rt_kprintf("reading_list: cover generate async schedule failed\n");
    }
}

static void ui_reading_cover_generate_async_cb(void *user_data)
{
    ui_reading_cover_start_result_t start_result;
    ui_reading_cover_scan_stats_t stats;

    LV_UNUSED(user_data);

    if (ui_Reading_List == NULL)
    {
        return;
    }

    s_reading_cover_missing_count = ui_reading_cover_collect_targets(&stats);
    s_reading_cover_prompt_stats = stats;
    if (s_reading_cover_missing_count == 0U)
    {
        rt_kprintf("reading_list: cover prompt generate skipped targets=0 ready=%u no_cover=%u\n",
                   (unsigned int)stats.ready_count,
                   (unsigned int)stats.no_cover_count);
        ui_reading_cover_prompt_close();
        ui_reading_cover_set_prompt_shown(false);
        return;
    }

    ui_reading_cover_set_dismissed_signature(false, 0U);
    ui_reading_cover_prompt_close();
    ui_reading_cover_show_progress(s_reading_cover_missing_count > UI_READING_COVER_BATCH_MAX ?
                                       UI_READING_COVER_BATCH_MAX :
                                       s_reading_cover_missing_count);
    rt_kprintf("reading_list: cover generation start unknown=%u retry_failed=%u ready=%u no_cover=%u\n",
               (unsigned int)stats.unknown_count,
               (unsigned int)stats.failed_count,
               (unsigned int)stats.ready_count,
               (unsigned int)stats.no_cover_count);
    start_result = ui_reading_cover_start_worker();
    if (start_result != UI_READING_COVER_START_OK)
    {
        if (s_reading_cover_progress_label != NULL)
        {
            switch (start_result)
            {
            case UI_READING_COVER_START_ALREADY_RUNNING:
                lv_label_set_text(s_reading_cover_progress_label, "封面生成已在运行");
                break;
            case UI_READING_COVER_START_NO_VALID_PATH:
                lv_label_set_text(s_reading_cover_progress_label, "没有可生成封面的EPUB");
                break;
            case UI_READING_COVER_START_NO_MISSING:
            default:
                lv_label_set_text(s_reading_cover_progress_label, "没有需要生成的封面");
                break;
            }
        }
        ui_reading_cover_set_prompt_shown(false);
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
    lv_obj_set_style_bg_opa(s_reading_cover_prompt_overlay, LV_OPA_TRANSP, 0);
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

    ui_reading_cover_worker_set_progress(total, 0U);
    s_reading_cover_finish_ticks = 0U;
    ui_reading_cover_progress_update();
}

static void ui_reading_cover_show_prompt(const ui_reading_cover_scan_stats_t *stats)
{
    lv_obj_t *panel;
    lv_obj_t *button;
    lv_obj_t *label;
    char message[128];

    if (s_reading_cover_prompt_overlay != NULL ||
        ui_Reading_List == NULL ||
        stats == NULL ||
        stats->target_count == 0U)
    {
        return;
    }

    s_reading_cover_prompt_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_reading_cover_prompt_overlay);
    lv_obj_set_size(s_reading_cover_prompt_overlay, 528, 704);
    lv_obj_set_style_bg_opa(s_reading_cover_prompt_overlay, LV_OPA_TRANSP, 0);
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
    label = ui_create_label(panel,
                            "生成书籍封面",
                            0,
                            26,
                            420,
                            34,
                            24,
                            LV_TEXT_ALIGN_CENTER,
                            true,
                            false);
    ui_reading_make_touch_passthrough(label);
    if (stats->unknown_count > 0U && stats->failed_count > 0U)
    {
        rt_snprintf(message,
                    sizeof(message),
                    "发现 %u 本未生成封面\n另有 %u 本失败可重试",
                    (unsigned int)stats->unknown_count,
                    (unsigned int)stats->failed_count);
    }
    else if (stats->unknown_count > 0U)
    {
        rt_snprintf(message,
                    sizeof(message),
                    "发现 %u 本未生成封面\n生成后书架会自动刷新",
                    (unsigned int)stats->unknown_count);
    }
    else
    {
        rt_snprintf(message,
                    sizeof(message),
                    "%u 本封面上次生成失败\n是否重新尝试生成",
                    (unsigned int)stats->failed_count);
    }
    label = ui_create_label(panel,
                            message,
                            32,
                            74,
                            356,
                            64,
                            19,
                            LV_TEXT_ALIGN_CENTER,
                            false,
                            false);
    ui_reading_make_touch_passthrough(label);

    button = ui_reading_prompt_button(panel, 42, 156, 138, 44, 10, 0xffffff, 1);
    label = ui_create_label(button,
                            "稍后",
                            0,
                            ui_reading_centered_text_y(20, 44),
                            138,
                            24,
                            20,
                            LV_TEXT_ALIGN_CENTER,
                            false,
                            false);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, ui_reading_cover_prompt_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(label, ui_reading_cover_prompt_cancel_cb, LV_EVENT_CLICKED, NULL);

    button = ui_reading_prompt_button(panel, 240, 156, 138, 44, 10, 0x000000, 0);
    label = ui_create_label(button,
                            "生成",
                            0,
                            ui_reading_centered_text_y(20, 44),
                            138,
                            24,
                            20,
                            LV_TEXT_ALIGN_CENTER,
                            true,
                            true);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_add_event_cb(button, ui_reading_cover_prompt_generate_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(label, ui_reading_cover_prompt_generate_cb, LV_EVENT_CLICKED, NULL);
}

static void ui_reading_cover_maybe_prompt(void)
{
    if (ui_reading_cover_prompt_shown() ||
        ui_reading_cover_worker_snapshot().running ||
        ui_Reading_List == NULL)
    {
        return;
    }

    s_reading_cover_missing_count = s_reading_cover_prompt_scan_job.count;
    if (s_reading_cover_missing_count == 0U)
    {
        return;
    }
    if (ui_reading_cover_dismissed_signature_matches(s_reading_cover_prompt_scan_job.stats.signature))
    {
        return;
    }

    s_reading_cover_prompt_stats = s_reading_cover_prompt_scan_job.stats;
    ui_reading_cover_set_prompt_shown(true);
    ui_reading_cover_show_prompt(&s_reading_cover_prompt_stats);
}

static void ui_reading_cover_prompt_scan_reset(void)
{
    memset(&s_reading_cover_prompt_scan_job, 0, sizeof(s_reading_cover_prompt_scan_job));
}

static void ui_reading_cover_prompt_scan_begin(void)
{
    ui_reading_cover_prompt_scan_reset();
    memset(s_reading_cover_missing_indices, 0, sizeof(s_reading_cover_missing_indices));
    s_reading_cover_prompt_scan_job.active = true;
    s_reading_cover_prompt_scan_job.start_tick = rt_tick_get();
    s_reading_cover_prompt_scan_job.stats.signature = 2166136261UL;
    rt_kprintf("reading_list: cover prompt scan start files=%u\n",
               (unsigned int)s_reading_file_count);
}

static bool ui_reading_cover_prompt_scan_step(void)
{
    uint16_t processed = 0U;

    if (!s_reading_cover_prompt_scan_job.active)
    {
        return false;
    }

    if (s_reading_scan_state != UI_READING_SCAN_OK ||
        ui_reading_cover_prompt_shown() ||
        ui_reading_cover_worker_snapshot().running ||
        ui_Reading_List == NULL)
    {
        ui_reading_cover_prompt_scan_reset();
        return false;
    }

    while (processed < UI_READING_COVER_PROMPT_SCAN_SLICE_MAX &&
           s_reading_cover_prompt_scan_job.index < s_reading_file_count &&
           s_reading_cover_prompt_scan_job.count < UI_READING_MAX_FILES)
    {
        uint16_t file_index = s_reading_cover_prompt_scan_job.index++;
        reading_cover_cache_state_t state;

        ++processed;
        if (s_reading_files[file_index].file_type != UI_READING_FILE_TYPE_EPUB ||
            s_reading_files[file_index].path[0] == '\0')
        {
            continue;
        }

        ++s_reading_cover_prompt_scan_job.stats.total_epub;
        state = reading_cover_cache_get_state(s_reading_files[file_index].path,
                                              UI_READING_GRID_COVER_WIDTH,
                                              UI_READING_GRID_COVER_HEIGHT);
        s_reading_cover_prompt_scan_job.stats.signature =
            ui_reading_cover_signature_mix(s_reading_cover_prompt_scan_job.stats.signature,
                                           file_index,
                                           state);

        if (state == READING_COVER_CACHE_READY)
        {
            ++s_reading_cover_prompt_scan_job.stats.ready_count;
            continue;
        }
        if (state == READING_COVER_CACHE_NO_COVER)
        {
            ++s_reading_cover_prompt_scan_job.stats.no_cover_count;
            continue;
        }
        if (state == READING_COVER_CACHE_FAILED)
        {
            ++s_reading_cover_prompt_scan_job.stats.failed_count;
        }
        else
        {
            ++s_reading_cover_prompt_scan_job.stats.unknown_count;
        }

        s_reading_cover_missing_indices[s_reading_cover_prompt_scan_job.count] = file_index;
        ++s_reading_cover_prompt_scan_job.count;
    }

    app_watchdog_progress(APP_WDT_MODULE_UI);
    if (s_reading_cover_prompt_scan_job.index < s_reading_file_count &&
        s_reading_cover_prompt_scan_job.count < UI_READING_MAX_FILES)
    {
        return true;
    }

    s_reading_cover_prompt_scan_job.stats.target_count = s_reading_cover_prompt_scan_job.count;
    rt_kprintf("reading_list: cover prompt scan done total=%u ready=%u no_cover=%u failed=%u unknown=%u target=%u\n",
               (unsigned int)s_reading_cover_prompt_scan_job.stats.total_epub,
               (unsigned int)s_reading_cover_prompt_scan_job.stats.ready_count,
               (unsigned int)s_reading_cover_prompt_scan_job.stats.no_cover_count,
               (unsigned int)s_reading_cover_prompt_scan_job.stats.failed_count,
               (unsigned int)s_reading_cover_prompt_scan_job.stats.unknown_count,
               (unsigned int)s_reading_cover_prompt_scan_job.stats.target_count);
    ui_reading_perf_log("cover_prompt_scan_total", s_reading_cover_prompt_scan_job.start_tick);
    s_reading_cover_prompt_scan_job.active = false;
    ui_reading_cover_maybe_prompt();
    ui_reading_cover_prompt_scan_reset();
    return false;
}

static void ui_reading_cover_prompt_timer_cb(lv_timer_t *timer)
{
    if (!s_reading_cover_prompt_scan_job.active)
    {
        ui_reading_cover_prompt_scan_begin();
        lv_timer_set_period(timer, UI_READING_COVER_PROMPT_SCAN_INTERVAL_MS);
    }

    if (ui_reading_cover_prompt_scan_step())
    {
        return;
    }

    if (s_reading_cover_prompt_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_prompt_timer);
        s_reading_cover_prompt_timer = NULL;
    }
}

static void ui_reading_cover_request_prompt_deferred(void)
{
    if (s_reading_cover_prompt_timer != NULL ||
        ui_reading_cover_prompt_shown() ||
        ui_reading_cover_worker_snapshot().running ||
        ui_Reading_List == NULL)
    {
        return;
    }

#if UI_READING_DEBUG_AUTO_GENERATE_COVERS
    if (!s_reading_cover_debug_auto_started)
    {
        s_reading_cover_debug_auto_started = true;
        rt_kprintf("reading_list: debug auto cover generation scheduled\n");
        if (lv_async_call(ui_reading_cover_generate_async_cb, NULL) != LV_RESULT_OK)
        {
            rt_kprintf("reading_list: debug auto cover generation schedule failed\n");
        }
        return;
    }
#endif

    s_reading_cover_prompt_timer = lv_timer_create(ui_reading_cover_prompt_timer_cb,
                                                   UI_READING_COVER_PROMPT_DELAY_MS,
                                                   NULL);
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
    ui_reading_cover_worker_set_cancel(true);
    ui_reading_refresh_files(true);
    ui_reading_list_render();
    if (s_reading_scan_state == UI_READING_SCAN_OK)
    {
        ui_reading_cover_request_prompt_deferred();
    }
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
        if (refs->cover_img != NULL)
        {
            lv_image_set_src(refs->cover_img, NULL);
        }
        reading_cover_cache_release_image(&refs->cover_dsc);
        memset(&refs->cover_dsc, 0, sizeof(refs->cover_dsc));
        refs->cover_loaded = false;
    }
    refs->cover_path[0] = '\0';
}

static bool ui_reading_card_cover_matches(ui_reading_card_refs_t *refs, uint16_t file_index)
{
    char path[UI_READING_MAX_PATH_LEN];

    if (refs == NULL || !refs->cover_loaded || file_index >= s_reading_file_count)
    {
        return false;
    }

    if (!ui_reading_get_file_path(file_index, path, sizeof(path)))
    {
        return false;
    }

    return strcmp(refs->cover_path, path) == 0;
}

static void ui_reading_show_text_cover(ui_reading_card_refs_t *refs,
                                       uint16_t file_index,
                                       int cover_w,
                                       int cover_h)
{
    if (refs == NULL || refs->cover_box == NULL || refs->cover_title_label == NULL ||
        file_index >= s_reading_file_count)
    {
        return;
    }

    if (refs->cover_img != NULL)
    {
        lv_obj_add_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(refs->cover_img, NULL);
    }
    lv_obj_clear_flag(refs->cover_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(refs->cover_box, 1, LV_PART_MAIN);
    ui_reading_format_cover_title(s_reading_files[file_index].name,
                                  refs->cover_title_text,
                                  sizeof(refs->cover_title_text));
    ui_reading_layout_cover_title(refs->cover_title_label,
                                  cover_w,
                                  cover_h,
                                  refs->cover_title_text);
    lv_label_set_text_static(refs->cover_title_label, refs->cover_title_text);
}

static void ui_reading_bind_card_cover(ui_reading_card_refs_t *refs,
                                       uint16_t file_index,
                                       int cover_w,
                                       int cover_h,
                                       bool lazy_load)
{
    if (refs == NULL || file_index >= s_reading_file_count)
    {
        return;
    }

    refs->file_index = file_index;
    refs->has_file = true;
    refs->cover_pending = false;

    if (ui_reading_card_cover_matches(refs, file_index) && refs->cover_img != NULL)
    {
        lv_obj_clear_flag(refs->cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(refs->cover_img, &refs->cover_dsc);
        lv_obj_add_flag(refs->cover_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(refs->cover_box, 1, LV_PART_MAIN);
        return;
    }

    ui_reading_release_card_cover(refs);
    ui_reading_show_text_cover(refs, file_index, cover_w, cover_h);

    if (lazy_load && s_reading_files[file_index].file_type == UI_READING_FILE_TYPE_EPUB)
    {
        refs->cover_pending = true;
        ui_reading_schedule_cover_load();
    }
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
        lv_obj_set_style_border_width(refs->cover_box, 1, LV_PART_MAIN);
        return;
    }

    ui_reading_show_text_cover(refs, file_index, cover_w, cover_h);
}

static bool ui_reading_load_pending_card_cover(ui_reading_card_refs_t *refs,
                                               int cover_w,
                                               int cover_h)
{
    rt_tick_t start_tick;

    if (refs == NULL || !refs->has_file || !refs->cover_pending ||
        refs->file_index >= s_reading_file_count || refs->card == NULL ||
        lv_obj_has_flag(refs->card, LV_OBJ_FLAG_HIDDEN))
    {
        return false;
    }

    refs->cover_pending = false;
    start_tick = rt_tick_get();
    app_watchdog_progress(APP_WDT_MODULE_UI);
    ui_reading_update_cover(refs, refs->file_index, cover_w, cover_h);
    app_watchdog_progress(APP_WDT_MODULE_UI);
    ui_reading_perf_log("cover_load_one", start_tick);
    if (refs->card != NULL)
    {
        lv_obj_invalidate(refs->card);
    }
    if (s_reading_cover_loaded_since_refresh < UINT8_MAX)
    {
        ++s_reading_cover_loaded_since_refresh;
    }
    return true;
}

static void ui_reading_cover_load_timer_cb(lv_timer_t *timer)
{
    uint16_t i;
    uint8_t loaded = 0U;

    if (ui_Reading_List == NULL)
    {
        if (s_reading_cover_load_timer != NULL)
        {
            lv_timer_delete(s_reading_cover_load_timer);
            s_reading_cover_load_timer = NULL;
        }
        return;
    }

    if (s_reading_cover_loaded_since_refresh >= UI_READING_COVER_LOAD_MAX_PER_RENDER)
    {
        if (s_reading_cover_load_timer != NULL)
        {
            lv_timer_delete(s_reading_cover_load_timer);
            s_reading_cover_load_timer = NULL;
        }
        s_reading_cover_load_first_tick = false;
        ui_reading_list_request_full_refresh("cover_lazy_budget_done");
        app_watchdog_progress(APP_WDT_MODULE_UI);
        return;
    }

    if (s_reading_cover_load_first_tick)
    {
        s_reading_cover_load_first_tick = false;
        lv_timer_set_period(timer, UI_READING_COVER_LOAD_INTERVAL_MS);
    }

    if (ui_reading_load_pending_card_cover(&s_reading_continue_card,
                                           UI_READING_GRID_COVER_WIDTH,
                                           UI_READING_GRID_COVER_HEIGHT))
    {
        ++loaded;
    }

    for (i = 0U; i < UI_READING_VISIBLE_COUNT; ++i)
    {
        if (loaded >= UI_READING_COVER_LOAD_BATCH_SIZE)
        {
            return;
        }
        if (ui_reading_load_pending_card_cover(&s_reading_cards[i],
                                               UI_READING_GRID_COVER_WIDTH,
                                               UI_READING_GRID_COVER_HEIGHT))
        {
            ++loaded;
        }
    }

    if (loaded >= UI_READING_COVER_LOAD_BATCH_SIZE)
    {
        return;
    }

    if (s_reading_cover_load_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_load_timer);
        s_reading_cover_load_timer = NULL;
    }
    if (s_reading_cover_loaded_since_refresh > 0U)
    {
        s_reading_cover_loaded_since_refresh = 0U;
        ui_reading_list_request_full_refresh("cover_lazy_batch_done");
    }
    app_watchdog_progress(APP_WDT_MODULE_UI);
}

static void ui_reading_schedule_cover_load(void)
{
    if (ui_Reading_List == NULL || s_reading_cover_load_timer != NULL)
    {
        return;
    }

    s_reading_cover_load_first_tick = true;
    s_reading_cover_load_timer = lv_timer_create(ui_reading_cover_load_timer_cb,
                                                 UI_READING_COVER_LOAD_INITIAL_DELAY_MS,
                                                 NULL);
    s_reading_cover_loaded_since_refresh = 0U;
}

static void ui_reading_show_card(uint16_t slot_index, uint16_t file_index, bool clickable)
{
    ui_reading_card_refs_t *refs;

    if (slot_index >= UI_READING_VISIBLE_COUNT || file_index >= s_reading_file_count)
    {
        return;
    }

    refs = &s_reading_cards[slot_index];
    if (refs->card == NULL || refs->cover_title_label == NULL)
    {
        return;
    }

    lv_obj_clear_flag(refs->card, LV_OBJ_FLAG_HIDDEN);
    ui_reading_bind_card_cover(refs,
                               file_index,
                               UI_READING_GRID_COVER_WIDTH,
                               UI_READING_GRID_COVER_HEIGHT,
                               true);
    if (refs->title_label != NULL)
    {
        lv_obj_add_flag(refs->title_label, LV_OBJ_FLAG_HIDDEN);
    }

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
    refs->has_file = false;
    refs->cover_pending = false;
}

static void ui_reading_show_continue_card(uint16_t file_index)
{
    char progress_text[64];

    if (s_reading_continue_card.card == NULL || file_index >= s_reading_file_count)
    {
        return;
    }

    lv_obj_clear_flag(s_reading_continue_card.card, LV_OBJ_FLAG_HIDDEN);
    ui_reading_bind_card_cover(&s_reading_continue_card,
                               file_index,
                               UI_READING_GRID_COVER_WIDTH,
                               UI_READING_GRID_COVER_HEIGHT,
                               true);
    ui_reading_format_progress(file_index, progress_text, sizeof(progress_text));
    if (s_reading_continue_card.title_label != NULL)
    {
        ui_reading_format_continue_title(s_reading_files[file_index].name,
                                         s_reading_continue_card.title_text,
                                         sizeof(s_reading_continue_card.title_text));
        lv_obj_clear_flag(s_reading_continue_card.title_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_static(s_reading_continue_card.title_label,
                                 s_reading_continue_card.title_text);
    }
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
    s_reading_continue_card.has_file = false;
    s_reading_continue_card.cover_pending = false;
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

    if (s_reading_cover_prompt_overlay != NULL && !ui_reading_cover_worker_snapshot().running)
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

    if (s_reading_cover_prompt_overlay != NULL && !ui_reading_cover_worker_snapshot().running)
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

    refs->card = ui_reading_plain_obj(parent, x, y, UI_READING_GRID_COVER_WIDTH, 192, 0, LV_OPA_TRANSP, 0xffffff, 0);
    refs->cover_box = ui_reading_plain_obj(refs->card,
                                           0,
                                           0,
                                           UI_READING_GRID_COVER_WIDTH,
                                           UI_READING_GRID_COVER_HEIGHT,
                                           9,
                                           LV_OPA_COVER,
                                           0xffffff,
                                           1);
    ui_reading_make_touch_passthrough(refs->cover_box);
    refs->cover_img = ui_create_image_slot(refs->cover_box, 0, 0, UI_READING_GRID_COVER_WIDTH, UI_READING_GRID_COVER_HEIGHT);
    ui_reading_make_touch_passthrough(refs->cover_img);
    refs->cover_title_label = ui_create_label(refs->cover_box,
                                              "",
                                              17,
                                              54,
                                              UI_READING_GRID_COVER_WIDTH - 34,
                                              78,
                                              18,
                                              LV_TEXT_ALIGN_CENTER,
                                              true,
                                              true);
    ui_reading_make_touch_passthrough(refs->cover_title_label);
    lv_label_set_long_mode(refs->cover_title_label, LV_LABEL_LONG_WRAP);
    refs->title_label = ui_create_label(refs->card,
                                        "",
                                        16,
                                        126,
                                        UI_READING_GRID_COVER_WIDTH,
                                        52,
                                        18,
                                        LV_TEXT_ALIGN_CENTER,
                                        true,
                                        true);
    ui_reading_make_touch_passthrough(refs->title_label);
    lv_obj_add_flag(refs->title_label, LV_OBJ_FLAG_HIDDEN);
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
    refs->cover_box = ui_reading_plain_obj(refs->card, 23, 22, 140, 186, 8, LV_OPA_COVER, 0xffffff, 1);
    ui_reading_make_touch_passthrough(refs->cover_box);
    refs->cover_img = ui_create_image_slot(refs->cover_box, 0, 0, 140, 186);
    ui_reading_make_touch_passthrough(refs->cover_img);
    refs->cover_title_label = ui_create_label(refs->cover_box, "", 17, 54, 106, 78, 18, LV_TEXT_ALIGN_CENTER, true, true);
    ui_reading_make_touch_passthrough(refs->cover_title_label);
    lv_label_set_long_mode(refs->cover_title_label, LV_LABEL_LONG_WRAP);
    refs->title_label = ui_create_label(refs->card, "", 188, 34, 244, 56, 20, LV_TEXT_ALIGN_LEFT, true, false);
    ui_reading_make_touch_passthrough(refs->title_label);
    lv_label_set_long_mode(refs->title_label, LV_LABEL_LONG_WRAP);
    lv_obj_t *badge = ui_reading_plain_obj(refs->card, 188, 94, 150, 48, 24, LV_OPA_COVER, 0x343434, 0);
    ui_reading_make_touch_passthrough(badge);
    lv_obj_t *badge_label = ui_create_label(badge,
                                            "继续阅读",
                                            0,
                                            ui_reading_centered_text_y(24, 48),
                                            150,
                                            28,
                                            24,
                                            LV_TEXT_ALIGN_CENTER,
                                            true,
                                            true);
    ui_reading_make_touch_passthrough(badge_label);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    refs->meta_label = ui_create_label(refs->card, "", 188, 158, 192, 44, 20, LV_TEXT_ALIGN_LEFT, false, false);
    ui_reading_make_touch_passthrough(refs->meta_label);
    lv_label_set_long_mode(refs->meta_label, LV_LABEL_LONG_DOT);
    ui_reading_make_touch_passthrough(ui_create_label(refs->card, ">", 420, 96, 28, 44, 28, LV_TEXT_ALIGN_CENTER, true, false));
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
    if (s_reading_bottom_nav != NULL)
    {
        if (first_page)
        {
            lv_obj_clear_flag(s_reading_bottom_nav, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_reading_bottom_nav, LV_OBJ_FLAG_HIDDEN);
        }
    }

    can_prev = s_reading_page_offset > 0U;
    can_next = (uint16_t)(s_reading_page_offset + page_capacity) < s_reading_file_count;
    ui_reading_set_button_enabled(s_reading_prev_button, can_prev);
    ui_reading_set_button_enabled(s_reading_next_button, can_next);

    if (s_reading_page_label != NULL)
    {
        lv_obj_clear_flag(s_reading_page_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_reading_page_label,
                       ui_px_x(32),
                       ui_px_y(first_page ? UI_READING_FIRST_PAGE_TITLE_Y : UI_READING_NEXT_PAGE_TITLE_Y));
        if (first_page)
        {
            rt_snprintf(count_text,
                        sizeof(count_text),
                        ui_i18n_pick("全部书籍（%u）", "All Books (%u)"),
                        (unsigned int)s_reading_file_count);
        }
        else
        {
            rt_snprintf(count_text,
                        sizeof(count_text),
                        ui_i18n_pick("全部书籍（%u）", "All Books (%u)"),
                        (unsigned int)s_reading_file_count);
        }
        lv_label_set_text(s_reading_page_label, count_text);
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
    else if (s_reading_scan_state == UI_READING_SCAN_DIR_FAILED)
    {
        rt_snprintf(status_text,
                    sizeof(status_text),
                    "%s",
                    ui_i18n_pick("TF 卡 books 目录不可用", "TF books directory unavailable"));
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
    if (s_reading_bottom_nav != NULL)
    {
        lv_obj_clear_flag(s_reading_bottom_nav, LV_OBJ_FLAG_HIDDEN);
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
    ui_top_nav_create(ui_Reading_List, UI_TOP_TAB_AI);
    s_reading_bottom_nav = ui_bottom_nav_create(ui_Reading_List, UI_BOTTOM_TAB_BOOKS);

    s_reading_status_label = NULL;
    s_reading_status_text[0] = '\0';

    ui_reading_create_continue_card(ui_Reading_List);

    s_reading_page_label = ui_create_label(ui_Reading_List,
                                           "全部书籍（0）",
                                           32,
                                           UI_READING_FIRST_PAGE_TITLE_Y,
                                           260,
                                           40,
                                           28,
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

    if (ui_reading_cover_worker_snapshot().running && s_reading_cover_progress_timer == NULL)
    {
        s_reading_cover_progress_timer = lv_timer_create(ui_reading_cover_progress_timer_cb,
                                                         UI_READING_COVER_PROGRESS_INTERVAL_MS,
                                                         NULL);
    }

    if (s_reading_refresh_timer == NULL)
    {
        s_reading_refresh_timer = lv_timer_create(ui_reading_refresh_timer_cb, UI_READING_REFRESH_FAST_MS, NULL);
    }

    if (s_reading_open_timer == NULL)
    {
        s_reading_open_timer = lv_timer_create(ui_reading_open_timer_cb, 60, NULL);
    }
}

void ui_Reading_List_screen_destroy(void)
{
    uint16_t i;

    ui_reading_scan_job_cancel();
    ui_reading_cover_worker_set_cancel(true);
    if (ui_reading_cover_worker_snapshot().running)
    {
        ui_reading_cover_worker_mark_done(ui_reading_cover_worker_snapshot().done_count);
    }
    else
    {
        ui_reading_cover_unblock_sleep();
    }
    ui_reading_set_enter_refresh_requested(false);

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
    ui_reading_cover_prompt_scan_reset();

    if (s_reading_cover_load_timer != NULL)
    {
        lv_timer_delete(s_reading_cover_load_timer);
        s_reading_cover_load_timer = NULL;
    }
    s_reading_cover_load_first_tick = false;

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
    s_reading_bottom_nav = NULL;
    s_reading_open_detail_pending = false;
    ui_reading_set_enter_refresh_requested(false);
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
