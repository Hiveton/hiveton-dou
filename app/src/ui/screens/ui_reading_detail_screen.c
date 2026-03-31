#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dfs_posix.h"
#include "rtthread.h"
#include "rthw.h"
#include "mem_section.h"
#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"
#define STBTT_malloc(x, u) ((void)(u), lv_malloc(x))
#define STBTT_free(x, u) ((void)(u), lv_free(x))
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../../sdk/external/lvgl_v9/src/libs/tiny_ttf/stb_truetype_htcw.h"

#define UI_READING_DETAIL_MAX_FILE_BYTES (64 * 1024)
#define UI_READING_DETAIL_MAX_PAGE_COUNT 768U
#define UI_READING_DETAIL_PAGE_BUFFER_BYTES 12288U
#define UI_READING_DETAIL_AUTO_PAGE_MS 5000U
#define UI_READING_DETAIL_LOAD_THREAD_STACK_SIZE 16384
#define UI_READING_DETAIL_LOAD_THREAD_PRIORITY 22
#define UI_READING_DETAIL_LOAD_THREAD_TICK 10
#define UI_READING_DETAIL_LOAD_TIMER_MS 30U
#define UI_READING_DETAIL_PROGRESS_TIMER_MS 200U
#define UI_READING_DETAIL_TEXT_WIDTH 514
#define UI_READING_DETAIL_TEXT_HEIGHT 558
#define UI_READING_DETAIL_TEXT_FONT 20
#define UI_READING_DETAIL_TEXT_LINE_SPACE 2
#define UI_READING_DETAIL_FONT_DELTA 0
#define UI_READING_DETAIL_BITMAP_GLYPH_SCRATCH_BYTES 4096U

typedef struct
{
    lv_obj_t *content_label;
    lv_obj_t *content_image;
    lv_obj_t *page_label;
    lv_obj_t *prev_button;
    lv_obj_t *next_button;
} ui_reading_detail_refs_t;

typedef enum
{
    UI_READING_DETAIL_LOAD_IDLE = 0,
    UI_READING_DETAIL_LOAD_LOADING,
    UI_READING_DETAIL_LOAD_READY,
} ui_reading_detail_load_state_t;

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
static char s_reading_detail_text[UI_READING_DETAIL_MAX_FILE_BYTES + 1U];
static char s_reading_detail_first_page_layout[UI_READING_DETAIL_PAGE_BUFFER_BYTES];
static char s_reading_detail_page_buffer[UI_READING_DETAIL_PAGE_BUFFER_BYTES];
static char s_reading_detail_page_text[32];
static uint32_t s_reading_detail_page_offsets[UI_READING_DETAIL_MAX_PAGE_COUNT + 1U];
static volatile uint16_t s_reading_detail_page_count = 0U;
static uint16_t s_reading_detail_current_page = 0U;
static lv_timer_t *s_reading_detail_auto_timer = NULL;
static lv_timer_t *s_reading_detail_load_timer = NULL;
static volatile bool s_reading_detail_has_last_page = false;
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
static const lv_font_t *s_reading_detail_width_cache_font = NULL;
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
static bool ui_reading_detail_render_page(void);
static bool ui_reading_detail_selected_matches_request(void);
static bool ui_reading_detail_render_text_bitmap(const char *formatted_text);
static uint16_t ui_reading_detail_get_glyph_width_fast(const lv_font_t *font,
                                                       uint32_t codepoint,
                                                       uint32_t codepoint_next,
                                                       uint16_t fallback_width);
static const lv_font_t *ui_reading_detail_get_text_font(void);

static const lv_font_t *ui_reading_detail_get_text_font(void)
{
    return ui_font_get(UI_READING_DETAIL_TEXT_FONT);
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

    font_px = (int)ui_scaled_font_size(UI_READING_DETAIL_TEXT_FONT) + UI_READING_DETAIL_FONT_DELTA;
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
    s_reading_detail_bitmap_buffer = (lv_color_t *)rt_malloc(data_size);
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
    memset(s_reading_detail_page_offsets, 0, sizeof(s_reading_detail_page_offsets));
    s_reading_detail_page_offsets[0] = 0U;
    s_reading_detail_page_count = 0U;
    s_reading_detail_has_last_page = false;
    rt_hw_interrupt_enable(level);

    s_reading_detail_current_page = 0U;
}

static void ui_reading_detail_show_loading_state(void)
{
    rt_snprintf(s_reading_detail_page_buffer,
                sizeof(s_reading_detail_page_buffer),
                "正在读取文本内容...\n\n请稍候。");
    rt_snprintf(s_reading_detail_page_text, sizeof(s_reading_detail_page_text), "-- / --");

    if (s_reading_detail_refs.content_label != NULL)
    {
        lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_reading_detail_refs.content_label, s_reading_detail_page_buffer);
    }

    if (s_reading_detail_refs.content_image != NULL)
    {
        lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_reading_detail_refs.page_label != NULL)
    {
        lv_label_set_text(s_reading_detail_refs.page_label, s_reading_detail_page_text);
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
    const lv_font_t *font;
    uint16_t max_lines;

    max_height = ui_px_h(UI_READING_DETAIL_TEXT_HEIGHT);
    font = ui_reading_detail_get_text_font();
    line_space = ui_px_y(UI_READING_DETAIL_TEXT_LINE_SPACE);
    line_height = lv_font_get_line_height(font) + line_space;
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
            uint8_t gray;

            if (px < 0 || px >= max_w)
            {
                continue;
            }

            coverage = glyph_bitmap[gy * glyph_w + gx];
            if (coverage == 0U)
            {
                continue;
            }

            gray = (uint8_t)(255U - coverage);
            s_reading_detail_bitmap_buffer[py * max_w + px] = lv_color_make(gray, gray, gray);
        }
    }
}

static bool ui_reading_detail_render_text_bitmap(const char *formatted_text)
{
    const lv_font_t *font;
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
    font = ui_font_get(UI_READING_DETAIL_TEXT_FONT);
    line_step = lv_font_get_line_height(font) + ui_px_y(UI_READING_DETAIL_TEXT_LINE_SPACE);
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

        advance_px = (int)ui_reading_detail_get_glyph_width_fast(font,
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
    uint16_t width;

    if (font == NULL)
    {
        return fallback_width;
    }

    if (s_reading_detail_width_cache_font != font)
    {
        memset(s_reading_detail_ascii_width_cache, 0, sizeof(s_reading_detail_ascii_width_cache));
        s_reading_detail_cjk_width = 0U;
        s_reading_detail_fullwidth_width = 0U;
        s_reading_detail_width_cache_font = font;
    }

    if (codepoint < 128U)
    {
        width = s_reading_detail_ascii_width_cache[codepoint];
        if (width == 0U)
        {
            width = (uint16_t)lv_font_get_glyph_width(font, codepoint, next_codepoint);
            if (width == 0U)
            {
                width = fallback_width;
            }
            s_reading_detail_ascii_width_cache[codepoint] = width;
        }
        return width;
    }

    if (ui_reading_detail_is_cjk_codepoint(codepoint))
    {
        if (s_reading_detail_cjk_width == 0U)
        {
            s_reading_detail_cjk_width = (uint16_t)lv_font_get_glyph_width(font, 0x4E2DU, 0U);
            if (s_reading_detail_cjk_width == 0U)
            {
                s_reading_detail_cjk_width = fallback_width;
            }
        }
        return s_reading_detail_cjk_width;
    }

    if (ui_reading_detail_is_fullwidth_codepoint(codepoint))
    {
        if (s_reading_detail_fullwidth_width == 0U)
        {
            s_reading_detail_fullwidth_width = (uint16_t)lv_font_get_glyph_width(font, 0x3002U, 0U);
            if (s_reading_detail_fullwidth_width == 0U)
            {
                s_reading_detail_fullwidth_width = fallback_width;
            }
        }
        return s_reading_detail_fullwidth_width;
    }

    width = (uint16_t)lv_font_get_glyph_width(font, codepoint, next_codepoint);
    if (width == 0U)
    {
        width = fallback_width;
    }

    return width;
}

static uint32_t ui_reading_detail_measure_page(uint32_t start,
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
    const lv_font_t *font;
    uint16_t fallback_width;
    size_t written = 0U;

    if (formatted_buffer != NULL && formatted_buffer_size > 0U)
    {
        formatted_buffer[0] = '\0';
    }

    max_lines = ui_reading_detail_get_max_lines();
    max_width = ui_px_w(UI_READING_DETAIL_TEXT_WIDTH);
    font = ui_reading_detail_get_text_font();
    letter_space = 0;
    fallback_width = (uint16_t)(lv_font_get_line_height(font) / 2);
    if (fallback_width == 0U)
    {
        fallback_width = 1U;
    }

    while (s_reading_detail_text[current_index] != '\0')
    {
        int32_t char_width;
        size_t char_len;

        previous_index = current_index;
        char_start = current_index;
        letter = ui_reading_detail_utf8_next(s_reading_detail_text, &current_index);
        if (letter == 0U)
        {
            current_index = previous_index + 1U;
            end = current_index;
            if (formatted_buffer != NULL && formatted_buffer_size > 1U && written + 1U < formatted_buffer_size)
            {
                formatted_buffer[written++] = s_reading_detail_text[previous_index];
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

        letter_next = s_reading_detail_text[current_index] != '\0' ?
                          ui_reading_detail_utf8_next(&s_reading_detail_text[current_index], NULL) :
                          0U;
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
            memcpy(&formatted_buffer[written], &s_reading_detail_text[char_start], char_len);
            written += char_len;
            formatted_buffer[written] = '\0';
        }
    }

    if (end <= start && s_reading_detail_text[start] != '\0')
    {
        previous_index = start;
        (void)ui_reading_detail_utf8_next(s_reading_detail_text, &previous_index);
        end = previous_index;
        if (formatted_buffer != NULL && formatted_buffer_size > 1U)
        {
            size_t char_len = (size_t)(previous_index - start);
            if (char_len >= formatted_buffer_size)
            {
                char_len = formatted_buffer_size - 1U;
            }
            memcpy(formatted_buffer, &s_reading_detail_text[start], char_len);
            formatted_buffer[char_len] = '\0';
        }
    }

    return end;
}

static bool ui_reading_detail_append_next_page(char *formatted_buffer,
                                               size_t formatted_buffer_size,
                                               uint16_t *page_count_out,
                                               bool *has_last_page_out)
{
    uint32_t start;
    uint32_t end;
    uint16_t local_page_count;
    bool has_last_page;
    bool is_last_page;
    rt_base_t level;

    local_page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    if (has_last_page || local_page_count >= UI_READING_DETAIL_MAX_PAGE_COUNT)
    {
        if (page_count_out != NULL)
        {
            *page_count_out = local_page_count;
        }
        if (has_last_page_out != NULL)
        {
            *has_last_page_out = has_last_page;
        }
        return false;
    }

    start = s_reading_detail_page_offsets[local_page_count];
    if (s_reading_detail_text[start] == '\0')
    {
        level = rt_hw_interrupt_disable();
        s_reading_detail_has_last_page = true;
        rt_hw_interrupt_enable(level);
        if (page_count_out != NULL)
        {
            *page_count_out = local_page_count;
        }
        if (has_last_page_out != NULL)
        {
            *has_last_page_out = true;
        }
        return false;
    }

    end = ui_reading_detail_measure_page(start, formatted_buffer, formatted_buffer_size);
    ++local_page_count;
    is_last_page = s_reading_detail_text[end] == '\0' ||
                   local_page_count >= UI_READING_DETAIL_MAX_PAGE_COUNT;

    level = rt_hw_interrupt_disable();
    s_reading_detail_page_offsets[local_page_count] = end;
    s_reading_detail_page_count = local_page_count;
    if (is_last_page)
    {
        s_reading_detail_has_last_page = true;
    }
    rt_hw_interrupt_enable(level);

    if (page_count_out != NULL)
    {
        *page_count_out = local_page_count;
    }
    if (has_last_page_out != NULL)
    {
        *has_last_page_out = is_last_page;
    }

    return true;
}

static bool ui_reading_detail_ensure_page_available(uint16_t page_index)
{
    bool has_last_page = false;
    uint16_t page_count = ui_reading_detail_snapshot_page_count(&has_last_page);

    while (page_count <= page_index && !has_last_page)
    {
        uint32_t page_start_ms = ui_reading_detail_now_ms();

        if (!ui_reading_detail_append_next_page(NULL, 0U, &page_count, &has_last_page))
        {
            break;
        }

        rt_kprintf("reading_detail: lazy page_ready=%u known_last=%u page_ms=%lu\n",
                   (unsigned int)page_count,
                   has_last_page ? 1U : 0U,
                   (unsigned long)(ui_reading_detail_now_ms() - page_start_ms));
    }

    return page_index < page_count;
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
                        "TF 卡中还没有可阅读的文本文件。\n\n请先返回列表页确认文件是否已经识别。");
            rt_kprintf("reading_detail: request=%lu fallback without selected file\n",
                       (unsigned long)request_id);
        }
        else
        {
            (void)ui_reading_detail_load_text_from_path(file_path);
        }

        if (!ui_reading_detail_request_is_current(request_id))
        {
            rt_kprintf("reading_detail: request=%lu canceled after load\n",
                       (unsigned long)request_id);
            continue;
        }

        {
            bool has_last_page = false;
            uint16_t page_count = 0U;
            uint32_t page_start_ms = ui_reading_detail_now_ms();
            rt_base_t level;

            if (!ui_reading_detail_append_next_page(s_reading_detail_first_page_layout,
                                                    sizeof(s_reading_detail_first_page_layout),
                                                    &page_count,
                                                    &has_last_page))
            {
                rt_kprintf("reading_detail: request=%lu first page build failed\n",
                           (unsigned long)request_id);
                continue;
            }

            level = rt_hw_interrupt_disable();
            s_reading_detail_first_page_layout_ready = true;
            s_reading_detail_load_state = UI_READING_DETAIL_LOAD_READY;
            s_reading_detail_completed_request_id = request_id;
            rt_hw_interrupt_enable(level);

            rt_kprintf("reading_detail: request=%lu first_page_ready pages=%u last=%u page_ms=%lu total_ms=%lu\n",
                       (unsigned long)request_id,
                       (unsigned int)page_count,
                       has_last_page ? 1U : 0U,
                       (unsigned long)(ui_reading_detail_now_ms() - page_start_ms),
                       (unsigned long)(ui_reading_detail_now_ms() - load_start_ms));
            rt_kprintf("reading_detail: request=%lu first_page_text_len=%lu first_page_start=%lu first_page_end=%lu\n",
                       (unsigned long)request_id,
                       (unsigned long)strlen(s_reading_detail_first_page_layout),
                       (unsigned long)s_reading_detail_page_offsets[0],
                       (unsigned long)s_reading_detail_page_offsets[1]);
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

    ui_reading_detail_set_button_enabled(s_reading_detail_refs.prev_button,
                                         s_reading_detail_current_page > 0U);
    ui_reading_detail_set_button_enabled(s_reading_detail_refs.next_button,
                                         !has_last_page ||
                                             (uint16_t)(s_reading_detail_current_page + 1U) < page_count);
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

    lv_label_set_text(s_reading_detail_refs.page_label, s_reading_detail_page_text);
}

static bool ui_reading_detail_load_text_from_path(const char *file_path)
{
    int fd;
    ssize_t read_size;
    size_t total = 0U;
    size_t src = 0U;
    size_t dst = 0U;
    uint32_t open_start_ms;
    uint32_t read_start_ms;
    uint32_t normalize_start_ms;
    uint32_t read_elapsed_ms;
    uint32_t normalize_elapsed_ms;

    memset(s_reading_detail_text, 0, sizeof(s_reading_detail_text));

    if (file_path == NULL || file_path[0] == '\0')
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "TF 卡中还没有可阅读的文本文件。\n\n请先返回列表页确认文件是否已经识别。");
        return false;
    }

    open_start_ms = ui_reading_detail_now_ms();
    fd = open(file_path, O_RDONLY);
    if (fd < 0)
    {
        rt_kprintf("reading_detail: open failed path=%s errno=%d\n", file_path, rt_get_errno());
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "打开文件失败：\n%s\n\n请确认文件存在且 TF 卡可正常读取。",
                    file_path);
        return false;
    }

    read_start_ms = ui_reading_detail_now_ms();
    while (total < UI_READING_DETAIL_MAX_FILE_BYTES)
    {
        read_size = read(fd,
                         &s_reading_detail_text[total],
                         UI_READING_DETAIL_MAX_FILE_BYTES - total);
        if (read_size <= 0)
        {
            break;
        }
        total += (size_t)read_size;
    }
    close(fd);
    read_elapsed_ms = ui_reading_detail_now_ms() - read_start_ms;

    normalize_start_ms = ui_reading_detail_now_ms();
    if (total >= 3U &&
        (unsigned char)s_reading_detail_text[0] == 0xEFU &&
        (unsigned char)s_reading_detail_text[1] == 0xBBU &&
        (unsigned char)s_reading_detail_text[2] == 0xBFU)
    {
        src = 3U;
    }

    while (src < total)
    {
        char ch = s_reading_detail_text[src++];

        if (ch == '\r')
        {
            continue;
        }

        s_reading_detail_text[dst++] = ch;
    }
    s_reading_detail_text[dst] = '\0';
    normalize_elapsed_ms = ui_reading_detail_now_ms() - normalize_start_ms;

    if (dst == 0U)
    {
        rt_snprintf(s_reading_detail_text,
                    sizeof(s_reading_detail_text),
                    "这个文本文件目前是空的。");
    }

    rt_kprintf("reading_detail: file=%s bytes=%lu open_ms=%lu read_ms=%lu normalize_ms=%lu total_ms=%lu\n",
               file_path,
               (unsigned long)dst,
               (unsigned long)(read_start_ms - open_start_ms),
               (unsigned long)read_elapsed_ms,
               (unsigned long)normalize_elapsed_ms,
               (unsigned long)(ui_reading_detail_now_ms() - open_start_ms));

    return true;
}

static void ui_reading_detail_request_async_load(void)
{
    char file_path[sizeof(s_reading_detail_request_path)];

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

bool ui_reading_detail_prepare_selected_async(void)
{
    ui_reading_detail_start_load_worker();
    ui_reading_detail_request_async_load();
    return s_reading_detail_request_path[0] != '\0' || s_reading_detail_load_state == UI_READING_DETAIL_LOAD_LOADING;
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
    uint32_t start;
    uint32_t end;
    uint32_t page_len;
    rt_base_t level;
    bool has_last_page = false;
    uint16_t page_count;
    uint32_t render_start_ms;
    const char *page_text;
    size_t page_text_len;

    if (s_reading_detail_refs.content_label == NULL || s_reading_detail_refs.content_image == NULL)
    {
        return false;
    }

    if (!ui_reading_detail_is_content_ready())
    {
        return false;
    }

    if (!ui_reading_detail_ensure_page_available(s_reading_detail_current_page))
    {
        return false;
    }

    page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    if (page_count == 0U || s_reading_detail_current_page >= page_count)
    {
        return false;
    }

    level = rt_hw_interrupt_disable();
    start = s_reading_detail_page_offsets[s_reading_detail_current_page];
    end = s_reading_detail_page_offsets[s_reading_detail_current_page + 1U];
    rt_hw_interrupt_enable(level);

    if (end < start)
    {
        end = start;
    }

    page_len = end - start;
    render_start_ms = ui_reading_detail_now_ms();
    if (s_reading_detail_current_page == 0U && s_reading_detail_first_page_layout_ready)
    {
        page_text = s_reading_detail_first_page_layout;
    }
    else
    {
        (void)ui_reading_detail_measure_page(start,
                                             s_reading_detail_page_buffer,
                                             sizeof(s_reading_detail_page_buffer));
        page_text = s_reading_detail_page_buffer;
    }

    page_text_len = strlen(page_text);
    if (page_text_len == 0U)
    {
        page_text = "正文为空。";
        page_text_len = strlen(page_text);
    }

    lv_label_set_text(s_reading_detail_refs.content_label, page_text);
    lv_obj_clear_flag(s_reading_detail_refs.content_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_reading_detail_refs.content_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_reading_detail_refs.content_label);
    ui_reading_detail_refresh_page_label();
    ui_reading_detail_refresh_nav_buttons();
    rt_kprintf("reading_detail: render page=%u ready_pages=%u known=%u chars=%lu text_len=%lu render_ms=%lu\n",
               (unsigned int)(s_reading_detail_current_page + 1U),
               (unsigned int)page_count,
               has_last_page ? 1U : 0U,
               (unsigned long)page_len,
               (unsigned long)page_text_len,
               (unsigned long)(ui_reading_detail_now_ms() - render_start_ms));
    return true;
}

static void ui_reading_detail_auto_page_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (ui_Reading_Detail == NULL)
    {
        return;
    }

    ui_reading_detail_next_page();
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

        if (s_reading_detail_auto_timer == NULL)
        {
            s_reading_detail_auto_timer = lv_timer_create(ui_reading_detail_auto_page_timer_cb,
                                                          UI_READING_DETAIL_AUTO_PAGE_MS,
                                                          NULL);
        }
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
    if (!ui_reading_detail_is_content_ready())
    {
        return;
    }

    if (s_reading_detail_current_page == 0U)
    {
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
    if ((uint16_t)(s_reading_detail_current_page + 1U) >= page_count && !has_last_page)
    {
        (void)ui_reading_detail_ensure_page_available((uint16_t)(s_reading_detail_current_page + 1U));
        page_count = ui_reading_detail_snapshot_page_count(&has_last_page);
    }

    if ((uint16_t)(s_reading_detail_current_page + 1U) < page_count)
    {
        ++s_reading_detail_current_page;
        (void)ui_reading_detail_render_page();
        return;
    }

    if (has_last_page)
    {
        s_reading_detail_current_page = 0U;
        (void)ui_reading_detail_render_page();
    }
    else
    {
        rt_kprintf("reading_detail: next pending current=%u ready_pages=%u\n",
                   (unsigned int)(s_reading_detail_current_page + 1U),
                   (unsigned int)page_count);
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

void ui_Reading_Detail_screen_init(void)
{
    ui_screen_scaffold_t page;
    lv_obj_t *reading_box;
    const char *title;

    if (ui_Reading_Detail != NULL)
    {
        return;
    }

    ui_reading_detail_start_load_worker();
    memset(&s_reading_detail_refs, 0, sizeof(s_reading_detail_refs));
    memset(s_reading_detail_page_text, 0, sizeof(s_reading_detail_page_text));
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;

    ui_Reading_Detail = ui_create_screen_base();
    title = ui_reading_list_get_selected_name();
    rt_kprintf("reading_detail: init title=%s\n", title);
    ui_build_standard_screen(&page, ui_Reading_Detail, title, UI_SCREEN_READING_LIST);

    reading_box = ui_create_card(page.content, 24, 12, 534, 572, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_border_width(reading_box, 0, 0);
    lv_obj_set_style_bg_opa(reading_box, LV_OPA_TRANSP, 0);
    s_reading_detail_refs.content_label = ui_create_label(reading_box,
                                                          "",
                                                          10,
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
    lv_obj_set_pos(s_reading_detail_refs.content_image, 10, 8);
    lv_obj_set_size(s_reading_detail_refs.content_image,
                    ui_px_w(UI_READING_DETAIL_TEXT_WIDTH),
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
    s_reading_detail_refs.prev_button = ui_create_button(page.content, 198, 603, 175, 40, "上一页", 20, UI_SCREEN_NONE, false);
    s_reading_detail_refs.next_button = ui_create_button(page.content, 383, 603, 175, 40, "下一页", 20, UI_SCREEN_NONE, true);
    lv_obj_add_event_cb(s_reading_detail_refs.prev_button, ui_reading_detail_prev_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_reading_detail_refs.next_button, ui_reading_detail_next_event_cb, LV_EVENT_CLICKED, NULL);

    if (ui_reading_detail_is_selected_ready())
    {
        (void)ui_reading_detail_render_page();
        s_reading_detail_first_render_done = true;
        if (s_reading_detail_auto_timer == NULL)
        {
            s_reading_detail_auto_timer = lv_timer_create(ui_reading_detail_auto_page_timer_cb,
                                                          UI_READING_DETAIL_AUTO_PAGE_MS,
                                                          NULL);
        }
        return;
    }

    ui_reading_detail_show_loading_state();
    ui_reading_detail_request_async_load();

    if (s_reading_detail_load_timer == NULL)
    {
        s_reading_detail_load_timer = lv_timer_create(ui_reading_detail_load_timer_cb,
                                                      UI_READING_DETAIL_LOAD_TIMER_MS,
                                                      NULL);
    }
}

void ui_Reading_Detail_screen_destroy(void)
{
    if (s_reading_detail_load_timer != NULL)
    {
        lv_timer_delete(s_reading_detail_load_timer);
        s_reading_detail_load_timer = NULL;
    }

    if (s_reading_detail_auto_timer != NULL)
    {
        lv_timer_delete(s_reading_detail_auto_timer);
        s_reading_detail_auto_timer = NULL;
    }

    if (ui_Reading_Detail != NULL)
    {
        lv_obj_delete(ui_Reading_Detail);
        ui_Reading_Detail = NULL;
    }

    if (s_reading_detail_bitmap_buffer != NULL)
    {
        rt_free(s_reading_detail_bitmap_buffer);
        s_reading_detail_bitmap_buffer = NULL;
    }
    memset(&s_reading_detail_bitmap_dsc, 0, sizeof(s_reading_detail_bitmap_dsc));
    s_reading_detail_bitmap_inited = false;

    memset(&s_reading_detail_refs, 0, sizeof(s_reading_detail_refs));
    s_reading_detail_current_page = 0U;
    s_reading_detail_first_render_done = false;
    s_reading_detail_last_reported_page_count = 0U;
    s_reading_detail_last_reported_has_last_page = false;
    s_reading_detail_request_path[0] = '\0';
    ++s_reading_detail_request_id;
    s_reading_detail_load_state = UI_READING_DETAIL_LOAD_IDLE;
    s_reading_detail_completed_request_id = 0U;
}
