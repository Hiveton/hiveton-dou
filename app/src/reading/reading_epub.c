#include "reading_epub.h"
#include "../ui/ui_image_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include "dfs_posix.h"
#include "rtthread.h"
#include "audio_mem.h"
#include "mem_section.h"

#include "../../../sdk/external/lvgl_v9/src/libs/lodepng/lodepng.h"
#include "../../../sdk/external/lvgl_v9/src/libs/tjpgd/tjpgd.h"
#include "../../../sdk/external/micropython/extmod/uzlib/tinf.h"
#include "../../../sdk/external/micropython/extmod/uzlib/tinflate.c"
#include "../../../sdk/external/micropython/extmod/uzlib/tinfzlib.c"
#include "../../../sdk/external/micropython/extmod/uzlib/adler32.c"
#include "../../../sdk/external/micropython/extmod/uzlib/crc32.c"

#define READING_EPUB_MAX_ENTRIES 128U
#define READING_EPUB_MAX_MANIFEST_ITEMS 96U
#define READING_EPUB_MAX_SPINE_ITEMS 96U
#define READING_EPUB_MAX_TEXT_ENTITY 16U
#define READING_EPUB_MAX_TAG 256U
#define READING_EPUB_MAX_NAME 160U
#define READING_EPUB_ZIP_TAIL_BYTES (64U * 1024U)
#define READING_EPUB_INFLATE_DICT_BITS 15U
#define READING_EPUB_IMAGE_PSRAM_HEAP_SIZE (1024 * 1024U)
#define READING_EPUB_IMAGE_ZIP_COMPRESSED_MAX_BYTES (1536U * 1024U)
#define READING_EPUB_IMAGE_ZIP_UNCOMPRESSED_MAX_BYTES (1536U * 1024U)
#define READING_EPUB_PNG_RAW_MAX_BYTES (1U * 1024U * 1024U)
#define READING_EPUB_IMAGE_RGB565_MAX_BYTES (1U * 1024U * 1024U)
#define READING_EPUB_IMAGE_MEM_MAGIC_PSRAM 0x4550524DU
#define READING_EPUB_IMAGE_MEM_MAGIC_SYS   0x45525359U

typedef struct
{
    char name[READING_EPUB_MAX_INTERNAL_PATH];
    uint32_t local_header_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t compression_method;
} reading_epub_zip_entry_t;

typedef struct
{
    reading_epub_zip_entry_t entries[READING_EPUB_MAX_ENTRIES];
    uint16_t entry_count;
} reading_epub_zip_t;

typedef struct
{
    char id[READING_EPUB_MAX_NAME];
    char href[READING_EPUB_MAX_INTERNAL_PATH];
    char media_type[48];
} reading_epub_manifest_item_t;

typedef struct
{
    reading_epub_manifest_item_t items[READING_EPUB_MAX_MANIFEST_ITEMS];
    uint16_t item_count;
    char opf_path[READING_EPUB_MAX_INTERNAL_PATH];
    char root_dir[READING_EPUB_MAX_INTERNAL_PATH];
} reading_epub_package_t;

typedef struct
{
    char *text_buffer;
    size_t text_capacity;
    size_t text_length;
    reading_epub_block_t *blocks;
    uint16_t max_block_count;
    uint16_t block_count;
    reading_epub_image_ref_t *images;
    uint16_t max_image_count;
    uint16_t image_count;
    char *error_buffer;
    size_t error_buffer_size;
} reading_epub_build_state_t;

typedef struct
{
    const uint8_t *data;
    size_t size;
    size_t offset;
} reading_epub_jpeg_source_t;

typedef struct
{
    uint32_t magic;
    uint32_t size;
} reading_epub_image_mem_hdr_t;

typedef struct
{
    unsigned src_width;
    unsigned src_height;
    unsigned src_crop_x;
    unsigned src_crop_y;
    unsigned src_crop_width;
    unsigned src_crop_height;
    unsigned target_width;
    unsigned target_height;
    lv_color_t *target_pixels;
} reading_epub_jpeg_target_t;

typedef struct
{
    reading_epub_jpeg_source_t source;
    reading_epub_jpeg_target_t target;
} reading_epub_jpeg_context_t;

static bool s_reading_epub_uzlib_ready = false;
static struct rt_memheap s_reading_epub_image_psram_heap;
static rt_bool_t s_reading_epub_image_psram_heap_ready = RT_FALSE;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(reading_epub_image_psram_heap_pool)
ALIGN(4) static uint8_t s_reading_epub_image_psram_heap_pool[READING_EPUB_IMAGE_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_reading_epub_image_psram_heap_pool[READING_EPUB_IMAGE_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(reading_epub_image_psram_heap_pool);
#endif

static int reading_epub_image_mem_init(void)
{
    rt_err_t err;

    err = rt_memheap_init(&s_reading_epub_image_psram_heap,
                          "epub_img_psram",
                          s_reading_epub_image_psram_heap_pool,
                          sizeof(s_reading_epub_image_psram_heap_pool));
    if (err == RT_EOK)
    {
        s_reading_epub_image_psram_heap_ready = RT_TRUE;
        rt_kprintf("reading epub image psram heap ready: %u bytes\n",
                   (unsigned int)sizeof(s_reading_epub_image_psram_heap_pool));
    }
    else
    {
        rt_kprintf("reading epub image psram heap init failed: %d\n", err);
    }

    return err;
}
INIT_PREV_EXPORT(reading_epub_image_mem_init);

static void *reading_epub_alloc_zero(size_t size)
{
    void *ptr = audio_mem_calloc(1U, (uint32_t)size);

    if (ptr != NULL)
    {
        memset(ptr, 0, size);
    }

    return ptr;
}

static void reading_epub_free_zero(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    audio_mem_free(ptr);
}

static void *reading_epub_alloc_bytes(size_t size)
{
    return audio_mem_malloc((uint32_t)size);
}

static void reading_epub_free_bytes(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    audio_mem_free(ptr);
}

static bool reading_epub_size_add_checked(size_t left, size_t right, size_t *out)
{
    if (out != NULL)
    {
        *out = 0U;
    }

    if (left > ((size_t)-1) - right)
    {
        return false;
    }

    if (out != NULL)
    {
        *out = left + right;
    }
    return true;
}

static bool reading_epub_size_mul_checked(size_t left, size_t right, size_t *out)
{
    if (out != NULL)
    {
        *out = 0U;
    }

    if (left != 0U && right > ((size_t)-1) / left)
    {
        return false;
    }

    if (out != NULL)
    {
        *out = left * right;
    }
    return true;
}

static bool reading_epub_align_size_checked(size_t size, rt_size_t *out)
{
    size_t aligned;

    if (out != NULL)
    {
        *out = 0U;
    }

    if (size > ((size_t)-1) - (RT_ALIGN_SIZE - 1U))
    {
        return false;
    }

    aligned = (size + (RT_ALIGN_SIZE - 1U)) & ~((size_t)RT_ALIGN_SIZE - 1U);
    if (aligned > (size_t)((rt_size_t)-1))
    {
        return false;
    }

    if (out != NULL)
    {
        *out = (rt_size_t)aligned;
    }
    return true;
}

static bool reading_epub_image_bytes_for_pixels(size_t pixel_count, size_t *byte_count_out)
{
    size_t byte_count;

    if (pixel_count == 0U ||
        !reading_epub_size_mul_checked(pixel_count, sizeof(lv_color_t), &byte_count) ||
        byte_count > READING_EPUB_IMAGE_RGB565_MAX_BYTES ||
        byte_count > 0xFFFFFFFFUL)
    {
        if (byte_count_out != NULL)
        {
            *byte_count_out = 0U;
        }
        return false;
    }

    if (byte_count_out != NULL)
    {
        *byte_count_out = byte_count;
    }
    return true;
}

static bool reading_epub_image_target_size_allowed(unsigned width,
                                                   unsigned height,
                                                   size_t *pixel_count_out,
                                                   size_t *byte_count_out)
{
    size_t pixel_count;
    size_t byte_count;

    if (pixel_count_out != NULL)
    {
        *pixel_count_out = 0U;
    }
    if (byte_count_out != NULL)
    {
        *byte_count_out = 0U;
    }

    if (width == 0U ||
        height == 0U ||
        !reading_epub_size_mul_checked((size_t)width, (size_t)height, &pixel_count) ||
        !reading_epub_image_bytes_for_pixels(pixel_count, &byte_count))
    {
        return false;
    }

    if (pixel_count_out != NULL)
    {
        *pixel_count_out = pixel_count;
    }
    if (byte_count_out != NULL)
    {
        *byte_count_out = byte_count;
    }
    return true;
}

static lv_color_t *reading_epub_alloc_image_pixels(size_t pixel_count)
{
    reading_epub_image_mem_hdr_t *hdr = NULL;
    rt_size_t alloc_size;
    size_t byte_count;
    size_t total_size;

    if (!reading_epub_image_bytes_for_pixels(pixel_count, &byte_count) ||
        !reading_epub_size_add_checked(sizeof(*hdr), byte_count, &total_size) ||
        !reading_epub_align_size_checked(total_size, &alloc_size))
    {
        return NULL;
    }

    if (!s_reading_epub_image_psram_heap_ready)
    {
        return NULL;
    }

    hdr = (reading_epub_image_mem_hdr_t *)rt_memheap_alloc(&s_reading_epub_image_psram_heap,
                                                           alloc_size);
    if (hdr == NULL)
    {
        return NULL;
    }

    hdr->magic = READING_EPUB_IMAGE_MEM_MAGIC_PSRAM;
    hdr->size = (uint32_t)byte_count;
    return (lv_color_t *)(hdr + 1);
}

static void *reading_epub_alloc_large_block(size_t size)
{
    reading_epub_image_mem_hdr_t *hdr = NULL;
    rt_size_t alloc_size;
    size_t total_size;

    if (size == 0U ||
        size > READING_EPUB_PNG_RAW_MAX_BYTES ||
        size > 0xFFFFFFFFUL ||
        !reading_epub_size_add_checked(sizeof(*hdr), size, &total_size) ||
        !reading_epub_align_size_checked(total_size, &alloc_size))
    {
        return NULL;
    }

    if (!s_reading_epub_image_psram_heap_ready)
    {
        return NULL;
    }

    hdr = (reading_epub_image_mem_hdr_t *)rt_memheap_alloc(&s_reading_epub_image_psram_heap,
                                                           alloc_size);
    if (hdr == NULL)
    {
        return NULL;
    }

    hdr->magic = READING_EPUB_IMAGE_MEM_MAGIC_PSRAM;
    hdr->size = (uint32_t)size;
    return (void *)(hdr + 1);
}

static void reading_epub_free_image_pixels(void *ptr)
{
    reading_epub_image_mem_hdr_t *hdr;

    if (ptr == NULL)
    {
        return;
    }

    hdr = ((reading_epub_image_mem_hdr_t *)ptr) - 1;
    if (hdr->magic == READING_EPUB_IMAGE_MEM_MAGIC_PSRAM)
    {
        rt_memheap_free(hdr);
        return;
    }

    rt_free(hdr);
}

static void reading_epub_free_large_block(void *ptr)
{
    reading_epub_free_image_pixels(ptr);
}

void *reading_epub_png_alloc(size_t size)
{
    void *ptr = reading_epub_alloc_large_block(size);

    if (ptr == NULL)
    {
        rt_kprintf("reading_epub: png alloc failed size=%lu\n", (unsigned long)size);
    }

    return ptr;
}

void *reading_epub_png_realloc(void *ptr, size_t new_size)
{
    reading_epub_image_mem_hdr_t *hdr;
    void *new_ptr;
    size_t copy_size;

    if (ptr == NULL)
    {
        return reading_epub_png_alloc(new_size);
    }

    if (new_size == 0U)
    {
        reading_epub_free_large_block(ptr);
        return NULL;
    }

    hdr = ((reading_epub_image_mem_hdr_t *)ptr) - 1;
    copy_size = hdr->size < new_size ? hdr->size : new_size;

    new_ptr = reading_epub_alloc_large_block(new_size);
    if (new_ptr == NULL)
    {
        rt_kprintf("reading_epub: png realloc failed old=%lu new=%lu\n",
                   (unsigned long)copy_size,
                   (unsigned long)new_size);
        return NULL;
    }

    memcpy(new_ptr, ptr, copy_size);
    reading_epub_free_large_block(ptr);
    return new_ptr;
}

void reading_epub_png_free(void *ptr)
{
    reading_epub_free_large_block(ptr);
}

static void reading_epub_compute_target_size(unsigned src_width,
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

static void reading_epub_compute_fill_crop(unsigned src_width,
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

static uint16_t reading_epub_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t reading_epub_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t reading_epub_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void reading_epub_set_error(reading_epub_build_state_t *state, const char *message)
{
    if (state == NULL || state->error_buffer == NULL || state->error_buffer_size == 0U)
    {
        return;
    }

    rt_snprintf(state->error_buffer, state->error_buffer_size, "%s", message != NULL ? message : "EPUB error");
}

static bool reading_epub_has_extension(const char *path, const char *extension)
{
    const char *dot;
    size_t ext_len;

    if (path == NULL || extension == NULL)
    {
        return false;
    }

    dot = strrchr(path, '.');
    if (dot == NULL)
    {
        return false;
    }

    ext_len = strlen(extension);
    if (strlen(dot) != ext_len)
    {
        return false;
    }

    return strncasecmp(dot, extension, ext_len) == 0;
}

static const reading_epub_zip_entry_t *reading_epub_zip_find(const reading_epub_zip_t *zip,
                                                             const char *name)
{
    uint16_t i;

    if (zip == NULL || name == NULL)
    {
        return NULL;
    }

    for (i = 0; i < zip->entry_count; ++i)
    {
        if (strcmp(zip->entries[i].name, name) == 0)
        {
            return &zip->entries[i];
        }
    }

    return NULL;
}

static bool reading_epub_is_image_path(const char *path)
{
    return reading_epub_has_extension(path, ".png") ||
           reading_epub_has_extension(path, ".jpg") ||
           reading_epub_has_extension(path, ".jpeg");
}

static bool reading_epub_zip_entry_within_image_limits(const reading_epub_zip_entry_t *entry,
                                                       const char *name)
{
    if (entry == NULL || !reading_epub_is_image_path(name))
    {
        return true;
    }

    if (entry->compressed_size > READING_EPUB_IMAGE_ZIP_COMPRESSED_MAX_BYTES ||
        entry->uncompressed_size > READING_EPUB_IMAGE_ZIP_UNCOMPRESSED_MAX_BYTES)
    {
        rt_kprintf("reading_epub: image zip entry too large internal=%s comp=%lu uncomp=%lu\n",
                   name != NULL ? name : "<null>",
                   (unsigned long)entry->compressed_size,
                   (unsigned long)entry->uncompressed_size);
        return false;
    }

    return true;
}

static bool reading_epub_zip_open(const char *epub_path, reading_epub_zip_t *zip)
{
    int fd;
    off_t file_size;
    off_t tail_offset;
    uint8_t *tail_buffer;
    size_t tail_size;
    ssize_t tail_read;
    ssize_t i;
    uint32_t central_dir_offset = 0U;
    uint16_t entry_count = 0U;
    uint32_t offset;

    if (epub_path == NULL || zip == NULL)
    {
        rt_kprintf("reading_epub: zip open invalid args\n");
        return false;
    }

    memset(zip, 0, sizeof(*zip));

    fd = open(epub_path, O_RDONLY);
    if (fd < 0)
    {
        rt_kprintf("reading_epub: zip open failed path=%s errno=%d\n",
                   epub_path,
                   rt_get_errno());
        return false;
    }

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0)
    {
        rt_kprintf("reading_epub: zip invalid file size path=%s size=%ld\n",
                   epub_path,
                   (long)file_size);
        close(fd);
        return false;
    }

    tail_offset = file_size > (off_t)READING_EPUB_ZIP_TAIL_BYTES ?
                      (file_size - (off_t)READING_EPUB_ZIP_TAIL_BYTES) :
                      0;
    if (lseek(fd, tail_offset, SEEK_SET) < 0)
    {
        rt_kprintf("reading_epub: zip tail seek failed path=%s off=%ld errno=%d\n",
                   epub_path,
                   (long)tail_offset,
                   rt_get_errno());
        close(fd);
        return false;
    }

    tail_size = (size_t)(file_size - tail_offset);
    tail_buffer = (uint8_t *)reading_epub_alloc_bytes(tail_size);
    if (tail_buffer == NULL)
    {
        rt_kprintf("reading_epub: zip tail alloc failed path=%s size=%lu\n",
                   epub_path,
                   (unsigned long)tail_size);
        close(fd);
        return false;
    }

    tail_read = read(fd, tail_buffer, tail_size);
    close(fd);
    if (tail_read != (ssize_t)tail_size)
    {
        rt_kprintf("reading_epub: zip tail read failed path=%s read=%ld size=%lu errno=%d\n",
                   epub_path,
                   (long)tail_read,
                   (unsigned long)tail_size,
                   rt_get_errno());
        reading_epub_free_bytes(tail_buffer);
        return false;
    }

    for (i = tail_read - 22; i >= 0; --i)
    {
        if (tail_buffer[i] == 0x50 &&
            tail_buffer[i + 1] == 0x4BU &&
            tail_buffer[i + 2] == 0x05U &&
            tail_buffer[i + 3] == 0x06U)
        {
            entry_count = reading_epub_le16(&tail_buffer[i + 10]);
            central_dir_offset = reading_epub_le32(&tail_buffer[i + 16]);
            break;
        }
    }

    reading_epub_free_bytes(tail_buffer);

    if (entry_count == 0U || central_dir_offset == 0U)
    {
        rt_kprintf("reading_epub: zip eocd invalid path=%s entries=%u cd_off=%lu\n",
                   epub_path,
                   (unsigned int)entry_count,
                   (unsigned long)central_dir_offset);
        return false;
    }

    fd = open(epub_path, O_RDONLY);
    if (fd < 0)
    {
        rt_kprintf("reading_epub: zip reopen failed path=%s errno=%d\n",
                   epub_path,
                   rt_get_errno());
        return false;
    }

    if (lseek(fd, (off_t)central_dir_offset, SEEK_SET) < 0)
    {
        rt_kprintf("reading_epub: zip cdir seek failed path=%s off=%lu errno=%d\n",
                   epub_path,
                   (unsigned long)central_dir_offset,
                   rt_get_errno());
        close(fd);
        return false;
    }

    offset = central_dir_offset;
    while (zip->entry_count < entry_count && zip->entry_count < READING_EPUB_MAX_ENTRIES)
    {
        uint8_t header[46];
        ssize_t read_size;
        uint16_t name_len;
        uint16_t extra_len;
        uint16_t comment_len;
        char name_buffer[READING_EPUB_MAX_INTERNAL_PATH];

        read_size = read(fd, header, sizeof(header));
        if (read_size != (ssize_t)sizeof(header))
        {
            rt_kprintf("reading_epub: zip cdir header read failed path=%s idx=%u errno=%d\n",
                       epub_path,
                       (unsigned int)zip->entry_count,
                       rt_get_errno());
            break;
        }

        if (reading_epub_le32(header) != 0x02014B50UL)
        {
            rt_kprintf("reading_epub: zip cdir signature mismatch path=%s idx=%u sig=%02x%02x%02x%02x\n",
                       epub_path,
                       (unsigned int)zip->entry_count,
                       header[0],
                       header[1],
                       header[2],
                       header[3]);
            break;
        }

        name_len = reading_epub_le16(&header[28]);
        extra_len = reading_epub_le16(&header[30]);
        comment_len = reading_epub_le16(&header[32]);
        if (name_len == 0U || name_len >= READING_EPUB_MAX_INTERNAL_PATH)
        {
            if (lseek(fd, (off_t)(name_len + extra_len + comment_len), SEEK_CUR) < 0)
            {
                break;
            }
            continue;
        }

        if (read(fd, name_buffer, name_len) != (ssize_t)name_len)
        {
            break;
        }
        name_buffer[name_len] = '\0';

        if (lseek(fd, (off_t)(extra_len + comment_len), SEEK_CUR) < 0)
        {
            break;
        }

        rt_snprintf(zip->entries[zip->entry_count].name,
                    sizeof(zip->entries[zip->entry_count].name),
                    "%s",
                    name_buffer);
        zip->entries[zip->entry_count].compression_method = reading_epub_le16(&header[10]);
        zip->entries[zip->entry_count].compressed_size = reading_epub_le32(&header[20]);
        zip->entries[zip->entry_count].uncompressed_size = reading_epub_le32(&header[24]);
        zip->entries[zip->entry_count].local_header_offset = reading_epub_le32(&header[42]);
        ++zip->entry_count;
        offset += 46U + (uint32_t)name_len + (uint32_t)extra_len + (uint32_t)comment_len;
    }

    close(fd);
    if (zip->entry_count == 0U)
    {
        rt_kprintf("reading_epub: zip open no entries stored path=%s total=%u\n",
                   epub_path,
                   (unsigned int)entry_count);
    }
    return zip->entry_count > 0U;
}

static bool reading_epub_zip_read(const char *epub_path,
                                  const reading_epub_zip_t *zip,
                                  const char *name,
                                  uint8_t **out_buffer,
                                  size_t *out_size)
{
    const reading_epub_zip_entry_t *entry;
    int fd;
    uint8_t local_header[30];
    uint16_t name_len;
    uint16_t extra_len;
    uint8_t *compressed_buffer = NULL;
    uint8_t *output_buffer = NULL;

    if (out_buffer != NULL)
    {
        *out_buffer = NULL;
    }
    if (out_size != NULL)
    {
        *out_size = 0U;
    }

    entry = reading_epub_zip_find(zip, name);
    if (entry == NULL)
    {
        return false;
    }

    if (!reading_epub_zip_entry_within_image_limits(entry, name))
    {
        return false;
    }

    if (entry->compression_method != 0U && entry->compression_method != 8U)
    {
        return false;
    }

    if (entry->compression_method == 0U &&
        entry->compressed_size != entry->uncompressed_size)
    {
        rt_kprintf("reading_epub: zip stored size mismatch internal=%s comp=%lu uncomp=%lu\n",
                   name != NULL ? name : "<null>",
                   (unsigned long)entry->compressed_size,
                   (unsigned long)entry->uncompressed_size);
        return false;
    }

    if ((size_t)entry->uncompressed_size > ((size_t)-1) - 1U)
    {
        return false;
    }

    fd = open(epub_path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    if (lseek(fd, (off_t)entry->local_header_offset, SEEK_SET) < 0)
    {
        close(fd);
        return false;
    }

    if (read(fd, local_header, sizeof(local_header)) != (ssize_t)sizeof(local_header) ||
        reading_epub_le32(local_header) != 0x04034B50UL)
    {
        close(fd);
        return false;
    }

    name_len = reading_epub_le16(&local_header[26]);
    extra_len = reading_epub_le16(&local_header[28]);
    if (lseek(fd, (off_t)(name_len + extra_len), SEEK_CUR) < 0)
    {
        close(fd);
        return false;
    }

    compressed_buffer = (uint8_t *)reading_epub_alloc_bytes(entry->compressed_size);
    output_buffer = (uint8_t *)reading_epub_alloc_bytes((size_t)entry->uncompressed_size + 1U);
    if (compressed_buffer == NULL || output_buffer == NULL)
    {
        close(fd);
        if (compressed_buffer != NULL) reading_epub_free_bytes(compressed_buffer);
        if (output_buffer != NULL) reading_epub_free_bytes(output_buffer);
        return false;
    }

    if (read(fd, compressed_buffer, entry->compressed_size) != (ssize_t)entry->compressed_size)
    {
        close(fd);
        reading_epub_free_bytes(compressed_buffer);
        reading_epub_free_bytes(output_buffer);
        return false;
    }
    close(fd);

    if (entry->compression_method == 0U)
    {
        memcpy(output_buffer, compressed_buffer, entry->compressed_size);
    }
    else if (entry->compression_method == 8U)
    {
        TINF_DATA decomp;
        uint8_t *dict;
        int status;

        memset(&decomp, 0, sizeof(decomp));
        dict = (uint8_t *)audio_mem_malloc(1U << READING_EPUB_INFLATE_DICT_BITS);
        if (dict == NULL)
        {
            reading_epub_free_bytes(compressed_buffer);
            reading_epub_free_bytes(output_buffer);
            return false;
        }

        if (!s_reading_epub_uzlib_ready)
        {
            uzlib_init();
            s_reading_epub_uzlib_ready = true;
        }
        uzlib_uncompress_init(&decomp, dict, 1U << READING_EPUB_INFLATE_DICT_BITS);
        decomp.source = compressed_buffer;
        decomp.source_limit = compressed_buffer + entry->compressed_size;
        decomp.dest = output_buffer;
        decomp.dest_limit = output_buffer + entry->uncompressed_size;

        status = uzlib_uncompress(&decomp);
        audio_mem_free(dict);
        if (status != TINF_DONE && status != TINF_OK)
        {
            reading_epub_free_bytes(compressed_buffer);
            reading_epub_free_bytes(output_buffer);
            return false;
        }
    }
    else
    {
        reading_epub_free_bytes(compressed_buffer);
        reading_epub_free_bytes(output_buffer);
        return false;
    }

    output_buffer[entry->uncompressed_size] = '\0';
    reading_epub_free_bytes(compressed_buffer);

    if (out_buffer != NULL)
    {
        *out_buffer = output_buffer;
    }
    else
    {
        reading_epub_free_bytes(output_buffer);
    }
    if (out_size != NULL)
    {
        *out_size = entry->uncompressed_size;
    }

    return true;
}

static bool reading_epub_extract_attribute(const char *tag,
                                           const char *attribute_name,
                                           char *buffer,
                                           size_t buffer_size)
{
    const char *attribute;
    const char *value_start;
    const char *value_end;

    if (tag == NULL || attribute_name == NULL || buffer == NULL || buffer_size == 0U)
    {
        return false;
    }

    attribute = strstr(tag, attribute_name);
    if (attribute == NULL)
    {
        buffer[0] = '\0';
        return false;
    }

    attribute = strchr(attribute, '=');
    if (attribute == NULL)
    {
        buffer[0] = '\0';
        return false;
    }
    ++attribute;

    while (*attribute == ' ' || *attribute == '\t')
    {
        ++attribute;
    }

    if (*attribute != '"' && *attribute != '\'')
    {
        buffer[0] = '\0';
        return false;
    }

    value_start = attribute + 1;
    value_end = strchr(value_start, *attribute);
    if (value_end == NULL)
    {
        buffer[0] = '\0';
        return false;
    }

    rt_snprintf(buffer, buffer_size, "%.*s", (int)(value_end - value_start), value_start);
    return true;
}

static void reading_epub_get_directory(const char *path, char *buffer, size_t buffer_size)
{
    const char *slash;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (path == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    slash = strrchr(path, '/');
    if (slash == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    rt_snprintf(buffer, buffer_size, "%.*s", (int)(slash - path), path);
}

static void reading_epub_resolve_path(const char *base_path,
                                      const char *relative_path,
                                      char *buffer,
                                      size_t buffer_size)
{
    char temp[READING_EPUB_MAX_INTERNAL_PATH];
    char directory[READING_EPUB_MAX_INTERNAL_PATH];
    char *segments[24];
    uint16_t segment_count = 0U;
    char *token;

    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }

    if (relative_path == NULL || relative_path[0] == '\0')
    {
        buffer[0] = '\0';
        return;
    }

    if (strchr(relative_path, ':') != NULL || relative_path[0] == '/')
    {
        rt_snprintf(buffer, buffer_size, "%s", relative_path);
        return;
    }

    reading_epub_get_directory(base_path, directory, sizeof(directory));
    if (directory[0] != '\0')
    {
        rt_snprintf(temp, sizeof(temp), "%s/%s", directory, relative_path);
    }
    else
    {
        rt_snprintf(temp, sizeof(temp), "%s", relative_path);
    }

    segment_count = 0U;
    token = strtok(temp, "/");
    while (token != NULL && segment_count < (sizeof(segments) / sizeof(segments[0])))
    {
        if (strcmp(token, ".") == 0)
        {
            token = strtok(NULL, "/");
            continue;
        }
        if (strcmp(token, "..") == 0)
        {
            if (segment_count > 0U)
            {
                --segment_count;
            }
            token = strtok(NULL, "/");
            continue;
        }
        segments[segment_count++] = token;
        token = strtok(NULL, "/");
    }

    buffer[0] = '\0';
    for (uint16_t i = 0; i < segment_count; ++i)
    {
        if (i > 0U)
        {
            strncat(buffer, "/", buffer_size - strlen(buffer) - 1U);
        }
        strncat(buffer, segments[i], buffer_size - strlen(buffer) - 1U);
    }
}

static uint8_t reading_epub_choose_jpeg_scale(unsigned src_width,
                                              unsigned src_height,
                                              uint16_t max_width,
                                              uint16_t max_height)
{
    uint8_t scale = 0U;

    while (scale < 3U)
    {
        unsigned next_width = (src_width + ((unsigned)1U << (scale + 1U)) - 1U) >> (scale + 1U);
        unsigned next_height = (src_height + ((unsigned)1U << (scale + 1U)) - 1U) >> (scale + 1U);

        if (next_width < max_width || next_height < max_height)
        {
            break;
        }

        scale++;
    }

    return scale;
}

static bool reading_epub_load_package(const char *epub_path,
                                      const reading_epub_zip_t *zip,
                                      reading_epub_package_t *package)
{
    uint8_t *container_data = NULL;
    uint8_t *opf_data = NULL;
    size_t container_size;
    size_t opf_size;
    char opf_path[READING_EPUB_MAX_INTERNAL_PATH];
    const char *cursor;
    const char *spine_cursor;

    if (!reading_epub_zip_read(epub_path, zip, "META-INF/container.xml", &container_data, &container_size))
    {
        return false;
    }

    memset(package, 0, sizeof(*package));
    if (!reading_epub_extract_attribute((const char *)container_data, "full-path", opf_path, sizeof(opf_path)))
    {
        reading_epub_free_bytes(container_data);
        return false;
    }
    reading_epub_free_bytes(container_data);

    rt_snprintf(package->opf_path, sizeof(package->opf_path), "%s", opf_path);
    reading_epub_get_directory(opf_path, package->root_dir, sizeof(package->root_dir));

    if (!reading_epub_zip_read(epub_path, zip, package->opf_path, &opf_data, &opf_size))
    {
        return false;
    }

    cursor = (const char *)opf_data;
    while ((cursor = strstr(cursor, "<item ")) != NULL &&
           package->item_count < READING_EPUB_MAX_MANIFEST_ITEMS)
    {
        const char *tag_end = strchr(cursor, '>');
        char tag[READING_EPUB_MAX_TAG];

        if (tag_end == NULL)
        {
            break;
        }

        rt_snprintf(tag, sizeof(tag), "%.*s", (int)(tag_end - cursor + 1), cursor);
        if (reading_epub_extract_attribute(tag, "id", package->items[package->item_count].id, sizeof(package->items[package->item_count].id)) &&
            reading_epub_extract_attribute(tag, "href", package->items[package->item_count].href, sizeof(package->items[package->item_count].href)) &&
            reading_epub_extract_attribute(tag, "media-type", package->items[package->item_count].media_type, sizeof(package->items[package->item_count].media_type)))
        {
            ++package->item_count;
        }

        cursor = tag_end + 1;
    }

    spine_cursor = strstr((const char *)opf_data, "<spine");
    if (spine_cursor != NULL)
    {
        cursor = spine_cursor;
        package->item_count = package->item_count;
    }

    reading_epub_free_bytes(opf_data);
    return package->item_count > 0U;
}

static const reading_epub_manifest_item_t *reading_epub_manifest_find(const reading_epub_package_t *package,
                                                                      const char *id)
{
    uint16_t i;

    if (package == NULL || id == NULL)
    {
        return NULL;
    }

    for (i = 0; i < package->item_count; ++i)
    {
        if (strcmp(package->items[i].id, id) == 0)
        {
            return &package->items[i];
        }
    }

    return NULL;
}

static bool reading_epub_append_text_char(reading_epub_build_state_t *state, char ch)
{
    if (state == NULL || state->text_buffer == NULL || state->text_capacity < 2U)
    {
        return false;
    }

    if (state->text_length + 1U >= state->text_capacity)
    {
        return false;
    }

    state->text_buffer[state->text_length++] = ch;
    state->text_buffer[state->text_length] = '\0';
    return true;
}

static void reading_epub_trim_trailing_spaces(reading_epub_build_state_t *state)
{
    if (state == NULL || state->text_buffer == NULL)
    {
        return;
    }

    while (state->text_length > 0U &&
           (state->text_buffer[state->text_length - 1U] == ' ' ||
            state->text_buffer[state->text_length - 1U] == '\t'))
    {
        --state->text_length;
        state->text_buffer[state->text_length] = '\0';
    }
}

static void reading_epub_append_line_break(reading_epub_build_state_t *state, uint8_t count)
{
    uint8_t i;

    if (state == NULL)
    {
        return;
    }

    reading_epub_trim_trailing_spaces(state);
    for (i = 0; i < count; ++i)
    {
        if (state->text_length == 0U || state->text_buffer[state->text_length - 1U] != '\n')
        {
            (void)reading_epub_append_text_char(state, '\n');
        }
        else if (i + 1U < count &&
                 state->text_length >= 2U &&
                 state->text_buffer[state->text_length - 2U] != '\n')
        {
            (void)reading_epub_append_text_char(state, '\n');
        }
    }
}

static bool reading_epub_append_text_block(reading_epub_build_state_t *state,
                                           uint32_t start,
                                           uint32_t end)
{
    if (state == NULL || start >= end)
    {
        return true;
    }

    if (state->block_count >= state->max_block_count)
    {
        reading_epub_set_error(state, "EPUB block count exceeded");
        return false;
    }

    state->blocks[state->block_count].type = READING_EPUB_BLOCK_TEXT;
    state->blocks[state->block_count].text_start = start;
    state->blocks[state->block_count].text_end = end;
    state->blocks[state->block_count].image_index = 0U;
    ++state->block_count;
    return true;
}

static bool reading_epub_append_image_block(reading_epub_build_state_t *state,
                                            const char *internal_path)
{
    if (state == NULL || internal_path == NULL || internal_path[0] == '\0')
    {
        return false;
    }

    if (state->image_count >= state->max_image_count ||
        state->block_count >= state->max_block_count)
    {
        reading_epub_set_error(state, "EPUB image count exceeded");
        return false;
    }

    rt_snprintf(state->images[state->image_count].internal_path,
                sizeof(state->images[state->image_count].internal_path),
                "%s",
                internal_path);
    state->blocks[state->block_count].type = READING_EPUB_BLOCK_IMAGE;
    state->blocks[state->block_count].text_start = 0U;
    state->blocks[state->block_count].text_end = 0U;
    state->blocks[state->block_count].image_index = state->image_count;
    ++state->image_count;
    ++state->block_count;
    return true;
}

static bool reading_epub_append_entity(reading_epub_build_state_t *state, const char *entity)
{
    if (strcmp(entity, "nbsp") == 0)
    {
        return reading_epub_append_text_char(state, ' ');
    }
    if (strcmp(entity, "lt") == 0)
    {
        return reading_epub_append_text_char(state, '<');
    }
    if (strcmp(entity, "gt") == 0)
    {
        return reading_epub_append_text_char(state, '>');
    }
    if (strcmp(entity, "amp") == 0)
    {
        return reading_epub_append_text_char(state, '&');
    }
    if (strcmp(entity, "quot") == 0)
    {
        return reading_epub_append_text_char(state, '"');
    }

    return true;
}

static bool reading_epub_extract_image_path(const char *tag,
                                            char *buffer,
                                            size_t buffer_size)
{
    if (reading_epub_extract_attribute(tag, "src", buffer, buffer_size))
    {
        return true;
    }

    if (reading_epub_extract_attribute(tag, "xlink:href", buffer, buffer_size))
    {
        return true;
    }

    return reading_epub_extract_attribute(tag, "href", buffer, buffer_size);
}

static bool reading_epub_parse_xhtml(const char *xhtml_path,
                                     const uint8_t *data,
                                     size_t data_size,
                                     reading_epub_build_state_t *state)
{
    size_t i = 0U;
    uint32_t text_block_start;
    bool in_space = false;

    if (xhtml_path == NULL || data == NULL || state == NULL)
    {
        return false;
    }

    text_block_start = (uint32_t)state->text_length;

    while (i < data_size)
    {
        if (data[i] == '<')
        {
            size_t tag_start = i;
            size_t tag_end = i;
            char tag[READING_EPUB_MAX_TAG];
            char src_path[READING_EPUB_MAX_INTERNAL_PATH];
            char resolved_path[READING_EPUB_MAX_INTERNAL_PATH];
            bool closing = false;

            while (tag_end < data_size && data[tag_end] != '>')
            {
                ++tag_end;
            }
            if (tag_end >= data_size)
            {
                break;
            }

            rt_snprintf(tag, sizeof(tag), "%.*s", (int)(tag_end - tag_start + 1U), &data[tag_start]);
            closing = tag[1] == '/';

            if (!closing &&
                (strstr(tag, "<p") == tag ||
                 strstr(tag, "<div") == tag ||
                 strstr(tag, "<h1") == tag ||
                 strstr(tag, "<h2") == tag ||
                 strstr(tag, "<h3") == tag ||
                 strstr(tag, "<li") == tag))
            {
                if (state->text_length > text_block_start)
                {
                    reading_epub_append_line_break(state, 1U);
                }
            }
            else if (strstr(tag, "<br") == tag)
            {
                reading_epub_append_line_break(state, 1U);
            }
            else if (!closing &&
                     (strstr(tag, "<img") == tag ||
                      strstr(tag, "<image") == tag))
            {
                reading_epub_trim_trailing_spaces(state);
                if (!reading_epub_append_text_block(state, text_block_start, (uint32_t)state->text_length))
                {
                    return false;
                }

                if (reading_epub_extract_image_path(tag, src_path, sizeof(src_path)))
                {
                    reading_epub_resolve_path(xhtml_path, src_path, resolved_path, sizeof(resolved_path));
                    if (!reading_epub_append_image_block(state, resolved_path))
                    {
                        return false;
                    }
                }

                text_block_start = (uint32_t)state->text_length;
            }
            else if (closing &&
                     (strstr(tag, "</p") == tag ||
                      strstr(tag, "</div") == tag ||
                      strstr(tag, "</h1") == tag ||
                      strstr(tag, "</h2") == tag ||
                      strstr(tag, "</h3") == tag ||
                      strstr(tag, "</li") == tag))
            {
                reading_epub_append_line_break(state, 2U);
                in_space = false;
            }

            i = tag_end + 1U;
            continue;
        }

        if (data[i] == '&')
        {
            size_t entity_end = i + 1U;
            char entity[READING_EPUB_MAX_TEXT_ENTITY];

            while (entity_end < data_size &&
                   data[entity_end] != ';' &&
                   (entity_end - i) < (READING_EPUB_MAX_TEXT_ENTITY - 1U))
            {
                ++entity_end;
            }

            if (entity_end < data_size && data[entity_end] == ';')
            {
                rt_snprintf(entity, sizeof(entity), "%.*s", (int)(entity_end - i - 1U), &data[i + 1U]);
                (void)reading_epub_append_entity(state, entity);
                i = entity_end + 1U;
                in_space = false;
                continue;
            }
        }

        if (isspace((unsigned char)data[i]))
        {
            if (!in_space &&
                state->text_length > 0U &&
                state->text_buffer[state->text_length - 1U] != '\n')
            {
                (void)reading_epub_append_text_char(state, ' ');
            }
            in_space = true;
            ++i;
            continue;
        }

        if (!reading_epub_append_text_char(state, (char)data[i]))
        {
            reading_epub_set_error(state, "EPUB text buffer exceeded");
            return false;
        }
        in_space = false;
        ++i;
    }

    reading_epub_append_line_break(state, 2U);
    return reading_epub_append_text_block(state, text_block_start, (uint32_t)state->text_length);
}

static bool reading_epub_collect_spine(const char *epub_path,
                                       const reading_epub_zip_t *zip,
                                       const reading_epub_package_t *package,
                                       reading_epub_build_state_t *state)
{
    uint8_t *opf_data = NULL;
    size_t opf_size = 0U;
    const char *cursor;

    if (!reading_epub_zip_read(epub_path, zip, package->opf_path, &opf_data, &opf_size))
    {
        reading_epub_set_error(state, "Failed to read EPUB package");
        return false;
    }

    cursor = strstr((const char *)opf_data, "<spine");
    if (cursor == NULL)
    {
        reading_epub_free_bytes(opf_data);
        reading_epub_set_error(state, "EPUB spine not found");
        return false;
    }

    while ((cursor = strstr(cursor, "<itemref ")) != NULL)
    {
        const char *tag_end = strchr(cursor, '>');
        char tag[READING_EPUB_MAX_TAG];
        char idref[READING_EPUB_MAX_NAME];
        char resolved_path[READING_EPUB_MAX_INTERNAL_PATH];
        const reading_epub_manifest_item_t *item;
        uint8_t *xhtml_data = NULL;
        size_t xhtml_size = 0U;

        if (tag_end == NULL)
        {
            break;
        }

        rt_snprintf(tag, sizeof(tag), "%.*s", (int)(tag_end - cursor + 1), cursor);
        if (!reading_epub_extract_attribute(tag, "idref", idref, sizeof(idref)))
        {
            cursor = tag_end + 1;
            continue;
        }

        item = reading_epub_manifest_find(package, idref);
        if (item == NULL)
        {
            cursor = tag_end + 1;
            continue;
        }

        reading_epub_resolve_path(package->opf_path, item->href, resolved_path, sizeof(resolved_path));
        if (reading_epub_zip_read(epub_path, zip, resolved_path, &xhtml_data, &xhtml_size))
        {
            if (!reading_epub_parse_xhtml(resolved_path, xhtml_data, xhtml_size, state))
            {
                reading_epub_free_bytes(xhtml_data);
                reading_epub_free_bytes(opf_data);
                return false;
            }
            reading_epub_free_bytes(xhtml_data);
        }

        cursor = tag_end + 1;
    }

    reading_epub_free_bytes(opf_data);
    return state->block_count > 0U;
}

static bool reading_epub_collect_spine_items(const char *epub_path,
                                             const reading_epub_zip_t *zip,
                                             const reading_epub_package_t *package,
                                             reading_epub_spine_item_t *items,
                                             uint16_t max_item_count,
                                             uint16_t *item_count_out,
                                             reading_epub_build_state_t *state)
{
    uint8_t *opf_data = NULL;
    size_t opf_size = 0U;
    const char *cursor;
    uint16_t item_count = 0U;

    if (item_count_out != NULL)
    {
        *item_count_out = 0U;
    }

    if (items == NULL || max_item_count == 0U)
    {
        reading_epub_set_error(state, "EPUB spine output is invalid");
        return false;
    }

    if (!reading_epub_zip_read(epub_path, zip, package->opf_path, &opf_data, &opf_size))
    {
        reading_epub_set_error(state, "Failed to read EPUB package");
        return false;
    }

    cursor = strstr((const char *)opf_data, "<spine");
    if (cursor == NULL)
    {
        reading_epub_free_bytes(opf_data);
        reading_epub_set_error(state, "EPUB spine not found");
        return false;
    }

    while ((cursor = strstr(cursor, "<itemref ")) != NULL)
    {
        const char *tag_end = strchr(cursor, '>');
        char tag[READING_EPUB_MAX_TAG];
        char idref[READING_EPUB_MAX_NAME];
        char resolved_path[READING_EPUB_MAX_INTERNAL_PATH];
        const reading_epub_manifest_item_t *item;

        if (tag_end == NULL)
        {
            break;
        }

        rt_snprintf(tag, sizeof(tag), "%.*s", (int)(tag_end - cursor + 1), cursor);
        if (!reading_epub_extract_attribute(tag, "idref", idref, sizeof(idref)))
        {
            cursor = tag_end + 1;
            continue;
        }

        item = reading_epub_manifest_find(package, idref);
        if (item == NULL)
        {
            cursor = tag_end + 1;
            continue;
        }

        if (item_count >= max_item_count)
        {
            reading_epub_free_bytes(opf_data);
            reading_epub_set_error(state, "EPUB spine item count exceeded");
            return false;
        }

        reading_epub_resolve_path(package->opf_path, item->href, resolved_path, sizeof(resolved_path));
        rt_snprintf(items[item_count].internal_path,
                    sizeof(items[item_count].internal_path),
                    "%s",
                    resolved_path);
        ++item_count;
        cursor = tag_end + 1;
    }

    reading_epub_free_bytes(opf_data);
    if (item_count_out != NULL)
    {
        *item_count_out = item_count;
    }
    return item_count > 0U;
}

bool reading_epub_build_index(const char *epub_path,
                              reading_epub_spine_item_t *items,
                              uint16_t max_item_count,
                              uint16_t *item_count_out,
                              char *error_buffer,
                              size_t error_buffer_size)
{
    reading_epub_zip_t *zip = NULL;
    reading_epub_package_t *package = NULL;
    reading_epub_build_state_t state;
    bool ok = false;

    if (item_count_out != NULL)
    {
        *item_count_out = 0U;
    }
    if (error_buffer != NULL && error_buffer_size > 0U)
    {
        error_buffer[0] = '\0';
    }

    if (epub_path == NULL || items == NULL || max_item_count == 0U)
    {
        return false;
    }

    memset(&state, 0, sizeof(state));
    state.error_buffer = error_buffer;
    state.error_buffer_size = error_buffer_size;
    memset(items, 0, max_item_count * sizeof(items[0]));

    zip = (reading_epub_zip_t *)reading_epub_alloc_zero(sizeof(*zip));
    package = (reading_epub_package_t *)reading_epub_alloc_zero(sizeof(*package));
    if (zip == NULL || package == NULL)
    {
        reading_epub_set_error(&state, "Out of memory for EPUB workspace");
        goto cleanup;
    }

    if (!reading_epub_zip_open(epub_path, zip))
    {
        reading_epub_set_error(&state, "Failed to open EPUB ZIP");
        goto cleanup;
    }

    if (!reading_epub_load_package(epub_path, zip, package))
    {
        reading_epub_set_error(&state, "Failed to parse EPUB package");
        goto cleanup;
    }

    if (!reading_epub_collect_spine_items(epub_path,
                                          zip,
                                          package,
                                          items,
                                          max_item_count,
                                          item_count_out,
                                          &state))
    {
        if (state.error_buffer != NULL && state.error_buffer[0] == '\0')
        {
            reading_epub_set_error(&state, "No readable EPUB chapters found");
        }
        goto cleanup;
    }

    ok = true;

cleanup:
    if (package != NULL) reading_epub_free_zero(package);
    if (zip != NULL) reading_epub_free_zero(zip);
    return ok;
}

bool reading_epub_load_chapter(const char *epub_path,
                               const char *chapter_internal_path,
                               char *text_buffer,
                               size_t text_buffer_size,
                               reading_epub_block_t *blocks,
                               uint16_t max_block_count,
                               uint16_t *block_count_out,
                               reading_epub_image_ref_t *images,
                               uint16_t max_image_count,
                               uint16_t *image_count_out,
                               char *error_buffer,
                               size_t error_buffer_size)
{
    reading_epub_zip_t *zip = NULL;
    reading_epub_build_state_t state;
    uint8_t *xhtml_data = NULL;
    size_t xhtml_size = 0U;
    bool ok = false;

    if (block_count_out != NULL)
    {
        *block_count_out = 0U;
    }
    if (image_count_out != NULL)
    {
        *image_count_out = 0U;
    }
    if (error_buffer != NULL && error_buffer_size > 0U)
    {
        error_buffer[0] = '\0';
    }

    if (epub_path == NULL ||
        chapter_internal_path == NULL ||
        chapter_internal_path[0] == '\0' ||
        text_buffer == NULL ||
        text_buffer_size == 0U ||
        blocks == NULL ||
        images == NULL)
    {
        return false;
    }

    memset(&state, 0, sizeof(state));
    state.text_buffer = text_buffer;
    state.text_capacity = text_buffer_size;
    state.blocks = blocks;
    state.max_block_count = max_block_count;
    state.images = images;
    state.max_image_count = max_image_count;
    state.error_buffer = error_buffer;
    state.error_buffer_size = error_buffer_size;
    state.text_buffer[0] = '\0';
    memset(blocks, 0, max_block_count * sizeof(blocks[0]));
    memset(images, 0, max_image_count * sizeof(images[0]));

    zip = (reading_epub_zip_t *)reading_epub_alloc_zero(sizeof(*zip));
    if (zip == NULL)
    {
        reading_epub_set_error(&state, "Out of memory for EPUB ZIP");
        goto cleanup;
    }

    if (!reading_epub_zip_open(epub_path, zip))
    {
        reading_epub_set_error(&state, "Failed to open EPUB ZIP");
        goto cleanup;
    }

    if (!reading_epub_zip_read(epub_path, zip, chapter_internal_path, &xhtml_data, &xhtml_size))
    {
        reading_epub_set_error(&state, "Failed to read EPUB chapter");
        goto cleanup;
    }

    if (!reading_epub_parse_xhtml(chapter_internal_path, xhtml_data, xhtml_size, &state))
    {
        goto cleanup;
    }

    if (state.block_count == 0U)
    {
        reading_epub_set_error(&state, "The chapter has no readable content");
        goto cleanup;
    }

    if (block_count_out != NULL)
    {
        *block_count_out = state.block_count;
    }
    if (image_count_out != NULL)
    {
        *image_count_out = state.image_count;
    }

    ok = true;

cleanup:
    if (xhtml_data != NULL) reading_epub_free_bytes(xhtml_data);
    if (zip != NULL) reading_epub_free_zero(zip);
    return ok;
}

bool reading_epub_load(const char *epub_path,
                       char *text_buffer,
                       size_t text_buffer_size,
                       reading_epub_block_t *blocks,
                       uint16_t max_block_count,
                       uint16_t *block_count_out,
                       reading_epub_image_ref_t *images,
                       uint16_t max_image_count,
                       uint16_t *image_count_out,
                       char *error_buffer,
                       size_t error_buffer_size)
{
    reading_epub_zip_t *zip = NULL;
    reading_epub_package_t *package = NULL;
    reading_epub_build_state_t *state = NULL;
    bool ok = false;

    if (block_count_out != NULL)
    {
        *block_count_out = 0U;
    }
    if (image_count_out != NULL)
    {
        *image_count_out = 0U;
    }
    if (error_buffer != NULL && error_buffer_size > 0U)
    {
        error_buffer[0] = '\0';
    }

    if (epub_path == NULL || text_buffer == NULL || text_buffer_size == 0U || blocks == NULL || images == NULL)
    {
        return false;
    }

    state = (reading_epub_build_state_t *)reading_epub_alloc_zero(sizeof(*state));
    if (state == NULL)
    {
        if (error_buffer != NULL && error_buffer_size > 0U)
        {
            rt_snprintf(error_buffer, error_buffer_size, "%s", "Out of memory for EPUB state");
        }
        return false;
    }

    zip = (reading_epub_zip_t *)reading_epub_alloc_zero(sizeof(*zip));
    package = (reading_epub_package_t *)reading_epub_alloc_zero(sizeof(*package));
    if (zip == NULL || package == NULL)
    {
        reading_epub_set_error(state, "Out of memory for EPUB workspace");
        goto cleanup;
    }

    state->text_buffer = text_buffer;
    state->text_capacity = text_buffer_size;
    state->blocks = blocks;
    state->max_block_count = max_block_count;
    state->images = images;
    state->max_image_count = max_image_count;
    state->error_buffer = error_buffer;
    state->error_buffer_size = error_buffer_size;
    state->text_buffer[0] = '\0';

    if (!reading_epub_zip_open(epub_path, zip))
    {
        reading_epub_set_error(state, "Failed to open EPUB ZIP");
        goto cleanup;
    }

    if (!reading_epub_load_package(epub_path, zip, package))
    {
        reading_epub_set_error(state, "Failed to parse EPUB package");
        goto cleanup;
    }

    if (!reading_epub_collect_spine(epub_path, zip, package, state))
    {
        if (state->error_buffer != NULL && state->error_buffer[0] == '\0')
        {
            reading_epub_set_error(state, "No readable EPUB chapters found");
        }
        goto cleanup;
    }

    if (block_count_out != NULL)
    {
        *block_count_out = state->block_count;
    }
    if (image_count_out != NULL)
    {
        *image_count_out = state->image_count;
    }

    ok = true;

cleanup:
    if (package != NULL) reading_epub_free_zero(package);
    if (zip != NULL) reading_epub_free_zero(zip);
    if (state != NULL) reading_epub_free_zero(state);
    return ok;
}

static uint8_t reading_epub_bayer8_value(unsigned x, unsigned y)
{
    static const uint8_t bayer8x8[8][8] = {
        {0, 48, 12, 60, 3, 51, 15, 63},
        {32, 16, 44, 28, 35, 19, 47, 31},
        {8, 56, 4, 52, 11, 59, 7, 55},
        {40, 24, 36, 20, 43, 27, 39, 23},
        {2, 50, 14, 62, 1, 49, 13, 61},
        {34, 18, 46, 30, 33, 17, 45, 29},
        {10, 58, 6, 54, 9, 57, 5, 53},
        {42, 26, 38, 22, 41, 25, 37, 21},
    };

    return bayer8x8[y & 7U][x & 7U];
}

static lv_color_t reading_epub_dither_to_bw_color(unsigned x,
                                                  unsigned y,
                                                  uint8_t r,
                                                  uint8_t g,
                                                  uint8_t b)
{
    uint8_t level;
    uint8_t out_gray;

    level = ui_image_gray4_level_from_rgb(r, g, b);
    (void)x;
    (void)y;
    out_gray = ui_image_gray4_level_to_u8(level);
    return lv_color_make(out_gray, out_gray, out_gray);
}

static unsigned reading_epub_png_bit_extract(const unsigned char *row,
                                             unsigned x,
                                             unsigned bitdepth)
{
    unsigned bit_index;
    unsigned byte_index;
    unsigned shift;

    switch (bitdepth)
    {
    case 1U:
        bit_index = x;
        byte_index = bit_index >> 3;
        shift = 7U - (bit_index & 7U);
        return (row[byte_index] >> shift) & 0x01U;
    case 2U:
        bit_index = x * 2U;
        byte_index = bit_index >> 3;
        shift = 6U - (bit_index & 7U);
        return (row[byte_index] >> shift) & 0x03U;
    case 4U:
        bit_index = x * 4U;
        byte_index = bit_index >> 3;
        shift = 4U - (bit_index & 7U);
        return (row[byte_index] >> shift) & 0x0FU;
    case 8U:
        return row[x];
    case 16U:
        return ((unsigned)row[x * 2U] << 8) | (unsigned)row[x * 2U + 1U];
    default:
        return 0U;
    }
}

static unsigned reading_epub_png_channel_count(unsigned color_type)
{
    switch (color_type)
    {
    case LCT_GREY:
    case LCT_PALETTE:
        return 1U;
    case LCT_RGB:
        return 3U;
    case LCT_GREY_ALPHA:
        return 2U;
    case LCT_RGBA:
        return 4U;
    default:
        return 0U;
    }
}

static bool reading_epub_png_row_stride_for_params(unsigned width,
                                                   unsigned color_type,
                                                   unsigned bitdepth,
                                                   size_t *row_stride_out)
{
    unsigned channels;
    size_t bits_per_row;
    size_t row_stride;

    if (row_stride_out != NULL)
    {
        *row_stride_out = 0U;
    }

    channels = reading_epub_png_channel_count(color_type);
    if (width == 0U ||
        channels == 0U ||
        bitdepth == 0U ||
        !reading_epub_size_mul_checked((size_t)width, (size_t)channels, &bits_per_row) ||
        !reading_epub_size_mul_checked(bits_per_row, (size_t)bitdepth, &bits_per_row) ||
        !reading_epub_size_add_checked(bits_per_row, 7U, &bits_per_row))
    {
        return false;
    }

    row_stride = bits_per_row >> 3;
    if (row_stride_out != NULL)
    {
        *row_stride_out = row_stride;
    }
    return true;
}

static bool reading_epub_png_raw_size_for_params(unsigned width,
                                                 unsigned height,
                                                 unsigned color_type,
                                                 unsigned bitdepth,
                                                 size_t *raw_size_out)
{
    size_t row_stride;
    size_t raw_size;

    if (raw_size_out != NULL)
    {
        *raw_size_out = 0U;
    }

    if (height == 0U ||
        !reading_epub_png_row_stride_for_params(width, color_type, bitdepth, &row_stride) ||
        !reading_epub_size_mul_checked(row_stride, (size_t)height, &raw_size))
    {
        return false;
    }

    if (raw_size_out != NULL)
    {
        *raw_size_out = raw_size;
    }
    return true;
}

static bool reading_epub_png_header_within_limits(const uint8_t *image_data,
                                                  size_t image_size,
                                                  const char *internal_path)
{
    unsigned width;
    unsigned height;
    unsigned bitdepth;
    unsigned color_type;
    size_t raw_size;

    if (image_data == NULL ||
        image_size < 33U ||
        memcmp(image_data, "\x89PNG\r\n\x1a\n", 8U) != 0 ||
        reading_epub_be32(&image_data[8]) != 13U ||
        memcmp(&image_data[12], "IHDR", 4U) != 0)
    {
        rt_kprintf("reading_epub: png header invalid internal=%s size=%lu\n",
                   internal_path != NULL ? internal_path : "<null>",
                   (unsigned long)image_size);
        return false;
    }

    width = reading_epub_be32(&image_data[16]);
    height = reading_epub_be32(&image_data[20]);
    bitdepth = image_data[24];
    color_type = image_data[25];

    if (!reading_epub_png_raw_size_for_params(width,
                                              height,
                                              color_type,
                                              bitdepth,
                                              &raw_size) ||
        raw_size > READING_EPUB_PNG_RAW_MAX_BYTES)
    {
        rt_kprintf("reading_epub: png raw too large internal=%s src=%ux%u type=%u depth=%u raw=%lu\n",
                   internal_path != NULL ? internal_path : "<null>",
                   width,
                   height,
                   color_type,
                   bitdepth,
                   (unsigned long)raw_size);
        return false;
    }

    return true;
}

static unsigned reading_epub_png_row_stride(unsigned width,
                                            const LodePNGColorMode *mode)
{
    size_t row_stride;

    if (mode == NULL ||
        !reading_epub_png_row_stride_for_params(width,
                                                mode->colortype,
                                                mode->bitdepth,
                                                &row_stride) ||
        row_stride > (size_t)~0U)
    {
        return 0U;
    }

    return (unsigned)row_stride;
}

static uint8_t reading_epub_png_scale_sample_to_u8(unsigned sample,
                                                   unsigned bitdepth)
{
    if (bitdepth == 0U)
    {
        return 0U;
    }

    if (bitdepth >= 8U)
    {
        return (uint8_t)(sample >> (bitdepth - 8U));
    }

    return (uint8_t)((sample * 255U) / ((1U << bitdepth) - 1U));
}

static void reading_epub_png_read_rgb(const unsigned char *src,
                                      unsigned src_width,
                                      unsigned src_height,
                                      const LodePNGColorMode *mode,
                                      unsigned x,
                                      unsigned y,
                                      uint8_t *r,
                                      uint8_t *g,
                                      uint8_t *b)
{
    const unsigned char *row;
    unsigned stride;
    uint8_t rr = 255U;
    uint8_t gg = 255U;
    uint8_t bb = 255U;
    uint8_t aa = 255U;

    if (r != NULL) *r = 255U;
    if (g != NULL) *g = 255U;
    if (b != NULL) *b = 255U;

    if (src == NULL || mode == NULL || x >= src_width || y >= src_height)
    {
        return;
    }

    stride = reading_epub_png_row_stride(src_width, mode);
    if (stride == 0U)
    {
        return;
    }

    row = src + ((size_t)y * stride);

    switch (mode->colortype)
    {
    case LCT_PALETTE:
    {
        unsigned index = reading_epub_png_bit_extract(row, x, mode->bitdepth);
        if (mode->palette != NULL && index < mode->palettesize)
        {
            const unsigned char *palette = &mode->palette[index * 4U];
            rr = palette[0];
            gg = palette[1];
            bb = palette[2];
            aa = palette[3];
        }
        break;
    }
    case LCT_GREY:
    {
        uint8_t gray = reading_epub_png_scale_sample_to_u8(
            reading_epub_png_bit_extract(row, x, mode->bitdepth),
            mode->bitdepth);
        rr = gray;
        gg = gray;
        bb = gray;
        break;
    }
    case LCT_GREY_ALPHA:
    {
        unsigned gray_sample;
        unsigned alpha_sample;

        if (mode->bitdepth == 8U)
        {
            gray_sample = row[x * 2U];
            alpha_sample = row[x * 2U + 1U];
        }
        else
        {
            gray_sample = ((unsigned)row[x * 4U] << 8) | row[x * 4U + 1U];
            alpha_sample = ((unsigned)row[x * 4U + 2U] << 8) | row[x * 4U + 3U];
        }

        rr = gg = bb = reading_epub_png_scale_sample_to_u8(gray_sample, mode->bitdepth);
        aa = reading_epub_png_scale_sample_to_u8(alpha_sample, mode->bitdepth);
        break;
    }
    case LCT_RGB:
        if (mode->bitdepth == 8U)
        {
            const unsigned char *pixel = &row[x * 3U];
            rr = pixel[0];
            gg = pixel[1];
            bb = pixel[2];
        }
        else
        {
            const unsigned char *pixel = &row[x * 6U];
            rr = pixel[0];
            gg = pixel[2];
            bb = pixel[4];
        }
        break;
    case LCT_RGBA:
        if (mode->bitdepth == 8U)
        {
            const unsigned char *pixel = &row[x * 4U];
            rr = pixel[0];
            gg = pixel[1];
            bb = pixel[2];
            aa = pixel[3];
        }
        else
        {
            const unsigned char *pixel = &row[x * 8U];
            rr = pixel[0];
            gg = pixel[2];
            bb = pixel[4];
            aa = pixel[6];
        }
        break;
    default:
        break;
    }

    if (aa < 255U)
    {
        rr = (uint8_t)(((unsigned)rr * aa + 255U * (255U - aa)) / 255U);
        gg = (uint8_t)(((unsigned)gg * aa + 255U * (255U - aa)) / 255U);
        bb = (uint8_t)(((unsigned)bb * aa + 255U * (255U - aa)) / 255U);
    }

    if (r != NULL) *r = rr;
    if (g != NULL) *g = gg;
    if (b != NULL) *b = bb;
}

static void reading_epub_scale_png_raw_to_rgb565(const unsigned char *src,
                                                 unsigned src_width,
                                                 unsigned src_height,
                                                 const LodePNGColorMode *mode,
                                                 uint16_t max_width,
                                                 uint16_t max_height,
                                                 lv_image_dsc_t *out_image)
{
    unsigned target_width;
    unsigned target_height;
    unsigned crop_x = 0U;
    unsigned crop_y = 0U;
    unsigned crop_width = src_width;
    unsigned crop_height = src_height;
    lv_color_t *pixels;
    size_t pixel_count;
    size_t data_size;

    if (src == NULL || mode == NULL || out_image == NULL)
    {
        return;
    }

    target_width = (max_width > 0U) ? (unsigned)max_width : src_width;
    target_height = (max_height > 0U) ? (unsigned)max_height : src_height;
    if (target_width == 0U) target_width = src_width;
    if (target_height == 0U) target_height = src_height;

    reading_epub_compute_fill_crop(src_width,
                                   src_height,
                                   target_width,
                                   target_height,
                                   &crop_x,
                                   &crop_y,
                                   &crop_width,
                                   &crop_height);

    if (!reading_epub_image_target_size_allowed(target_width,
                                                target_height,
                                                &pixel_count,
                                                &data_size))
    {
        rt_kprintf("reading_epub: png target too large src=%ux%u dst=%ux%u\n",
                   src_width, src_height, target_width, target_height);
        memset(out_image, 0, sizeof(*out_image));
        return;
    }

    pixels = reading_epub_alloc_image_pixels(pixel_count);
    if (pixels == NULL)
    {
        memset(out_image, 0, sizeof(*out_image));
        return;
    }

    for (unsigned y = 0; y < target_height; ++y)
    {
        unsigned src_y = crop_y + (unsigned)(((uint64_t)y * crop_height) / target_height);
        if (src_y >= src_height)
        {
            src_y = src_height - 1U;
        }

        for (unsigned x = 0; x < target_width; ++x)
        {
            unsigned src_x = crop_x + (unsigned)(((uint64_t)x * crop_width) / target_width);
            uint8_t r;
            uint8_t g;
            uint8_t b;

            if (src_x >= src_width)
            {
                src_x = src_width - 1U;
            }

            reading_epub_png_read_rgb(src, src_width, src_height, mode, src_x, src_y, &r, &g, &b);
            pixels[y * target_width + x] = reading_epub_dither_to_bw_color(x, y, r, g, b);
        }
    }

    memset(out_image, 0, sizeof(*out_image));
    out_image->header.magic = LV_IMAGE_HEADER_MAGIC;
    out_image->header.cf = LV_COLOR_FORMAT_RGB565;
    out_image->header.w = (lv_coord_t)target_width;
    out_image->header.h = (lv_coord_t)target_height;
    out_image->header.stride = (lv_coord_t)(target_width * sizeof(lv_color_t));
    out_image->data_size = data_size;
    out_image->data = (const uint8_t *)pixels;
}

static void reading_epub_scale_rgb_to_rgb565(const unsigned char *src,
                                             unsigned src_width,
                                             unsigned src_height,
                                             unsigned src_channels,
                                             uint16_t max_width,
                                             uint16_t max_height,
                                             lv_image_dsc_t *out_image)
{
    unsigned target_width;
    unsigned target_height;
    unsigned crop_x = 0U;
    unsigned crop_y = 0U;
    unsigned crop_width = src_width;
    unsigned crop_height = src_height;
    lv_color_t *pixels;
    size_t pixel_count;
    size_t data_size;

    target_width = (max_width > 0U) ? (unsigned)max_width : src_width;
    target_height = (max_height > 0U) ? (unsigned)max_height : src_height;
    if (target_width == 0U) target_width = src_width;
    if (target_height == 0U) target_height = src_height;

    reading_epub_compute_fill_crop(src_width,
                                   src_height,
                                   target_width,
                                   target_height,
                                   &crop_x,
                                   &crop_y,
                                   &crop_width,
                                   &crop_height);

    if (!reading_epub_image_target_size_allowed(target_width,
                                                target_height,
                                                &pixel_count,
                                                &data_size))
    {
        rt_kprintf("reading_epub: image target too large src=%ux%u dst=%ux%u\n",
                   src_width, src_height, target_width, target_height);
        memset(out_image, 0, sizeof(*out_image));
        return;
    }

    pixels = reading_epub_alloc_image_pixels(pixel_count);
    if (pixels == NULL)
    {
        rt_kprintf("reading_epub: image alloc failed src=%ux%u dst=%ux%u\n",
                   src_width, src_height, target_width, target_height);
        memset(out_image, 0, sizeof(*out_image));
        return;
    }

    for (unsigned y = 0; y < target_height; ++y)
    {
        unsigned src_y = crop_y + (unsigned)(((uint64_t)y * crop_height) / target_height);

        if (src_y >= src_height)
        {
            src_y = src_height - 1U;
        }

        for (unsigned x = 0; x < target_width; ++x)
        {
            unsigned src_x = crop_x + (unsigned)(((uint64_t)x * crop_width) / target_width);
            const unsigned char *pixel;

            if (src_x >= src_width)
            {
                src_x = src_width - 1U;
            }

            pixel = &src[(src_y * src_width + src_x) * src_channels];

            pixels[y * target_width + x] =
                reading_epub_dither_to_bw_color(x, y, pixel[0], pixel[1], pixel[2]);
        }
    }

    memset(out_image, 0, sizeof(*out_image));
    out_image->header.magic = LV_IMAGE_HEADER_MAGIC;
    out_image->header.cf = LV_COLOR_FORMAT_RGB565;
    out_image->header.w = (lv_coord_t)target_width;
    out_image->header.h = (lv_coord_t)target_height;
    out_image->header.stride = (lv_coord_t)(target_width * sizeof(lv_color_t));
    out_image->data_size = data_size;
    out_image->data = (const uint8_t *)pixels;
}

static size_t reading_epub_jpeg_input(JDEC *jd, uint8_t *buffer, size_t size)
{
    reading_epub_jpeg_context_t *context = (reading_epub_jpeg_context_t *)jd->device;
    reading_epub_jpeg_source_t *source;
    size_t remain;
    size_t read_size;

    if (context == NULL)
    {
        return 0U;
    }

    source = &context->source;
    if (source == NULL || source->offset >= source->size)
    {
        return 0U;
    }

    remain = source->size - source->offset;
    read_size = size < remain ? size : remain;

    if (buffer != NULL)
    {
        memcpy(buffer, &source->data[source->offset], read_size);
    }
    source->offset += read_size;
    return read_size;
}

static int reading_epub_jpeg_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    reading_epub_jpeg_context_t *context = (reading_epub_jpeg_context_t *)jd->device;
    reading_epub_jpeg_target_t *target;
    const uint8_t *src = (const uint8_t *)bitmap;
    unsigned rect_width;

    if (context == NULL)
    {
        return 0;
    }

    target = &context->target;
    if (target == NULL || bitmap == NULL || rect == NULL || target->target_pixels == NULL)
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

        dst_y0 = ((uint64_t)(sy - target->src_crop_y) * target->target_height) /
                 target->src_crop_height;
        dst_y1 = ((uint64_t)(sy - target->src_crop_y + 1U) * target->target_height) /
                 target->src_crop_height;
        if (dst_y1 <= dst_y0)
        {
            dst_y1 = dst_y0 + 1U;
        }
        if (dst_y0 >= target->target_height)
        {
            continue;
        }
        if (dst_y1 > target->target_height)
        {
            dst_y1 = target->target_height;
        }

        for (unsigned sx = rect->left; sx <= rect->right; ++sx)
        {
            unsigned col = sx - rect->left;
            unsigned dst_x0;
            unsigned dst_x1;
            const uint8_t *pixel;

            if (target->src_width == 0U ||
                sx < target->src_crop_x ||
                sx >= (target->src_crop_x + target->src_crop_width))
            {
                continue;
            }

            pixel = &src[(row * rect_width + col) * 3U];
            dst_x0 = ((uint64_t)(sx - target->src_crop_x) * target->target_width) /
                     target->src_crop_width;
            dst_x1 = ((uint64_t)(sx - target->src_crop_x + 1U) * target->target_width) /
                     target->src_crop_width;
            if (dst_x1 <= dst_x0)
            {
                dst_x1 = dst_x0 + 1U;
            }
            if (dst_x0 >= target->target_width)
            {
                continue;
            }
            if (dst_x1 > target->target_width)
            {
                dst_x1 = target->target_width;
            }

            for (unsigned dy = dst_y0; dy < dst_y1; ++dy)
            {
                for (unsigned dx = dst_x0; dx < dst_x1; ++dx)
                {
                    target->target_pixels[dy * target->target_width + dx] =
                        reading_epub_dither_to_bw_color(dx, dy, pixel[2], pixel[1], pixel[0]);
                }
            }
        }
    }

    return 1;
}

static bool reading_epub_decode_jpeg(const uint8_t *image_data,
                                     size_t image_size,
                                     uint16_t max_width,
                                     uint16_t max_height,
                                     lv_image_dsc_t *out_image)
{
    reading_epub_jpeg_context_t context;
    JDEC jd;
    uint8_t *workbuf;
    lv_color_t *pixels;
    JRESULT result;
    unsigned decode_scale = 0U;
    unsigned target_width;
    unsigned target_height;
    size_t pixel_count;
    size_t data_size;

    if (image_data == NULL || image_size == 0U || out_image == NULL)
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    memset(&jd, 0, sizeof(jd));

    context.source.data = image_data;
    context.source.size = image_size;
    workbuf = (uint8_t *)rt_malloc(4096U);
    if (workbuf == NULL)
    {
        return false;
    }

    result = jd_prepare(&jd, reading_epub_jpeg_input, workbuf, 4096U, &context);
    if (result != JDR_OK || jd.width == 0U || jd.height == 0U)
    {
        rt_kprintf("reading_epub: jpeg prepare failed result=%d size=%lu\n",
                   (int)result,
                   (unsigned long)image_size);
        rt_free(workbuf);
        return false;
    }

    target_width = (max_width > 0U) ? (unsigned)max_width : (unsigned)jd.width;
    target_height = (max_height > 0U) ? (unsigned)max_height : (unsigned)jd.height;
    if (target_width == 0U)
    {
        target_width = (unsigned)jd.width;
    }
    if (target_height == 0U)
    {
        target_height = (unsigned)jd.height;
    }

    if (!reading_epub_image_target_size_allowed(target_width,
                                                target_height,
                                                &pixel_count,
                                                &data_size))
    {
        rt_kprintf("reading_epub: jpeg target too large src=%ux%u dst=%ux%u scale=%u\n",
                   jd.width,
                   jd.height,
                   target_width,
                   target_height,
                   decode_scale);
        rt_free(workbuf);
        return false;
    }

    pixels = reading_epub_alloc_image_pixels(pixel_count);
    if (pixels == NULL)
    {
        rt_kprintf("reading_epub: jpeg alloc failed src=%ux%u dst=%ux%u scale=%u\n",
                   jd.width,
                   jd.height,
                   target_width,
                   target_height,
                   decode_scale);
        rt_free(workbuf);
        return false;
    }

    memset(pixels, 0xFF, data_size);
    context.target.src_width = (unsigned)jd.width;
    context.target.src_height = (unsigned)jd.height;
    reading_epub_compute_fill_crop(context.target.src_width,
                                   context.target.src_height,
                                   target_width,
                                   target_height,
                                   &context.target.src_crop_x,
                                   &context.target.src_crop_y,
                                   &context.target.src_crop_width,
                                   &context.target.src_crop_height);
    context.target.target_width = target_width;
    context.target.target_height = target_height;
    context.target.target_pixels = pixels;

    context.source.offset = 0U;
    result = jd_prepare(&jd, reading_epub_jpeg_input, workbuf, 4096U, &context);
    if (result != JDR_OK)
    {
        rt_kprintf("reading_epub: jpeg second prepare failed result=%d\n", (int)result);
        reading_epub_free_image_pixels(pixels);
        rt_free(workbuf);
        return false;
    }

    result = jd_decomp(&jd, reading_epub_jpeg_output, decode_scale);
    rt_free(workbuf);
    if (result != JDR_OK)
    {
        rt_kprintf("reading_epub: jpeg decomp failed result=%d\n", (int)result);
        reading_epub_free_image_pixels(pixels);
        return false;
    }

    memset(out_image, 0, sizeof(*out_image));
    out_image->header.magic = LV_IMAGE_HEADER_MAGIC;
    out_image->header.cf = LV_COLOR_FORMAT_RGB565;
    out_image->header.w = (lv_coord_t)context.target.target_width;
    out_image->header.h = (lv_coord_t)context.target.target_height;
    out_image->header.stride = (lv_coord_t)(context.target.target_width * sizeof(lv_color_t));
    out_image->data_size = data_size;
    out_image->data = (const uint8_t *)pixels;
    return true;
}

static bool reading_epub_probe_jpeg_size(const uint8_t *image_data,
                                         size_t image_size,
                                         uint16_t *width_out,
                                         uint16_t *height_out)
{
    reading_epub_jpeg_context_t context;
    JDEC jd;
    uint8_t *workbuf;
    JRESULT result;

    if (image_data == NULL || image_size == 0U || width_out == NULL || height_out == NULL)
    {
        return false;
    }

    memset(&context, 0, sizeof(context));
    memset(&jd, 0, sizeof(jd));
    context.source.data = image_data;
    context.source.size = image_size;

    workbuf = (uint8_t *)rt_malloc(4096U);
    if (workbuf == NULL)
    {
        return false;
    }

    result = jd_prepare(&jd, reading_epub_jpeg_input, workbuf, 4096U, &context);
    rt_free(workbuf);
    if (result != JDR_OK || jd.width == 0U || jd.height == 0U)
    {
        return false;
    }

    *width_out = (uint16_t)jd.width;
    *height_out = (uint16_t)jd.height;
    return true;
}

bool reading_epub_decode_image(const char *epub_path,
                               const char *internal_path,
                               uint16_t max_width,
                               uint16_t max_height,
                               lv_image_dsc_t *out_image)
{
    reading_epub_zip_t *zip = NULL;
    uint8_t *image_data = NULL;
    size_t image_size = 0U;
    bool ok = false;

    if (out_image == NULL)
    {
        return false;
    }
    memset(out_image, 0, sizeof(*out_image));
    (void)max_width;
    (void)max_height;

    zip = (reading_epub_zip_t *)reading_epub_alloc_zero(sizeof(*zip));
    if (zip == NULL)
    {
        return false;
    }

    if (!reading_epub_zip_open(epub_path, zip) ||
        !reading_epub_zip_read(epub_path, zip, internal_path, &image_data, &image_size))
    {
        rt_kprintf("reading_epub: image zip read failed path=%s internal=%s\n",
                   epub_path != NULL ? epub_path : "<null>",
                   internal_path != NULL ? internal_path : "<null>");
        goto cleanup;
    }

    if (reading_epub_has_extension(internal_path, ".png"))
    {
        unsigned char *raw = NULL;
        unsigned width = 0U;
        unsigned height = 0U;
        unsigned error;
        size_t raw_size = 0U;
        LodePNGState state;

        if (!reading_epub_png_header_within_limits(image_data, image_size, internal_path))
        {
            reading_epub_free_bytes(image_data);
            image_data = NULL;
            goto cleanup;
        }

        lodepng_state_init(&state);
        state.decoder.color_convert = 0U;
        error = lodepng_decode(&raw, &width, &height, &state, image_data, image_size);

        reading_epub_free_bytes(image_data);
        image_data = NULL;
        if (error != 0U || raw == NULL)
        {
            rt_kprintf("reading_epub: png decode failed error=%u internal=%s type=%u depth=%u\n",
                       error,
                       internal_path != NULL ? internal_path : "<null>",
                       (unsigned)state.info_png.color.colortype,
                       (unsigned)state.info_png.color.bitdepth);
            if (raw != NULL) reading_epub_png_free(raw);
            lodepng_state_cleanup(&state);
            goto cleanup;
        }

        if (!reading_epub_png_raw_size_for_params(width,
                                                  height,
                                                  state.info_png.color.colortype,
                                                  state.info_png.color.bitdepth,
                                                  &raw_size) ||
            raw_size > READING_EPUB_PNG_RAW_MAX_BYTES)
        {
            rt_kprintf("reading_epub: png decoded raw too large internal=%s src=%ux%u raw=%lu\n",
                       internal_path != NULL ? internal_path : "<null>",
                       width,
                       height,
                       (unsigned long)raw_size);
            reading_epub_png_free(raw);
            lodepng_state_cleanup(&state);
            goto cleanup;
        }

        reading_epub_scale_png_raw_to_rgb565(raw,
                                             width,
                                             height,
                                             &state.info_png.color,
                                             max_width,
                                             max_height,
                                             out_image);
        reading_epub_png_free(raw);
        lodepng_state_cleanup(&state);
        ok = out_image->data != NULL;
        goto cleanup;
    }

    {
        ok = reading_epub_decode_jpeg(image_data, image_size, max_width, max_height, out_image);
        reading_epub_free_bytes(image_data);
        image_data = NULL;
    }

    if (!ok)
    {
        rt_kprintf("reading_epub: image decode failed internal=%s max=%ux%u\n",
                   internal_path != NULL ? internal_path : "<null>",
                   max_width,
                   max_height);
    }

cleanup:
    if (image_data != NULL) reading_epub_free_bytes(image_data);
    if (zip != NULL) reading_epub_free_zero(zip);
    return ok;
}

bool reading_epub_probe_image_size(const char *epub_path,
                                   const char *internal_path,
                                   uint16_t *width_out,
                                   uint16_t *height_out)
{
    reading_epub_zip_t *zip = NULL;
    uint8_t *image_data = NULL;
    size_t image_size = 0U;
    bool ok = false;

    if (width_out == NULL || height_out == NULL)
    {
        return false;
    }

    *width_out = 0U;
    *height_out = 0U;

    zip = (reading_epub_zip_t *)reading_epub_alloc_zero(sizeof(*zip));
    if (zip == NULL)
    {
        return false;
    }

    if (!reading_epub_zip_open(epub_path, zip) ||
        !reading_epub_zip_read(epub_path, zip, internal_path, &image_data, &image_size))
    {
        goto cleanup;
    }

    if (reading_epub_has_extension(internal_path, ".png"))
    {
        if (image_size >= 24U &&
            memcmp(image_data, "\x89PNG\r\n\x1a\n", 8U) == 0)
        {
            *width_out = (uint16_t)reading_epub_be32(&image_data[16]);
            *height_out = (uint16_t)reading_epub_be32(&image_data[20]);
            ok = (*width_out > 0U && *height_out > 0U);
        }
        goto cleanup;
    }

    ok = reading_epub_probe_jpeg_size(image_data, image_size, width_out, height_out);

cleanup:
    if (image_data != NULL) reading_epub_free_bytes(image_data);
    if (zip != NULL) reading_epub_free_zero(zip);
    return ok;
}

void reading_epub_release_image(lv_image_dsc_t *image)
{
    if (image == NULL)
    {
        return;
    }

    if (image->data != NULL)
    {
        reading_epub_free_image_pixels((void *)image->data);
    }

    memset(image, 0, sizeof(*image));
}
