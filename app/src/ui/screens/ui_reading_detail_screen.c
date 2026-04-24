#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

#include "dfs_posix.h"
#include "rtthread.h"
#include "rthw.h"
#include "app_watchdog.h"
#include "audio_mem.h"
#include "drv_lcd.h"
#include "mem_section.h"
#include "reading_epub.h"
#include "ui.h"
#include "ui_i18n.h"
#include "config/app_config.h"
#include "ui_font_manager.h"
#include "ui_helpers.h"
#include "../ui_image_policy.h"
#include "ui_runtime_adapter.h"
#include "../../../../sdk/external/lvgl_v9/src/draw/lv_draw_buf.h"

#define UI_READING_DETAIL_MAX_FILE_BYTES (256 * 1024)
#define UI_READING_DETAIL_MAX_PAGE_COUNT 768U
#define UI_READING_DETAIL_PAGE_BUFFER_BYTES 12288U
#define UI_READING_DETAIL_LOAD_THREAD_STACK_SIZE (64 * 1024)
#define UI_READING_DETAIL_LOAD_THREAD_PRIORITY 22
#define UI_READING_DETAIL_LOAD_THREAD_TICK 10
#define UI_READING_DETAIL_LOAD_TIMER_MS 30U
#define UI_READING_DETAIL_PROGRESS_TIMER_MS 200U
#define UI_READING_DETAIL_IMAGE_WIDTH LCD_HOR_RES_MAX
#define UI_READING_DETAIL_IMAGE_DECODE_HEIGHT 4095
#define UI_READING_DETAIL_READING_BOX_HEIGHT 736
#define UI_READING_DETAIL_IMAGE_TEXT_GAP 8
#define UI_READING_DETAIL_TEXT_WIDTH 460
#define UI_READING_DETAIL_TEXT_HEIGHT 720
#define UI_READING_DETAIL_TEXT_FONT 22
#define UI_READING_DETAIL_TEXT_LINE_SPACE 2
#define UI_READING_DETAIL_TEXT_FONT_MIN 18
#define UI_READING_DETAIL_TEXT_FONT_MAX 30
#define UI_READING_DETAIL_TEXT_FONT_STEP 2
#define UI_READING_DETAIL_TEXT_LINE_SPACE_MIN 0
#define UI_READING_DETAIL_TEXT_LINE_SPACE_MAX 12
#define UI_READING_DETAIL_TEXT_LINE_SPACE_STEP 1
#define UI_READING_DETAIL_FONT_ITEM_MAX 32U
#define UI_READING_DETAIL_GLYPH_COVERAGE_THRESHOLD 160U
#define UI_READING_DETAIL_BITMAP_PALETTE_BYTES \
    (LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I4) * sizeof(lv_color32_t))
#define UI_READING_DETAIL_BITMAP_GLYPH_SCRATCH_BYTES 4096U
#define UI_READING_DETAIL_HDFONT_PACKAGE_MAGIC "HDFPKG1"
#define UI_READING_DETAIL_HDFONT_FACE_MAGIC "HDFNTC1"
#define UI_READING_DETAIL_HDFONT_PACKAGE_HEADER_SIZE 64U
#define UI_READING_DETAIL_HDFONT_PACKAGE_DIR_ENTRY_SIZE 16U
#define UI_READING_DETAIL_HDFONT_FACE_HEADER_SIZE 64U
#define UI_READING_DETAIL_HDFONT_GLYPH_ENTRY_SIZE 24U
#define UI_READING_DETAIL_HDFONT_FLAG_RLE 0x0001U
#define UI_READING_DETAIL_HDFONT_FLAG_ALPHA8 0x0002U
#define UI_READING_DETAIL_SETTINGS_PANEL_HEIGHT 222
#define UI_READING_DETAIL_SETTINGS_VALUE_WIDTH 76
#define UI_READING_DETAIL_SETTINGS_FONT_NAME_WIDTH 164
#define UI_READING_DETAIL_SWIPE_HANDLE_HEIGHT 48
#define UI_READING_DETAIL_MAX_BLOCK_COUNT 256U
#define UI_READING_DETAIL_MAX_IMAGE_COUNT 48U
#define UI_READING_DETAIL_MAX_SPINE_ITEM_COUNT 96U
#define UI_READING_DETAIL_TEXT_STREAM_WINDOW_BYTES (128 * 1024U)
#define UI_READING_DETAIL_NAV_LOCK_MS 520U

#ifndef UI_EPD_REFRESH_LOG_ENABLED
#define UI_EPD_REFRESH_LOG_ENABLED 0
#endif

#if UI_EPD_REFRESH_LOG_ENABLED
#define UI_EPD_REFRESH_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define UI_EPD_REFRESH_LOG(...) do { } while (0)
#endif

typedef struct
{
    lv_obj_t *reading_box;
    lv_obj_t *content_label;
    lv_obj_t *content_image;
    lv_obj_t *page_label;
    lv_obj_t *file_name_label;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;
    lv_obj_t *settings_overlay;
    lv_obj_t *settings_panel;
    lv_obj_t *font_value_label;
    lv_obj_t *line_space_value_label;
    lv_obj_t *font_family_value_label;
    lv_obj_t *font_dec_button;
    lv_obj_t *font_inc_button;
    lv_obj_t *line_space_dec_button;
    lv_obj_t *line_space_inc_button;
    lv_obj_t *font_family_prev_button;
    lv_obj_t *font_family_next_button;
    lv_obj_t *swipe_handle;
} ui_reading_detail_refs_t;

typedef struct
{
    bool active;
    bool triggered;
    lv_point_t start_point;
    lv_point_t last_point;
} ui_reading_detail_swipe_state_t;

typedef enum
{
    UI_READING_DETAIL_LOAD_IDLE = 0,
    UI_READING_DETAIL_LOAD_LOADING,
    UI_READING_DETAIL_LOAD_READY,
} ui_reading_detail_load_state_t;

typedef enum
{
    UI_READING_DETAIL_PAGE_TEXT = 0,
    UI_READING_DETAIL_PAGE_IMAGE,
    UI_READING_DETAIL_PAGE_IMAGE_TEXT,
} ui_reading_detail_page_type_t;

typedef enum
{
    UI_READING_DETAIL_TEXT_SOURCE_NONE = 0,
    UI_READING_DETAIL_TEXT_SOURCE_MEMORY,
    UI_READING_DETAIL_TEXT_SOURCE_FILE,
} ui_reading_detail_text_source_type_t;

typedef struct
{
    ui_reading_detail_page_type_t type;
    uint32_t start;
    uint32_t end;
    uint16_t image_index;
    uint16_t image_height;
    uint16_t text_height;
} ui_reading_detail_page_entry_t;

typedef struct
{
    uint32_t codepoint;
    uint32_t bitmap_offset;
    uint32_t bitmap_length;
    int32_t advance26;
    int16_t x_off;
    int16_t y_off;
    uint16_t width;
    uint16_t height;
} ui_reading_detail_hdfont_glyph_t;

typedef struct
{
    bool ready;
    int fd;
    bool index_from_audio;
    uint16_t size_px;
    uint16_t flags;
    uint32_t face_offset;
    uint32_t glyph_count;
    uint32_t line_height26;
    uint32_t ascent26;
    uint32_t descent26;
    uint32_t index_offset;
    uint32_t bitmap_offset;
    uint32_t bitmap_size;
    uint8_t *index_data;
    char path[UI_FONT_MANAGER_PATH_MAX];
} ui_reading_detail_hdfont_state_t;

lv_obj_t *ui_Reading_Detail = NULL;

static ui_reading_detail_refs_t s_reading_detail_refs;
static struct rt_thread s_reading_detail_load_thread;
static struct rt_semaphore s_reading_detail_load_sem;
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(reading_detail_load_thread_stack)
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_reading_detail_load_thread_stack[UI_READING_DETAIL_LOAD_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t s_reading_detail_load_thread_stack[UI_READING_DETAIL_LOAD_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(reading_detail_load_thread_stack);
#endif
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(reading_detail_text_buffer)
static char s_reading_detail_text[UI_READING_DETAIL_MAX_FILE_BYTES + 1U];
L2_RET_BSS_SECT_END
#else
static char s_reading_detail_text[UI_READING_DETAIL_MAX_FILE_BYTES + 1U]
    L2_RET_BSS_SECT(reading_detail_text_buffer);
#endif
static char s_reading_detail_first_page_layout[UI_READING_DETAIL_PAGE_BUFFER_BYTES];
static char s_reading_detail_page_buffer[UI_READING_DETAIL_PAGE_BUFFER_BYTES];
static char s_reading_detail_page_text[32];
static ui_reading_detail_page_entry_t s_reading_detail_pages[UI_READING_DETAIL_MAX_PAGE_COUNT];
static reading_epub_block_t s_reading_detail_epub_blocks[UI_READING_DETAIL_MAX_BLOCK_COUNT];
static reading_epub_image_ref_t s_reading_detail_epub_images[UI_READING_DETAIL_MAX_IMAGE_COUNT];
static reading_epub_spine_item_t s_reading_detail_epub_spine[UI_READING_DETAIL_MAX_SPINE_ITEM_COUNT];
static volatile uint16_t s_reading_detail_page_count = 0U;
static uint16_t s_reading_detail_current_page = 0U;
static bool s_reading_detail_render_in_progress = false;
static uint32_t s_reading_detail_nav_lock_until_ms = 0U;
static lv_timer_t *s_reading_detail_load_timer = NULL;
static volatile bool s_reading_detail_has_last_page = false;
static volatile uint16_t s_reading_detail_epub_block_count = 0U;
static volatile uint16_t s_reading_detail_epub_image_count = 0U;
static volatile uint16_t s_reading_detail_epub_spine_count = 0U;
static uint16_t s_reading_detail_epub_current_chapter = 0U;
static bool s_reading_detail_epub_lazy_mode = false;
static bool s_reading_detail_load_worker_started = false;
static bool s_reading_detail_first_render_done = false;
static bool s_reading_detail_image_transition_refresh_pending = false;
static uint16_t s_reading_detail_last_reported_page_count = 0U;
static bool s_reading_detail_last_reported_has_last_page = false;
static volatile uint32_t s_reading_detail_request_id = 0U;
static volatile uint32_t s_reading_detail_completed_request_id = 0U;
static volatile ui_reading_detail_load_state_t s_reading_detail_load_state = UI_READING_DETAIL_LOAD_IDLE;
static volatile bool s_reading_detail_first_page_layout_ready = false;
static volatile bool s_reading_detail_first_page_bitmap_ready = false;
static char s_reading_detail_request_path[256];
static lv_image_dsc_t s_reading_detail_current_image_dsc;
static uint8_t *s_reading_detail_bitmap_storage = NULL;
static uint8_t *s_reading_detail_bitmap_pixels = NULL;
static uint32_t s_reading_detail_bitmap_pixel_bytes = 0U;
static lv_draw_buf_t s_reading_detail_bitmap_draw_buf;
static lv_image_dsc_t s_reading_detail_bitmap_dsc;
static bool s_reading_detail_bitmap_inited = false;
static ui_reading_detail_hdfont_state_t s_reading_detail_hdfont;
static uint8_t s_reading_detail_glyph_scratch[UI_READING_DETAIL_BITMAP_GLYPH_SCRATCH_BYTES];
static ui_font_manager_item_t s_reading_detail_font_items[UI_READING_DETAIL_FONT_ITEM_MAX];
static uint16_t s_reading_detail_font_item_count = 0U;
static uint16_t s_reading_detail_font_item_index = 0U;
static bool s_reading_detail_font_use_system = true;
static char s_reading_detail_font_path[UI_FONT_MANAGER_PATH_MAX];
static char s_reading_detail_font_name[UI_FONT_MANAGER_NAME_MAX] = "系统字体";
static char s_reading_detail_last_rejected_font_path[UI_FONT_MANAGER_PATH_MAX];
static ui_reading_detail_text_source_type_t s_reading_detail_text_source = UI_READING_DETAIL_TEXT_SOURCE_NONE;
static int s_reading_detail_text_fd = -1;
static uint32_t s_reading_detail_text_length = 0U;
static uint32_t s_reading_detail_text_bom_skip = 0U;
static uint32_t s_reading_detail_text_window_start = 0U;
static uint32_t s_reading_detail_text_window_len = 0U;
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(reading_detail_text_stream_window)
static char s_reading_detail_text_stream_window[UI_READING_DETAIL_TEXT_STREAM_WINDOW_BYTES];
L2_RET_BSS_SECT_END
#else
static char s_reading_detail_text_stream_window[UI_READING_DETAIL_TEXT_STREAM_WINDOW_BYTES]
    L2_RET_BSS_SECT(reading_detail_text_stream_window);
#endif
static uint16_t s_reading_detail_font_size = UI_READING_DETAIL_TEXT_FONT;
static uint16_t s_reading_detail_line_space = UI_READING_DETAIL_TEXT_LINE_SPACE;
static ui_reading_detail_swipe_state_t s_reading_detail_swipe_state;
static uint16_t s_reading_detail_watchdog_long_task_depth = 0U;

#define UI_READING_DETAIL_WATCHDOG_LONG_TASK_TIMEOUT_MS 60000U

static void ui_reading_detail_watchdog_begin_long_task(void)
{
    if (s_reading_detail_watchdog_long_task_depth == 0U)
    {
        app_watchdog_begin_long_task(APP_WDT_MODULE_READING,
                                     UI_READING_DETAIL_WATCHDOG_LONG_TASK_TIMEOUT_MS);
    }

    ++s_reading_detail_watchdog_long_task_depth;
}

static void ui_reading_detail_watchdog_progress(void)
{
    if (s_reading_detail_watchdog_long_task_depth > 0U)
    {
        app_watchdog_progress(APP_WDT_MODULE_READING);
    }
    else
    {
        app_watchdog_heartbeat(APP_WDT_MODULE_READING);
    }
}

static void ui_reading_detail_watchdog_end_long_task(void)
{
    if (s_reading_detail_watchdog_long_task_depth == 0U)
    {
        return;
    }

    --s_reading_detail_watchdog_long_task_depth;
    if (s_reading_detail_watchdog_long_task_depth == 0U)
    {
        app_watchdog_end_long_task(APP_WDT_MODULE_READING);
    }
}

static uint16_t ui_reading_detail_get_actual_font_size(void);
static void ui_reading_detail_load_config_defaults(void);
static void ui_reading_detail_save_font_selection(void);
static void ui_reading_detail_save_layout(void);
static void ui_reading_detail_save_config(void);
static bool ui_reading_detail_init_hdfont(void);
static bool ui_reading_detail_has_hdfont_suffix(const char *path);
static bool ui_reading_detail_hdfont_find_glyph(uint32_t codepoint,
                                                ui_reading_detail_hdfont_glyph_t *glyph);
static uint16_t ui_reading_detail_hdfont_glyph_width(uint32_t codepoint,
                                                     uint16_t fallback_width);
static bool ui_reading_detail_render_text_hdfont_bitmap(const char *formatted_text);

static lv_coord_t ui_reading_detail_get_text_offset_x(void)
{
    return (lv_coord_t)((UI_READING_DETAIL_IMAGE_WIDTH - UI_READING_DETAIL_TEXT_WIDTH) / 2);
}

static void ui_reading_detail_set_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL)
    {
        return;
    }

    lv_label_set_text(label, text != NULL ? text : "");
}

static void ui_reading_detail_refresh_file_name_label(void)
{
    if (s_reading_detail_refs.file_name_label == NULL)
    {
        return;
    }

    ui_reading_detail_set_label_text(s_reading_detail_refs.file_name_label,
                                     ui_reading_list_get_selected_name());
}

static void ui_reading_detail_release_hdfont(void)
{
    if (s_reading_detail_hdfont.index_data != NULL)
    {
        if (s_reading_detail_hdfont.index_from_audio)
        {
            audio_mem_free(s_reading_detail_hdfont.index_data);
        }
        else
        {
            rt_free(s_reading_detail_hdfont.index_data);
        }
    }

    if (s_reading_detail_hdfont.ready && s_reading_detail_hdfont.fd >= 0)
    {
        close(s_reading_detail_hdfont.fd);
    }

    memset(&s_reading_detail_hdfont, 0, sizeof(s_reading_detail_hdfont));
    s_reading_detail_hdfont.fd = -1;
}

static void ui_reading_detail_release_font_resources(void)
{
    ui_reading_detail_release_hdfont();
}

static void ui_reading_detail_load_config_defaults(void)
{
    char font_path[UI_FONT_MANAGER_PATH_MAX];

    s_reading_detail_font_size = app_config_get_reading_font_size();
    if (s_reading_detail_font_size < UI_READING_DETAIL_TEXT_FONT_MIN)
    {
        s_reading_detail_font_size = UI_READING_DETAIL_TEXT_FONT_MIN;
    }
    else if (s_reading_detail_font_size > UI_READING_DETAIL_TEXT_FONT_MAX)
    {
        s_reading_detail_font_size = UI_READING_DETAIL_TEXT_FONT_MAX;
    }

    s_reading_detail_line_space = app_config_get_reading_line_space();
    if (s_reading_detail_line_space < UI_READING_DETAIL_TEXT_LINE_SPACE_MIN)
    {
        s_reading_detail_line_space = UI_READING_DETAIL_TEXT_LINE_SPACE_MIN;
    }
    else if (s_reading_detail_line_space > UI_READING_DETAIL_TEXT_LINE_SPACE_MAX)
    {
        s_reading_detail_line_space = UI_READING_DETAIL_TEXT_LINE_SPACE_MAX;
    }

    s_reading_detail_font_use_system = app_config_get_reading_use_system_font();
    app_config_get_reading_font_path(font_path, sizeof(font_path));
    s_reading_detail_font_path[0] = '\0';
    s_reading_detail_font_name[0] = '\0';
    s_reading_detail_last_rejected_font_path[0] = '\0';

    if (s_reading_detail_font_use_system || font_path[0] == '\0')
    {
        s_reading_detail_font_use_system = true;
        rt_strncpy(s_reading_detail_font_name,
                   ui_i18n_pick("系统字体", "System Font"),
                   sizeof(s_reading_detail_font_name) - 1U);
    }
    else
    {
        const char *base;

        rt_strncpy(s_reading_detail_font_path, font_path, sizeof(s_reading_detail_font_path) - 1U);
        base = strrchr(font_path, '/');
        base = (base != NULL) ? (base + 1) : font_path;
        rt_strncpy(s_reading_detail_font_name, base, sizeof(s_reading_detail_font_name) - 1U);
    }

    s_reading_detail_font_path[sizeof(s_reading_detail_font_path) - 1U] = '\0';
    s_reading_detail_font_name[sizeof(s_reading_detail_font_name) - 1U] = '\0';
}

static void ui_reading_detail_save_font_selection(void)
{
    app_config_set_reading_use_system_font(s_reading_detail_font_use_system);
    app_config_set_reading_font_path(s_reading_detail_font_use_system ? "" : s_reading_detail_font_path);
}

static void ui_reading_detail_save_layout(void)
{
    app_config_set_reading_font_size(s_reading_detail_font_size);
    app_config_set_reading_line_space(s_reading_detail_line_space);
}

static void ui_reading_detail_save_config(void)
{
    ui_reading_detail_save_font_selection();
    ui_reading_detail_save_layout();
    app_config_save();
}

static const char *ui_reading_detail_get_selected_font_name(void)
{
    if (s_reading_detail_font_use_system)
    {
        return ui_i18n_pick("系统字体", "System Font");
    }

    return s_reading_detail_font_name[0] != '\0' ?
           s_reading_detail_font_name :
           ui_i18n_pick("未知字体", "Unknown Font");
}

static bool ui_reading_detail_get_selected_font_path(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U ||
        s_reading_detail_font_use_system ||
        s_reading_detail_font_path[0] == '\0')
    {
        return false;
    }

    rt_strncpy(buffer, s_reading_detail_font_path, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
    return true;
}

static bool ui_reading_detail_should_log_rejected_font(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (strcmp(s_reading_detail_last_rejected_font_path, path) == 0)
    {
        return false;
    }

    rt_strncpy(s_reading_detail_last_rejected_font_path,
               path,
               sizeof(s_reading_detail_last_rejected_font_path) - 1U);
    s_reading_detail_last_rejected_font_path[sizeof(s_reading_detail_last_rejected_font_path) - 1U] = '\0';
    return true;
}

static bool ui_reading_detail_font_file_usable(const char *path, bool verbose)
{
    struct stat st;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!ui_reading_detail_has_hdfont_suffix(path))
    {
        if (verbose && ui_reading_detail_should_log_rejected_font(path))
        {
            rt_kprintf("reading_detail: reject non-hdfont %s\n", path);
        }
        return false;
    }

    if (stat(path, &st) != 0 || st.st_size <= 0)
    {
        if (verbose && ui_reading_detail_should_log_rejected_font(path))
        {
            rt_kprintf("reading_detail: reject font missing %s\n", path);
        }
        return false;
    }

    return true;
}

static bool ui_reading_detail_ascii_ieq(char left, char right)
{
    if (left >= 'A' && left <= 'Z')
    {
        left = (char)(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z')
    {
        right = (char)(right + ('a' - 'A'));
    }
    return left == right;
}

static bool ui_reading_detail_has_hdfont_suffix(const char *path)
{
    const char *suffix = ".hdfont";
    size_t path_len;
    size_t suffix_len;
    size_t i;

    if (path == NULL)
    {
        return false;
    }

    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len < suffix_len)
    {
        return false;
    }

    path += path_len - suffix_len;
    for (i = 0U; i < suffix_len; ++i)
    {
        if (!ui_reading_detail_ascii_ieq(path[i], suffix[i]))
        {
            return false;
        }
    }

    return true;
}

static uint16_t ui_reading_detail_rd_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t ui_reading_detail_rd_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int16_t ui_reading_detail_rd_i16(const uint8_t *data)
{
    return (int16_t)ui_reading_detail_rd_u16(data);
}

static int32_t ui_reading_detail_rd_i32(const uint8_t *data)
{
    return (int32_t)ui_reading_detail_rd_u32(data);
}

static bool ui_reading_detail_hdfont_read_exact(int fd,
                                                uint32_t offset,
                                                void *buffer,
                                                size_t length)
{
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0U;

    if (fd < 0 || buffer == NULL)
    {
        return false;
    }

    if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
    {
        return false;
    }

    while (done < length)
    {
        int ret = read(fd, out + done, length - done);
        if (ret <= 0)
        {
            return false;
        }
        done += (size_t)ret;
    }

    return true;
}

static bool ui_reading_detail_hdfont_parse_face_header(const uint8_t *header,
                                                       uint32_t face_offset,
                                                       uint32_t face_length,
                                                       ui_reading_detail_hdfont_state_t *state)
{
    uint32_t index_offset;
    uint32_t bitmap_offset;
    uint32_t bitmap_size;

    if (memcmp(header, UI_READING_DETAIL_HDFONT_FACE_MAGIC, 7U) != 0)
    {
        return false;
    }

    index_offset = ui_reading_detail_rd_u32(header + 32U);
    bitmap_offset = ui_reading_detail_rd_u32(header + 36U);
    bitmap_size = ui_reading_detail_rd_u32(header + 40U);
    if (index_offset < UI_READING_DETAIL_HDFONT_FACE_HEADER_SIZE ||
        bitmap_offset < index_offset ||
        bitmap_size == 0U)
    {
        return false;
    }

    if (face_length > 0U &&
        (bitmap_offset > face_length || bitmap_size > face_length - bitmap_offset))
    {
        return false;
    }

    state->flags = ui_reading_detail_rd_u16(header + 10U);
    state->size_px = ui_reading_detail_rd_u16(header + 12U);
    state->glyph_count = ui_reading_detail_rd_u32(header + 16U);
    state->line_height26 = ui_reading_detail_rd_u32(header + 20U);
    state->ascent26 = ui_reading_detail_rd_u32(header + 24U);
    state->descent26 = ui_reading_detail_rd_u32(header + 28U);
    state->face_offset = face_offset;
    state->index_offset = face_offset + index_offset;
    state->bitmap_offset = face_offset + bitmap_offset;
    state->bitmap_size = bitmap_size;

    if (state->glyph_count == 0U || state->glyph_count > 65535U ||
        (state->flags & UI_READING_DETAIL_HDFONT_FLAG_ALPHA8) == 0U)
    {
        return false;
    }

    return true;
}

static bool ui_reading_detail_hdfont_select_face(int fd,
                                                 uint16_t requested_size,
                                                 ui_reading_detail_hdfont_state_t *state)
{
    uint8_t header[UI_READING_DETAIL_HDFONT_PACKAGE_HEADER_SIZE];
    uint8_t face_header[UI_READING_DETAIL_HDFONT_FACE_HEADER_SIZE];
    uint16_t face_count;
    uint32_t dir_offset;
    uint16_t dir_entry_size;
    uint32_t best_face_offset = 0U;
    uint32_t best_face_length = 0U;
    uint16_t best_delta = 0xFFFFU;
    uint16_t i;

    if (!ui_reading_detail_hdfont_read_exact(fd, 0U, header, sizeof(header)))
    {
        return false;
    }

    if (memcmp(header, UI_READING_DETAIL_HDFONT_FACE_MAGIC, 7U) == 0)
    {
        return ui_reading_detail_hdfont_parse_face_header(header, 0U, 0U, state);
    }

    if (memcmp(header, UI_READING_DETAIL_HDFONT_PACKAGE_MAGIC, 7U) != 0)
    {
        return false;
    }

    face_count = ui_reading_detail_rd_u16(header + 12U);
    dir_offset = ui_reading_detail_rd_u32(header + 16U);
    dir_entry_size = ui_reading_detail_rd_u16(header + 20U);
    if (face_count == 0U ||
        face_count > 128U ||
        dir_offset < UI_READING_DETAIL_HDFONT_PACKAGE_HEADER_SIZE ||
        dir_entry_size < UI_READING_DETAIL_HDFONT_PACKAGE_DIR_ENTRY_SIZE)
    {
        return false;
    }

    for (i = 0U; i < face_count; ++i)
    {
        uint8_t dir_entry[UI_READING_DETAIL_HDFONT_PACKAGE_DIR_ENTRY_SIZE];
        uint16_t face_size;
        uint16_t delta;
        uint32_t face_offset;
        uint32_t face_length;

        if (!ui_reading_detail_hdfont_read_exact(fd,
                                                 dir_offset + (uint32_t)i * dir_entry_size,
                                                 dir_entry,
                                                 sizeof(dir_entry)))
        {
            return false;
        }

        face_size = ui_reading_detail_rd_u16(dir_entry);
        face_offset = ui_reading_detail_rd_u32(dir_entry + 4U);
        face_length = ui_reading_detail_rd_u32(dir_entry + 8U);
        delta = (face_size > requested_size) ?
                (uint16_t)(face_size - requested_size) :
                (uint16_t)(requested_size - face_size);

        if (best_face_offset == 0U || delta < best_delta)
        {
            best_delta = delta;
            best_face_offset = face_offset;
            best_face_length = face_length;
        }

        if (delta == 0U)
        {
            break;
        }
    }

    if (best_face_offset == 0U ||
        !ui_reading_detail_hdfont_read_exact(fd,
                                             best_face_offset,
                                             face_header,
                                             sizeof(face_header)))
    {
        return false;
    }

    return ui_reading_detail_hdfont_parse_face_header(face_header,
                                                     best_face_offset,
                                                     best_face_length,
                                                     state);
}

static bool ui_reading_detail_init_hdfont(void)
{
    char font_path[UI_FONT_MANAGER_PATH_MAX];
    uint16_t actual_size;
    uint32_t index_bytes;
    int fd;
    ui_reading_detail_hdfont_state_t next_state;

    if (!ui_reading_detail_get_selected_font_path(font_path, sizeof(font_path)) ||
        !ui_reading_detail_has_hdfont_suffix(font_path))
    {
        if (s_reading_detail_hdfont.ready)
        {
            ui_reading_detail_release_hdfont();
        }
        return false;
    }

    actual_size = ui_reading_detail_get_actual_font_size();
    if (s_reading_detail_hdfont.ready &&
        s_reading_detail_hdfont.size_px == actual_size &&
        strcmp(s_reading_detail_hdfont.path, font_path) == 0)
    {
        return true;
    }

    fd = open(font_path, O_RDONLY, 0);
    if (fd < 0)
    {
        rt_kprintf("reading_detail: hdfont open failed %s\n", font_path);
        ui_reading_detail_release_hdfont();
        return false;
    }

    memset(&next_state, 0, sizeof(next_state));
    next_state.fd = fd;
    if (!ui_reading_detail_hdfont_select_face(fd, actual_size, &next_state))
    {
        rt_kprintf("reading_detail: hdfont parse failed %s size=%u\n",
                   font_path,
                   (unsigned int)actual_size);
        close(fd);
        ui_reading_detail_release_hdfont();
        return false;
    }

    index_bytes = next_state.glyph_count * UI_READING_DETAIL_HDFONT_GLYPH_ENTRY_SIZE;
    next_state.index_data = (uint8_t *)audio_mem_malloc(index_bytes);
    next_state.index_from_audio = true;
    if (next_state.index_data == NULL)
    {
        next_state.index_data = (uint8_t *)rt_malloc(index_bytes);
        next_state.index_from_audio = false;
    }
    if (next_state.index_data == NULL)
    {
        rt_kprintf("reading_detail: hdfont index alloc failed glyphs=%lu bytes=%lu\n",
                   (unsigned long)next_state.glyph_count,
                   (unsigned long)index_bytes);
        close(fd);
        ui_reading_detail_release_hdfont();
        return false;
    }

    if (!ui_reading_detail_hdfont_read_exact(fd,
                                             next_state.index_offset,
                                             next_state.index_data,
                                             index_bytes))
    {
        rt_kprintf("reading_detail: hdfont index read failed %s\n", font_path);
        if (next_state.index_from_audio)
        {
            audio_mem_free(next_state.index_data);
        }
        else
        {
            rt_free(next_state.index_data);
        }
        close(fd);
        ui_reading_detail_release_hdfont();
        return false;
    }

    rt_strncpy(next_state.path, font_path, sizeof(next_state.path) - 1U);
    next_state.path[sizeof(next_state.path) - 1U] = '\0';
    next_state.ready = true;

    ui_reading_detail_release_hdfont();
    s_reading_detail_hdfont = next_state;
    rt_kprintf("reading_detail: hdfont loaded %s size=%u glyphs=%lu face=%lu\n",
               font_path,
               (unsigned int)s_reading_detail_hdfont.size_px,
               (unsigned long)s_reading_detail_hdfont.glyph_count,
               (unsigned long)s_reading_detail_hdfont.face_offset);
    return true;
}

static void ui_reading_detail_hdfont_parse_glyph(uint32_t index,
                                                 ui_reading_detail_hdfont_glyph_t *glyph)
{
    const uint8_t *entry;

    entry = s_reading_detail_hdfont.index_data +
            index * UI_READING_DETAIL_HDFONT_GLYPH_ENTRY_SIZE;
    glyph->codepoint = ui_reading_detail_rd_u32(entry);
    glyph->bitmap_offset = ui_reading_detail_rd_u32(entry + 4U);
    glyph->bitmap_length = ui_reading_detail_rd_u32(entry + 8U);
    glyph->advance26 = ui_reading_detail_rd_i32(entry + 12U);
    glyph->x_off = ui_reading_detail_rd_i16(entry + 16U);
    glyph->y_off = ui_reading_detail_rd_i16(entry + 18U);
    glyph->width = ui_reading_detail_rd_u16(entry + 20U);
    glyph->height = ui_reading_detail_rd_u16(entry + 22U);
}

static bool ui_reading_detail_hdfont_find_glyph(uint32_t codepoint,
                                                ui_reading_detail_hdfont_glyph_t *glyph)
{
    uint32_t left = 0U;
    uint32_t right;

    if (!s_reading_detail_hdfont.ready ||
        s_reading_detail_hdfont.index_data == NULL ||
        glyph == NULL)
    {
        return false;
    }

    right = s_reading_detail_hdfont.glyph_count;
    while (left < right)
    {
        uint32_t mid = left + (right - left) / 2U;
        ui_reading_detail_hdfont_glyph_t current;

        ui_reading_detail_hdfont_parse_glyph(mid, &current);
        if (current.codepoint == codepoint)
        {
            *glyph = current;
            return true;
        }
        if (current.codepoint < codepoint)
        {
            left = mid + 1U;
        }
        else
        {
            right = mid;
        }
    }

    return false;
}

static uint16_t ui_reading_detail_hdfont_glyph_width(uint32_t codepoint,
                                                     uint16_t fallback_width)
{
    ui_reading_detail_hdfont_glyph_t glyph;
    int32_t advance;

    if (!ui_reading_detail_init_hdfont() ||
        !ui_reading_detail_hdfont_find_glyph(codepoint, &glyph))
    {
        return fallback_width;
    }

    advance = (glyph.advance26 + 32) >> 6;
    if (advance <= 0)
    {
        return fallback_width;
    }

    return (uint16_t)advance;
}

static bool ui_reading_detail_set_selected_font_item(uint16_t index)
{
    const ui_font_manager_item_t *item;
    bool changed;

    if (index >= s_reading_detail_font_item_count)
    {
        return false;
    }

    item = &s_reading_detail_font_items[index];
    if (!item->system && !ui_reading_detail_font_file_usable(item->path, true))
    {
        return false;
    }

    changed = (s_reading_detail_font_use_system != item->system);
    if (!item->system)
    {
        changed = changed || strcmp(s_reading_detail_font_path, item->path) != 0;
    }

    s_reading_detail_font_item_index = index;
    s_reading_detail_font_use_system = item->system;
    if (item->system)
    {
        s_reading_detail_font_path[0] = '\0';
        rt_strncpy(s_reading_detail_font_name,
                   ui_i18n_pick("系统字体", "System Font"),
                   sizeof(s_reading_detail_font_name) - 1U);
    }
    else
    {
        rt_strncpy(s_reading_detail_font_path, item->path, sizeof(s_reading_detail_font_path) - 1U);
        rt_strncpy(s_reading_detail_font_name, item->name, sizeof(s_reading_detail_font_name) - 1U);
    }
    s_reading_detail_font_path[sizeof(s_reading_detail_font_path) - 1U] = '\0';
    s_reading_detail_font_name[sizeof(s_reading_detail_font_name) - 1U] = '\0';

    if (changed)
    {
        ui_reading_detail_release_font_resources();
    }

    return true;
}

static void ui_reading_detail_refresh_font_items(void)
{
    ui_font_manager_item_t discovered[UI_READING_DETAIL_FONT_ITEM_MAX];
    bool previous_system = app_config_get_reading_use_system_font();
    char previous_path[UI_FONT_MANAGER_PATH_MAX];
    char configured_path[UI_FONT_MANAGER_PATH_MAX];
    uint16_t count;
    uint16_t index = 0U;
    uint16_t i;

    app_config_get_reading_font_path(configured_path, sizeof(configured_path));
    previous_path[0] = '\0';
    if (!previous_system)
    {
        rt_strncpy(previous_path, configured_path, sizeof(previous_path) - 1U);
    }
    previous_path[sizeof(previous_path) - 1U] = '\0';

    count = ui_font_manager_list_items(discovered,
                                       (uint16_t)(sizeof(discovered) /
                                                  sizeof(discovered[0])));
    s_reading_detail_font_item_count = 0U;
    memset(s_reading_detail_font_items, 0, sizeof(s_reading_detail_font_items));
    for (i = 0U; i < count && s_reading_detail_font_item_count < UI_READING_DETAIL_FONT_ITEM_MAX; ++i)
    {
        if (!discovered[i].system &&
            (!ui_reading_detail_has_hdfont_suffix(discovered[i].path) ||
             !ui_reading_detail_font_file_usable(discovered[i].path, true)))
        {
            continue;
        }

        s_reading_detail_font_items[s_reading_detail_font_item_count] = discovered[i];
        s_reading_detail_font_item_count++;
    }

    count = s_reading_detail_font_item_count;
    if (count == 0U)
    {
        memset(&s_reading_detail_font_items[0], 0, sizeof(s_reading_detail_font_items[0]));
        rt_strncpy(s_reading_detail_font_items[0].name,
                   ui_i18n_pick("系统字体", "System Font"),
                   sizeof(s_reading_detail_font_items[0].name) - 1U);
        s_reading_detail_font_items[0].system = true;
        count = 1U;
    }

    for (i = 0U; i < count; ++i)
    {
        if (previous_system && s_reading_detail_font_items[i].system)
        {
            index = i;
            break;
        }
        if (!previous_system &&
            !s_reading_detail_font_items[i].system &&
            strcmp(previous_path, s_reading_detail_font_items[i].path) == 0)
        {
            index = i;
            break;
        }
    }

    s_reading_detail_font_item_count = count;
    if (!ui_reading_detail_set_selected_font_item(index))
    {
        (void)ui_reading_detail_set_selected_font_item(0U);
    }
}

static bool ui_reading_detail_using_external_font(void)
{
    return !s_reading_detail_font_use_system && s_reading_detail_font_path[0] != '\0';
}

static uint32_t ui_reading_detail_now_ms(void)
{
    return rt_tick_get_millisecond();
}

static void ui_reading_detail_clear_navigation_lock(void)
{
    s_reading_detail_render_in_progress = false;
    s_reading_detail_nav_lock_until_ms = 0U;
}

static void ui_reading_detail_arm_navigation_lock(uint32_t duration_ms)
{
    s_reading_detail_nav_lock_until_ms = ui_reading_detail_now_ms() + duration_ms;
}

static bool ui_reading_detail_navigation_locked(void)
{
    if (s_reading_detail_render_in_progress)
    {
        return true;
    }

    if (s_reading_detail_nav_lock_until_ms == 0U)
    {
        return false;
    }

    return ((int32_t)(ui_reading_detail_now_ms() - s_reading_detail_nav_lock_until_ms) < 0);
}

static void ui_reading_detail_next_page(void);
static void ui_reading_detail_refresh_page_label(void);
static void ui_reading_detail_refresh_nav_buttons(void);
static void ui_reading_detail_set_button_enabled(lv_obj_t *button, bool enabled);
static bool ui_reading_detail_load_text_from_path(const char *file_path);
static bool ui_reading_detail_load_epub_from_path(const char *file_path);
static void ui_reading_detail_load_selected_sync(void);
static bool ui_reading_detail_render_page(void);
static bool ui_reading_detail_selected_matches_request(void);
static bool ui_reading_detail_render_text_bitmap(const char *formatted_text);
static void ui_reading_detail_refresh_settings_panel(void);
static void ui_reading_detail_set_settings_visible(bool visible);
static bool ui_reading_detail_settings_visible(void);
static void ui_reading_detail_apply_text_style(void);
static void ui_reading_detail_rebuild_layout(void);
static bool ui_reading_detail_has_rebuild_source(void);
static void ui_reading_detail_bottom_swipe_event_cb(lv_event_t *e);
static uint16_t ui_reading_detail_get_glyph_width_fast(const lv_font_t *font,
                                                       uint32_t codepoint,
                                                       uint32_t codepoint_next,
                                                       uint16_t fallback_width);
static int32_t ui_reading_detail_get_text_line_height_px(void);
static const lv_font_t *ui_reading_detail_get_text_font(void);
static bool ui_reading_detail_append_page_entry(ui_reading_detail_page_type_t type,
                                                uint32_t start,
                                                uint32_t end,
                                                uint16_t image_index);
static bool ui_reading_detail_append_page_entry_ex(ui_reading_detail_page_type_t type,
                                                   uint32_t start,
                                                   uint32_t end,
                                                   uint16_t image_index,
                                                   uint16_t image_height,
                                                   uint16_t text_height);
static void ui_reading_detail_reset_text_source(void);
static void ui_reading_detail_use_memory_text(void);
static bool ui_reading_detail_open_text_stream(const char *file_path);
static uint32_t ui_reading_detail_get_text_length(void);
static char ui_reading_detail_get_text_byte(uint32_t index);
static uint32_t ui_reading_detail_source_utf8_next(uint32_t *index);
static size_t ui_reading_detail_copy_text_range(char *buffer,
                                                size_t buffer_size,
                                                uint32_t start,
                                                uint32_t end);
static uint16_t ui_reading_detail_snapshot_page_count(bool *has_last_page);
static uint32_t ui_reading_detail_measure_page_range(uint32_t start,
                                                     uint32_t end_limit,
                                                     char *formatted_buffer,
                                                     size_t formatted_buffer_size);
static uint32_t ui_reading_detail_measure_page_range_limited(uint32_t start,
                                                             uint32_t end_limit,
                                                             char *formatted_buffer,
                                                             size_t formatted_buffer_size,
                                                             uint16_t max_lines);
static bool ui_reading_detail_paginate_text_range(uint32_t start, uint32_t end);
static void ui_reading_detail_reset_epub_lazy_state(void);
static bool ui_reading_detail_load_epub_chapter(uint16_t chapter_index);
static void ui_reading_detail_release_current_image(void);
static bool ui_reading_detail_page_has_image(const ui_reading_detail_page_entry_t *page);
static void ui_reading_detail_prepare_page_transition_refresh(uint16_t target_page);
static void ui_reading_detail_queue_full_refresh_on_input(const char *source);
static bool ui_reading_detail_consume_image_transition_refresh(const char *reason);
static bool ui_reading_detail_probe_image_layout(uint16_t image_index,
                                                 uint16_t *scaled_height_out,
                                                 bool *cropped_out);
static uint16_t ui_reading_detail_get_max_lines_for_height(lv_coord_t text_height);
static bool ui_reading_detail_paginate_epub_blocks(void);

static uint16_t ui_reading_detail_get_actual_font_size(void)
{
    return ui_scaled_font_size(s_reading_detail_font_size);
}

static lv_coord_t ui_reading_detail_get_line_space(void)
{
    return ui_px_y((int32_t)s_reading_detail_line_space);
}

static const lv_font_t *ui_reading_detail_get_text_font(void)
{
    return ui_font_get_actual(ui_reading_detail_get_actual_font_size());
}

static void ui_reading_detail_apply_text_style(void)
{
    if (s_reading_detail_refs.content_label == NULL)
    {
        return;
    }

    lv_obj_set_style_text_font(s_reading_detail_refs.content_label,
                               ui_reading_detail_get_text_font(),
                               0);
    lv_obj_set_style_text_line_space(s_reading_detail_refs.content_label,
                                     ui_reading_detail_get_line_space(),
                                     0);
    lv_obj_invalidate(s_reading_detail_refs.content_label);
}

static void ui_reading_detail_release_text_bitmap_buffer(void)
{
    if (s_reading_detail_bitmap_storage != NULL)
    {
        audio_mem_free(s_reading_detail_bitmap_storage);
    }

    s_reading_detail_bitmap_storage = NULL;
    s_reading_detail_bitmap_pixels = NULL;
    s_reading_detail_bitmap_pixel_bytes = 0U;
    memset(&s_reading_detail_bitmap_draw_buf, 0, sizeof(s_reading_detail_bitmap_draw_buf));
    memset(&s_reading_detail_bitmap_dsc, 0, sizeof(s_reading_detail_bitmap_dsc));
    s_reading_detail_bitmap_inited = false;
    s_reading_detail_first_page_bitmap_ready = false;
}

static void ui_reading_detail_release_current_image(void)
{
    if (ui_Reading_Detail != NULL && s_reading_detail_refs.content_image != NULL)
    {
        lv_image_set_scale(s_reading_detail_refs.content_image, LV_SCALE_NONE);
        lv_image_set_pivot(s_reading_detail_refs.content_image, 0, 0);
        lv_image_set_antialias(s_reading_detail_refs.content_image, false);
        lv_image_set_inner_align(s_reading_detail_refs.content_image, LV_IMAGE_ALIGN_TOP_LEFT);
        ui_image_set_src(s_reading_detail_refs.content_image, NULL);
        lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    }

    reading_epub_release_image(&s_reading_detail_current_image_dsc);
    memset(&s_reading_detail_current_image_dsc, 0, sizeof(s_reading_detail_current_image_dsc));
}

static bool ui_reading_detail_page_has_image(const ui_reading_detail_page_entry_t *page)
{
    if (page == NULL)
    {
        return false;
    }

    return page->type == UI_READING_DETAIL_PAGE_IMAGE ||
           page->type == UI_READING_DETAIL_PAGE_IMAGE_TEXT;
}

static void ui_reading_detail_prepare_page_transition_refresh(uint16_t target_page)
{
    bool has_last_page = false;
    uint16_t page_count;

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    (void)has_last_page;
    if (page_count == 0U ||
        s_reading_detail_current_page >= page_count ||
        target_page >= page_count)
    {
        return;
    }

    if (ui_image_page_transition_requires_full(
            ui_reading_detail_page_has_image(&s_reading_detail_pages[s_reading_detail_current_page]),
            ui_reading_detail_page_has_image(&s_reading_detail_pages[target_page])))
    {
        s_reading_detail_image_transition_refresh_pending = true;
        UI_EPD_REFRESH_LOG("reading_detail: image full refresh queued current=%u target=%u\n",
                           (unsigned int)(s_reading_detail_current_page + 1U),
                           (unsigned int)(target_page + 1U));
    }
}

static void ui_reading_detail_queue_full_refresh_on_input(const char *source)
{
    bool has_last_page = false;
    uint16_t page_count;

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    (void)has_last_page;
    if (page_count == 0U || s_reading_detail_current_page >= page_count)
    {
        return;
    }

    if (!ui_reading_detail_page_has_image(&s_reading_detail_pages[s_reading_detail_current_page]))
    {
        return;
    }

    if (!s_reading_detail_image_transition_refresh_pending)
    {
        s_reading_detail_image_transition_refresh_pending = true;
        UI_EPD_REFRESH_LOG("reading_detail: image full refresh queued by input source=%s page=%u\n",
                           source != NULL ? source : "?",
                           (unsigned int)(s_reading_detail_current_page + 1U));
    }
}

static bool ui_reading_detail_consume_image_transition_refresh(const char *reason)
{
    if (!s_reading_detail_image_transition_refresh_pending)
    {
        return false;
    }

    UI_EPD_REFRESH_LOG("reading_detail: image full refresh consume reason=%s page=%u\n",
                       reason != NULL ? reason : "?",
                       (unsigned int)(s_reading_detail_current_page + 1U));
    lcd_request_epd_force_full_refresh_once();
    s_reading_detail_image_transition_refresh_pending = false;
    return true;
}

void ui_reading_detail_request_leave_refresh(void)
{
    bool has_last_page = false;
    bool current_page_has_image = false;
    uint16_t page_count;

    if (ui_Reading_Detail == NULL)
    {
        return;
    }

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    (void)has_last_page;
    if (page_count > 0U && s_reading_detail_current_page < page_count)
    {
        current_page_has_image = ui_reading_detail_page_has_image(
            &s_reading_detail_pages[s_reading_detail_current_page]);
    }

    if (ui_reading_detail_consume_image_transition_refresh("leave"))
    {
        return;
    }

    if (!current_page_has_image)
    {
        return;
    }

    UI_EPD_REFRESH_LOG("reading_detail: image full refresh request on leave page=%u\n",
                       (unsigned int)(s_reading_detail_current_page + 1U));
    lcd_request_epd_force_full_refresh_once();
}

static uint32_t ui_reading_detail_compute_image_zoom(uint16_t src_width)
{
    uint32_t image_width;
    uint32_t zoom;

    if (src_width == 0U)
    {
        return LV_SCALE_NONE;
    }

    image_width = (uint32_t)ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH);
    zoom = ((image_width * (uint32_t)LV_SCALE_NONE) + ((uint32_t)src_width / 2U)) /
           (uint32_t)src_width;
    if (zoom == 0U)
    {
        zoom = 1U;
    }

    return zoom;
}

static uint16_t ui_reading_detail_compute_scaled_height(uint16_t src_width, uint16_t src_height)
{
    uint32_t zoom;
    uint32_t scaled_height;

    if (src_width == 0U || src_height == 0U)
    {
        return 0U;
    }

    zoom = ui_reading_detail_compute_image_zoom(src_width);
    scaled_height = (((uint32_t)src_height * zoom) + (uint32_t)(LV_SCALE_NONE / 2U)) /
                    (uint32_t)LV_SCALE_NONE;
    if (scaled_height == 0U)
    {
        scaled_height = 1U;
    }

    return (uint16_t)LV_MIN(scaled_height, 0xFFFFU);
}

static bool ui_reading_detail_probe_image_layout(uint16_t image_index,
                                                 uint16_t *scaled_height_out,
                                                 bool *cropped_out)
{
    uint16_t src_width = 0U;
    uint16_t src_height = 0U;
    uint32_t scaled_height;
    uint32_t viewport_height;

    if (scaled_height_out != NULL)
    {
        *scaled_height_out = 0U;
    }
    if (cropped_out != NULL)
    {
        *cropped_out = false;
    }

    if (image_index >= s_reading_detail_epub_image_count)
    {
        return false;
    }

    viewport_height = (uint32_t)ui_px_h(UI_READING_DETAIL_READING_BOX_HEIGHT);
    if (viewport_height == 0U)
    {
        return false;
    }

    if (!reading_epub_probe_image_size(s_reading_detail_request_path,
                                       s_reading_detail_epub_images[image_index].internal_path,
                                       &src_width,
                                       &src_height) ||
        src_width == 0U || src_height == 0U)
    {
        return false;
    }

    scaled_height = ui_reading_detail_compute_scaled_height(src_width, src_height);
    if (scaled_height == 0U)
    {
        scaled_height = 1U;
    }

    if (scaled_height_out != NULL)
    {
        *scaled_height_out = (uint16_t)LV_MIN(scaled_height, 0xFFFFU);
    }
    if (cropped_out != NULL)
    {
        *cropped_out = scaled_height > viewport_height;
    }

    return true;
}

static uint16_t ui_reading_detail_get_max_lines_for_height(lv_coord_t text_height)
{
    int32_t max_height;
    int32_t line_space;
    int32_t line_height;
    uint16_t max_lines;

    max_height = text_height;
    if (max_height <= 0)
    {
        return 0U;
    }

    line_space = ui_reading_detail_get_line_space();
    line_height = ui_reading_detail_get_text_line_height_px() + line_space;
    if (line_height <= 0)
    {
        line_height = 1;
    }

    max_lines = (uint16_t)((max_height + line_space) / line_height);
    if (max_lines == 0U)
    {
        max_lines = 1U;
    }

    return max_lines;
}

static bool ui_reading_detail_ensure_bitmap_buffer(void)
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_bytes;
    uint32_t data_size;
    uint8_t *image_data;
    uint8_t level;

    if (s_reading_detail_bitmap_storage != NULL)
    {
        return true;
    }

    width = (uint32_t)ui_px_w(UI_READING_DETAIL_TEXT_WIDTH);
    height = (uint32_t)ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT);
    stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I4);
    pixel_bytes = stride * height;
    data_size = UI_READING_DETAIL_BITMAP_PALETTE_BYTES + pixel_bytes;

    image_data = (uint8_t *)audio_mem_malloc(data_size);
    if (image_data == NULL)
    {
        rt_kprintf("reading_detail: bitmap alloc failed bytes=%lu\n",
                   (unsigned long)data_size);
        return false;
    }

    memset(image_data, 0, data_size);
    if (lv_draw_buf_init(&s_reading_detail_bitmap_draw_buf,
                         width,
                         height,
                         LV_COLOR_FORMAT_I4,
                         stride,
                         image_data,
                         data_size) != LV_RESULT_OK)
    {
        audio_mem_free(image_data);
        rt_kprintf("reading_detail: bitmap draw_buf init failed bytes=%lu\n",
                   (unsigned long)data_size);
        return false;
    }

    for (level = 0U; level < 16U; ++level)
    {
        uint8_t gray = (uint8_t)(255U - (level * 255U / 15U));
        lv_draw_buf_set_palette(&s_reading_detail_bitmap_draw_buf,
                                level,
                                lv_color32_make(gray, gray, gray, 255));
    }

    s_reading_detail_bitmap_storage = image_data;
    s_reading_detail_bitmap_pixels = image_data + UI_READING_DETAIL_BITMAP_PALETTE_BYTES;
    s_reading_detail_bitmap_pixel_bytes = pixel_bytes;
    lv_draw_buf_to_image(&s_reading_detail_bitmap_draw_buf, &s_reading_detail_bitmap_dsc);
    s_reading_detail_bitmap_inited = true;
    return true;
}

static uint32_t ui_reading_detail_utf8_next(const char *text, uint32_t *index)
{
    uint32_t i;
    uint32_t codepoint;
    unsigned char first;

    if (text == NULL)
    {
        return 0U;
    }

    i = index != NULL ? *index : 0U;
    first = (unsigned char)text[i];
    if (first == '\0')
    {
        return 0U;
    }

    if ((first & 0x80U) == 0U)
    {
        codepoint = first;
        i += 1U;
    }
    else if ((first & 0xE0U) == 0xC0U &&
             (text[i + 1U] != '\0'))
    {
        codepoint = ((uint32_t)(first & 0x1FU) << 6) |
                    (uint32_t)((unsigned char)text[i + 1U] & 0x3FU);
        i += 2U;
    }
    else if ((first & 0xF0U) == 0xE0U &&
             (text[i + 1U] != '\0') &&
             (text[i + 2U] != '\0'))
    {
        codepoint = ((uint32_t)(first & 0x0FU) << 12) |
                    ((uint32_t)((unsigned char)text[i + 1U] & 0x3FU) << 6) |
                    (uint32_t)((unsigned char)text[i + 2U] & 0x3FU);
        i += 3U;
    }
    else if ((first & 0xF8U) == 0xF0U &&
             (text[i + 1U] != '\0') &&
             (text[i + 2U] != '\0') &&
             (text[i + 3U] != '\0'))
    {
        codepoint = ((uint32_t)(first & 0x07U) << 18) |
                    ((uint32_t)((unsigned char)text[i + 1U] & 0x3FU) << 12) |
                    ((uint32_t)((unsigned char)text[i + 2U] & 0x3FU) << 6) |
                    (uint32_t)((unsigned char)text[i + 3U] & 0x3FU);
        i += 4U;
    }
    else
    {
        codepoint = first;
        i += 1U;
    }

    if (index != NULL)
    {
        *index = i;
    }

    return codepoint;
}

static void ui_reading_detail_reset_text_source(void)
{
    if (s_reading_detail_text_fd >= 0)
    {
        close(s_reading_detail_text_fd);
        s_reading_detail_text_fd = -1;
    }

    s_reading_detail_text_source = UI_READING_DETAIL_TEXT_SOURCE_NONE;
    s_reading_detail_text_length = 0U;
    s_reading_detail_text_bom_skip = 0U;
    s_reading_detail_text_window_start = 0U;
    s_reading_detail_text_window_len = 0U;
    memset(s_reading_detail_text_stream_window, 0, sizeof(s_reading_detail_text_stream_window));
}

static void ui_reading_detail_use_memory_text(void)
{
    ui_reading_detail_reset_text_source();
    s_reading_detail_text_source = UI_READING_DETAIL_TEXT_SOURCE_MEMORY;
    s_reading_detail_text_length = (uint32_t)strlen(s_reading_detail_text);
}

static bool ui_reading_detail_open_text_stream(const char *file_path)
{
    int fd;
    off_t file_size;
    unsigned char bom[3];
    ssize_t bom_read;

    ui_reading_detail_reset_text_source();
    if (file_path == NULL || file_path[0] == '\0')
    {
        return false;
    }

    fd = open(file_path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0 || lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        return false;
    }

    memset(bom, 0, sizeof(bom));
    bom_read = read(fd, bom, sizeof(bom));
    if (bom_read >= 3 &&
        bom[0] == 0xEFU &&
        bom[1] == 0xBBU &&
        bom[2] == 0xBFU)
    {
        s_reading_detail_text_bom_skip = 3U;
    }
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        return false;
    }

    s_reading_detail_text_fd = fd;
    s_reading_detail_text_source = UI_READING_DETAIL_TEXT_SOURCE_FILE;
    s_reading_detail_text_length =
        (uint32_t)((file_size > (off_t)s_reading_detail_text_bom_skip) ?
                       (file_size - (off_t)s_reading_detail_text_bom_skip) :
                       0);
    return true;
}

static uint32_t ui_reading_detail_get_text_length(void)
{
    if (s_reading_detail_text_source == UI_READING_DETAIL_TEXT_SOURCE_MEMORY)
    {
        return (uint32_t)strlen(s_reading_detail_text);
    }

    if (s_reading_detail_text_source == UI_READING_DETAIL_TEXT_SOURCE_FILE)
    {
        return s_reading_detail_text_length;
    }

    return 0U;
}

static bool ui_reading_detail_ensure_text_window(uint32_t index)
{
    uint32_t read_start;
    ssize_t read_size;

    if (s_reading_detail_text_source != UI_READING_DETAIL_TEXT_SOURCE_FILE ||
        s_reading_detail_text_fd < 0)
    {
        return false;
    }

    if (index < s_reading_detail_text_window_start ||
        index >= (s_reading_detail_text_window_start + s_reading_detail_text_window_len))
    {
        read_start = index;
        if (lseek(s_reading_detail_text_fd,
                  (off_t)(read_start + s_reading_detail_text_bom_skip),
                  SEEK_SET) < 0)
        {
            s_reading_detail_text_window_len = 0U;
            return false;
        }

        read_size = read(s_reading_detail_text_fd,
                         s_reading_detail_text_stream_window,
                         sizeof(s_reading_detail_text_stream_window));
        if (read_size <= 0)
        {
            s_reading_detail_text_window_len = 0U;
            return false;
        }

        s_reading_detail_text_window_start = read_start;
        s_reading_detail_text_window_len = (uint32_t)read_size;
    }

    return true;
}

static char ui_reading_detail_get_text_byte(uint32_t index)
{
    if (s_reading_detail_text_source == UI_READING_DETAIL_TEXT_SOURCE_MEMORY)
    {
        return s_reading_detail_text[index];
    }

    if (s_reading_detail_text_source == UI_READING_DETAIL_TEXT_SOURCE_FILE)
    {
        if (index >= s_reading_detail_text_length || !ui_reading_detail_ensure_text_window(index))
        {
            return '\0';
        }

        return s_reading_detail_text_stream_window[index - s_reading_detail_text_window_start];
    }

    return '\0';
}

static uint32_t ui_reading_detail_source_utf8_next(uint32_t *index)
{
    uint32_t i;
    uint32_t codepoint;
    unsigned char first;
    char second;
    char third;
    char fourth;

    i = index != NULL ? *index : 0U;
    first = (unsigned char)ui_reading_detail_get_text_byte(i);
    if (first == '\0')
    {
        return 0U;
    }

    second = ui_reading_detail_get_text_byte(i + 1U);
    third = ui_reading_detail_get_text_byte(i + 2U);
    fourth = ui_reading_detail_get_text_byte(i + 3U);

    if ((first & 0x80U) == 0U)
    {
        codepoint = first;
        i += 1U;
    }
    else if ((first & 0xE0U) == 0xC0U && second != '\0')
    {
        codepoint = ((uint32_t)(first & 0x1FU) << 6) |
                    (uint32_t)((unsigned char)second & 0x3FU);
        i += 2U;
    }
    else if ((first & 0xF0U) == 0xE0U && second != '\0' && third != '\0')
    {
        codepoint = ((uint32_t)(first & 0x0FU) << 12) |
                    ((uint32_t)((unsigned char)second & 0x3FU) << 6) |
                    (uint32_t)((unsigned char)third & 0x3FU);
        i += 3U;
    }
    else if ((first & 0xF8U) == 0xF0U && second != '\0' && third != '\0' && fourth != '\0')
    {
        codepoint = ((uint32_t)(first & 0x07U) << 18) |
                    ((uint32_t)((unsigned char)second & 0x3FU) << 12) |
                    ((uint32_t)((unsigned char)third & 0x3FU) << 6) |
                    (uint32_t)((unsigned char)fourth & 0x3FU);
        i += 4U;
    }
    else
    {
        codepoint = first;
        i += 1U;
    }

    if (index != NULL)
    {
        *index = i;
    }

    return codepoint;
}

static size_t ui_reading_detail_copy_text_range(char *buffer,
                                                size_t buffer_size,
                                                uint32_t start,
                                                uint32_t end)
{
    size_t written = 0U;
    uint32_t i;

    if (buffer == NULL || buffer_size == 0U)
    {
        return 0U;
    }

    buffer[0] = '\0';
    for (i = start; i < end && written + 1U < buffer_size; ++i)
    {
        char ch = ui_reading_detail_get_text_byte(i);

        if (ch == '\0')
        {
            break;
        }

        buffer[written++] = ch;
    }

    buffer[written] = '\0';
    return written;
}

static uint16_t ui_reading_detail_snapshot_page_count(bool *has_last_page)
{
    rt_base_t level;
    uint16_t page_count;
    bool last_page;

    level = rt_hw_interrupt_disable();
    page_count = s_reading_detail_page_count;
    last_page = s_reading_detail_has_last_page;
    rt_hw_interrupt_enable(level);

    if (has_last_page != NULL)
    {
        *has_last_page = last_page;
    }

    return page_count;
}

static bool ui_reading_detail_request_is_current(uint32_t request_id)
{
    return request_id == s_reading_detail_request_id;
}

static bool ui_reading_detail_selected_matches_request(void)
{
    char selected_path[sizeof(s_reading_detail_request_path)];

    selected_path[0] = '\0';
    if (!ui_reading_list_get_selected_path(selected_path, sizeof(selected_path)))
    {
        return s_reading_detail_request_path[0] == '\0';
    }

    return strcmp(selected_path, s_reading_detail_request_path) == 0;
}

static void ui_reading_detail_reset_pages(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    memset(s_reading_detail_pages, 0, sizeof(s_reading_detail_pages));
    s_reading_detail_page_count = 0U;
    s_reading_detail_has_last_page = false;
    memset(s_reading_detail_epub_blocks, 0, sizeof(s_reading_detail_epub_blocks));
    memset(s_reading_detail_epub_images, 0, sizeof(s_reading_detail_epub_images));
    s_reading_detail_epub_block_count = 0U;
    s_reading_detail_epub_image_count = 0U;
    rt_hw_interrupt_enable(level);

    ui_reading_detail_release_current_image();
    ui_reading_detail_release_text_bitmap_buffer();
    s_reading_detail_current_page = 0U;
    s_reading_detail_image_transition_refresh_pending = false;
    ui_reading_detail_clear_navigation_lock();
}

static void ui_reading_detail_reset_epub_lazy_state(void)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    memset(s_reading_detail_epub_spine, 0, sizeof(s_reading_detail_epub_spine));
    s_reading_detail_epub_spine_count = 0U;
    rt_hw_interrupt_enable(level);

    s_reading_detail_epub_current_chapter = 0U;
    s_reading_detail_epub_lazy_mode = false;
}

static void ui_reading_detail_show_loading_state(void)
{
    ui_reading_detail_release_current_image();

    rt_snprintf(s_reading_detail_page_buffer,
                sizeof(s_reading_detail_page_buffer),
                "%s",
                ui_i18n_pick("正在读取文本内容...\n\n请稍候。", "Loading the text content...\n\nPlease wait."));
    rt_snprintf(s_reading_detail_page_text, sizeof(s_reading_detail_page_text), "-- / --");

    if (s_reading_detail_refs.content_label != NULL)
    {
        lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
        ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                         s_reading_detail_page_buffer);
    }

    if (s_reading_detail_refs.content_image != NULL)
    {
        lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_reading_detail_refs.page_label != NULL)
    {
        ui_reading_detail_set_label_text(s_reading_detail_refs.page_label,
                                         s_reading_detail_page_text);
    }

    ui_reading_detail_set_button_enabled(s_reading_detail_refs.prev_button, false);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.next_button, false);
}

static bool ui_reading_detail_is_content_ready(void)
{
    return s_reading_detail_load_state == UI_READING_DETAIL_LOAD_READY &&
           s_reading_detail_completed_request_id == s_reading_detail_request_id;
}

static uint16_t ui_reading_detail_get_max_lines(void)
{
    return ui_reading_detail_get_max_lines_for_height(ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT));
}

static void ui_reading_detail_blend_glyph(const uint8_t *glyph_bitmap,
                                          int glyph_w,
                                          int glyph_h,
                                          int dst_x,
                                          int dst_y)
{
    int gx;
    int gy;
    int max_w;
    int max_h;

    if (glyph_bitmap == NULL || s_reading_detail_bitmap_pixels == NULL || !s_reading_detail_bitmap_inited)
    {
        return;
    }

    max_w = (int)s_reading_detail_bitmap_dsc.header.w;
    max_h = (int)s_reading_detail_bitmap_dsc.header.h;

    for (gy = 0; gy < glyph_h; ++gy)
    {
        int py = dst_y + gy;
        if (py < 0 || py >= max_h)
        {
            continue;
        }

        for (gx = 0; gx < glyph_w; ++gx)
        {
            uint8_t coverage;
            uint8_t *pixel_byte;
            uint8_t current_level;
            uint8_t target_level;
            int px = dst_x + gx;

            if (px < 0 || px >= max_w)
            {
                continue;
            }

            coverage = glyph_bitmap[gy * glyph_w + gx];
            if (coverage < UI_READING_DETAIL_GLYPH_COVERAGE_THRESHOLD)
            {
                continue;
            }

            target_level = (uint8_t)((coverage * 15U + 127U) / 255U);
            if (target_level == 0U)
            {
                target_level = 1U;
            }

            pixel_byte = s_reading_detail_bitmap_pixels +
                         ((size_t)py * (size_t)s_reading_detail_bitmap_dsc.header.stride) +
                         ((uint32_t)px >> 1);
            if ((px & 1) == 0)
            {
                current_level = (uint8_t)((*pixel_byte >> 4) & 0x0FU);
                if (target_level > current_level)
                {
                    *pixel_byte = (uint8_t)((*pixel_byte & 0x0FU) | (target_level << 4));
                }
            }
            else
            {
                current_level = (uint8_t)(*pixel_byte & 0x0FU);
                if (target_level > current_level)
                {
                    *pixel_byte = (uint8_t)((*pixel_byte & 0xF0U) | target_level);
                }
            }
        }
    }
}

static bool ui_reading_detail_hdfont_decode_rle(const uint8_t *encoded,
                                                uint32_t encoded_length,
                                                uint8_t *decoded,
                                                uint32_t decoded_length)
{
    uint32_t in_pos = 0U;
    uint32_t out_pos = 0U;

    if (encoded == NULL || decoded == NULL)
    {
        return false;
    }

    while (in_pos < encoded_length && out_pos < decoded_length)
    {
        uint8_t token = encoded[in_pos++];
        uint32_t run_length = (uint32_t)(token & 0x7FU) + 1U;

        if ((token & 0x80U) == 0U)
        {
            if (run_length > decoded_length - out_pos)
            {
                return false;
            }
            memset(decoded + out_pos, 0, run_length);
            out_pos += run_length;
            continue;
        }

        if (run_length > encoded_length - in_pos ||
            run_length > decoded_length - out_pos)
        {
            return false;
        }
        memcpy(decoded + out_pos, encoded + in_pos, run_length);
        in_pos += run_length;
        out_pos += run_length;
    }

    return in_pos == encoded_length && out_pos == decoded_length;
}

static bool ui_reading_detail_render_text_hdfont_bitmap(const char *formatted_text)
{
    uint32_t index;
    uint32_t progress_tick;
    int pen_x;
    int pen_y;
    int baseline;
    int line_step;
    uint32_t codepoint;
    uint32_t scratch_half = (uint32_t)(sizeof(s_reading_detail_glyph_scratch) / 2U);

    if (formatted_text == NULL)
    {
        return false;
    }

    if (!ui_reading_detail_ensure_bitmap_buffer() || !ui_reading_detail_init_hdfont())
    {
        return false;
    }

    memset(s_reading_detail_bitmap_pixels, 0, s_reading_detail_bitmap_pixel_bytes);
    line_step = (int)((s_reading_detail_hdfont.line_height26 + 32U) >> 6);
    if (line_step <= 0)
    {
        line_step = ui_px_h(UI_READING_DETAIL_TEXT_FONT);
    }
    line_step += ui_reading_detail_get_line_space();
    if (line_step <= 0)
    {
        line_step = 1;
    }

    baseline = (int)((s_reading_detail_hdfont.ascent26 + 32U) >> 6);
    if (baseline <= 0)
    {
        baseline = line_step;
    }

    pen_x = 0;
    pen_y = baseline;
    index = 0U;
    progress_tick = 0U;

    while ((codepoint = ui_reading_detail_utf8_next(formatted_text, &index)) != 0U)
    {
        ui_reading_detail_hdfont_glyph_t glyph;
        uint16_t fallback_width;
        int advance_px;

        if (codepoint == '\n')
        {
            pen_x = 0;
            pen_y += line_step;
            continue;
        }

        fallback_width = (uint16_t)LV_MAX(line_step / 2, 1);
        if (!ui_reading_detail_hdfont_find_glyph(codepoint, &glyph))
        {
            pen_x += fallback_width;
            continue;
        }

        advance_px = (glyph.advance26 + 32) >> 6;
        if (advance_px <= 0)
        {
            advance_px = fallback_width;
        }

        if (glyph.width > 0U &&
            glyph.height > 0U &&
            glyph.bitmap_length > 0U &&
            glyph.bitmap_offset <= s_reading_detail_hdfont.bitmap_size &&
            glyph.bitmap_length <= s_reading_detail_hdfont.bitmap_size - glyph.bitmap_offset)
        {
            uint32_t raw_bytes = (uint32_t)glyph.width * (uint32_t)glyph.height;
            uint8_t *encoded = NULL;
            uint8_t *decoded = NULL;
            bool encoded_heap = false;
            bool decoded_heap = false;
            bool glyph_ready = false;

            if (raw_bytes > 0U)
            {
                if (glyph.bitmap_length <= scratch_half)
                {
                    encoded = s_reading_detail_glyph_scratch;
                }
                else
                {
                    encoded = (uint8_t *)rt_malloc(glyph.bitmap_length);
                    encoded_heap = true;
                }

                if (raw_bytes <= scratch_half)
                {
                    decoded = s_reading_detail_glyph_scratch + scratch_half;
                }
                else
                {
                    decoded = (uint8_t *)rt_malloc(raw_bytes);
                    decoded_heap = true;
                }

                if (encoded != NULL && decoded != NULL &&
                    ui_reading_detail_hdfont_read_exact(s_reading_detail_hdfont.fd,
                                                        s_reading_detail_hdfont.bitmap_offset + glyph.bitmap_offset,
                                                        encoded,
                                                        glyph.bitmap_length))
                {
                    if ((s_reading_detail_hdfont.flags & UI_READING_DETAIL_HDFONT_FLAG_RLE) != 0U)
                    {
                        glyph_ready = ui_reading_detail_hdfont_decode_rle(encoded,
                                                                          glyph.bitmap_length,
                                                                          decoded,
                                                                          raw_bytes);
                    }
                    else if (glyph.bitmap_length == raw_bytes)
                    {
                        memcpy(decoded, encoded, raw_bytes);
                        glyph_ready = true;
                    }
                }

                if (glyph_ready)
                {
                    ui_reading_detail_blend_glyph(decoded,
                                                  (int)glyph.width,
                                                  (int)glyph.height,
                                                  pen_x + (int)glyph.x_off,
                                                  pen_y + (int)glyph.y_off);
                }

                if (encoded_heap && encoded != NULL)
                {
                    rt_free(encoded);
                }
                if (decoded_heap && decoded != NULL)
                {
                    rt_free(decoded);
                }
            }
        }

        pen_x += advance_px;
        if ((progress_tick++ & 0x3FU) == 0U)
        {
            ui_reading_detail_watchdog_progress();
        }
    }

    return true;
}

static bool ui_reading_detail_render_text_bitmap(const char *formatted_text)
{
    if (formatted_text == NULL)
    {
        return false;
    }

    if (!ui_reading_detail_init_hdfont())
    {
        return false;
    }

    return ui_reading_detail_render_text_hdfont_bitmap(formatted_text);
}

static uint16_t ui_reading_detail_get_glyph_width_fast(const lv_font_t *font,
                                                       uint32_t codepoint,
                                                       uint32_t next_codepoint,
                                                       uint16_t fallback_width)
{
    lv_font_glyph_dsc_t glyph_dsc;
    uint16_t width;

    if (s_reading_detail_font_path[0] != '\0' &&
        ui_reading_detail_has_hdfont_suffix(s_reading_detail_font_path) &&
        ui_reading_detail_init_hdfont())
    {
        return ui_reading_detail_hdfont_glyph_width(codepoint, fallback_width);
    }

    if (font != NULL)
    {
        memset(&glyph_dsc, 0, sizeof(glyph_dsc));
        if (lv_font_get_glyph_dsc(font, &glyph_dsc, codepoint, next_codepoint))
        {
            width = glyph_dsc.adv_w;
            if (width == 0U)
            {
                width = fallback_width;
            }
            return width;
        }
    }

    return fallback_width;
}

static int32_t ui_reading_detail_get_text_line_height_px(void)
{
    const lv_font_t *font;

    if (s_reading_detail_font_path[0] != '\0' &&
        ui_reading_detail_has_hdfont_suffix(s_reading_detail_font_path) &&
        ui_reading_detail_init_hdfont())
    {
        int32_t hdfont_line_height = (int32_t)((s_reading_detail_hdfont.line_height26 + 32U) >> 6);
        if (hdfont_line_height > 0)
        {
            return hdfont_line_height;
        }
    }

    font = ui_reading_detail_get_text_font();
    if (font != NULL && font->line_height > 0)
    {
        return font->line_height;
    }

    return ui_px_h(UI_READING_DETAIL_TEXT_FONT);
}

static bool ui_reading_detail_append_page_entry(ui_reading_detail_page_type_t type,
                                                uint32_t start,
                                                uint32_t end,
                                                uint16_t image_index)
{
    return ui_reading_detail_append_page_entry_ex(type, start, end, image_index, 0U, 0U);
}

static bool ui_reading_detail_append_page_entry_ex(ui_reading_detail_page_type_t type,
                                                   uint32_t start,
                                                   uint32_t end,
                                                   uint16_t image_index,
                                                   uint16_t image_height,
                                                   uint16_t text_height)
{
    rt_base_t level;
    uint16_t next_index;

    level = rt_hw_interrupt_disable();
    next_index = s_reading_detail_page_count;
    if (next_index >= UI_READING_DETAIL_MAX_PAGE_COUNT)
    {
        rt_hw_interrupt_enable(level);
        return false;
    }

    s_reading_detail_pages[next_index].type = type;
    s_reading_detail_pages[next_index].start = start;
    s_reading_detail_pages[next_index].end = end;
    s_reading_detail_pages[next_index].image_index = image_index;
    s_reading_detail_pages[next_index].image_height = image_height;
    s_reading_detail_pages[next_index].text_height = text_height;
    s_reading_detail_page_count = (uint16_t)(next_index + 1U);
    s_reading_detail_has_last_page = true;
    rt_hw_interrupt_enable(level);
    return true;
}

static uint32_t ui_reading_detail_measure_page_range_limited(uint32_t start,
                                                             uint32_t end_limit,
                                                             char *formatted_buffer,
                                                             size_t formatted_buffer_size,
                                                             uint16_t max_lines)
{
    uint32_t end = start;
    uint32_t current_index = start;
    uint32_t previous_index = start;
    uint32_t char_start = start;
    uint32_t progress_tick = 0U;
    uint32_t letter;
    uint32_t letter_next;
    int32_t line_width = 0;
    uint16_t lines_used = 1U;
    int32_t max_width;
    int32_t letter_space;
    uint16_t fallback_width;
    const lv_font_t *font;
    size_t written = 0U;

    if (formatted_buffer != NULL && formatted_buffer_size > 0U)
    {
        formatted_buffer[0] = '\0';
    }

    if (max_lines == 0U)
    {
        max_lines = ui_reading_detail_get_max_lines();
    }
    max_width = ui_px_w(UI_READING_DETAIL_TEXT_WIDTH);
    letter_space = 0;
    font = ui_reading_detail_using_external_font() ? NULL : ui_reading_detail_get_text_font();
    fallback_width = (uint16_t)(ui_reading_detail_get_text_line_height_px() / 2);
    if (fallback_width == 0U)
    {
        fallback_width = 1U;
    }

    while (current_index < end_limit)
    {
        int32_t char_width;
        size_t char_len;
        char current_byte;

        previous_index = current_index;
        char_start = current_index;
        current_byte = ui_reading_detail_get_text_byte(current_index);
        if (current_byte == '\0')
        {
            break;
        }

        if (current_byte == '\r')
        {
            ++current_index;
            end = current_index;
            if ((progress_tick++ & 0x3FU) == 0U)
            {
                ui_reading_detail_watchdog_progress();
            }
            continue;
        }

        letter = ui_reading_detail_source_utf8_next(&current_index);
        if (letter == 0U)
        {
            current_index = previous_index + 1U;
            end = current_index;
            if (formatted_buffer != NULL && formatted_buffer_size > 1U && written + 1U < formatted_buffer_size)
            {
                formatted_buffer[written++] = current_byte;
                formatted_buffer[written] = '\0';
            }
            if ((progress_tick++ & 0x3FU) == 0U)
            {
                ui_reading_detail_watchdog_progress();
            }
            continue;
        }

        end = current_index;
        if (letter == '\n')
        {
            if (formatted_buffer != NULL && formatted_buffer_size > 1U && written + 1U < formatted_buffer_size)
            {
                formatted_buffer[written++] = '\n';
                formatted_buffer[written] = '\0';
            }

            if (lines_used >= max_lines)
            {
                return end;
            }

            ++lines_used;
            line_width = 0;
            if ((progress_tick++ & 0x3FU) == 0U)
            {
                ui_reading_detail_watchdog_progress();
            }
            continue;
        }

        if (current_index < end_limit && ui_reading_detail_get_text_byte(current_index) != '\0')
        {
            uint32_t next_index = current_index;

            letter_next = ui_reading_detail_source_utf8_next(&next_index);
        }
        else
        {
            letter_next = 0U;
        }
        char_width = (int32_t)ui_reading_detail_get_glyph_width_fast(font,
                                                                     letter,
                                                                     letter_next,
                                                                     fallback_width);

        if (line_width > 0)
        {
            char_width += letter_space;
        }

        if ((line_width + char_width) > max_width)
        {
            if (lines_used >= max_lines)
            {
                return previous_index > start ? previous_index : end;
            }

            if (formatted_buffer != NULL && formatted_buffer_size > 1U && written + 1U < formatted_buffer_size)
            {
                formatted_buffer[written++] = '\n';
                formatted_buffer[written] = '\0';
            }

            ++lines_used;
            line_width = char_width;
        }
        else
        {
            line_width += char_width;
        }

        char_len = (size_t)(current_index - char_start);
        if (formatted_buffer != NULL &&
            formatted_buffer_size > 1U &&
            written + char_len < formatted_buffer_size)
        {
            written += ui_reading_detail_copy_text_range(&formatted_buffer[written],
                                                         formatted_buffer_size - written,
                                                         char_start,
                                                         current_index);
            formatted_buffer[written] = '\0';
        }

        if ((progress_tick++ & 0x3FU) == 0U)
        {
            ui_reading_detail_watchdog_progress();
        }
    }

    if (end < end_limit && current_index >= end_limit)
    {
        end = end_limit;
    }

    if (end <= start && start < end_limit && ui_reading_detail_get_text_byte(start) != '\0')
    {
        previous_index = start;
        (void)ui_reading_detail_source_utf8_next(&previous_index);
        end = previous_index;
        if (formatted_buffer != NULL && formatted_buffer_size > 1U)
        {
            size_t char_len = (size_t)(previous_index - start);
            if (char_len >= formatted_buffer_size)
            {
                char_len = formatted_buffer_size - 1U;
            }
            (void)ui_reading_detail_copy_text_range(formatted_buffer,
                                                    char_len + 1U,
                                                    start,
                                                    previous_index);
        }
    }

    return end;
}

static uint32_t ui_reading_detail_measure_page_range(uint32_t start,
                                                     uint32_t end_limit,
                                                     char *formatted_buffer,
                                                     size_t formatted_buffer_size)
{
    return ui_reading_detail_measure_page_range_limited(start,
                                                        end_limit,
                                                        formatted_buffer,
                                                        formatted_buffer_size,
                                                        0U);
}

static bool ui_reading_detail_paginate_text_range(uint32_t start, uint32_t end)
{
    uint32_t progress_tick = 0U;

    while (start < end && ui_reading_detail_get_text_byte(start) != '\0')
    {
        uint32_t page_end = ui_reading_detail_measure_page_range(start, end, NULL, 0U);

        if (page_end <= start)
        {
            break;
        }

        if (!ui_reading_detail_append_page_entry(UI_READING_DETAIL_PAGE_TEXT, start, page_end, 0U))
        {
            return false;
        }

        start = page_end;
        if ((progress_tick++ & 0x07U) == 0U)
        {
            ui_reading_detail_watchdog_progress();
        }
    }

    return true;
}

static bool ui_reading_detail_paginate_epub_blocks(void)
{
    uint16_t i = 0U;
    uint32_t progress_tick = 0U;
    uint32_t viewport_height = (uint32_t)ui_px_h(UI_READING_DETAIL_READING_BOX_HEIGHT);
    uint32_t gap_height = (uint32_t)ui_px_y(UI_READING_DETAIL_IMAGE_TEXT_GAP);

    while (i < s_reading_detail_epub_block_count)
    {
        const reading_epub_block_t *block = &s_reading_detail_epub_blocks[i];

        if (block->type == READING_EPUB_BLOCK_TEXT)
        {
            if (!ui_reading_detail_paginate_text_range(block->text_start, block->text_end))
            {
                return false;
            }
            ++i;
            if ((progress_tick++ & 0x03U) == 0U)
            {
                ui_reading_detail_watchdog_progress();
            }
            continue;
        }

        if (block->image_index >= s_reading_detail_epub_image_count)
        {
            ++i;
            continue;
        }

        {
            uint16_t scaled_height = 0U;
            bool cropped = false;

            if (!ui_reading_detail_probe_image_layout(block->image_index, &scaled_height, &cropped) ||
                scaled_height == 0U)
            {
                if (!ui_reading_detail_append_page_entry(UI_READING_DETAIL_PAGE_IMAGE, 0U, 0U, block->image_index))
                {
                    return false;
                }
                ++i;
                if ((progress_tick++ & 0x03U) == 0U)
                {
                    ui_reading_detail_watchdog_progress();
                }
                continue;
            }

            if (cropped || scaled_height >= viewport_height)
            {
                if (!ui_reading_detail_append_page_entry_ex(UI_READING_DETAIL_PAGE_IMAGE,
                                                            0U,
                                                            0U,
                                                            block->image_index,
                                                            (uint16_t)viewport_height,
                                                            0U))
                {
                    return false;
                }
                ++i;
                if ((progress_tick++ & 0x03U) == 0U)
                {
                    ui_reading_detail_watchdog_progress();
                }
                continue;
            }

            if ((uint16_t)(i + 1U) < s_reading_detail_epub_block_count &&
                s_reading_detail_epub_blocks[i + 1U].type == READING_EPUB_BLOCK_TEXT)
            {
                const reading_epub_block_t *next_text = &s_reading_detail_epub_blocks[i + 1U];
                uint32_t available_text_height = viewport_height - (uint32_t)scaled_height;
                uint16_t max_lines;
                uint32_t text_end;

                if (available_text_height > gap_height)
                {
                    available_text_height -= gap_height;
                }
                else
                {
                    available_text_height = 0U;
                }

                max_lines = ui_reading_detail_get_max_lines_for_height((lv_coord_t)available_text_height);
                text_end = ui_reading_detail_measure_page_range_limited(next_text->text_start,
                                                                       next_text->text_end,
                                                                       NULL,
                                                                       0U,
                                                                       max_lines);

                if (max_lines > 0U && text_end > next_text->text_start)
                {
                    if (!ui_reading_detail_append_page_entry_ex(UI_READING_DETAIL_PAGE_IMAGE_TEXT,
                                                                next_text->text_start,
                                                                text_end,
                                                                block->image_index,
                                                                scaled_height,
                                                                (uint16_t)available_text_height))
                    {
                        return false;
                    }

                    if (text_end < next_text->text_end &&
                        !ui_reading_detail_paginate_text_range(text_end, next_text->text_end))
                    {
                        return false;
                    }

                    i = (uint16_t)(i + 2U);
                    if ((progress_tick++ & 0x03U) == 0U)
                    {
                        ui_reading_detail_watchdog_progress();
                    }
                    continue;
                }
            }

            if (!ui_reading_detail_append_page_entry_ex(UI_READING_DETAIL_PAGE_IMAGE,
                                                        0U,
                                                        0U,
                                                        block->image_index,
                                                        scaled_height,
                                                        0U))
            {
                return false;
            }
        }

        ++i;
        if ((progress_tick++ & 0x03U) == 0U)
        {
            ui_reading_detail_watchdog_progress();
        }
    }

    return true;
}

static void ui_reading_detail_load_thread_entry(void *parameter)
{
    char file_path[sizeof(s_reading_detail_request_path)];
    uint32_t request_id;
    uint32_t load_start_ms;

    (void)parameter;

    while (1)
    {
        if (rt_sem_take(&s_reading_detail_load_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        request_id = s_reading_detail_request_id;
        rt_snprintf(file_path, sizeof(file_path), "%s", s_reading_detail_request_path);
        load_start_ms = ui_reading_detail_now_ms();
        ui_reading_detail_watchdog_begin_long_task();

        if (file_path[0] == '\0')
        {
            memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
            rt_snprintf(s_reading_detail_text,
                        sizeof(s_reading_detail_text),
                        "%s",
                        ui_i18n_pick("TF 卡中还没有可阅读的文本文件。\n\n请先返回列表页确认文件是否已经识别。",
                                     "There are no readable text files on the TF card yet.\n\nPlease go back to the list and check whether the file has been detected."));
            ui_reading_detail_use_memory_text();
            ui_reading_detail_reset_pages();
            ui_reading_detail_use_memory_text();
            (void)ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length());
            rt_kprintf("reading_detail: request=%lu fallback without selected file\n",
                       (unsigned long)request_id);
        }
        else
        {
            if (strrchr(file_path, '.') != NULL &&
                strcasecmp(strrchr(file_path, '.'), ".epub") == 0)
            {
                (void)ui_reading_detail_load_epub_from_path(file_path);
            }
            else
            {
                (void)ui_reading_detail_load_text_from_path(file_path);
            }
        }

        if (!ui_reading_detail_request_is_current(request_id))
        {
            rt_kprintf("reading_detail: request=%lu canceled after load\n",
                       (unsigned long)request_id);
            ui_reading_detail_watchdog_end_long_task();
            continue;
        }

        {
            rt_base_t level;
            bool has_last_page = false;
            uint16_t page_count = ui_reading_detail_snapshot_page_count(&has_last_page);

            level = rt_hw_interrupt_disable();
            s_reading_detail_first_page_layout_ready = page_count > 0U;
            s_reading_detail_load_state = UI_READING_DETAIL_LOAD_READY;
            s_reading_detail_completed_request_id = request_id;
            rt_hw_interrupt_enable(level);

            rt_kprintf("reading_detail: request=%lu pages=%u last=%u total_ms=%lu\n",
                       (unsigned long)request_id,
                       (unsigned int)page_count,
                       has_last_page ? 1U : 0U,
                       (unsigned long)(ui_reading_detail_now_ms() - load_start_ms));
        }

        ui_reading_detail_watchdog_end_long_task();
    }
}

static void ui_reading_detail_start_load_worker(void)
{
    rt_err_t result;

    if (s_reading_detail_load_worker_started)
    {
        return;
    }

    result = rt_sem_init(&s_reading_detail_load_sem, "rdload", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("reading_detail: sem init failed=%d\n", result);
        return;
    }

    result = rt_thread_init(&s_reading_detail_load_thread,
                            "rdload",
                            ui_reading_detail_load_thread_entry,
                            RT_NULL,
                            s_reading_detail_load_thread_stack,
                            sizeof(s_reading_detail_load_thread_stack),
                            UI_READING_DETAIL_LOAD_THREAD_PRIORITY,
                            UI_READING_DETAIL_LOAD_THREAD_TICK);
    if (result != RT_EOK)
    {
        rt_kprintf("reading_detail: thread init failed=%d\n", result);
        rt_sem_detach(&s_reading_detail_load_sem);
        return;
    }

    rt_thread_startup(&s_reading_detail_load_thread);
    s_reading_detail_load_worker_started = true;
}

static void ui_reading_detail_refresh_nav_buttons(void)
{
    bool has_last_page = false;
    uint16_t page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    bool has_prev = s_reading_detail_current_page > 0U;
    bool has_next = !has_last_page ||
                    (uint16_t)(s_reading_detail_current_page + 1U) < page_count;

    if (s_reading_detail_epub_lazy_mode)
    {
        has_prev = has_prev || s_reading_detail_epub_current_chapter > 0U;
        has_next = has_next ||
                   (uint16_t)(s_reading_detail_epub_current_chapter + 1U) < s_reading_detail_epub_spine_count;
    }

    ui_reading_detail_set_button_enabled(s_reading_detail_refs.prev_button, has_prev);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.next_button, has_next);
}

static void ui_reading_detail_refresh_page_label(void)
{
    bool has_last_page = false;
    uint16_t display_total = ui_reading_detail_snapshot_page_count(&has_last_page);
    uint16_t display_current = (uint16_t)(s_reading_detail_current_page + 1U);

    if (s_reading_detail_refs.page_label == NULL)
    {
        return;
    }

    if (display_total == 0U)
    {
        display_total = 1U;
    }

    if (display_current > display_total)
    {
        display_current = display_total;
    }

    if (s_reading_detail_epub_lazy_mode && s_reading_detail_epub_spine_count > 1U)
    {
        if (has_last_page)
        {
            rt_snprintf(s_reading_detail_page_text,
                        sizeof(s_reading_detail_page_text),
                        "C%u/%u P%u/%u",
                        (unsigned int)(s_reading_detail_epub_current_chapter + 1U),
                        (unsigned int)s_reading_detail_epub_spine_count,
                        (unsigned int)display_current,
                        (unsigned int)display_total);
        }
        else
        {
            rt_snprintf(s_reading_detail_page_text,
                        sizeof(s_reading_detail_page_text),
                        "C%u/%u P%u/--",
                        (unsigned int)(s_reading_detail_epub_current_chapter + 1U),
                        (unsigned int)s_reading_detail_epub_spine_count,
                        (unsigned int)display_current);
        }
    }
    else
    {
        if (has_last_page)
        {
            rt_snprintf(s_reading_detail_page_text,
                        sizeof(s_reading_detail_page_text),
                        "%u / %u",
                        (unsigned int)display_current,
                        (unsigned int)display_total);
        }
        else
        {
            rt_snprintf(s_reading_detail_page_text,
                        sizeof(s_reading_detail_page_text),
                        "%u / --",
                        (unsigned int)display_current);
        }
    }

    ui_reading_detail_set_label_text(s_reading_detail_refs.page_label,
                                     s_reading_detail_page_text);
}

static bool ui_reading_detail_settings_visible(void)
{
    return s_reading_detail_refs.settings_overlay != NULL &&
           !lv_obj_has_flag(s_reading_detail_refs.settings_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void ui_reading_detail_refresh_settings_panel(void)
{
    char value_text[16];

    if (s_reading_detail_refs.font_value_label != NULL)
    {
        rt_snprintf(value_text, sizeof(value_text), "%u", (unsigned int)s_reading_detail_font_size);
        ui_reading_detail_set_label_text(s_reading_detail_refs.font_value_label, value_text);
    }

    if (s_reading_detail_refs.line_space_value_label != NULL)
    {
        rt_snprintf(value_text, sizeof(value_text), "%u", (unsigned int)s_reading_detail_line_space);
        ui_reading_detail_set_label_text(s_reading_detail_refs.line_space_value_label, value_text);
    }

    if (s_reading_detail_refs.font_family_value_label != NULL)
    {
        ui_reading_detail_set_label_text(s_reading_detail_refs.font_family_value_label,
                                         ui_reading_detail_get_selected_font_name());
    }

    ui_reading_detail_set_button_enabled(s_reading_detail_refs.font_dec_button,
                                         s_reading_detail_font_size > UI_READING_DETAIL_TEXT_FONT_MIN);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.font_inc_button,
                                         s_reading_detail_font_size < UI_READING_DETAIL_TEXT_FONT_MAX);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.line_space_dec_button,
                                         s_reading_detail_line_space > UI_READING_DETAIL_TEXT_LINE_SPACE_MIN);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.line_space_inc_button,
                                         s_reading_detail_line_space < UI_READING_DETAIL_TEXT_LINE_SPACE_MAX);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.font_family_prev_button,
                                         s_reading_detail_font_item_count > 1U);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.font_family_next_button,
                                         s_reading_detail_font_item_count > 1U);
}

static bool ui_reading_detail_has_rebuild_source(void)
{
    if (ui_reading_detail_get_text_length() > 0U)
    {
        return true;
    }

    if (s_reading_detail_epub_block_count > 0U ||
        s_reading_detail_epub_image_count > 0U ||
        s_reading_detail_page_count > 0U)
    {
        return true;
    }

    return false;
}

static void ui_reading_detail_set_settings_visible(bool visible)
{
    if (s_reading_detail_refs.settings_overlay == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_obj_clear_flag(s_reading_detail_refs.settings_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_reading_detail_refs.settings_overlay);
        if (s_reading_detail_refs.settings_panel != NULL)
        {
            lv_obj_move_foreground(s_reading_detail_refs.settings_panel);
        }
        ui_reading_detail_refresh_settings_panel();
    }
    else
    {
        lv_obj_add_flag(s_reading_detail_refs.settings_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_reading_detail_rebuild_layout(void)
{
    bool preserve_text_anchor = false;
    uint32_t text_anchor = 0U;
    uint16_t fallback_page;
    uint16_t old_page_count;
    uint16_t new_page_count;
    uint16_t target_page = 0U;

    if (s_reading_detail_load_state != UI_READING_DETAIL_LOAD_READY ||
        !ui_reading_detail_has_rebuild_source())
    {
        ui_reading_detail_apply_text_style();
        return;
    }

    ui_reading_detail_watchdog_begin_long_task();
    fallback_page = s_reading_detail_current_page;
    old_page_count = ui_reading_detail_snapshot_page_count(NULL);
    if (fallback_page < old_page_count)
    {
        const ui_reading_detail_page_entry_t *old_page = &s_reading_detail_pages[fallback_page];

        if ((old_page->type == UI_READING_DETAIL_PAGE_TEXT ||
             old_page->type == UI_READING_DETAIL_PAGE_IMAGE_TEXT) &&
            old_page->end > old_page->start)
        {
            preserve_text_anchor = true;
            text_anchor = old_page->start;
        }
    }

    ui_reading_detail_release_current_image();
    ui_reading_detail_release_text_bitmap_buffer();
    s_reading_detail_first_page_layout_ready = false;
    s_reading_detail_first_page_bitmap_ready = false;
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    memset(s_reading_detail_first_page_layout, 0, sizeof(s_reading_detail_first_page_layout));
    memset(s_reading_detail_page_buffer, 0, sizeof(s_reading_detail_page_buffer));
    memset(s_reading_detail_pages, 0, sizeof(s_reading_detail_pages));
    s_reading_detail_page_count = 0U;
    s_reading_detail_has_last_page = false;

    if (s_reading_detail_epub_block_count > 0U)
    {
        if (!ui_reading_detail_paginate_epub_blocks())
        {
            ui_reading_detail_watchdog_end_long_task();
            return;
        }
    }
    else if (!ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length()))
    {
        ui_reading_detail_watchdog_end_long_task();
        return;
    }

    s_reading_detail_first_page_layout_ready = true;
    new_page_count = ui_reading_detail_snapshot_page_count(NULL);
    if (new_page_count > 0U)
    {
        if (preserve_text_anchor)
        {
            uint16_t i;

            target_page = new_page_count - 1U;
            for (i = 0U; i < new_page_count; ++i)
            {
                const ui_reading_detail_page_entry_t *page = &s_reading_detail_pages[i];

                if ((page->type == UI_READING_DETAIL_PAGE_TEXT ||
                     page->type == UI_READING_DETAIL_PAGE_IMAGE_TEXT) &&
                    page->end > page->start)
                {
                    if (text_anchor >= page->start && text_anchor < page->end)
                    {
                        target_page = i;
                        break;
                    }

                    if (text_anchor < page->start)
                    {
                        target_page = i;
                        break;
                    }
                }
            }
        }
        else if (fallback_page < new_page_count)
        {
            target_page = fallback_page;
        }
        else
        {
            target_page = new_page_count - 1U;
        }
    }

    s_reading_detail_current_page = target_page;
    ui_reading_detail_apply_text_style();

    (void)ui_reading_detail_render_page();
    ui_reading_detail_watchdog_end_long_task();
}

static bool ui_reading_detail_load_text_from_path(const char *file_path)
{
    uint32_t open_start_ms;
    uint32_t open_elapsed_ms;
    uint32_t paginate_start_ms;
    uint32_t paginate_elapsed_ms;
    bool using_memory_fallback = false;
    bool ok = false;

    ui_reading_detail_watchdog_begin_long_task();
    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
    ui_reading_detail_reset_text_source();
    ui_reading_detail_reset_epub_lazy_state();

    if (file_path == NULL || file_path[0] == '\0')
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "%s",
                    ui_i18n_pick("TF 卡中还没有可阅读的文本文件。\n\n请先返回列表页确认文件是否已经识别。",
                                 "There are no readable text files on the TF card yet.\n\nPlease go back to the list and check whether the file has been detected."));
        ui_reading_detail_use_memory_text();
        goto cleanup;
    }

    open_start_ms = ui_reading_detail_now_ms();
    if (!ui_reading_detail_open_text_stream(file_path))
    {
        rt_kprintf("reading_detail: open failed path=%s errno=%d\n", file_path, rt_get_errno());
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    ui_i18n_pick("打开文件失败：\n%s\n\n请确认文件存在且 TF 卡可正常读取。",
                                 "Failed to open file:\n%s\n\nPlease confirm the file exists and the TF card is readable."),
                    file_path);
        ui_reading_detail_use_memory_text();
        goto cleanup;
    }
    open_elapsed_ms = ui_reading_detail_now_ms() - open_start_ms;
    ui_reading_detail_watchdog_progress();

    if (ui_reading_detail_get_text_length() == 0U)
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "%s",
                    ui_i18n_pick("这个文本文件目前是空的。", "This text file is currently empty."));
        ui_reading_detail_use_memory_text();
        using_memory_fallback = true;
    }

    ui_reading_detail_reset_pages();
    if (using_memory_fallback)
    {
        ui_reading_detail_use_memory_text();
        paginate_elapsed_ms = 0U;
        (void)ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length());
    }
    else
    {
        paginate_start_ms = ui_reading_detail_now_ms();
        if (!ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length()))
        {
            rt_snprintf(s_reading_detail_text,
                        sizeof(s_reading_detail_text),
                        "%s",
                        ui_i18n_pick("文本分页失败，请尝试更换文件。", "Failed to paginate the text file."));
            ui_reading_detail_use_memory_text();
            ui_reading_detail_reset_pages();
            ui_reading_detail_use_memory_text();
            (void)ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length());
            using_memory_fallback = true;
        }
        paginate_elapsed_ms = ui_reading_detail_now_ms() - paginate_start_ms;
    }
    ui_reading_detail_watchdog_progress();

    rt_kprintf("reading_detail: file=%s bytes=%lu open_ms=%lu paginate_ms=%lu total_ms=%lu\n",
               file_path,
               (unsigned long)ui_reading_detail_get_text_length(),
               (unsigned long)open_elapsed_ms,
               (unsigned long)paginate_elapsed_ms,
               (unsigned long)(ui_reading_detail_now_ms() - open_start_ms));

    ok = true;

cleanup:
    ui_reading_detail_watchdog_end_long_task();
    return ok;
}

static bool ui_reading_detail_load_epub_chapter(uint16_t chapter_index)
{
    char error_text[128];
    uint16_t block_count = 0U;
    uint16_t image_count = 0U;
    bool ok = false;

    ui_reading_detail_watchdog_begin_long_task();
    if (!s_reading_detail_epub_lazy_mode ||
        chapter_index >= s_reading_detail_epub_spine_count)
    {
        goto cleanup;
    }

    ui_reading_detail_reset_pages();
    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
    error_text[0] = '\0';

    if (!reading_epub_load_chapter(s_reading_detail_request_path,
                                   s_reading_detail_epub_spine[chapter_index].internal_path,
                                   s_reading_detail_text,
                                   sizeof(s_reading_detail_text),
                                   s_reading_detail_epub_blocks,
                                   UI_READING_DETAIL_MAX_BLOCK_COUNT,
                                   &block_count,
                                   s_reading_detail_epub_images,
                                   UI_READING_DETAIL_MAX_IMAGE_COUNT,
                                   &image_count,
                                   error_text,
                                   sizeof(error_text)))
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    ui_i18n_pick("EPUB 解析失败：\n%s", "Failed to parse EPUB:\n%s"),
                    error_text[0] != '\0' ? error_text : ui_i18n_pick("未知错误", "Unknown error"));
        ui_reading_detail_use_memory_text();
        ui_reading_detail_reset_pages();
        ui_reading_detail_use_memory_text();
        (void)ui_reading_detail_paginate_text_range(0U, (uint32_t)strlen(s_reading_detail_text));
        goto cleanup;
    }
    ui_reading_detail_watchdog_progress();

    ui_reading_detail_use_memory_text();

    memset(s_reading_detail_pages, 0, sizeof(s_reading_detail_pages));
    s_reading_detail_page_count = 0U;
    s_reading_detail_has_last_page = false;
    s_reading_detail_epub_block_count = block_count;
    s_reading_detail_epub_image_count = image_count;

    if (!ui_reading_detail_paginate_epub_blocks())
    {
        rt_kprintf("reading_detail: epub chapter paginate failed chapter=%u\n",
                   (unsigned int)chapter_index);
    }
    ui_reading_detail_watchdog_progress();

    if (s_reading_detail_page_count == 0U)
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "%s",
                    ui_i18n_pick("EPUB 中没有可显示内容。", "The EPUB has no readable content."));
        ui_reading_detail_use_memory_text();
        ui_reading_detail_reset_pages();
        ui_reading_detail_use_memory_text();
        (void)ui_reading_detail_paginate_text_range(0U, (uint32_t)strlen(s_reading_detail_text));
    }

    s_reading_detail_epub_current_chapter = chapter_index;
    s_reading_detail_image_transition_refresh_pending = true;
    ok = true;

cleanup:
    ui_reading_detail_watchdog_end_long_task();
    return ok;
}

static bool ui_reading_detail_load_epub_from_path(const char *file_path)
{
    char error_text[128];
    uint16_t spine_count = 0U;
    bool ok = false;

    ui_reading_detail_watchdog_begin_long_task();
    ui_reading_detail_reset_text_source();
    ui_reading_detail_reset_pages();
    ui_reading_detail_reset_epub_lazy_state();
    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
    error_text[0] = '\0';

    if (!reading_epub_build_index(file_path,
                                  s_reading_detail_epub_spine,
                                  UI_READING_DETAIL_MAX_SPINE_ITEM_COUNT,
                                  &spine_count,
                                  error_text,
                                  sizeof(error_text)))
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    ui_i18n_pick("EPUB 解析失败：\n%s", "Failed to parse EPUB:\n%s"),
                    error_text[0] != '\0' ? error_text : ui_i18n_pick("未知错误", "Unknown error"));
        ui_reading_detail_use_memory_text();
        ui_reading_detail_reset_pages();
        ui_reading_detail_use_memory_text();
        (void)ui_reading_detail_paginate_text_range(0U, (uint32_t)strlen(s_reading_detail_text));
        goto cleanup;
    }
    ui_reading_detail_watchdog_progress();

    s_reading_detail_epub_lazy_mode = true;
    s_reading_detail_epub_spine_count = spine_count;
    s_reading_detail_epub_current_chapter = 0U;

    if (!ui_reading_detail_load_epub_chapter(0U))
    {
        ui_reading_detail_reset_epub_lazy_state();
        goto cleanup;
    }
    ui_reading_detail_watchdog_progress();

    rt_kprintf("reading_detail: epub indexed chapters=%u current=%u pages=%u\n",
               (unsigned int)s_reading_detail_epub_spine_count,
               (unsigned int)(s_reading_detail_epub_current_chapter + 1U),
               (unsigned int)s_reading_detail_page_count);
    ok = true;

cleanup:
    ui_reading_detail_watchdog_end_long_task();
    return ok;
}

static void ui_reading_detail_request_async_load(void)
{
    char file_path[sizeof(s_reading_detail_request_path)];

    ui_reading_detail_reset_text_source();
    ui_reading_detail_reset_pages();
    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
    memset(s_reading_detail_first_page_layout, 0, sizeof(s_reading_detail_first_page_layout));
    memset(s_reading_detail_page_buffer, 0, sizeof(s_reading_detail_page_buffer));
    file_path[0] = '\0';
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    s_reading_detail_first_page_layout_ready = false;
    s_reading_detail_first_page_bitmap_ready = false;

    (void)ui_reading_list_get_selected_path(file_path, sizeof(file_path));

    rt_snprintf(s_reading_detail_request_path,
                sizeof(s_reading_detail_request_path),
                "%s",
                file_path);
    ++s_reading_detail_request_id;
    s_reading_detail_load_state = UI_READING_DETAIL_LOAD_LOADING;
    s_reading_detail_completed_request_id = 0U;
    rt_kprintf("reading_detail: request=%lu queued path=%s\n",
               (unsigned long)s_reading_detail_request_id,
               s_reading_detail_request_path[0] != '\0' ? s_reading_detail_request_path : "<none>");
    rt_sem_release(&s_reading_detail_load_sem);
}

static void ui_reading_detail_load_selected_sync(void)
{
    char file_path[sizeof(s_reading_detail_request_path)];
    uint32_t request_id;
    uint32_t load_start_ms;

    ui_reading_detail_watchdog_begin_long_task();
    ui_reading_detail_reset_text_source();
    ui_reading_detail_reset_pages();
    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));
    memset(s_reading_detail_first_page_layout, 0, sizeof(s_reading_detail_first_page_layout));
    memset(s_reading_detail_page_buffer, 0, sizeof(s_reading_detail_page_buffer));
    file_path[0] = '\0';
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    s_reading_detail_first_page_layout_ready = false;
    s_reading_detail_first_page_bitmap_ready = false;

    (void)ui_reading_list_get_selected_path(file_path, sizeof(file_path));

    rt_snprintf(s_reading_detail_request_path,
                sizeof(s_reading_detail_request_path),
                "%s",
                file_path);
    ++s_reading_detail_request_id;
    request_id = s_reading_detail_request_id;
    s_reading_detail_load_state = UI_READING_DETAIL_LOAD_LOADING;
    s_reading_detail_completed_request_id = 0U;
    load_start_ms = ui_reading_detail_now_ms();

    rt_kprintf("reading_detail: request=%lu queued path=%s\n",
               (unsigned long)request_id,
               s_reading_detail_request_path[0] != '\0' ? s_reading_detail_request_path : "<none>");
    ui_reading_detail_watchdog_progress();

    if (file_path[0] == '\0')
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "%s",
                    ui_i18n_pick("TF 卡中还没有可阅读的文本文件。\n\n请先返回列表页确认文件是否已经识别。",
                                 "There are no readable text files on the TF card yet.\n\nPlease go back to the list and check whether the file has been detected."));
        ui_reading_detail_use_memory_text();
        ui_reading_detail_reset_pages();
        ui_reading_detail_use_memory_text();
        (void)ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length());
        rt_kprintf("reading_detail: request=%lu fallback without selected file\n",
                   (unsigned long)request_id);
    }
    else if (strrchr(file_path, '.') != NULL &&
             strcasecmp(strrchr(file_path, '.'), ".epub") == 0)
    {
        (void)ui_reading_detail_load_epub_from_path(file_path);
    }
    else
    {
        (void)ui_reading_detail_load_text_from_path(file_path);
    }

    {
        bool has_last_page = false;
        uint16_t page_count = ui_reading_detail_snapshot_page_count(&has_last_page);

        s_reading_detail_first_page_layout_ready = page_count > 0U;
        s_reading_detail_load_state = UI_READING_DETAIL_LOAD_READY;
        s_reading_detail_completed_request_id = request_id;
        ui_reading_detail_watchdog_progress();

        rt_kprintf("reading_detail: request=%lu pages=%u last=%u total_ms=%lu\n",
                   (unsigned long)request_id,
                   (unsigned int)page_count,
                   has_last_page ? 1U : 0U,
                   (unsigned long)(ui_reading_detail_now_ms() - load_start_ms));
    }

    ui_reading_detail_watchdog_end_long_task();
}

bool ui_reading_detail_prepare_selected_async(void)
{
    ui_reading_detail_load_selected_sync();
    return false;
}

bool ui_reading_detail_is_selected_ready(void)
{
    return ui_reading_detail_is_content_ready() && ui_reading_detail_selected_matches_request();
}

static void ui_reading_detail_set_button_enabled(lv_obj_t *button, bool enabled)
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

static bool ui_reading_detail_render_page(void)
{
    bool render_ok = false;
    bool pending_refresh_consumed = false;
    bool has_last_page = false;
    uint16_t page_count;
    uint32_t render_start_ms;
    const ui_reading_detail_page_entry_t *page;
    lv_coord_t image_max_width;
    lv_coord_t decode_image_height;

    if (s_reading_detail_render_in_progress)
    {
        rt_kprintf("reading_detail: render skipped busy page=%u\n",
                   (unsigned int)(s_reading_detail_current_page + 1U));
        return false;
    }

    if (s_reading_detail_refs.content_label == NULL || s_reading_detail_refs.content_image == NULL)
    {
        return false;
    }

    if (!ui_reading_detail_is_content_ready())
    {
        return false;
    }

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    if (page_count == 0U || s_reading_detail_current_page >= page_count)
    {
        return false;
    }

    render_start_ms = ui_reading_detail_now_ms();
    ui_reading_detail_watchdog_begin_long_task();
    s_reading_detail_render_in_progress = true;
    ui_reading_detail_watchdog_progress();
    page = &s_reading_detail_pages[s_reading_detail_current_page];
    image_max_width = ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH);
    decode_image_height = (page->type == UI_READING_DETAIL_PAGE_IMAGE)
                              ? ui_px_h(UI_READING_DETAIL_READING_BOX_HEIGHT)
                              : (lv_coord_t)page->image_height;
    if (decode_image_height <= 0)
    {
        decode_image_height = ui_px_h(UI_READING_DETAIL_READING_BOX_HEIGHT);
    }
    lcd_set_epd_mono_dither_enabled(RT_FALSE);
    pending_refresh_consumed = ui_reading_detail_consume_image_transition_refresh("render");

    if (page->type == UI_READING_DETAIL_PAGE_IMAGE ||
        page->type == UI_READING_DETAIL_PAGE_IMAGE_TEXT)
    {
        uint16_t text_top = 0U;

        if (!pending_refresh_consumed &&
            ui_image_page_enter_requires_full(!s_reading_detail_first_render_done, false))
        {
            UI_EPD_REFRESH_LOG("reading_detail: image full refresh consume first=%d pending=0 page=%u\n",
                               s_reading_detail_first_render_done ? 0 : 1,
                               (unsigned int)(s_reading_detail_current_page + 1U));
            lcd_request_epd_force_full_refresh_once();
        }

        ui_reading_detail_release_current_image();
        ui_reading_detail_release_text_bitmap_buffer();
        ui_reading_detail_watchdog_progress();
        if (!reading_epub_decode_image(s_reading_detail_request_path,
                                       s_reading_detail_epub_images[page->image_index].internal_path,
                                       (uint16_t)image_max_width,
                                       (uint16_t)decode_image_height,
                                       &s_reading_detail_current_image_dsc))
        {
            ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                             ui_i18n_pick("图片加载失败。", "Failed to load image."));
            lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_coord_t target_image_height;

            target_image_height = (page->type == UI_READING_DETAIL_PAGE_IMAGE)
                                      ? s_reading_detail_current_image_dsc.header.h
                                      : s_reading_detail_current_image_dsc.header.h;
            lcd_set_epd_image_refresh_hint(RT_TRUE);
            ui_image_set_src(s_reading_detail_refs.content_image, &s_reading_detail_current_image_dsc);
            lv_image_set_pivot(s_reading_detail_refs.content_image, 0, 0);
            lv_image_set_scale_x(s_reading_detail_refs.content_image, LV_SCALE_NONE);
            lv_image_set_scale_y(s_reading_detail_refs.content_image, LV_SCALE_NONE);
            lv_image_set_antialias(s_reading_detail_refs.content_image, false);
            lv_image_set_offset_x(s_reading_detail_refs.content_image, 0);
            lv_image_set_offset_y(s_reading_detail_refs.content_image, 0);
            lv_image_set_inner_align(s_reading_detail_refs.content_image, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_obj_set_size(s_reading_detail_refs.content_image,
                            s_reading_detail_current_image_dsc.header.w,
                            target_image_height);
            lv_obj_set_pos(s_reading_detail_refs.content_image,
                           0,
                           0);
            UI_EPD_REFRESH_LOG("reading_detail: image render decoded=%dx%d target_h=%d page_type=%d\n",
                               (int)s_reading_detail_current_image_dsc.header.w,
                               (int)s_reading_detail_current_image_dsc.header.h,
                               (int)target_image_height,
                               (int)page->type);
            lv_obj_clear_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);

            if (page->type == UI_READING_DETAIL_PAGE_IMAGE_TEXT)
            {
                text_top = (uint16_t)(page->image_height + ui_px_y(UI_READING_DETAIL_IMAGE_TEXT_GAP));
                (void)ui_reading_detail_measure_page_range(page->start,
                                                           page->end,
                                                           s_reading_detail_page_buffer,
                                                           sizeof(s_reading_detail_page_buffer));
                if (s_reading_detail_page_buffer[0] == '\0')
                {
                    rt_snprintf(s_reading_detail_page_buffer,
                                sizeof(s_reading_detail_page_buffer),
                                "%s",
                                ui_i18n_pick("正文为空。", "The page is empty."));
                }

                lv_obj_set_pos(s_reading_detail_refs.content_label,
                               ui_reading_detail_get_text_offset_x(),
                               (lv_coord_t)text_top);
                lv_obj_set_size(s_reading_detail_refs.content_label,
                                ui_px_w(UI_READING_DETAIL_TEXT_WIDTH),
                                (lv_coord_t)page->text_height);
                ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                                 s_reading_detail_page_buffer);
                lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    else
    {
        ui_reading_detail_release_current_image();
        ui_reading_detail_release_text_bitmap_buffer();
        ui_reading_detail_apply_text_style();
        lv_obj_set_pos(s_reading_detail_refs.content_label,
                       ui_reading_detail_get_text_offset_x(),
                       8);
        lv_obj_set_size(s_reading_detail_refs.content_label,
                        ui_px_w(UI_READING_DETAIL_TEXT_WIDTH),
                        ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT));
        ui_reading_detail_watchdog_progress();
        (void)ui_reading_detail_measure_page_range(page->start,
                                                   page->end,
                                                   s_reading_detail_page_buffer,
                                                   sizeof(s_reading_detail_page_buffer));
        if (s_reading_detail_page_buffer[0] == '\0')
        {
            rt_snprintf(s_reading_detail_page_buffer,
                        sizeof(s_reading_detail_page_buffer),
                        "%s",
                        ui_i18n_pick("正文为空。", "The page is empty."));
        }

        if (ui_reading_detail_using_external_font() &&
            ui_reading_detail_render_text_bitmap(s_reading_detail_page_buffer))
        {
            ui_image_set_src(s_reading_detail_refs.content_image, &s_reading_detail_bitmap_dsc);
            lv_image_set_pivot(s_reading_detail_refs.content_image, 0, 0);
            lv_image_set_scale_x(s_reading_detail_refs.content_image, LV_SCALE_NONE);
            lv_image_set_scale_y(s_reading_detail_refs.content_image, LV_SCALE_NONE);
            lv_image_set_antialias(s_reading_detail_refs.content_image, false);
            lv_image_set_offset_x(s_reading_detail_refs.content_image, 0);
            lv_image_set_offset_y(s_reading_detail_refs.content_image, 0);
            lv_image_set_inner_align(s_reading_detail_refs.content_image, LV_IMAGE_ALIGN_TOP_LEFT);
            lv_obj_set_pos(s_reading_detail_refs.content_image,
                           ui_reading_detail_get_text_offset_x(),
                           8);
            lv_obj_set_size(s_reading_detail_refs.content_image,
                            s_reading_detail_bitmap_dsc.header.w,
                            s_reading_detail_bitmap_dsc.header.h);
            lv_obj_add_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            if (ui_reading_detail_using_external_font())
            {
                rt_kprintf("reading_detail: external font render fallback path=%s page=%u\n",
                           s_reading_detail_font_path[0] != '\0' ? s_reading_detail_font_path : "<empty>",
                           (unsigned int)(s_reading_detail_current_page + 1U));
            }
            ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                             s_reading_detail_page_buffer);
            lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_invalidate(s_reading_detail_refs.content_label);
    lv_obj_invalidate(s_reading_detail_refs.content_image);
    if (s_reading_detail_refs.reading_box != NULL)
    {
        lv_obj_invalidate(s_reading_detail_refs.reading_box);
    }
    ui_reading_detail_refresh_page_label();
    ui_reading_detail_refresh_nav_buttons();
    UI_EPD_REFRESH_LOG("reading_detail: render page=%u ready_pages=%u kind=%u render_ms=%lu\n",
                       (unsigned int)(s_reading_detail_current_page + 1U),
                       (unsigned int)page_count,
                       (unsigned int)page->type,
                       (unsigned long)(ui_reading_detail_now_ms() - render_start_ms));
    render_ok = true;

done:
    ui_reading_detail_watchdog_progress();
    s_reading_detail_render_in_progress = false;
    if (render_ok)
    {
        ui_reading_detail_arm_navigation_lock(UI_READING_DETAIL_NAV_LOCK_MS);
    }
    ui_reading_detail_watchdog_end_long_task();
    return render_ok;
}

static void ui_reading_detail_load_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (ui_Reading_Detail == NULL)
    {
        return;
    }

    if (!ui_reading_detail_is_content_ready())
    {
        return;
    }

    if (!s_reading_detail_first_render_done)
    {
        if (!ui_reading_detail_render_page())
        {
            return;
        }

        s_reading_detail_first_render_done = true;
        s_reading_detail_last_reported_page_count =
            ui_reading_detail_snapshot_page_count(&s_reading_detail_last_reported_has_last_page);
        if (s_reading_detail_load_timer != NULL)
        {
            lv_timer_delete(s_reading_detail_load_timer);
            s_reading_detail_load_timer = NULL;
        }
        return;
    }
}

static void ui_reading_detail_prev_page(void)
{
    uint16_t page_count;

    if (!ui_reading_detail_is_content_ready())
    {
        return;
    }

    if (ui_reading_detail_navigation_locked())
    {
        rt_kprintf("reading_detail: prev skipped busy page=%u\n",
                   (unsigned int)(s_reading_detail_current_page + 1U));
        return;
    }

    ui_reading_detail_arm_navigation_lock(UI_READING_DETAIL_NAV_LOCK_MS);
    ui_reading_detail_watchdog_progress();

    if (s_reading_detail_current_page == 0U)
    {
        if (s_reading_detail_epub_lazy_mode && s_reading_detail_epub_current_chapter > 0U)
        {
            ui_reading_detail_release_current_image();
            if (ui_reading_detail_load_epub_chapter((uint16_t)(s_reading_detail_epub_current_chapter - 1U)))
            {
                page_count = ui_reading_detail_snapshot_page_count(NULL);
                if (page_count > 0U)
                {
                    s_reading_detail_current_page = (uint16_t)(page_count - 1U);
                }
                (void)ui_reading_detail_render_page();
            }
            else
            {
                s_reading_detail_current_page = 0U;
                (void)ui_reading_detail_render_page();
            }
        }
        return;
    }

    ui_reading_detail_prepare_page_transition_refresh((uint16_t)(s_reading_detail_current_page - 1U));
    ui_reading_detail_release_current_image();
    --s_reading_detail_current_page;
    (void)ui_reading_detail_render_page();
}

static void ui_reading_detail_next_page(void)
{
    bool has_last_page = false;
    uint16_t page_count;

    if (!ui_reading_detail_is_content_ready())
    {
        return;
    }

    if (ui_reading_detail_navigation_locked())
    {
        rt_kprintf("reading_detail: next skipped busy page=%u\n",
                   (unsigned int)(s_reading_detail_current_page + 1U));
        return;
    }

    ui_reading_detail_arm_navigation_lock(UI_READING_DETAIL_NAV_LOCK_MS);
    ui_reading_detail_watchdog_progress();

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    if ((uint16_t)(s_reading_detail_current_page + 1U) < page_count)
    {
        ui_reading_detail_prepare_page_transition_refresh((uint16_t)(s_reading_detail_current_page + 1U));
        ui_reading_detail_release_current_image();
        ++s_reading_detail_current_page;
        (void)ui_reading_detail_render_page();
        return;
    }

    if (s_reading_detail_epub_lazy_mode &&
        (uint16_t)(s_reading_detail_epub_current_chapter + 1U) < s_reading_detail_epub_spine_count)
    {
        ui_reading_detail_release_current_image();
        if (ui_reading_detail_load_epub_chapter((uint16_t)(s_reading_detail_epub_current_chapter + 1U)))
        {
            s_reading_detail_current_page = 0U;
            (void)ui_reading_detail_render_page();
        }
        else
        {
            s_reading_detail_current_page = 0U;
            (void)ui_reading_detail_render_page();
        }
        return;
    }

    if (has_last_page)
    {
        ui_reading_detail_prepare_page_transition_refresh(0U);
        ui_reading_detail_release_current_image();
        s_reading_detail_current_page = 0U;
        (void)ui_reading_detail_render_page();
    }
}

void ui_reading_detail_hardware_prev_page(void)
{
    ui_reading_detail_queue_full_refresh_on_input("hw_prev");
    ui_reading_detail_prev_page();
}

void ui_reading_detail_hardware_next_page(void)
{
    ui_reading_detail_queue_full_refresh_on_input("hw_next");
    ui_reading_detail_next_page();
}

static void ui_reading_detail_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_reading_detail_queue_full_refresh_on_input("touch_prev");
        ui_reading_detail_prev_page();
    }
}

static void ui_reading_detail_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_reading_detail_queue_full_refresh_on_input("touch_next");
        ui_reading_detail_next_page();
    }
}

static void ui_reading_detail_content_event_cb(lv_event_t *e)
{
    lv_point_t point;
    lv_indev_t *indev;
    lv_coord_t split_x;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (ui_reading_detail_settings_visible())
    {
        return;
    }

    indev = lv_indev_active();
    if (indev == NULL)
    {
        return;
    }

    ui_reading_detail_queue_full_refresh_on_input("touch_content");
    lv_indev_get_point(indev, &point);
    split_x = ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH) / 2;
    if (point.x < split_x)
    {
        ui_reading_detail_prev_page();
    }
    else
    {
        ui_reading_detail_next_page();
    }
}

static void ui_reading_detail_bottom_swipe_event_cb(lv_event_t *e)
{
    lv_event_code_t code;
    lv_point_t point;
    lv_coord_t screen_height;
    lv_coord_t start_threshold_y;
    lv_coord_t min_rise;
    lv_coord_t max_side_delta;

    if (ui_reading_detail_settings_visible())
    {
        return;
    }

    code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED &&
        code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST)
    {
        return;
    }

    if (lv_indev_active() == NULL)
    {
        return;
    }

    lv_indev_get_point(lv_indev_active(), &point);
    screen_height = lv_obj_get_height(ui_Reading_Detail);
    start_threshold_y = screen_height - ui_px_y(UI_READING_DETAIL_SWIPE_HANDLE_HEIGHT);
    min_rise = ui_px_y(64);
    max_side_delta = ui_px_x(120);

    if (code == LV_EVENT_PRESSED)
    {
        ui_reading_detail_queue_full_refresh_on_input("touch_swipe");
        memset(&s_reading_detail_swipe_state, 0, sizeof(s_reading_detail_swipe_state));
        if (point.y >= start_threshold_y)
        {
            s_reading_detail_swipe_state.active = true;
            s_reading_detail_swipe_state.start_point = point;
            s_reading_detail_swipe_state.last_point = point;
        }
        return;
    }

    if (!s_reading_detail_swipe_state.active)
    {
        return;
    }

    s_reading_detail_swipe_state.last_point = point;

    if (code == LV_EVENT_PRESSING)
    {
        lv_coord_t delta_y = s_reading_detail_swipe_state.start_point.y - point.y;
        lv_coord_t delta_x = point.x - s_reading_detail_swipe_state.start_point.x;
        if (delta_x < 0)
        {
            delta_x = -delta_x;
        }

        if (delta_y >= min_rise && delta_x <= max_side_delta)
        {
            s_reading_detail_swipe_state.triggered = true;
        }
        return;
    }

    if (code == LV_EVENT_RELEASED && s_reading_detail_swipe_state.triggered)
    {
        ui_reading_detail_set_settings_visible(true);
    }

    memset(&s_reading_detail_swipe_state, 0, sizeof(s_reading_detail_swipe_state));
}

static void ui_reading_detail_settings_overlay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    if (lv_event_get_target(e) == s_reading_detail_refs.settings_overlay)
    {
        ui_reading_detail_set_settings_visible(false);
    }
}

static void ui_reading_detail_adjust_font_event_cb(lv_event_t *e)
{
    intptr_t delta;
    int32_t next_value;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    delta = (intptr_t)lv_event_get_user_data(e);
    next_value = (int32_t)s_reading_detail_font_size + (int32_t)delta;
    if (next_value < UI_READING_DETAIL_TEXT_FONT_MIN ||
        next_value > UI_READING_DETAIL_TEXT_FONT_MAX)
    {
        return;
    }

    s_reading_detail_font_size = (uint16_t)next_value;
    ui_reading_detail_save_layout();
    app_config_save();
    ui_reading_detail_apply_text_style();
    ui_reading_detail_refresh_settings_panel();
    ui_reading_detail_rebuild_layout();
}

static void ui_reading_detail_adjust_line_space_event_cb(lv_event_t *e)
{
    intptr_t delta;
    int32_t next_value;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    delta = (intptr_t)lv_event_get_user_data(e);
    next_value = (int32_t)s_reading_detail_line_space + (int32_t)delta;
    if (next_value < UI_READING_DETAIL_TEXT_LINE_SPACE_MIN ||
        next_value > UI_READING_DETAIL_TEXT_LINE_SPACE_MAX)
    {
        return;
    }

    s_reading_detail_line_space = (uint16_t)next_value;
    ui_reading_detail_save_layout();
    app_config_save();
    ui_reading_detail_apply_text_style();
    ui_reading_detail_refresh_settings_panel();
    ui_reading_detail_rebuild_layout();
}

static void ui_reading_detail_adjust_font_family_event_cb(lv_event_t *e)
{
    intptr_t delta;
    int32_t next_index;
    const char *selected_name;
    char selected_path[UI_FONT_MANAGER_PATH_MAX];

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    ui_reading_detail_refresh_font_items();
    if (s_reading_detail_font_item_count <= 1U)
    {
        return;
    }

    delta = (intptr_t)lv_event_get_user_data(e);
    next_index = (int32_t)s_reading_detail_font_item_index + (int32_t)delta;
    if (next_index < 0)
    {
        next_index = (int32_t)s_reading_detail_font_item_count - 1;
    }
    else if (next_index >= (int32_t)s_reading_detail_font_item_count)
    {
        next_index = 0;
    }

    if ((uint16_t)next_index == s_reading_detail_font_item_index)
    {
        return;
    }

    if (!ui_reading_detail_set_selected_font_item((uint16_t)next_index))
    {
        return;
    }

    ui_reading_detail_save_config();
    selected_name = ui_reading_detail_get_selected_font_name();
    selected_path[0] = '\0';
    (void)ui_reading_detail_get_selected_font_path(selected_path, sizeof(selected_path));
    rt_kprintf("reading_detail: font switched name=%s system=%u path=%s\n",
               selected_name,
               s_reading_detail_font_use_system ? 1U : 0U,
               selected_path[0] != '\0' ? selected_path : "<system>");

    if (ui_reading_detail_using_external_font() &&
        !ui_reading_detail_init_hdfont())
    {
        rt_kprintf("reading_detail: selected hdfont init failed path=%s\n",
                   selected_path[0] != '\0' ? selected_path : "<empty>");
    }

    ui_reading_detail_apply_text_style();
    ui_reading_detail_refresh_settings_panel();
    ui_reading_detail_rebuild_layout();
}

void ui_Reading_Detail_screen_init(void)
{
    lv_obj_t *reading_box;
    lv_obj_t *divider;
    lv_obj_t *row;
    lv_obj_t *button;

    if (ui_Reading_Detail != NULL)
    {
        return;
    }

    (void)ui_reading_list_prepare_selected_file();

    memset(&s_reading_detail_refs, 0, sizeof(s_reading_detail_refs));
    memset(s_reading_detail_page_text, 0, sizeof(s_reading_detail_page_text));
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    ui_reading_detail_load_config_defaults();

    ui_Reading_Detail = ui_create_screen_base();
    rt_kprintf("reading_detail: init fullscreen detail\n");

    reading_box = ui_create_card(ui_Reading_Detail,
                                 0,
                                 8,
                                 UI_READING_DETAIL_IMAGE_WIDTH,
                                 UI_READING_DETAIL_READING_BOX_HEIGHT,
                                 UI_SCREEN_NONE,
                                 false,
                                 0);
    s_reading_detail_refs.reading_box = reading_box;
    lv_obj_set_style_border_width(reading_box, 0, 0);
    lv_obj_set_style_bg_opa(reading_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(reading_box, 0, 0);
    lv_obj_set_style_clip_corner(reading_box, true, 0);
    s_reading_detail_refs.content_label = ui_create_label(reading_box,
                                                          "",
                                                          ui_reading_detail_get_text_offset_x(),
                                                          8,
                                                          UI_READING_DETAIL_TEXT_WIDTH,
                                                          UI_READING_DETAIL_TEXT_HEIGHT,
                                                          UI_READING_DETAIL_TEXT_FONT,
                                                          LV_TEXT_ALIGN_LEFT,
                                                          false,
                                                          true);
    lv_label_set_long_mode(s_reading_detail_refs.content_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(s_reading_detail_refs.content_label,
                    ui_px_w(UI_READING_DETAIL_TEXT_WIDTH),
                    ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT));
    lv_obj_set_style_text_font(s_reading_detail_refs.content_label,
                               ui_reading_detail_get_text_font(),
                               0);
    lv_obj_set_style_text_align(s_reading_detail_refs.content_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(s_reading_detail_refs.content_label, 0, 0);
    lv_obj_set_style_text_line_space(s_reading_detail_refs.content_label,
                                     ui_reading_detail_get_line_space(),
                                     0);
    lv_obj_add_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_reading_detail_refs.content_label,
                        ui_reading_detail_content_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    s_reading_detail_refs.content_image = lv_image_create(reading_box);
    lv_image_set_pivot(s_reading_detail_refs.content_image, 0, 0);
    lv_image_set_scale(s_reading_detail_refs.content_image, LV_SCALE_NONE);
    lv_image_set_antialias(s_reading_detail_refs.content_image, false);
    lv_image_set_inner_align(s_reading_detail_refs.content_image, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_reading_detail_refs.content_image, 0, 8);
    lv_obj_set_size(s_reading_detail_refs.content_image,
                    ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH),
                    ui_px_h(UI_READING_DETAIL_READING_BOX_HEIGHT));
    lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_reading_detail_refs.content_image,
                        ui_reading_detail_content_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);
    lv_obj_add_flag(reading_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(reading_box, ui_reading_detail_content_event_cb, LV_EVENT_CLICKED, NULL);

    divider = lv_obj_create(ui_Reading_Detail);
    lv_obj_set_pos(divider, ui_px_x(16), ui_px_y(744));
    lv_obj_set_size(divider, ui_px_w(496), 1);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_30, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_shadow_width(divider, 0, 0);

    s_reading_detail_refs.page_label = ui_create_label(ui_Reading_Detail,
                                                       "1 / 1",
                                                       16,
                                                       754,
                                                       160,
                                                       24,
                                                       17,
                                                       LV_TEXT_ALIGN_LEFT,
                                                       false,
                                                       false);
    s_reading_detail_refs.file_name_label = ui_create_label(ui_Reading_Detail,
                                                            "",
                                                            196,
                                                            754,
                                                            316,
                                                            24,
                                                            17,
                                                            LV_TEXT_ALIGN_RIGHT,
                                                            false,
                                                            false);
    lv_label_set_long_mode(s_reading_detail_refs.file_name_label, LV_LABEL_LONG_DOT);
    ui_reading_detail_refresh_file_name_label();
    s_reading_detail_refs.prev_button = NULL;
    s_reading_detail_refs.next_button = NULL;

    s_reading_detail_refs.swipe_handle = lv_obj_create(ui_Reading_Detail);
    lv_obj_set_pos(s_reading_detail_refs.swipe_handle,
                   0,
                   lv_obj_get_height(ui_Reading_Detail) - ui_px_y(UI_READING_DETAIL_SWIPE_HANDLE_HEIGHT));
    lv_obj_set_size(s_reading_detail_refs.swipe_handle,
                    lv_obj_get_width(ui_Reading_Detail),
                    ui_px_y(UI_READING_DETAIL_SWIPE_HANDLE_HEIGHT));
    lv_obj_set_style_radius(s_reading_detail_refs.swipe_handle, 0, 0);
    lv_obj_set_style_bg_opa(s_reading_detail_refs.swipe_handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_reading_detail_refs.swipe_handle, 0, 0);
    lv_obj_set_style_shadow_width(s_reading_detail_refs.swipe_handle, 0, 0);
    lv_obj_add_flag(s_reading_detail_refs.swipe_handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_reading_detail_refs.swipe_handle, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_reading_detail_refs.swipe_handle, ui_reading_detail_bottom_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_reading_detail_refs.swipe_handle, ui_reading_detail_bottom_swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_reading_detail_refs.swipe_handle, ui_reading_detail_bottom_swipe_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_reading_detail_refs.swipe_handle, ui_reading_detail_bottom_swipe_event_cb, LV_EVENT_PRESS_LOST, NULL);

    s_reading_detail_refs.settings_overlay = lv_obj_create(ui_Reading_Detail);
    lv_obj_set_pos(s_reading_detail_refs.settings_overlay, 0, 0);
    lv_obj_set_size(s_reading_detail_refs.settings_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(s_reading_detail_refs.settings_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_reading_detail_refs.settings_overlay, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_reading_detail_refs.settings_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_reading_detail_refs.settings_overlay, 0, 0);
    lv_obj_set_style_border_width(s_reading_detail_refs.settings_overlay, 0, 0);
    lv_obj_add_flag(s_reading_detail_refs.settings_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_reading_detail_refs.settings_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_reading_detail_refs.settings_overlay,
                        ui_reading_detail_settings_overlay_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    s_reading_detail_refs.settings_panel = ui_create_card(s_reading_detail_refs.settings_overlay,
                                                          0,
                                                          lv_obj_get_height(ui_Reading_Detail) - ui_px_y(UI_READING_DETAIL_SETTINGS_PANEL_HEIGHT),
                                                          528,
                                                          UI_READING_DETAIL_SETTINGS_PANEL_HEIGHT,
                                                          UI_SCREEN_NONE,
                                                          false,
                                                          0);
    lv_obj_set_style_bg_color(s_reading_detail_refs.settings_panel, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_reading_detail_refs.settings_panel, 2, 0);
    lv_obj_set_style_pad_all(s_reading_detail_refs.settings_panel, 0, 0);
    lv_obj_add_flag(s_reading_detail_refs.settings_panel, LV_OBJ_FLAG_CLICKABLE);

    ui_create_label(s_reading_detail_refs.settings_panel,
                    ui_i18n_pick("阅读设置", "Reading Settings"),
                    24,
                    16,
                    160,
                    24,
                    20,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    row = ui_create_card(s_reading_detail_refs.settings_panel, 24, 52, 480, 42, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    ui_create_label(row, ui_i18n_pick("字号", "Font"), 0, 8, 80, 24, 20, LV_TEXT_ALIGN_LEFT, false, false);
    button = ui_create_button(row, 256, 0, 56, 42, "-", 24, UI_SCREEN_NONE, false);
    s_reading_detail_refs.font_dec_button = button;
    lv_obj_add_event_cb(button, ui_reading_detail_adjust_font_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(-UI_READING_DETAIL_TEXT_FONT_STEP));
    s_reading_detail_refs.font_value_label = ui_create_label(row,
                                                             "22",
                                                             324,
                                                             8,
                                                             UI_READING_DETAIL_SETTINGS_VALUE_WIDTH,
                                                             24,
                                                             20,
                                                             LV_TEXT_ALIGN_CENTER,
                                                             false,
                                                             false);
    button = ui_create_button(row, 404, 0, 56, 42, "+", 24, UI_SCREEN_NONE, true);
    s_reading_detail_refs.font_inc_button = button;
    lv_obj_add_event_cb(button, ui_reading_detail_adjust_font_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)UI_READING_DETAIL_TEXT_FONT_STEP);

    row = ui_create_card(s_reading_detail_refs.settings_panel, 24, 102, 480, 42, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    ui_create_label(row, ui_i18n_pick("行距", "Spacing"), 0, 8, 80, 24, 20, LV_TEXT_ALIGN_LEFT, false, false);
    button = ui_create_button(row, 256, 0, 56, 42, "-", 24, UI_SCREEN_NONE, false);
    s_reading_detail_refs.line_space_dec_button = button;
    lv_obj_add_event_cb(button,
                        ui_reading_detail_adjust_line_space_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)(-UI_READING_DETAIL_TEXT_LINE_SPACE_STEP));
    s_reading_detail_refs.line_space_value_label = ui_create_label(row,
                                                                   "2",
                                                                   324,
                                                                   8,
                                                                   UI_READING_DETAIL_SETTINGS_VALUE_WIDTH,
                                                                   24,
                                                                   20,
                                                                   LV_TEXT_ALIGN_CENTER,
                                                                   false,
                                                                   false);
    button = ui_create_button(row, 404, 0, 56, 42, "+", 24, UI_SCREEN_NONE, true);
    s_reading_detail_refs.line_space_inc_button = button;
    lv_obj_add_event_cb(button,
                        ui_reading_detail_adjust_line_space_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_READING_DETAIL_TEXT_LINE_SPACE_STEP);

    row = ui_create_card(s_reading_detail_refs.settings_panel, 24, 152, 480, 42, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    ui_create_label(row, ui_i18n_pick("字体", "Typeface"), 0, 8, 80, 24, 20, LV_TEXT_ALIGN_LEFT, false, false);
    button = ui_create_button(row, 204, 0, 48, 42, "<", 22, UI_SCREEN_NONE, false);
    s_reading_detail_refs.font_family_prev_button = button;
    lv_obj_add_event_cb(button,
                        ui_reading_detail_adjust_font_family_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)-1);
    s_reading_detail_refs.font_family_value_label = ui_create_label(row,
                                                                    ui_i18n_pick("系统字体", "System Font"),
                                                                    260,
                                                                    8,
                                                                    UI_READING_DETAIL_SETTINGS_FONT_NAME_WIDTH,
                                                                    24,
                                                                    18,
                                                                    LV_TEXT_ALIGN_CENTER,
                                                                    false,
                                                                    false);
    button = ui_create_button(row, 432, 0, 48, 42, ">", 22, UI_SCREEN_NONE, true);
    s_reading_detail_refs.font_family_next_button = button;
    lv_obj_add_event_cb(button,
                        ui_reading_detail_adjust_font_family_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)1);
    ui_reading_detail_refresh_font_items();
    ui_reading_detail_refresh_settings_panel();

    ui_reading_detail_show_loading_state();
    ui_reading_detail_load_selected_sync();

    if (ui_reading_detail_is_selected_ready())
    {
        rt_kprintf("reading_detail: refs after load box=%p label=%p image=%p page=%p\n",
                   s_reading_detail_refs.reading_box,
                   s_reading_detail_refs.content_label,
                   s_reading_detail_refs.content_image,
                   s_reading_detail_refs.page_label);
        (void)ui_reading_detail_render_page();
        s_reading_detail_first_render_done = true;
    }
    else
    {
        ui_reading_detail_show_loading_state();
    }
}

void ui_Reading_Detail_screen_destroy(void)
{
    if (s_reading_detail_load_timer != NULL)
    {
        lv_timer_delete(s_reading_detail_load_timer);
        s_reading_detail_load_timer = NULL;
    }

    lcd_request_epd_force_full_refresh_once();

    ui_reading_detail_release_current_image();
    ui_reading_detail_release_text_bitmap_buffer();

    if (ui_Reading_Detail != NULL)
    {
        lv_obj_delete(ui_Reading_Detail);
        ui_Reading_Detail = NULL;
    }

    ui_reading_detail_release_font_resources();

    memset(&s_reading_detail_refs, 0, sizeof(s_reading_detail_refs));
    memset(&s_reading_detail_swipe_state, 0, sizeof(s_reading_detail_swipe_state));
    s_reading_detail_current_page = 0U;
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    ui_reading_detail_clear_navigation_lock();
    s_reading_detail_request_path[0] = '\0';
    ui_reading_detail_reset_epub_lazy_state();
    ++s_reading_detail_request_id;
    s_reading_detail_load_state = UI_READING_DETAIL_LOAD_IDLE;
    s_reading_detail_completed_request_id = 0U;
}
