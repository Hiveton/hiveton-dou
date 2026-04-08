#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "dfs_posix.h"
#include "rtthread.h"
#include "rthw.h"
#include "audio_mem.h"
#include "drv_lcd.h"
#include "mem_section.h"
#include "reading_epub.h"
#include "ui.h"
#include "ui_i18n.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#define STBTT_malloc(x, u) ((void)(u), lv_malloc(x))
#define STBTT_free(x, u) ((void)(u), lv_free(x))
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../../sdk/external/lvgl_v9/src/libs/tiny_ttf/stb_truetype_htcw.h"

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
#define UI_READING_DETAIL_TEXT_WIDTH 460
#define UI_READING_DETAIL_TEXT_HEIGHT 558
#define UI_READING_DETAIL_TEXT_FONT 20
#define UI_READING_DETAIL_TEXT_LINE_SPACE 2
#define UI_READING_DETAIL_TEXT_FONT_MIN 16
#define UI_READING_DETAIL_TEXT_FONT_MAX 28
#define UI_READING_DETAIL_TEXT_FONT_STEP 2
#define UI_READING_DETAIL_TEXT_LINE_SPACE_MIN 0
#define UI_READING_DETAIL_TEXT_LINE_SPACE_MAX 12
#define UI_READING_DETAIL_TEXT_LINE_SPACE_STEP 1
#define UI_READING_DETAIL_FONT_DELTA 0
#define UI_READING_DETAIL_GLYPH_COVERAGE_THRESHOLD 160U
#define UI_READING_DETAIL_BITMAP_GLYPH_SCRATCH_BYTES 4096U
#define UI_READING_DETAIL_SETTINGS_PANEL_HEIGHT 172
#define UI_READING_DETAIL_SETTINGS_VALUE_WIDTH 76
#define UI_READING_DETAIL_SWIPE_HANDLE_HEIGHT 48
#define UI_READING_DETAIL_MAX_BLOCK_COUNT 256U
#define UI_READING_DETAIL_MAX_IMAGE_COUNT 48U
#define UI_READING_DETAIL_MAX_SPINE_ITEM_COUNT 96U
#define UI_READING_DETAIL_TEXT_STREAM_WINDOW_BYTES (128 * 1024U)

typedef struct
{
    lv_obj_t *reading_box;
    lv_obj_t *content_label;
    lv_obj_t *content_image;
    lv_obj_t *page_label;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;
    lv_obj_t *settings_overlay;
    lv_obj_t *settings_panel;
    lv_obj_t *font_value_label;
    lv_obj_t *line_space_value_label;
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
} ui_reading_detail_page_entry_t;

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
static lv_timer_t *s_reading_detail_load_timer = NULL;
static volatile bool s_reading_detail_has_last_page = false;
static volatile uint16_t s_reading_detail_epub_block_count = 0U;
static volatile uint16_t s_reading_detail_epub_image_count = 0U;
static volatile uint16_t s_reading_detail_epub_spine_count = 0U;
static uint16_t s_reading_detail_epub_current_chapter = 0U;
static bool s_reading_detail_epub_lazy_mode = false;
static bool s_reading_detail_load_worker_started = false;
static bool s_reading_detail_first_render_done = false;
static uint16_t s_reading_detail_last_reported_page_count = 0U;
static bool s_reading_detail_last_reported_has_last_page = false;
static volatile uint32_t s_reading_detail_request_id = 0U;
static volatile uint32_t s_reading_detail_completed_request_id = 0U;
static volatile ui_reading_detail_load_state_t s_reading_detail_load_state = UI_READING_DETAIL_LOAD_IDLE;
static volatile bool s_reading_detail_first_page_layout_ready = false;
static volatile bool s_reading_detail_first_page_bitmap_ready = false;
static char s_reading_detail_request_path[256];
static lv_image_dsc_t s_reading_detail_current_image_dsc;
static uint16_t s_reading_detail_width_cache_size = 0U;
static uint16_t s_reading_detail_ascii_width_cache[128];
static uint16_t s_reading_detail_cjk_width = 0U;
static uint16_t s_reading_detail_fullwidth_width = 0U;
static lv_color_t *s_reading_detail_bitmap_buffer = NULL;
static lv_image_dsc_t s_reading_detail_bitmap_dsc;
static bool s_reading_detail_bitmap_inited = false;
static stbtt_fontinfo s_reading_detail_stb_info;
static bool s_reading_detail_stb_ready = false;
static float s_reading_detail_stb_scale = 0.0f;
static int s_reading_detail_stb_ascent = 0;
static int s_reading_detail_stb_descent = 0;
static int s_reading_detail_stb_line_gap = 0;
static uint8_t s_reading_detail_glyph_scratch[UI_READING_DETAIL_BITMAP_GLYPH_SCRATCH_BYTES];
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

extern const unsigned char xiaozhi_font[];
extern const int xiaozhi_font_size;

static uint32_t ui_reading_detail_now_ms(void)
{
    return rt_tick_get_millisecond();
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
static void ui_reading_detail_rebuild_layout(void);
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
static uint32_t ui_reading_detail_measure_page_range(uint32_t start,
                                                     uint32_t end_limit,
                                                     char *formatted_buffer,
                                                     size_t formatted_buffer_size);
static bool ui_reading_detail_paginate_text_range(uint32_t start, uint32_t end);
static void ui_reading_detail_reset_epub_lazy_state(void);
static bool ui_reading_detail_load_epub_chapter(uint16_t chapter_index);

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

static bool ui_reading_detail_init_stb_font(void)
{
    int font_px;

    if (s_reading_detail_stb_ready)
    {
        return true;
    }

    if (!stbtt_InitFont(&s_reading_detail_stb_info,
                        xiaozhi_font,
                        stbtt_GetFontOffsetForIndex(xiaozhi_font, 0)))
    {
        rt_kprintf("reading_detail: stb init failed\n");
        return false;
    }

    font_px = (int)ui_reading_detail_get_actual_font_size() + UI_READING_DETAIL_FONT_DELTA;
    if (font_px <= 0)
    {
        font_px = UI_READING_DETAIL_TEXT_FONT + UI_READING_DETAIL_FONT_DELTA;
    }

    s_reading_detail_stb_scale = stbtt_ScaleForMappingEmToPixels(&s_reading_detail_stb_info,
                                                                 (float)font_px);
    stbtt_GetFontVMetrics(&s_reading_detail_stb_info,
                          &s_reading_detail_stb_ascent,
                          &s_reading_detail_stb_descent,
                          &s_reading_detail_stb_line_gap);
    s_reading_detail_stb_ready = true;
    return true;
}

static bool ui_reading_detail_ensure_bitmap_buffer(void)
{
    uint32_t pixel_count;
    uint32_t data_size;

    if (s_reading_detail_bitmap_buffer != NULL)
    {
        return true;
    }

    pixel_count = (uint32_t)ui_px_w(UI_READING_DETAIL_TEXT_WIDTH) *
                  (uint32_t)ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT);
    data_size = pixel_count * (uint32_t)sizeof(lv_color_t);
    s_reading_detail_bitmap_buffer = (lv_color_t *)audio_mem_malloc(data_size);
    if (s_reading_detail_bitmap_buffer == NULL)
    {
        rt_kprintf("reading_detail: bitmap alloc failed bytes=%lu\n",
                   (unsigned long)data_size);
        return false;
    }

    memset(s_reading_detail_bitmap_buffer, 0xFF, data_size);
    memset(&s_reading_detail_bitmap_dsc, 0, sizeof(s_reading_detail_bitmap_dsc));
    s_reading_detail_bitmap_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_reading_detail_bitmap_dsc.header.w = ui_px_w(UI_READING_DETAIL_TEXT_WIDTH);
    s_reading_detail_bitmap_dsc.header.h = ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT);
    s_reading_detail_bitmap_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_reading_detail_bitmap_dsc.header.stride =
        s_reading_detail_bitmap_dsc.header.w * (lv_coord_t)sizeof(lv_color_t);
    s_reading_detail_bitmap_dsc.data_size = data_size;
    s_reading_detail_bitmap_dsc.data = (const uint8_t *)s_reading_detail_bitmap_buffer;
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

    reading_epub_release_image(&s_reading_detail_current_image_dsc);
    s_reading_detail_current_page = 0U;
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
    int32_t max_height;
    int32_t line_space;
    int32_t line_height;
    uint16_t max_lines;

    max_height = ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT);
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

    if (glyph_bitmap == NULL || s_reading_detail_bitmap_buffer == NULL || !s_reading_detail_bitmap_inited)
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

            s_reading_detail_bitmap_buffer[py * max_w + px] = lv_color_make(0, 0, 0);
        }
    }
}

static bool ui_reading_detail_render_text_bitmap(const char *formatted_text)
{
    uint8_t *glyph_bitmap;
    uint32_t index;
    int pen_x;
    int pen_y;
    int baseline;
    int line_step;
    uint32_t codepoint;

    if (formatted_text == NULL)
    {
        return false;
    }

    if (!ui_reading_detail_ensure_bitmap_buffer() || !ui_reading_detail_init_stb_font())
    {
        return false;
    }

    memset(s_reading_detail_bitmap_buffer, 0xFF, s_reading_detail_bitmap_dsc.data_size);
    line_step = ui_reading_detail_get_text_line_height_px() + ui_reading_detail_get_line_space();
    if (line_step <= 0)
    {
        line_step = 1;
    }

    baseline = (int)(s_reading_detail_stb_ascent * s_reading_detail_stb_scale + 0.5f);
    pen_x = 0;
    pen_y = baseline;
    index = 0U;
    glyph_bitmap = s_reading_detail_glyph_scratch;

    while ((codepoint = ui_reading_detail_utf8_next(formatted_text, &index)) != 0U)
    {
        uint32_t next_index = index;
        uint32_t next_codepoint = ui_reading_detail_utf8_next(formatted_text, &next_index);
        int x0;
        int y0;
        int x1;
        int y1;
        int glyph_w;
        int glyph_h;
        int advance_px;

        if (codepoint == '\n')
        {
            pen_x = 0;
            pen_y += line_step;
            continue;
        }

        advance_px = (int)ui_reading_detail_get_glyph_width_fast(NULL,
                                                                 codepoint,
                                                                 next_codepoint,
                                                                 (uint16_t)ui_px_x(UI_READING_DETAIL_TEXT_FONT));
        stbtt_GetCodepointBitmapBox(&s_reading_detail_stb_info,
                                    (int)codepoint,
                                    s_reading_detail_stb_scale,
                                    s_reading_detail_stb_scale,
                                    &x0,
                                    &y0,
                                    &x1,
                                    &y1);
        glyph_w = x1 - x0;
        glyph_h = y1 - y0;
        if (glyph_w > 0 && glyph_h > 0)
        {
            size_t glyph_bytes = (size_t)glyph_w * (size_t)glyph_h;

            if (glyph_bytes > sizeof(s_reading_detail_glyph_scratch))
            {
                glyph_bitmap = (uint8_t *)rt_malloc(glyph_bytes);
                if (glyph_bitmap == NULL)
                {
                    return false;
                }
            }

            memset(glyph_bitmap, 0, glyph_bytes);
            stbtt_MakeCodepointBitmap(&s_reading_detail_stb_info,
                                      glyph_bitmap,
                                      glyph_w,
                                      glyph_h,
                                      glyph_w,
                                      s_reading_detail_stb_scale,
                                      s_reading_detail_stb_scale,
                                      (int)codepoint);
            ui_reading_detail_blend_glyph(glyph_bitmap,
                                          glyph_w,
                                          glyph_h,
                                          pen_x + x0,
                                          pen_y + y0);

            if (glyph_bitmap != s_reading_detail_glyph_scratch)
            {
                rt_free(glyph_bitmap);
                glyph_bitmap = s_reading_detail_glyph_scratch;
            }
        }

        pen_x += advance_px;
    }

    return true;
}

static bool ui_reading_detail_is_cjk_codepoint(uint32_t codepoint)
{
    return (codepoint >= 0x3400U && codepoint <= 0x4DBFU) ||
           (codepoint >= 0x4E00U && codepoint <= 0x9FFFU) ||
           (codepoint >= 0xF900U && codepoint <= 0xFAFFU);
}

static bool ui_reading_detail_is_fullwidth_codepoint(uint32_t codepoint)
{
    return (codepoint >= 0x3000U && codepoint <= 0x303FU) ||
           (codepoint >= 0xFF01U && codepoint <= 0xFF60U) ||
           (codepoint >= 0xFFE0U && codepoint <= 0xFFE6U);
}

static uint16_t ui_reading_detail_get_glyph_width_fast(const lv_font_t *font,
                                                       uint32_t codepoint,
                                                       uint32_t next_codepoint,
                                                       uint16_t fallback_width)
{
    int advance = 0;
    int lsb = 0;
    int kern_advance = 0;
    uint16_t actual_font_size;
    uint16_t width;

    LV_UNUSED(font);

    if (!ui_reading_detail_init_stb_font())
    {
        return fallback_width;
    }

    actual_font_size = ui_reading_detail_get_actual_font_size();
    if (s_reading_detail_width_cache_size != actual_font_size)
    {
        memset(s_reading_detail_ascii_width_cache, 0, sizeof(s_reading_detail_ascii_width_cache));
        s_reading_detail_cjk_width = 0U;
        s_reading_detail_fullwidth_width = 0U;
        s_reading_detail_width_cache_size = actual_font_size;
    }

    if (codepoint < 128U)
    {
        width = s_reading_detail_ascii_width_cache[codepoint];
        if (width == 0U)
        {
            stbtt_GetCodepointHMetrics(&s_reading_detail_stb_info, (int)codepoint, &advance, &lsb);
            kern_advance = next_codepoint != 0U
                               ? stbtt_GetCodepointKernAdvance(&s_reading_detail_stb_info,
                                                               (int)codepoint,
                                                               (int)next_codepoint)
                               : 0;
            width = (uint16_t)((advance + kern_advance) * s_reading_detail_stb_scale + 0.5f);
            if (width == 0U) width = fallback_width;
            s_reading_detail_ascii_width_cache[codepoint] = width;
        }
        return width;
    }

    if (ui_reading_detail_is_cjk_codepoint(codepoint))
    {
        if (s_reading_detail_cjk_width == 0U)
        {
            stbtt_GetCodepointHMetrics(&s_reading_detail_stb_info, 0x4E2D, &advance, &lsb);
            s_reading_detail_cjk_width = (uint16_t)(advance * s_reading_detail_stb_scale + 0.5f);
            if (s_reading_detail_cjk_width == 0U) s_reading_detail_cjk_width = fallback_width;
        }
        return s_reading_detail_cjk_width;
    }

    if (ui_reading_detail_is_fullwidth_codepoint(codepoint))
    {
        if (s_reading_detail_fullwidth_width == 0U)
        {
            stbtt_GetCodepointHMetrics(&s_reading_detail_stb_info, 0x3002, &advance, &lsb);
            s_reading_detail_fullwidth_width = (uint16_t)(advance * s_reading_detail_stb_scale + 0.5f);
            if (s_reading_detail_fullwidth_width == 0U) s_reading_detail_fullwidth_width = fallback_width;
        }
        return s_reading_detail_fullwidth_width;
    }

    stbtt_GetCodepointHMetrics(&s_reading_detail_stb_info, (int)codepoint, &advance, &lsb);
    kern_advance = next_codepoint != 0U
                       ? stbtt_GetCodepointKernAdvance(&s_reading_detail_stb_info,
                                                       (int)codepoint,
                                                       (int)next_codepoint)
                       : 0;
    width = (uint16_t)((advance + kern_advance) * s_reading_detail_stb_scale + 0.5f);
    if (width == 0U)
    {
        width = fallback_width;
    }

    return width;
}

static int32_t ui_reading_detail_get_text_line_height_px(void)
{
    float line_height;

    if (!ui_reading_detail_init_stb_font())
    {
        return ui_px_h(UI_READING_DETAIL_TEXT_FONT);
    }

    line_height = (float)(s_reading_detail_stb_ascent - s_reading_detail_stb_descent + s_reading_detail_stb_line_gap) *
                  s_reading_detail_stb_scale;
    if (line_height <= 0.0f)
    {
        line_height = (float)ui_px_h(UI_READING_DETAIL_TEXT_FONT);
    }

    return (int32_t)(line_height + 0.5f);
}

static bool ui_reading_detail_append_page_entry(ui_reading_detail_page_type_t type,
                                                uint32_t start,
                                                uint32_t end,
                                                uint16_t image_index)
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
    s_reading_detail_page_count = (uint16_t)(next_index + 1U);
    s_reading_detail_has_last_page = true;
    rt_hw_interrupt_enable(level);
    return true;
}

static uint32_t ui_reading_detail_measure_page_range(uint32_t start,
                                                     uint32_t end_limit,
                                                     char *formatted_buffer,
                                                     size_t formatted_buffer_size)
{
    uint32_t end = start;
    uint32_t current_index = start;
    uint32_t previous_index = start;
    uint32_t char_start = start;
    uint32_t letter;
    uint32_t letter_next;
    int32_t line_width = 0;
    uint16_t max_lines;
    uint16_t lines_used = 1U;
    int32_t max_width;
    int32_t letter_space;
    uint16_t fallback_width;
    size_t written = 0U;

    if (formatted_buffer != NULL && formatted_buffer_size > 0U)
    {
        formatted_buffer[0] = '\0';
    }

    max_lines = ui_reading_detail_get_max_lines();
    max_width = ui_px_w(UI_READING_DETAIL_TEXT_WIDTH);
    letter_space = 0;
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
        char_width = (int32_t)ui_reading_detail_get_glyph_width_fast(NULL,
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

static bool ui_reading_detail_paginate_text_range(uint32_t start, uint32_t end)
{
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
    if (s_reading_detail_load_state != UI_READING_DETAIL_LOAD_READY || ui_reading_detail_get_text_length() == 0U)
    {
        return;
    }

    s_reading_detail_stb_ready = false;
    s_reading_detail_width_cache_size = 0U;
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
        uint16_t i;

        for (i = 0; i < s_reading_detail_epub_block_count; ++i)
        {
            if (s_reading_detail_epub_blocks[i].type == READING_EPUB_BLOCK_TEXT)
            {
                if (!ui_reading_detail_paginate_text_range(s_reading_detail_epub_blocks[i].text_start,
                                                           s_reading_detail_epub_blocks[i].text_end))
                {
                    return;
                }
            }
            else if (!ui_reading_detail_append_page_entry(UI_READING_DETAIL_PAGE_IMAGE,
                                                          0U,
                                                          0U,
                                                          s_reading_detail_epub_blocks[i].image_index))
            {
                return;
            }
        }
    }
    else if (!ui_reading_detail_paginate_text_range(0U, ui_reading_detail_get_text_length()))
    {
        return;
    }

    s_reading_detail_first_page_layout_ready = true;
    s_reading_detail_current_page = 0U;

    (void)ui_reading_detail_render_page();
}

static bool ui_reading_detail_load_text_from_path(const char *file_path)
{
    uint32_t open_start_ms;
    uint32_t open_elapsed_ms;
    uint32_t paginate_start_ms;
    uint32_t paginate_elapsed_ms;
    bool using_memory_fallback = false;

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
        return false;
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
        return false;
    }
    open_elapsed_ms = ui_reading_detail_now_ms() - open_start_ms;

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

    rt_kprintf("reading_detail: file=%s bytes=%lu open_ms=%lu paginate_ms=%lu total_ms=%lu\n",
               file_path,
               (unsigned long)ui_reading_detail_get_text_length(),
               (unsigned long)open_elapsed_ms,
               (unsigned long)paginate_elapsed_ms,
               (unsigned long)(ui_reading_detail_now_ms() - open_start_ms));

    return true;
}

static bool ui_reading_detail_load_epub_chapter(uint16_t chapter_index)
{
    char error_text[128];
    uint16_t block_count = 0U;
    uint16_t image_count = 0U;
    uint16_t i;

    if (!s_reading_detail_epub_lazy_mode ||
        chapter_index >= s_reading_detail_epub_spine_count)
    {
        return false;
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
        return false;
    }

    ui_reading_detail_use_memory_text();

    memset(s_reading_detail_pages, 0, sizeof(s_reading_detail_pages));
    s_reading_detail_page_count = 0U;
    s_reading_detail_has_last_page = false;
    s_reading_detail_epub_block_count = block_count;
    s_reading_detail_epub_image_count = image_count;

    for (i = 0; i < block_count; ++i)
    {
        if (s_reading_detail_epub_blocks[i].type == READING_EPUB_BLOCK_TEXT)
        {
            if (!ui_reading_detail_paginate_text_range(s_reading_detail_epub_blocks[i].text_start,
                                                       s_reading_detail_epub_blocks[i].text_end))
            {
                break;
            }
        }
        else if (!ui_reading_detail_append_page_entry(UI_READING_DETAIL_PAGE_IMAGE,
                                                      0U,
                                                      0U,
                                                      s_reading_detail_epub_blocks[i].image_index))
        {
            break;
        }
    }

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
    return true;
}

static bool ui_reading_detail_load_epub_from_path(const char *file_path)
{
    char error_text[128];
    uint16_t spine_count = 0U;

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
        return false;
    }

    s_reading_detail_epub_lazy_mode = true;
    s_reading_detail_epub_spine_count = spine_count;
    s_reading_detail_epub_current_chapter = 0U;

    if (!ui_reading_detail_load_epub_chapter(0U))
    {
        ui_reading_detail_reset_epub_lazy_state();
        return false;
    }

    rt_kprintf("reading_detail: epub indexed chapters=%u current=%u pages=%u\n",
               (unsigned int)s_reading_detail_epub_spine_count,
               (unsigned int)(s_reading_detail_epub_current_chapter + 1U),
               (unsigned int)s_reading_detail_page_count);
    return true;
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

        rt_kprintf("reading_detail: request=%lu pages=%u last=%u total_ms=%lu\n",
                   (unsigned long)request_id,
                   (unsigned int)page_count,
                   has_last_page ? 1U : 0U,
                   (unsigned long)(ui_reading_detail_now_ms() - load_start_ms));
    }
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
    bool has_last_page = false;
    uint16_t page_count;
    uint32_t render_start_ms;
    const ui_reading_detail_page_entry_t *page;
    lv_coord_t image_max_width;

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
    page = &s_reading_detail_pages[s_reading_detail_current_page];
    image_max_width = s_reading_detail_refs.reading_box != NULL ?
                          lv_obj_get_content_width(s_reading_detail_refs.reading_box) :
                          ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH);
    lcd_set_epd_mono_dither_enabled(RT_FALSE);

    if (page->type == UI_READING_DETAIL_PAGE_IMAGE)
    {
        reading_epub_release_image(&s_reading_detail_current_image_dsc);
        if (!reading_epub_decode_image(s_reading_detail_request_path,
                                       s_reading_detail_epub_images[page->image_index].internal_path,
                                       (uint16_t)image_max_width,
                                       (uint16_t)UI_READING_DETAIL_IMAGE_DECODE_HEIGHT,
                                       &s_reading_detail_current_image_dsc))
        {
            ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                             ui_i18n_pick("图片加载失败。", "Failed to load image."));
            lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_image_set_src(s_reading_detail_refs.content_image, &s_reading_detail_current_image_dsc);
            lv_obj_set_size(s_reading_detail_refs.content_image,
                            s_reading_detail_current_image_dsc.header.w,
                            s_reading_detail_current_image_dsc.header.h);
            lv_obj_set_pos(s_reading_detail_refs.content_image,
                           (image_max_width - s_reading_detail_current_image_dsc.header.w) / 2,
                           8);
            lv_obj_clear_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else
    {
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

        ui_reading_detail_set_label_text(s_reading_detail_refs.content_label,
                                         s_reading_detail_page_buffer);
        lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_invalidate(s_reading_detail_refs.content_label);
    lv_obj_invalidate(s_reading_detail_refs.content_image);
    if (s_reading_detail_refs.reading_box != NULL)
    {
        lv_obj_invalidate(s_reading_detail_refs.reading_box);
    }
    ui_reading_detail_refresh_page_label();
    ui_reading_detail_refresh_nav_buttons();
    rt_kprintf("reading_detail: render page=%u ready_pages=%u kind=%u render_ms=%lu\n",
               (unsigned int)(s_reading_detail_current_page + 1U),
               (unsigned int)page_count,
               (unsigned int)page->type,
               (unsigned long)(ui_reading_detail_now_ms() - render_start_ms));
    return true;
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

    if (s_reading_detail_current_page == 0U)
    {
        if (s_reading_detail_epub_lazy_mode && s_reading_detail_epub_current_chapter > 0U)
        {
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

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    if ((uint16_t)(s_reading_detail_current_page + 1U) < page_count)
    {
        ++s_reading_detail_current_page;
        (void)ui_reading_detail_render_page();
        return;
    }

    if (s_reading_detail_epub_lazy_mode &&
        (uint16_t)(s_reading_detail_epub_current_chapter + 1U) < s_reading_detail_epub_spine_count)
    {
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
        s_reading_detail_current_page = 0U;
        (void)ui_reading_detail_render_page();
    }
}

static void ui_reading_detail_prev_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_reading_detail_prev_page();
    }
}

static void ui_reading_detail_next_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        ui_reading_detail_next_page();
    }
}

static void ui_reading_detail_content_event_cb(lv_event_t *e)
{
    lv_point_t point;
    lv_obj_t *target;
    lv_indev_t *indev;

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

    target = lv_event_get_target(e);
    lv_indev_get_point(indev, &point);
    if (point.x < (lv_obj_get_x(target) + lv_obj_get_width(target) / 2))
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
    ui_reading_detail_refresh_settings_panel();
    ui_reading_detail_rebuild_layout();
}

void ui_Reading_Detail_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *reading_box;
    lv_obj_t *row;
    lv_obj_t *button;
    const char *title = ui_i18n_pick("在线阅读", "Reading");

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

    ui_Reading_Detail = ui_create_screen_base();
    rt_kprintf("reading_detail: init title=%s\n", title != NULL ? title : "<null>");
    ui_build_standard_screen(&page, ui_Reading_Detail, title, UI_SCREEN_READING_LIST);

    reading_box = ui_create_card(page.content, 0, 12, UI_READING_DETAIL_IMAGE_WIDTH, 572, UI_SCREEN_NONE, false, 0);
    s_reading_detail_refs.reading_box = reading_box;
    lv_obj_set_style_border_width(reading_box, 0, 0);
    lv_obj_set_style_bg_opa(reading_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(reading_box, 0, 0);
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
                                     ui_px_y(UI_READING_DETAIL_TEXT_LINE_SPACE),
                                     0);
    s_reading_detail_refs.content_image = lv_image_create(reading_box);
    lv_obj_set_pos(s_reading_detail_refs.content_image, 0, 8);
    lv_obj_set_size(s_reading_detail_refs.content_image,
                    ui_px_w(UI_READING_DETAIL_IMAGE_WIDTH),
                    ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT));
    lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(reading_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(reading_box, ui_reading_detail_content_event_cb, LV_EVENT_CLICKED, NULL);
    s_reading_detail_refs.page_label = ui_create_label(page.content,
                                                       "1 / 1",
                                                       24,
                                                       611,
                                                       120,
                                                       17,
                                                       15,
                                                       LV_TEXT_ALIGN_LEFT,
                                                       false,
                                                       false);
    s_reading_detail_refs.prev_button = ui_create_button(page.content, 198, 603, 175, 40, ui_i18n_pick("上一页", "Previous"), 20, UI_SCREEN_NONE, false);
    s_reading_detail_refs.next_button = ui_create_button(page.content, 329, 603, 175, 40, ui_i18n_pick("下一页", "Next"), 20, UI_SCREEN_NONE, true);
    lv_obj_add_flag(s_reading_detail_refs.prev_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(s_reading_detail_refs.next_button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_reading_detail_refs.prev_button, ui_reading_detail_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_reading_detail_refs.next_button, ui_reading_detail_next_event_cb, LV_EVENT_CLICKED, NULL);

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
                    18,
                    LV_TEXT_ALIGN_LEFT,
                    false,
                    false);

    row = ui_create_card(s_reading_detail_refs.settings_panel, 24, 52, 480, 42, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    ui_create_label(row, ui_i18n_pick("字号", "Font"), 0, 8, 80, 24, 18, LV_TEXT_ALIGN_LEFT, false, false);
    button = ui_create_button(row, 256, 0, 56, 42, "-", 22, UI_SCREEN_NONE, false);
    lv_obj_add_event_cb(button, ui_reading_detail_adjust_font_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(-UI_READING_DETAIL_TEXT_FONT_STEP));
    s_reading_detail_refs.font_value_label = ui_create_label(row,
                                                             "20",
                                                             324,
                                                             8,
                                                             UI_READING_DETAIL_SETTINGS_VALUE_WIDTH,
                                                             24,
                                                             18,
                                                             LV_TEXT_ALIGN_CENTER,
                                                             false,
                                                             false);
    button = ui_create_button(row, 404, 0, 56, 42, "+", 22, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(button, ui_reading_detail_adjust_font_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)UI_READING_DETAIL_TEXT_FONT_STEP);

    row = ui_create_card(s_reading_detail_refs.settings_panel, 24, 102, 480, 42, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    ui_create_label(row, ui_i18n_pick("行距", "Spacing"), 0, 8, 80, 24, 18, LV_TEXT_ALIGN_LEFT, false, false);
    button = ui_create_button(row, 256, 0, 56, 42, "-", 22, UI_SCREEN_NONE, false);
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
                                                                   18,
                                                                   LV_TEXT_ALIGN_CENTER,
                                                                   false,
                                                                   false);
    button = ui_create_button(row, 404, 0, 56, 42, "+", 22, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(button,
                        ui_reading_detail_adjust_line_space_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_READING_DETAIL_TEXT_LINE_SPACE_STEP);
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

    if (ui_Reading_Detail != NULL)
    {
        lv_obj_delete(ui_Reading_Detail);
        ui_Reading_Detail = NULL;
    }

    if (s_reading_detail_bitmap_buffer != NULL)
    {
        audio_mem_free(s_reading_detail_bitmap_buffer);
        s_reading_detail_bitmap_buffer = NULL;
    }
    reading_epub_release_image(&s_reading_detail_current_image_dsc);
    memset(&s_reading_detail_bitmap_dsc, 0, sizeof(s_reading_detail_bitmap_dsc));
    s_reading_detail_bitmap_inited = false;

    memset(&s_reading_detail_refs, 0, sizeof(s_reading_detail_refs));
    memset(&s_reading_detail_swipe_state, 0, sizeof(s_reading_detail_swipe_state));
    s_reading_detail_current_page = 0U;
    s_reading_detail_font_size = UI_READING_DETAIL_TEXT_FONT;
    s_reading_detail_line_space = UI_READING_DETAIL_TEXT_LINE_SPACE;
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    s_reading_detail_request_path[0] = '\0';
    ui_reading_detail_reset_epub_lazy_state();
    ++s_reading_detail_request_id;
    s_reading_detail_load_state = UI_READING_DETAIL_LOAD_IDLE;
    s_reading_detail_completed_request_id = 0U;
}
