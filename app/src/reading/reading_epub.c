#include "reading_epub.h"

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
#define READING_EPUB_IMAGE_PSRAM_HEAP_SIZE (3 * 1024 * 1024U)
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
} reading_epub_image_mem_hdr_t;

typedef struct
{
    unsigned src_width;
    unsigned src_height;
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

static lv_color_t *reading_epub_alloc_image_pixels(size_t pixel_count)
{
    reading_epub_image_mem_hdr_t *hdr = NULL;
    rt_size_t alloc_size;

    if (pixel_count == 0U)
    {
        return NULL;
    }

    alloc_size = RT_ALIGN(sizeof(*hdr) + pixel_count * sizeof(lv_color_t), RT_ALIGN_SIZE);
    if (s_reading_epub_image_psram_heap_ready)
    {
        hdr = (reading_epub_image_mem_hdr_t *)rt_memheap_alloc(&s_reading_epub_image_psram_heap,
                                                               alloc_size);
        if (hdr != NULL)
        {
            hdr->magic = READING_EPUB_IMAGE_MEM_MAGIC_PSRAM;
            return (lv_color_t *)(hdr + 1);
        }
    }

    hdr = (reading_epub_image_mem_hdr_t *)rt_malloc(alloc_size);
    if (hdr == NULL)
    {
        return NULL;
    }

    hdr->magic = READING_EPUB_IMAGE_MEM_MAGIC_SYS;
    return (lv_color_t *)(hdr + 1);
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

static bool reading_epub_zip_open(const char *epub_path, reading_epub_zip_t *zip)
{
    int fd;
    off_t file_size;
    off_t tail_offset;
    uint8_t *tail_buffer;
    ssize_t tail_read;
    ssize_t i;
    uint32_t central_dir_offset = 0U;
    uint16_t entry_count = 0U;
    uint32_t offset;

    if (epub_path == NULL || zip == NULL)
    {
        return false;
    }

    memset(zip, 0, sizeof(*zip));

    fd = open(epub_path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0)
    {
        close(fd);
        return false;
    }

    tail_offset = file_size > (off_t)READING_EPUB_ZIP_TAIL_BYTES ?
                      (file_size - (off_t)READING_EPUB_ZIP_TAIL_BYTES) :
                      0;
    if (lseek(fd, tail_offset, SEEK_SET) < 0)
    {
        close(fd);
        return false;
    }

    tail_buffer = (uint8_t *)rt_malloc((size_t)(file_size - tail_offset));
    if (tail_buffer == NULL)
    {
        close(fd);
        return false;
    }

    tail_read = read(fd, tail_buffer, (size_t)(file_size - tail_offset));
    close(fd);
    if (tail_read <= 0)
    {
        rt_free(tail_buffer);
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

    rt_free(tail_buffer);

    if (entry_count == 0U || central_dir_offset == 0U)
    {
        return false;
    }

    fd = open(epub_path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    if (lseek(fd, (off_t)central_dir_offset, SEEK_SET) < 0)
    {
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
            break;
        }

        if (reading_epub_le32(header) != 0x02014B50UL)
        {
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
    output_buffer = (uint8_t *)reading_epub_alloc_bytes(entry->uncompressed_size + 1U);
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
    uint16_t gray = (uint16_t)(r * 30U + g * 59U + b * 11U) / 100U;
    uint8_t threshold = (uint8_t)(reading_epub_bayer8_value(x, y) * 4U + 2U);
    uint8_t bw = gray >= threshold ? 255U : 0U;

    return lv_color_make(bw, bw, bw);
}

static void reading_epub_scale_rgb_to_rgb565(const unsigned char *src,
                                             unsigned src_width,
                                             unsigned src_height,
                                             unsigned src_channels,
                                             uint16_t max_width,
                                             uint16_t max_height,
                                             lv_image_dsc_t *out_image)
{
    lv_color_t *pixels;

    (void)max_width;
    (void)max_height;

    pixels = reading_epub_alloc_image_pixels((size_t)src_width * (size_t)src_height);
    if (pixels == NULL)
    {
        rt_kprintf("reading_epub: image alloc failed src=%ux%u\n",
                   src_width, src_height);
        memset(out_image, 0, sizeof(*out_image));
        return;
    }

    for (unsigned y = 0; y < src_height; ++y)
    {
        for (unsigned x = 0; x < src_width; ++x)
        {
            const unsigned char *pixel = &src[(y * src_width + x) * src_channels];

            pixels[y * src_width + x] =
                reading_epub_dither_to_bw_color(x, y, pixel[0], pixel[1], pixel[2]);
        }
    }

    memset(out_image, 0, sizeof(*out_image));
    out_image->header.magic = LV_IMAGE_HEADER_MAGIC;
    out_image->header.cf = LV_COLOR_FORMAT_RGB565;
    out_image->header.w = (lv_coord_t)src_width;
    out_image->header.h = (lv_coord_t)src_height;
    out_image->header.stride = (lv_coord_t)(src_width * sizeof(lv_color_t));
    out_image->data_size = src_width * src_height * sizeof(lv_color_t);
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
        unsigned dst_y = sy;
        unsigned row = sy - rect->top;

        if (dst_y >= target->target_height)
        {
            continue;
        }

        for (unsigned sx = rect->left; sx <= rect->right; ++sx)
        {
            unsigned dst_x = sx;
            unsigned col = sx - rect->left;
            const uint8_t *pixel = &src[(row * rect_width + col) * 3U];

            if (dst_x >= target->target_width)
            {
                continue;
            }

            target->target_pixels[dst_y * target->target_width + dst_x] =
                reading_epub_dither_to_bw_color(dst_x, dst_y, pixel[2], pixel[1], pixel[0]);
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
    unsigned decode_width;
    unsigned decode_height;
    unsigned target_width;
    (void)max_height;

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

    target_width = max_width > 0U ? max_width : (unsigned)jd.width;
    while (decode_scale < 3U)
    {
        unsigned next_width = ((unsigned)jd.width + ((1U << (decode_scale + 1U)) - 1U)) >>
                              (decode_scale + 1U);
        if (next_width < target_width)
        {
            break;
        }
        ++decode_scale;
    }

    decode_width = ((unsigned)jd.width + ((1U << decode_scale) - 1U)) >> decode_scale;
    decode_height = ((unsigned)jd.height + ((1U << decode_scale) - 1U)) >> decode_scale;
    if (decode_width == 0U || decode_height == 0U)
    {
        rt_free(workbuf);
        return false;
    }

    pixels = reading_epub_alloc_image_pixels((size_t)decode_width * (size_t)decode_height);
    if (pixels == NULL)
    {
        rt_kprintf("reading_epub: jpeg alloc failed src=%ux%u decode=%ux%u scale=%u\n",
                   jd.width,
                   jd.height,
                   decode_width,
                   decode_height,
                   decode_scale);
        rt_free(workbuf);
        return false;
    }

    memset(pixels, 0xFF, decode_width * decode_height * sizeof(lv_color_t));
    context.target.src_width = decode_width;
    context.target.src_height = decode_height;
    context.target.target_width = decode_width;
    context.target.target_height = decode_height;
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
    out_image->data_size = context.target.target_width * context.target.target_height * sizeof(lv_color_t);
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
        unsigned char *rgb = NULL;
        unsigned width = 0U;
        unsigned height = 0U;
        unsigned error = lodepng_decode24(&rgb, &width, &height, image_data, image_size);

        reading_epub_free_bytes(image_data);
        image_data = NULL;
        if (error != 0U || rgb == NULL)
        {
            rt_kprintf("reading_epub: png decode failed error=%u internal=%s\n",
                       error,
                       internal_path != NULL ? internal_path : "<null>");
            if (rgb != NULL) lv_free(rgb);
            goto cleanup;
        }

        reading_epub_scale_rgb_to_rgb565(rgb, width, height, 3U, 0U, 0U, out_image);
        lv_free(rgb);
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
