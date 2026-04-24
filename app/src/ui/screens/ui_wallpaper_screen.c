#include "ui.h"
#include "ui_helpers.h"
#include "ui_i18n.h"
#include "../ui_image_policy.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "../../config/app_config.h"
#include "audio_mem.h"
#include "drv_lcd.h"
#include "../../../../sdk/external/lvgl_v9/src/draw/lv_draw_buf.h"
#include "../../../../sdk/external/lvgl_v9/src/libs/lodepng/lodepng.h"
#include "../../../../sdk/external/lvgl_v9/src/libs/tjpgd/tjpgd.h"
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(sz) audio_mem_malloc((uint32_t)(sz))
#define STBI_REALLOC(p,sz) audio_mem_realloc((p), (uint32_t)(sz))
#define STBI_FREE(p) audio_mem_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "../../../../sdk/external/rlottie/src/vector/stb/stb_image.h"

lv_obj_t *ui_Wallpaper = NULL;

#define UI_WALLPAPER_MAX_WIDTH  528U
#define UI_WALLPAPER_MAX_HEIGHT 792U

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
    lv_obj_t *image_card;
    lv_obj_t *image;
    lv_obj_t *status_label;
    lv_timer_t *render_timer;
} ui_wallpaper_refs_t;

typedef struct
{
    int fd;
    off_t size;
} ui_wallpaper_stbi_file_t;

typedef struct
{
    int fd;
    off_t size;
    off_t offset;
} ui_wallpaper_jpeg_source_t;

typedef struct
{
    unsigned src_width;
    unsigned src_height;
    unsigned src_crop_x;
    unsigned src_crop_y;
    unsigned src_crop_width;
    unsigned src_crop_height;
    unsigned width;
    unsigned height;
    uint32_t stride;
    uint8_t *pixels;
} ui_wallpaper_jpeg_target_t;

typedef struct
{
    ui_wallpaper_jpeg_source_t source;
    ui_wallpaper_jpeg_target_t target;
} ui_wallpaper_jpeg_context_t;

static ui_wallpaper_refs_t s_wallpaper_refs;
static char s_wallpaper_image_path[256];
static char s_wallpaper_image_src[264];
static char s_wallpaper_last_error[192];
static char s_wallpaper_status_text[384];
static lv_image_dsc_t s_wallpaper_image_dsc;
static lv_draw_buf_t s_wallpaper_draw_buf;
static uint8_t *s_wallpaper_image_data;
static bool s_wallpaper_force_full_refresh_pending = false;

static void ui_wallpaper_queue_full_refresh_on_input(const char *source)
{
    if (s_wallpaper_image_dsc.data == NULL)
    {
        return;
    }

    if (!s_wallpaper_force_full_refresh_pending)
    {
        s_wallpaper_force_full_refresh_pending = true;
        UI_EPD_REFRESH_LOG("wallpaper: full refresh queued by input source=%s\n",
                           source != NULL ? source : "?");
    }
}

static bool ui_wallpaper_consume_pending_full_refresh(const char *reason)
{
    if (!s_wallpaper_force_full_refresh_pending)
    {
        return false;
    }

    UI_EPD_REFRESH_LOG("wallpaper: full refresh consume reason=%s\n",
                       reason != NULL ? reason : "?");
    lcd_request_epd_force_full_refresh_once();
    s_wallpaper_force_full_refresh_pending = false;
    return true;
}

static void ui_wallpaper_render(void);
static bool ui_wallpaper_find_next_image_path(const char *current_path,
                                              char *buffer,
                                              size_t buffer_size);

static void ui_wallpaper_input_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED)
    {
        return;
    }

    ui_wallpaper_queue_full_refresh_on_input(code == LV_EVENT_PRESSED ? "pressed" : "clicked");

    if (code == LV_EVENT_CLICKED)
    {
        char next_path[256];

        next_path[0] = '\0';
        if (ui_wallpaper_find_next_image_path(s_wallpaper_image_path,
                                              next_path,
                                              sizeof(next_path)))
        {
            (void)app_config_set_wallpaper_path(next_path);
            (void)app_config_save();
            ui_wallpaper_render();
        }
    }
}

static void ui_wallpaper_render(void);
static bool ui_wallpaper_find_next_image_path(const char *current_path,
                                              char *buffer,
                                              size_t buffer_size);

static const char *s_wallpaper_tf_devices[] = {
    "sd0",
    "sd1",
    "sd2",
    "sdio0",
};

static bool ui_wallpaper_is_image_file(const char *name)
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

    return (strcasecmp(ext, ".png") == 0) ||
           (strcasecmp(ext, ".jpg") == 0) ||
           (strcasecmp(ext, ".jpeg") == 0) ||
           (strcasecmp(ext, ".bmp") == 0);
}

static bool ui_wallpaper_build_pic_dir(char *buffer, size_t buffer_size)
{
    size_t i;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    buffer[0] = '\0';

    for (i = 0; i < sizeof(s_wallpaper_tf_devices) / sizeof(s_wallpaper_tf_devices[0]); ++i)
    {
        rt_device_t device = rt_device_find(s_wallpaper_tf_devices[i]);
        const char *mount_path;
        DIR *dir = NULL;

        if (device == RT_NULL)
        {
            continue;
        }

        mount_path = dfs_filesystem_get_mounted_path(device);
        if (mount_path == NULL || mount_path[0] == '\0')
        {
            continue;
        }

        if (strcmp(mount_path, "/") == 0)
        {
            rt_snprintf(buffer, buffer_size, "/pic");
        }
        else
        {
            rt_snprintf(buffer, buffer_size, "%s/pic", mount_path);
        }

        dir = opendir(buffer);
        if (dir != NULL)
        {
            closedir(dir);
            return true;
        }
    }

    buffer[0] = '\0';
    return false;
}

static bool ui_wallpaper_find_next_image_path(const char *current_path,
                                              char *buffer,
                                              size_t buffer_size)
{
    char pic_dir[192];
    char first_path[256];
    DIR *dir = NULL;
    struct dirent *entry;
    bool return_next;

    if (buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    buffer[0] = '\0';
    first_path[0] = '\0';
    return_next = (current_path == NULL || current_path[0] == '\0');

    if (!ui_wallpaper_build_pic_dir(pic_dir, sizeof(pic_dir)))
    {
        return false;
    }

    dir = opendir(pic_dir);
    if (dir == NULL)
    {
        return false;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char candidate[256];

        if (entry->d_name[0] == '.' || !ui_wallpaper_is_image_file(entry->d_name))
        {
            continue;
        }

        rt_snprintf(candidate, sizeof(candidate), "%s/%s", pic_dir, entry->d_name);
        if (first_path[0] == '\0')
        {
            rt_snprintf(first_path, sizeof(first_path), "%s", candidate);
        }

        if (return_next)
        {
            rt_snprintf(buffer, buffer_size, "%s", candidate);
            closedir(dir);
            return true;
        }

        if (current_path != NULL && strcmp(candidate, current_path) == 0)
        {
            return_next = true;
        }
    }

    closedir(dir);

    if (first_path[0] == '\0')
    {
        return false;
    }

    rt_snprintf(buffer, buffer_size, "%s", first_path);
    return true;
}

static void ui_wallpaper_set_decode_errorf(const char *fmt, ...)
{
    va_list ap;

    if (fmt == NULL)
    {
        s_wallpaper_last_error[0] = '\0';
        return;
    }

    va_start(ap, fmt);
    vsnprintf(s_wallpaper_last_error, sizeof(s_wallpaper_last_error), fmt, ap);
    va_end(ap);
}

static void ui_wallpaper_set_status_text(const char *text)
{
    bool has_text;

    if (text == NULL)
    {
        s_wallpaper_status_text[0] = '\0';
    }
    else
    {
        rt_snprintf(s_wallpaper_status_text, sizeof(s_wallpaper_status_text), "%s", text);
    }

    has_text = (s_wallpaper_status_text[0] != '\0');

    if (s_wallpaper_refs.status_label != NULL)
    {
        lv_label_set_text(s_wallpaper_refs.status_label, s_wallpaper_status_text);
        if (has_text)
        {
            lv_obj_clear_flag(s_wallpaper_refs.status_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_wallpaper_refs.status_label);
        }
        else
        {
            lv_obj_add_flag(s_wallpaper_refs.status_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_wallpaper_set_status_overview(const char *pic_dir,
                                             const char *image_path,
                                             const char *result,
                                             const char *detail)
{
    rt_snprintf(s_wallpaper_status_text,
                sizeof(s_wallpaper_status_text),
                "目录：%s\n文件：%s\n结果：%s\n说明：%s",
                (pic_dir != NULL && pic_dir[0] != '\0') ? pic_dir : "未找到",
                (image_path != NULL && image_path[0] != '\0') ? image_path : "未选择",
                (result != NULL && result[0] != '\0') ? result : "无",
                (detail != NULL && detail[0] != '\0') ? detail : "无");

    ui_wallpaper_set_status_text(s_wallpaper_status_text);
}

static uint32_t ui_wallpaper_i4_palette_bytes(void)
{
    return (uint32_t)(LV_COLOR_INDEXED_PALETTE_SIZE(LV_COLOR_FORMAT_I4) * sizeof(lv_color32_t));
}

static uint8_t *ui_wallpaper_alloc_i4_image(unsigned width,
                                            unsigned height,
                                            uint32_t *stride_out,
                                            uint8_t **pixel_data_out,
                                            size_t *total_size_out)
{
    uint32_t stride;
    uint32_t palette_bytes;
    size_t pixel_bytes;
    size_t total_bytes;
    uint8_t *image_data;

    if (width == 0U || height == 0U)
    {
        return NULL;
    }

    stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I4);
    palette_bytes = ui_wallpaper_i4_palette_bytes();
    pixel_bytes = (size_t)stride * (size_t)height;
    total_bytes = (size_t)palette_bytes + pixel_bytes;

    image_data = (uint8_t *)audio_mem_malloc((uint32_t)total_bytes);
    if (image_data == NULL)
    {
        ui_wallpaper_set_decode_errorf("申请内存失败 需要约%lu字节",
                                       (unsigned long)total_bytes);
        return NULL;
    }

    memset(image_data, 0, total_bytes);

    if (stride_out != NULL)
    {
        *stride_out = stride;
    }
    if (pixel_data_out != NULL)
    {
        *pixel_data_out = image_data + palette_bytes;
    }
    if (total_size_out != NULL)
    {
        *total_size_out = total_bytes;
    }

    return image_data;
}

static void ui_wallpaper_release_decoded_image(void)
{
    if (s_wallpaper_image_data != NULL)
    {
        audio_mem_free(s_wallpaper_image_data);
        s_wallpaper_image_data = NULL;
    }

    memset(&s_wallpaper_draw_buf, 0, sizeof(s_wallpaper_draw_buf));
    memset(&s_wallpaper_image_dsc, 0, sizeof(s_wallpaper_image_dsc));
}

static void ui_wallpaper_compute_target_size(unsigned src_width,
                                             unsigned src_height,
                                             uint16_t max_width,
                                             uint16_t max_height,
                                             unsigned *target_width,
                                             unsigned *target_height)
{
    uint64_t width_limit;
    uint64_t height_limit;
    unsigned width;
    unsigned height;

    if (src_width == 0U || src_height == 0U)
    {
        if (target_width != NULL) *target_width = 0U;
        if (target_height != NULL) *target_height = 0U;
        return;
    }

    if (max_width == 0U)
    {
        max_width = 1U;
    }
    if (max_height == 0U)
    {
        max_height = 1U;
    }

    width_limit = (uint64_t)max_width * (uint64_t)src_height;
    height_limit = (uint64_t)max_height * (uint64_t)src_width;

    if (width_limit <= height_limit)
    {
        width = max_width;
        height = (unsigned)(((uint64_t)src_height * (uint64_t)max_width) / (uint64_t)src_width);
    }
    else
    {
        height = max_height;
        width = (unsigned)(((uint64_t)src_width * (uint64_t)max_height) / (uint64_t)src_height);
    }

    if (width == 0U) width = 1U;
    if (height == 0U) height = 1U;

    if (target_width != NULL) *target_width = width;
    if (target_height != NULL) *target_height = height;
}

static void ui_wallpaper_compute_fill_crop(unsigned src_width,
                                           unsigned src_height,
                                           unsigned dst_width,
                                           unsigned dst_height,
                                           unsigned *crop_x,
                                           unsigned *crop_y,
                                           unsigned *crop_width,
                                           unsigned *crop_height)
{
    unsigned out_x = 0U;
    unsigned out_y = 0U;
    unsigned out_w = src_width;
    unsigned out_h = src_height;
    uint64_t lhs;
    uint64_t rhs;

    if (src_width == 0U || src_height == 0U || dst_width == 0U || dst_height == 0U)
    {
        if (crop_x != NULL) *crop_x = 0U;
        if (crop_y != NULL) *crop_y = 0U;
        if (crop_width != NULL) *crop_width = src_width;
        if (crop_height != NULL) *crop_height = src_height;
        return;
    }

    lhs = (uint64_t)src_width * (uint64_t)dst_height;
    rhs = (uint64_t)dst_width * (uint64_t)src_height;

    if (lhs > rhs)
    {
        out_w = (unsigned)(((uint64_t)src_height * (uint64_t)dst_width) / (uint64_t)dst_height);
        if (out_w == 0U || out_w > src_width)
        {
            out_w = src_width;
        }
        out_x = (src_width - out_w) / 2U;
    }
    else if (lhs < rhs)
    {
        out_h = (unsigned)(((uint64_t)src_width * (uint64_t)dst_height) / (uint64_t)dst_width);
        if (out_h == 0U || out_h > src_height)
        {
            out_h = src_height;
        }
        out_y = (src_height - out_h) / 2U;
    }

    if (crop_x != NULL) *crop_x = out_x;
    if (crop_y != NULL) *crop_y = out_y;
    if (crop_width != NULL) *crop_width = out_w;
    if (crop_height != NULL) *crop_height = out_h;
}

static uint8_t ui_wallpaper_dither_to_gray4_index(unsigned x,
                                                  unsigned y,
                                                  uint8_t r,
                                                  uint8_t g,
                                                  uint8_t b)
{
    uint8_t level;

    (void)x;
    (void)y;
    level = ui_image_gray4_level_from_rgb(r, g, b);
    return ui_image_gray4_level_to_i4_palette(level);
}

static bool ui_wallpaper_set_image_descriptor(unsigned width,
                                              unsigned height,
                                              uint8_t *image_data,
                                              lv_image_dsc_t *out_image)
{
    uint32_t stride;
    size_t total_bytes;

    if (width == 0U || height == 0U || image_data == NULL || out_image == NULL)
    {
        return false;
    }

    stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_I4);
    total_bytes = (size_t)ui_wallpaper_i4_palette_bytes() + ((size_t)stride * (size_t)height);

    if (lv_draw_buf_init(&s_wallpaper_draw_buf,
                         width,
                         height,
                         LV_COLOR_FORMAT_I4,
                         stride,
                         image_data,
                         total_bytes) != LV_RESULT_OK)
    {
        ui_wallpaper_set_decode_errorf("申请内存失败 需要约%lu字节",
                                       (unsigned long)total_bytes);
        audio_mem_free(image_data);
        return false;
    }

    {
        uint8_t i;
        for (i = 0; i < 16U; ++i)
        {
            uint8_t gray = (uint8_t)(255U - (i * 255U / 15U));
            lv_draw_buf_set_palette(&s_wallpaper_draw_buf, i, lv_color32_make(gray, gray, gray, 255));
        }
    }

    s_wallpaper_image_data = image_data;
    lv_draw_buf_to_image(&s_wallpaper_draw_buf, out_image);
    return true;
}

static bool ui_wallpaper_read_file(const char *path, uint8_t **data_out, size_t *size_out)
{
    int fd;
    off_t file_size;
    uint8_t *buffer;
    size_t total_read = 0U;

    if (path == NULL || data_out == NULL || size_out == NULL)
    {
        return false;
    }

    *data_out = NULL;
    *size_out = 0U;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        ui_wallpaper_set_decode_errorf("打开失败 errno=%d", rt_get_errno());
        return false;
    }

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0)
    {
        ui_wallpaper_set_decode_errorf("文件大小无效=%ld", (long)file_size);
        close(fd);
        return false;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        ui_wallpaper_set_decode_errorf("回到文件头失败 errno=%d", rt_get_errno());
        close(fd);
        return false;
    }

    buffer = (uint8_t *)audio_mem_malloc((uint32_t)file_size);
    if (buffer == NULL)
    {
        ui_wallpaper_set_decode_errorf("申请内存失败 size=%ld", (long)file_size);
        close(fd);
        return false;
    }

    while (total_read < (size_t)file_size)
    {
        ssize_t read_size = read(fd,
                                 buffer + total_read,
                                 (size_t)file_size - total_read);
        if (read_size <= 0)
        {
            ui_wallpaper_set_decode_errorf("读取失败 read=%ld errno=%d",
                                           (long)read_size,
                                           rt_get_errno());
            close(fd);
            audio_mem_free(buffer);
            return false;
        }
        total_read += (size_t)read_size;
    }

    close(fd);

    *data_out = buffer;
    *size_out = (size_t)file_size;
    return true;
}

static bool ui_wallpaper_decode_png(const uint8_t *image_data,
                                    size_t image_size,
                                    uint16_t max_width,
                                    uint16_t max_height,
                                    lv_image_dsc_t *out_image)
{
    unsigned char *rgb = NULL;
    unsigned src_width = 0U;
    unsigned src_height = 0U;
    unsigned target_width = 0U;
    unsigned target_height = 0U;
    unsigned error;
    uint8_t *draw_image_data;
    uint8_t *pixels;
    uint32_t stride;

    error = lodepng_decode24(&rgb, &src_width, &src_height, image_data, image_size);
    if (error != 0U || rgb == NULL || src_width == 0U || src_height == 0U)
    {
        ui_wallpaper_set_decode_errorf("PNG失败 err=%u %s",
                                       error,
                                       lodepng_error_text(error));
        if (rgb != NULL) lv_free(rgb);
        return false;
    }

    target_width = max_width;
    target_height = max_height;
    draw_image_data = ui_wallpaper_alloc_i4_image(target_width,
                                                  target_height,
                                                  &stride,
                                                  &pixels,
                                                  NULL);
    if (draw_image_data == NULL)
    {
        lv_free(rgb);
        return false;
    }

    for (unsigned y = 0; y < target_height; ++y)
    {
        unsigned crop_x = 0U;
        unsigned crop_y = 0U;
        unsigned crop_w = src_width;
        unsigned crop_h = src_height;
        unsigned src_y;
        uint8_t *row = pixels + (size_t)y * stride;
        ui_wallpaper_compute_fill_crop(src_width, src_height,
                                       target_width, target_height,
                                       &crop_x, &crop_y, &crop_w, &crop_h);
        src_y = crop_y + (unsigned)(((uint64_t)y * crop_h) / target_height);
        if (src_y >= src_height) src_y = src_height - 1U;

        for (unsigned x = 0; x < target_width; ++x)
        {
            unsigned src_x = crop_x + (unsigned)(((uint64_t)x * crop_w) / target_width);
            const unsigned char *pixel;
            uint8_t gray4;

            if (src_x >= src_width) src_x = src_width - 1U;
            pixel = &rgb[(src_y * src_width + src_x) * 3U];
            gray4 = ui_wallpaper_dither_to_gray4_index(x, y, pixel[0], pixel[1], pixel[2]);
            if ((x & 1U) == 0U)
            {
                row[x >> 1] = (uint8_t)(gray4 << 4);
            }
            else
            {
                row[x >> 1] |= gray4;
            }
        }
    }

    lv_free(rgb);
    return ui_wallpaper_set_image_descriptor(target_width, target_height, draw_image_data, out_image);
}

static size_t ui_wallpaper_jpeg_input(JDEC *jd, uint8_t *buffer, size_t size)
{
    ui_wallpaper_jpeg_context_t *context = (ui_wallpaper_jpeg_context_t *)jd->device;
    ui_wallpaper_jpeg_source_t *source;
    size_t remain;
    size_t read_size;

    if (context == NULL)
    {
        return 0U;
    }

    source = &context->source;
    if (source->fd < 0 || source->offset >= source->size)
    {
        return 0U;
    }

    remain = (size_t)(source->size - source->offset);
    read_size = size < remain ? size : remain;

    if (buffer != NULL)
    {
        ssize_t actual = read(source->fd, buffer, read_size);
        if (actual <= 0)
        {
            return 0U;
        }
        source->offset += (off_t)actual;
        return (size_t)actual;
    }

    if (lseek(source->fd, (off_t)read_size, SEEK_CUR) < 0)
    {
        return 0U;
    }
    source->offset += (off_t)read_size;
    return read_size;
}

static int ui_wallpaper_jpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    ui_wallpaper_jpeg_context_t *context = (ui_wallpaper_jpeg_context_t *)jd->device;
    ui_wallpaper_jpeg_target_t *target;
    const uint8_t *src = (const uint8_t *)bitmap;
    unsigned rect_width;

    if (context == NULL || bitmap == NULL || rect == NULL)
    {
        return 0;
    }

    target = &context->target;
    if (target->pixels == NULL)
    {
        return 0;
    }

    rect_width = (unsigned)(rect->right - rect->left + 1U);
    for (unsigned sy = rect->top; sy <= rect->bottom; ++sy)
    {
        unsigned row = sy - rect->top;
        unsigned dst_y0;
        unsigned dst_y1;

        if (target->src_height == 0U ||
            sy < target->src_crop_y ||
            sy >= (target->src_crop_y + target->src_crop_height))
        {
            continue;
        }

        dst_y0 = ((uint64_t)(sy - target->src_crop_y) * target->height) / target->src_crop_height;
        dst_y1 = ((uint64_t)(sy - target->src_crop_y + 1U) * target->height) / target->src_crop_height;
        if (dst_y1 <= dst_y0)
        {
            dst_y1 = dst_y0 + 1U;
        }
        if (dst_y0 >= target->height)
        {
            continue;
        }
        if (dst_y1 > target->height)
        {
            dst_y1 = target->height;
        }

        for (unsigned sx = rect->left; sx <= rect->right; ++sx)
        {
            unsigned col = sx - rect->left;
            const uint8_t *pixel;
            uint8_t gray4;
            unsigned dst_x0;
            unsigned dst_x1;

            if (target->src_width == 0U ||
                sx < target->src_crop_x ||
                sx >= (target->src_crop_x + target->src_crop_width))
            {
                continue;
            }

            pixel = &src[(row * rect_width + col) * 3U];
            gray4 = ui_wallpaper_dither_to_gray4_index(sx, sy, pixel[2], pixel[1], pixel[0]);
            dst_x0 = ((uint64_t)(sx - target->src_crop_x) * target->width) / target->src_crop_width;
            dst_x1 = ((uint64_t)(sx - target->src_crop_x + 1U) * target->width) / target->src_crop_width;
            if (dst_x1 <= dst_x0)
            {
                dst_x1 = dst_x0 + 1U;
            }
            if (dst_x0 >= target->width)
            {
                continue;
            }
            if (dst_x1 > target->width)
            {
                dst_x1 = target->width;
            }

            for (unsigned dy = dst_y0; dy < dst_y1; ++dy)
            {
                uint8_t *dst_row = target->pixels + (size_t)dy * target->stride;
                for (unsigned dx = dst_x0; dx < dst_x1; ++dx)
                {
                    if ((dx & 1U) == 0U)
                    {
                        dst_row[dx >> 1] = (uint8_t)((dst_row[dx >> 1] & 0x0F) | (gray4 << 4));
                    }
                    else
                    {
                        dst_row[dx >> 1] = (uint8_t)((dst_row[dx >> 1] & 0xF0) | gray4);
                    }
                }
            }
        }
    }

    return 1;
}

static int ui_wallpaper_stbi_read(void *user, char *data, int size)
{
    ui_wallpaper_stbi_file_t *file = (ui_wallpaper_stbi_file_t *)user;
    ssize_t ret;

    if (file == NULL || file->fd < 0 || data == NULL || size <= 0)
    {
        return 0;
    }

    ret = read(file->fd, data, (size_t)size);
    if (ret <= 0)
    {
        return 0;
    }

    return (int)ret;
}

static void ui_wallpaper_stbi_skip(void *user, int n)
{
    ui_wallpaper_stbi_file_t *file = (ui_wallpaper_stbi_file_t *)user;

    if (file == NULL || file->fd < 0 || n == 0)
    {
        return;
    }

    (void)lseek(file->fd, (off_t)n, SEEK_CUR);
}

static int ui_wallpaper_stbi_eof(void *user)
{
    ui_wallpaper_stbi_file_t *file = (ui_wallpaper_stbi_file_t *)user;
    off_t pos;

    if (file == NULL || file->fd < 0)
    {
        return 1;
    }

    pos = lseek(file->fd, 0, SEEK_CUR);
    if (pos < 0)
    {
        return 1;
    }

    return pos >= file->size;
}

static bool ui_wallpaper_decode_jpeg_file(const char *path,
                                          uint16_t max_width,
                                          uint16_t max_height,
                                          lv_image_dsc_t *out_image)
{
    ui_wallpaper_jpeg_context_t context;
    JDEC jd;
    JRESULT result;
    uint8_t *workbuf = NULL;
    uint8_t *image_data = NULL;
    uint8_t *pixels = NULL;
    unsigned decode_scale = 0U;
    unsigned target_width = 0U;
    unsigned target_height = 0U;
    uint32_t stride;

    if (path == NULL || out_image == NULL)
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    memset(&jd, 0, sizeof(jd));
    context.source.fd = open(path, O_RDONLY, 0);
    context.source.size = 0;
    context.source.offset = 0;

    if (context.source.fd < 0)
    {
        ui_wallpaper_set_decode_errorf("打开失败 errno=%d", rt_get_errno());
        return false;
    }

    context.source.size = lseek(context.source.fd, 0, SEEK_END);
    if (context.source.size <= 0)
    {
        ui_wallpaper_set_decode_errorf("文件大小无效=%ld", (long)context.source.size);
        close(context.source.fd);
        return false;
    }

    if (lseek(context.source.fd, 0, SEEK_SET) < 0)
    {
        ui_wallpaper_set_decode_errorf("回到文件头失败 errno=%d", rt_get_errno());
        close(context.source.fd);
        return false;
    }

    workbuf = (uint8_t *)audio_mem_malloc(4096U);
    if (workbuf == NULL)
    {
        ui_wallpaper_set_decode_errorf("申请内存失败 需要约4096字节");
        close(context.source.fd);
        return false;
    }

    result = jd_prepare(&jd, ui_wallpaper_jpeg_input, workbuf, 4096U, &context);
    if (result != JDR_OK || jd.width == 0U || jd.height == 0U)
    {
        ui_wallpaper_set_decode_errorf("JPG失败 prepare=%d", (int)result);
        audio_mem_free(workbuf);
        close(context.source.fd);
        return false;
    }

    decode_scale = 0U;
    context.target.src_width = (unsigned)jd.width;
    context.target.src_height = (unsigned)jd.height;
    ui_wallpaper_compute_fill_crop(context.target.src_width,
                                   context.target.src_height,
                                   max_width,
                                   max_height,
                                   &context.target.src_crop_x,
                                   &context.target.src_crop_y,
                                   &context.target.src_crop_width,
                                   &context.target.src_crop_height);
    target_width = max_width;
    target_height = max_height;
    if (target_width == 0U || target_height == 0U)
    {
        ui_wallpaper_set_decode_errorf("JPG失败 尺寸无效");
        audio_mem_free(workbuf);
        close(context.source.fd);
        return false;
    }

    image_data = ui_wallpaper_alloc_i4_image(target_width,
                                             target_height,
                                             &stride,
                                             &pixels,
                                             NULL);
    if (image_data == NULL)
    {
        audio_mem_free(workbuf);
        close(context.source.fd);
        return false;
    }
    context.target.width = target_width;
    context.target.height = target_height;
    context.target.stride = stride;
    context.target.pixels = pixels;
    if (context.target.src_crop_width == 0U || context.target.src_crop_height == 0U)
    {
        ui_wallpaper_compute_fill_crop(context.target.src_width,
                                       context.target.src_height,
                                       target_width,
                                       target_height,
                                       &context.target.src_crop_x,
                                       &context.target.src_crop_y,
                                       &context.target.src_crop_width,
                                       &context.target.src_crop_height);
    }

    if (lseek(context.source.fd, 0, SEEK_SET) < 0)
    {
        ui_wallpaper_set_decode_errorf("回到文件头失败 errno=%d", rt_get_errno());
        audio_mem_free(workbuf);
        audio_mem_free(image_data);
        close(context.source.fd);
        return false;
    }
    context.source.offset = 0;
    result = jd_prepare(&jd, ui_wallpaper_jpeg_input, workbuf, 4096U, &context);
    if (result != JDR_OK)
    {
        ui_wallpaper_set_decode_errorf("JPG失败 prepare2=%d", (int)result);
        audio_mem_free(workbuf);
        audio_mem_free(image_data);
        close(context.source.fd);
        return false;
    }

    result = jd_decomp(&jd, ui_wallpaper_jpeg_output, (uint8_t)decode_scale);
    audio_mem_free(workbuf);
    close(context.source.fd);
    if (result != JDR_OK)
    {
        ui_wallpaper_set_decode_errorf("JPG失败 decomp=%d", (int)result);
        audio_mem_free(image_data);
        return false;
    }

    return ui_wallpaper_set_image_descriptor(target_width, target_height, image_data, out_image);
}

static bool ui_wallpaper_decode_jpeg(const uint8_t *image_data,
                                     size_t image_size,
                                     uint16_t max_width,
                                     uint16_t max_height,
                                     lv_image_dsc_t *out_image)
{
    int src_width = 0;
    int src_height = 0;
    int src_channels = 0;
    unsigned target_width = 0U;
    unsigned target_height = 0U;
    stbi_uc *rgb = NULL;
    uint8_t *draw_image_data = NULL;
    uint8_t *pixels = NULL;
    uint32_t stride;

    if (image_data == NULL || image_size == 0U || out_image == NULL)
    {
        return false;
    }

    rgb = stbi_load_from_memory((const stbi_uc *)image_data,
                                (int)image_size,
                                &src_width,
                                &src_height,
                                &src_channels,
                                3);
    if (rgb == NULL || src_width <= 0 || src_height <= 0)
    {
        const char *reason = stbi_failure_reason();
        int info_w = 0;
        int info_h = 0;
        int info_comp = 0;
        int info_ok = stbi_info_from_memory((const stbi_uc *)image_data,
                                            (int)image_size,
                                            &info_w,
                                            &info_h,
                                            &info_comp);
        ui_wallpaper_set_decode_errorf("JPG失败 reason=%s info=%d %dx%d c=%d",
                                       reason != NULL ? reason : "unknown",
                                       info_ok,
                                       info_w,
                                       info_h,
                                       info_comp);
        if (rgb != NULL)
        {
            stbi_image_free(rgb);
        }
        return false;
    }

    target_width = max_width;
    target_height = max_height;
    draw_image_data = ui_wallpaper_alloc_i4_image(target_width,
                                                  target_height,
                                                  &stride,
                                                  &pixels,
                                                  NULL);
    if (draw_image_data == NULL)
    {
        stbi_image_free(rgb);
        return false;
    }

    for (unsigned y = 0; y < target_height; ++y)
    {
        unsigned crop_x = 0U;
        unsigned crop_y = 0U;
        unsigned crop_w = (unsigned)src_width;
        unsigned crop_h = (unsigned)src_height;
        unsigned src_y;
        uint8_t *row = pixels + (size_t)y * stride;
        ui_wallpaper_compute_fill_crop((unsigned)src_width, (unsigned)src_height,
                                       target_width, target_height,
                                       &crop_x, &crop_y, &crop_w, &crop_h);
        src_y = crop_y + (unsigned)(((uint64_t)y * crop_h) / target_height);
        if (src_y >= (unsigned)src_height) src_y = (unsigned)src_height - 1U;

        for (unsigned x = 0; x < target_width; ++x)
        {
            unsigned src_x = crop_x + (unsigned)(((uint64_t)x * crop_w) / target_width);
            const stbi_uc *pixel;
            uint8_t r;
            uint8_t g;
            uint8_t b;

            if (src_x >= (unsigned)src_width) src_x = (unsigned)src_width - 1U;
            pixel = &rgb[(src_y * (unsigned)src_width + src_x) * 3U];
            r = pixel[0];
            g = pixel[1];
            b = pixel[2];
            {
                uint8_t gray4 = ui_wallpaper_dither_to_gray4_index(x, y, r, g, b);
                if ((x & 1U) == 0U)
                {
                    row[x >> 1] = (uint8_t)(gray4 << 4);
                }
                else
                {
                    row[x >> 1] |= gray4;
                }
            }
        }
    }

    stbi_image_free(rgb);
    return ui_wallpaper_set_image_descriptor(target_width, target_height, draw_image_data, out_image);
}

static bool ui_wallpaper_decode_file_to_image(const char *path, lv_image_dsc_t *out_image)
{
    const char *ext;
    uint8_t *file_data = NULL;
    size_t file_size = 0U;
    bool ok = false;

    if (path == NULL || out_image == NULL)
    {
        return false;
    }

    s_wallpaper_last_error[0] = '\0';

    ext = strrchr(path, '.');
    if (ext == NULL)
    {
        ui_wallpaper_set_decode_errorf("文件无扩展名");
        return false;
    }

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
    {
        return ui_wallpaper_decode_jpeg_file(path,
                                             UI_WALLPAPER_MAX_WIDTH,
                                             UI_WALLPAPER_MAX_HEIGHT,
                                             out_image);
    }

    if (!ui_wallpaper_read_file(path, &file_data, &file_size))
    {
        return false;
    }

    if (strcasecmp(ext, ".png") == 0)
    {
        ok = ui_wallpaper_decode_png(file_data, file_size,
                                     UI_WALLPAPER_MAX_WIDTH,
                                     UI_WALLPAPER_MAX_HEIGHT,
                                     out_image);
    }
    audio_mem_free(file_data);
    return ok;
}

static void ui_wallpaper_render_timer_cb(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        lv_timer_del(timer);
    }

    s_wallpaper_refs.render_timer = NULL;
    ui_wallpaper_render();
}

static void ui_wallpaper_render(void)
{
    char pic_dir[192];
    char configured_path[256];
    DIR *dir = NULL;
    struct dirent *entry;
    bool attempted_file = false;

    if (s_wallpaper_refs.image != NULL)
    {
        lv_obj_delete(s_wallpaper_refs.image);
        s_wallpaper_refs.image = NULL;
    }

    ui_wallpaper_release_decoded_image();
    memset(s_wallpaper_image_path, 0, sizeof(s_wallpaper_image_path));
    memset(s_wallpaper_image_src, 0, sizeof(s_wallpaper_image_src));

    app_config_get_wallpaper_path(configured_path, sizeof(configured_path));
    if (configured_path[0] != '\0')
    {
        attempted_file = true;
        rt_snprintf(s_wallpaper_image_path,
                    sizeof(s_wallpaper_image_path),
                    "%s",
                    configured_path);
        if (ui_wallpaper_decode_file_to_image(s_wallpaper_image_path, &s_wallpaper_image_dsc))
        {
            rt_kprintf("wallpaper: decode ok path=%s\n", s_wallpaper_image_path);
            goto render_ready;
        }

        rt_kprintf("wallpaper: decode failed path=%s reason=%s\n",
                   s_wallpaper_image_path,
                   s_wallpaper_last_error[0] != '\0' ? s_wallpaper_last_error : "unknown");
    }

    if (!ui_wallpaper_build_pic_dir(pic_dir, sizeof(pic_dir)))
    {
        (void)ui_wallpaper_consume_pending_full_refresh("no_pic_dir");
        ui_wallpaper_set_status_overview("未找到",
                                         "",
                                         "失败",
                                         "未找到可用 TF 卡的 pic 目录");
        return;
    }

    dir = opendir(pic_dir);
    if (dir == NULL)
    {
        (void)ui_wallpaper_consume_pending_full_refresh("open_pic_dir_failed");
        ui_wallpaper_set_status_overview(pic_dir,
                                         "",
                                         "失败",
                                         "无法打开 pic 目录");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] == '.' || !ui_wallpaper_is_image_file(entry->d_name))
        {
            continue;
        }

        rt_snprintf(s_wallpaper_image_path, sizeof(s_wallpaper_image_path), "%s/%s", pic_dir, entry->d_name);
        attempted_file = true;
        if (ui_wallpaper_decode_file_to_image(s_wallpaper_image_path, &s_wallpaper_image_dsc))
        {
            rt_kprintf("wallpaper: decode ok path=%s\n", s_wallpaper_image_path);
            break;
        }

        rt_kprintf("wallpaper: decode failed path=%s reason=%s\n",
                   s_wallpaper_image_path,
                   s_wallpaper_last_error[0] != '\0' ? s_wallpaper_last_error : "unknown");
    }

    closedir(dir);

    if (s_wallpaper_image_dsc.data == NULL)
    {
        (void)ui_wallpaper_consume_pending_full_refresh("image_load_failed");
        ui_wallpaper_set_status_overview(pic_dir,
                                         s_wallpaper_image_path,
                                         "失败",
                                         s_wallpaper_last_error[0] != '\0' ?
                                         s_wallpaper_last_error :
                                         (attempted_file ?
                                          "JPEG/PNG 解码失败，请换基础格式图片" :
                                         "目录中未找到图片文件"));
        return;
    }

render_ready:
    (void)ui_wallpaper_consume_pending_full_refresh("image_present");
    lcd_set_epd_image_refresh_hint(RT_TRUE);
    s_wallpaper_refs.image = lv_img_create(s_wallpaper_refs.image_card);
    lv_obj_set_style_bg_opa(s_wallpaper_refs.image, LV_OPA_TRANSP, 0);
    ui_img_set_src(s_wallpaper_refs.image, &s_wallpaper_image_dsc);
    lv_image_set_inner_align(s_wallpaper_refs.image, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(s_wallpaper_refs.image, false);
    lv_obj_set_size(s_wallpaper_refs.image, 528, 792);
    lv_obj_set_pos(s_wallpaper_refs.image, 0, 0);
    ui_wallpaper_set_status_text(NULL);
}

void ui_Wallpaper_screen_init(void)
{
    if (ui_Wallpaper != NULL)
    {
        ui_Wallpaper_screen_destroy();
    }

    memset(&s_wallpaper_refs, 0, sizeof(s_wallpaper_refs));
    memset(s_wallpaper_image_path, 0, sizeof(s_wallpaper_image_path));
    memset(s_wallpaper_image_src, 0, sizeof(s_wallpaper_image_src));
    memset(s_wallpaper_last_error, 0, sizeof(s_wallpaper_last_error));
    memset(s_wallpaper_status_text, 0, sizeof(s_wallpaper_status_text));
    memset(&s_wallpaper_image_dsc, 0, sizeof(s_wallpaper_image_dsc));
    s_wallpaper_force_full_refresh_pending = true;
    UI_EPD_REFRESH_LOG("wallpaper: full refresh queued on enter\n");

    ui_Wallpaper = ui_create_screen_base();
    s_wallpaper_refs.image_card = ui_create_card(ui_Wallpaper, 0, 0, 528, 792, UI_SCREEN_NONE, false, 0);
    lv_obj_set_style_bg_color(s_wallpaper_refs.image_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_wallpaper_refs.image_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_wallpaper_refs.image_card, 0, 0);
    lv_obj_set_style_pad_all(s_wallpaper_refs.image_card, 0, 0);
    lv_obj_set_style_radius(s_wallpaper_refs.image_card, 0, 0);
    lv_obj_clear_flag(s_wallpaper_refs.image_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wallpaper_refs.image_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wallpaper_refs.image_card, ui_wallpaper_input_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_wallpaper_refs.image_card, ui_wallpaper_input_event_cb, LV_EVENT_CLICKED, NULL);

    s_wallpaper_refs.status_label = ui_create_label(ui_Wallpaper,
                                                    ui_i18n_pick("目录：检测中\n文件：未选择\n结果：进行中\n说明：正在检测 TF 卡 pic 目录",
                                                                 "Scanning TF /pic..."),
                                                    24,
                                                    596,
                                                    480,
                                                    168,
                                                    18,
                                                    LV_TEXT_ALIGN_CENTER,
                                                    false,
                                                    true);
    lv_label_set_long_mode(s_wallpaper_refs.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_wallpaper_refs.status_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(s_wallpaper_refs.status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_wallpaper_refs.status_label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_wallpaper_refs.status_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_wallpaper_refs.status_label, 1, 0);
    lv_obj_set_style_pad_all(s_wallpaper_refs.status_label, 8, 0);
    lv_obj_add_flag(s_wallpaper_refs.status_label, LV_OBJ_FLAG_HIDDEN);

    if (s_wallpaper_refs.render_timer == NULL)
    {
        s_wallpaper_refs.render_timer = lv_timer_create(ui_wallpaper_render_timer_cb, 1, NULL);
    }

    lv_obj_add_flag(ui_Wallpaper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui_Wallpaper, ui_wallpaper_input_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Wallpaper, ui_wallpaper_input_event_cb, LV_EVENT_CLICKED, NULL);
}

void ui_Wallpaper_screen_destroy(void)
{
    if (s_wallpaper_refs.render_timer != NULL)
    {
        lv_timer_del(s_wallpaper_refs.render_timer);
        s_wallpaper_refs.render_timer = NULL;
    }

    s_wallpaper_force_full_refresh_pending = false;
    UI_EPD_REFRESH_LOG("wallpaper: full refresh request on exit\n");
    lcd_request_epd_force_full_refresh_once();

    memset(&s_wallpaper_refs, 0, sizeof(s_wallpaper_refs));
    memset(s_wallpaper_image_path, 0, sizeof(s_wallpaper_image_path));
    memset(s_wallpaper_image_src, 0, sizeof(s_wallpaper_image_src));
    memset(s_wallpaper_last_error, 0, sizeof(s_wallpaper_last_error));
    memset(s_wallpaper_status_text, 0, sizeof(s_wallpaper_status_text));
    ui_wallpaper_release_decoded_image();

    if (ui_Wallpaper != NULL)
    {
        lv_obj_delete(ui_Wallpaper);
        ui_Wallpaper = NULL;
    }
}
