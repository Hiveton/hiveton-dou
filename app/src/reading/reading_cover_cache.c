#include "reading_cover_cache.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dfs_posix.h"
#include "reading_epub.h"
#include "rtthread.h"

#define READING_COVER_CACHE_MAGIC 0x52564F43U
#define READING_COVER_STATE_MAGIC 0x53564F43U
#define READING_COVER_CACHE_VERSION 1U
#define READING_COVER_CACHE_FORMAT_RGB565 1U
#define READING_COVER_CACHE_PATH_MAX 192U
#define READING_COVER_CACHE_TEMP_SUFFIX ".tmp"

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint32_t source_size;
    uint32_t source_mtime;
    uint32_t data_size;
} reading_cover_cache_header_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t state;
    uint32_t source_size;
    uint32_t source_mtime;
} reading_cover_state_record_t;

typedef struct
{
    uint32_t key;
    uint32_t source_size;
    uint32_t source_mtime;
} reading_cover_source_key_t;

static const char *const s_cover_cache_dirs[] = {
    "/config/cache/covers",
    "/cache/covers",
    "/tf/config/cache/covers",
    "/sd/config/cache/covers",
    "/sd0/config/cache/covers",
};

static uint32_t reading_cover_hash_update(uint32_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;

    while (size-- > 0U)
    {
        hash ^= (uint32_t)(*bytes++);
        hash *= 16777619UL;
    }

    return hash;
}

static bool reading_cover_source_key(const char *book_path, reading_cover_source_key_t *key)
{
    struct stat st;
    uint32_t hash = 2166136261UL;

    if (book_path == NULL || book_path[0] == '\0' || key == NULL)
    {
        return false;
    }

    if (stat(book_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        return false;
    }

    memset(key, 0, sizeof(*key));
    key->source_size = (uint32_t)st.st_size;
    key->source_mtime = (uint32_t)st.st_mtime;
    hash = reading_cover_hash_update(hash, book_path, strlen(book_path));
    hash = reading_cover_hash_update(hash, &key->source_size, sizeof(key->source_size));
    hash = reading_cover_hash_update(hash, &key->source_mtime, sizeof(key->source_mtime));
    key->key = hash;
    return true;
}

static uint32_t reading_cover_dimension_key(const reading_cover_source_key_t *source,
                                            uint16_t width,
                                            uint16_t height)
{
    uint32_t hash;

    if (source == NULL)
    {
        return 0U;
    }

    hash = source->key;
    hash = reading_cover_hash_update(hash, &width, sizeof(width));
    hash = reading_cover_hash_update(hash, &height, sizeof(height));
    return hash;
}

static void reading_cover_ensure_dir_tree(const char *path)
{
    char buffer[READING_COVER_CACHE_PATH_MAX];
    char *cursor;
    int written;

    if (path == NULL || path[0] != '/')
    {
        return;
    }

    written = rt_snprintf(buffer, sizeof(buffer), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(buffer))
    {
        return;
    }

    cursor = buffer + 1;
    while (*cursor != '\0')
    {
        if (*cursor == '/')
        {
            *cursor = '\0';
            (void)mkdir(buffer, 0);
            *cursor = '/';
        }
        ++cursor;
    }
    (void)mkdir(buffer, 0);
}

static bool reading_cover_find_existing_path(char *buffer,
                                             size_t buffer_size,
                                             const char *filename)
{
    size_t i;

    if (buffer == NULL || buffer_size == 0U || filename == NULL)
    {
        return false;
    }

    for (i = 0U; i < sizeof(s_cover_cache_dirs) / sizeof(s_cover_cache_dirs[0]); ++i)
    {
        struct stat st;
        int written;

        written = rt_snprintf(buffer, buffer_size, "%s/%s", s_cover_cache_dirs[i], filename);
        if (written < 0 || (size_t)written >= buffer_size)
        {
            buffer[0] = '\0';
            continue;
        }

        if (stat(buffer, &st) == 0 && S_ISREG(st.st_mode))
        {
            return true;
        }
    }

    buffer[0] = '\0';
    return false;
}

static bool reading_cover_make_write_path(char *buffer,
                                          size_t buffer_size,
                                          const char *filename)
{
    int written;

    if (buffer == NULL || buffer_size == 0U || filename == NULL)
    {
        return false;
    }

    reading_cover_ensure_dir_tree(s_cover_cache_dirs[0]);
    written = rt_snprintf(buffer, buffer_size, "%s/%s", s_cover_cache_dirs[0], filename);
    if (written < 0 || (size_t)written >= buffer_size)
    {
        buffer[0] = '\0';
        return false;
    }

    return buffer[0] != '\0';
}

static void reading_cover_format_cover_filename(uint32_t dimension_key,
                                                uint16_t width,
                                                uint16_t height,
                                                char *buffer,
                                                size_t buffer_size)
{
    rt_snprintf(buffer,
                buffer_size,
                "%08lx_%ux%u.cover",
                (unsigned long)dimension_key,
                (unsigned int)width,
                (unsigned int)height);
}

static void reading_cover_format_state_filename(uint32_t source_key,
                                                char *buffer,
                                                size_t buffer_size)
{
    rt_snprintf(buffer, buffer_size, "%08lx.state", (unsigned long)source_key);
}

static bool reading_cover_read_exact(int fd, void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0U;

    while (done < size)
    {
        ssize_t n = read(fd, bytes + done, size - done);
        if (n <= 0)
        {
            return false;
        }
        done += (size_t)n;
    }

    return true;
}

static bool reading_cover_write_exact(int fd, const void *buffer, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0U;

    while (done < size)
    {
        ssize_t n = write(fd, bytes + done, size - done);
        if (n <= 0)
        {
            return false;
        }
        done += (size_t)n;
    }

    return true;
}

static bool reading_cover_atomic_rename(const char *temp_path, const char *target_path)
{
    if (rename(temp_path, target_path) == 0)
    {
        return true;
    }

    (void)unlink(target_path);
    if (rename(temp_path, target_path) == 0)
    {
        return true;
    }

    (void)unlink(temp_path);
    return false;
}

static bool reading_cover_cache_file_exists(const reading_cover_source_key_t *source,
                                            uint16_t width,
                                            uint16_t height)
{
    char filename[48];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_cache_header_t header;
    struct stat st;
    uint32_t dimension_key;
    uint32_t expected_data_size;
    int fd;

    if (source == NULL)
    {
        return false;
    }

    dimension_key = reading_cover_dimension_key(source, width, height);
    reading_cover_format_cover_filename(dimension_key, width, height, filename, sizeof(filename));
    if (!reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        return false;
    }

    expected_data_size = (uint32_t)width * (uint32_t)height * 2U;
    if (stat(path, &st) != 0 ||
        st.st_size != (off_t)(sizeof(header) + expected_data_size))
    {
        (void)unlink(path);
        return false;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    memset(&header, 0, sizeof(header));
    if (!reading_cover_read_exact(fd, &header, sizeof(header)) ||
        header.magic != READING_COVER_CACHE_MAGIC ||
        header.version != READING_COVER_CACHE_VERSION ||
        header.width != width ||
        header.height != height ||
        header.format != READING_COVER_CACHE_FORMAT_RGB565 ||
        header.source_size != source->source_size ||
        header.source_mtime != source->source_mtime ||
        header.data_size != expected_data_size)
    {
        close(fd);
        (void)unlink(path);
        return false;
    }

    close(fd);
    return true;
}

static reading_cover_cache_state_t reading_cover_read_state_file(const reading_cover_source_key_t *source)
{
    char filename[32];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_state_record_t record;
    int fd;

    if (source == NULL)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    reading_cover_format_state_filename(source->key, filename, sizeof(filename));
    if (!reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    memset(&record, 0, sizeof(record));
    if (!reading_cover_read_exact(fd, &record, sizeof(record)))
    {
        close(fd);
        return READING_COVER_CACHE_UNKNOWN;
    }
    close(fd);

    if (record.magic != READING_COVER_STATE_MAGIC ||
        record.version != READING_COVER_CACHE_VERSION ||
        record.source_size != source->source_size ||
        record.source_mtime != source->source_mtime)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    if (record.state == READING_COVER_CACHE_NO_COVER)
    {
        return READING_COVER_CACHE_NO_COVER;
    }
    if (record.state == READING_COVER_CACHE_FAILED)
    {
        return READING_COVER_CACHE_FAILED;
    }

    return READING_COVER_CACHE_UNKNOWN;
}

static void reading_cover_write_state_file(const reading_cover_source_key_t *source,
                                           reading_cover_cache_state_t state)
{
    char filename[32];
    char path[READING_COVER_CACHE_PATH_MAX];
    char temp_path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_state_record_t record;
    int fd;
    int written;

    if (source == NULL ||
        (state != READING_COVER_CACHE_NO_COVER && state != READING_COVER_CACHE_FAILED))
    {
        return;
    }

    reading_cover_format_state_filename(source->key, filename, sizeof(filename));
    if (!reading_cover_make_write_path(path, sizeof(path), filename))
    {
        return;
    }
    written = rt_snprintf(temp_path, sizeof(temp_path), "%s%s", path, READING_COVER_CACHE_TEMP_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(temp_path))
    {
        return;
    }

    memset(&record, 0, sizeof(record));
    record.magic = READING_COVER_STATE_MAGIC;
    record.version = READING_COVER_CACHE_VERSION;
    record.state = (uint16_t)state;
    record.source_size = source->source_size;
    record.source_mtime = source->source_mtime;

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        return;
    }
    if (!reading_cover_write_exact(fd, &record, sizeof(record)) ||
        fsync(fd) != 0)
    {
        (void)unlink(temp_path);
        (void)close(fd);
        return;
    }

    if (close(fd) != 0)
    {
        (void)unlink(temp_path);
        return;
    }

    (void)reading_cover_atomic_rename(temp_path, path);
}

static void reading_cover_remove_state_file(const reading_cover_source_key_t *source)
{
    char filename[32];
    char path[READING_COVER_CACHE_PATH_MAX];

    if (source == NULL)
    {
        return;
    }

    reading_cover_format_state_filename(source->key, filename, sizeof(filename));
    if (reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        (void)unlink(path);
    }
}

reading_cover_cache_state_t reading_cover_cache_get_state(const char *book_path,
                                                          uint16_t width,
                                                          uint16_t height)
{
    reading_cover_source_key_t source;
    reading_cover_cache_state_t state;

    if (!reading_cover_source_key(book_path, &source) || width == 0U || height == 0U)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    if (reading_cover_cache_file_exists(&source, width, height))
    {
        return READING_COVER_CACHE_READY;
    }

    state = reading_cover_read_state_file(&source);
    if (state != READING_COVER_CACHE_UNKNOWN)
    {
        return state;
    }

    return READING_COVER_CACHE_UNKNOWN;
}

bool reading_cover_cache_load_image(const char *book_path,
                                    uint16_t width,
                                    uint16_t height,
                                    lv_image_dsc_t *out_image)
{
    reading_cover_source_key_t source;
    char filename[48];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_cache_header_t header;
    uint8_t *pixels = NULL;
    int fd;

    if (out_image == NULL)
    {
        return false;
    }

    memset(out_image, 0, sizeof(*out_image));
    if (!reading_cover_source_key(book_path, &source) || width == 0U || height == 0U)
    {
        return false;
    }

    reading_cover_format_cover_filename(reading_cover_dimension_key(&source, width, height),
                                        width,
                                        height,
                                        filename,
                                        sizeof(filename));
    if (!reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        return false;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return false;
    }

    memset(&header, 0, sizeof(header));
    if (!reading_cover_read_exact(fd, &header, sizeof(header)) ||
        header.magic != READING_COVER_CACHE_MAGIC ||
        header.version != READING_COVER_CACHE_VERSION ||
        header.width != width ||
        header.height != height ||
        header.format != READING_COVER_CACHE_FORMAT_RGB565 ||
        header.source_size != source.source_size ||
        header.source_mtime != source.source_mtime ||
        header.data_size != (uint32_t)width * (uint32_t)height * 2U)
    {
        close(fd);
        return false;
    }

    pixels = (uint8_t *)lv_malloc(header.data_size);
    if (pixels == NULL)
    {
        close(fd);
        return false;
    }

    if (!reading_cover_read_exact(fd, pixels, header.data_size))
    {
        close(fd);
        lv_free(pixels);
        return false;
    }
    close(fd);

    out_image->header.magic = LV_IMAGE_HEADER_MAGIC;
    out_image->header.cf = LV_COLOR_FORMAT_RGB565;
    out_image->header.w = (lv_coord_t)width;
    out_image->header.h = (lv_coord_t)height;
    out_image->header.stride = (lv_coord_t)(width * 2U);
    out_image->data_size = header.data_size;
    out_image->data = pixels;
    return true;
}

static bool reading_cover_cache_write_image(const char *book_path,
                                            const reading_cover_source_key_t *source,
                                            uint16_t width,
                                            uint16_t height,
                                            const lv_image_dsc_t *image)
{
    char filename[48];
    char path[READING_COVER_CACHE_PATH_MAX];
    char temp_path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_cache_header_t header;
    int fd;
    int written;

    LV_UNUSED(book_path);

    if (source == NULL ||
        image == NULL ||
        image->data == NULL ||
        image->data_size != (uint32_t)width * (uint32_t)height * 2U)
    {
        return false;
    }

    reading_cover_format_cover_filename(reading_cover_dimension_key(source, width, height),
                                        width,
                                        height,
                                        filename,
                                        sizeof(filename));
    if (!reading_cover_make_write_path(path, sizeof(path), filename))
    {
        return false;
    }
    written = rt_snprintf(temp_path, sizeof(temp_path), "%s%s", path, READING_COVER_CACHE_TEMP_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(temp_path))
    {
        return false;
    }

    memset(&header, 0, sizeof(header));
    header.magic = READING_COVER_CACHE_MAGIC;
    header.version = READING_COVER_CACHE_VERSION;
    header.width = width;
    header.height = height;
    header.format = READING_COVER_CACHE_FORMAT_RGB565;
    header.source_size = source->source_size;
    header.source_mtime = source->source_mtime;
    header.data_size = image->data_size;

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        rt_kprintf("cover_cache: open write failed %s\n", temp_path);
        return false;
    }

    if (!reading_cover_write_exact(fd, &header, sizeof(header)) ||
        !reading_cover_write_exact(fd, image->data, image->data_size))
    {
        close(fd);
        (void)unlink(temp_path);
        return false;
    }

    if (fsync(fd) != 0)
    {
        close(fd);
        (void)unlink(temp_path);
        return false;
    }

    if (close(fd) != 0)
    {
        (void)unlink(temp_path);
        return false;
    }

    return reading_cover_atomic_rename(temp_path, path);
}

reading_cover_cache_state_t reading_cover_cache_build(const char *book_path,
                                                      uint16_t width,
                                                      uint16_t height)
{
    reading_cover_source_key_t source;
    char cover_internal_path[READING_EPUB_MAX_INTERNAL_PATH];
    lv_image_dsc_t image;
    reading_cover_cache_state_t state;

    if (!reading_cover_source_key(book_path, &source) || width == 0U || height == 0U)
    {
        return READING_COVER_CACHE_FAILED;
    }

    state = reading_cover_cache_get_state(book_path, width, height);
    if (state == READING_COVER_CACHE_READY ||
        state == READING_COVER_CACHE_NO_COVER)
    {
        return state;
    }

    memset(&image, 0, sizeof(image));
    cover_internal_path[0] = '\0';
    if (!reading_epub_find_cover_image(book_path, cover_internal_path, sizeof(cover_internal_path)))
    {
        reading_cover_write_state_file(&source, READING_COVER_CACHE_NO_COVER);
        return READING_COVER_CACHE_NO_COVER;
    }

    if (!reading_epub_decode_image(book_path, cover_internal_path, width, height, &image))
    {
        reading_cover_write_state_file(&source, READING_COVER_CACHE_FAILED);
        return READING_COVER_CACHE_FAILED;
    }

    if (!reading_cover_cache_write_image(book_path, &source, width, height, &image))
    {
        reading_epub_release_image(&image);
        reading_cover_write_state_file(&source, READING_COVER_CACHE_FAILED);
        return READING_COVER_CACHE_FAILED;
    }

    reading_epub_release_image(&image);
    reading_cover_remove_state_file(&source);
    return READING_COVER_CACHE_READY;
}

void reading_cover_cache_release_image(lv_image_dsc_t *image)
{
    if (image == NULL)
    {
        return;
    }

    if (image->data != NULL)
    {
        lv_free((void *)image->data);
    }
    memset(image, 0, sizeof(*image));
}
