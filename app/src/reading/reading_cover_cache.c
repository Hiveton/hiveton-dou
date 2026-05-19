#include "reading_cover_cache.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dfs_posix.h"
#define DIR FATFS_DIR
#include "ff.h"
#undef DIR
#include "../app_tf_storage.h"
#include "reading_epub.h"
#include "rtthread.h"

#define READING_COVER_CACHE_MAGIC 0x52564F43U
#define READING_COVER_STATE_MAGIC 0x53564F43U
#define READING_COVER_CACHE_VERSION 1U
#define READING_COVER_CACHE_FORMAT_RGB565 1U
#define READING_COVER_CACHE_PATH_MAX 192U
#define READING_COVER_CACHE_TEMP_SUFFIX ".tmp"
#define READING_COVER_CACHE_BACKUP_SUFFIX ".bak"
#define READING_COVER_CACHE_INDEX_MAX 160U

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

typedef struct
{
    uint32_t source_size;
    uint32_t source_mtime;
    uint16_t width;
    uint16_t height;
    char path[READING_COVER_CACHE_PATH_MAX];
} reading_cover_cache_index_entry_t;

static struct rt_mutex s_reading_cover_cache_mutex;
static bool s_reading_cover_cache_mutex_ready = false;
static bool s_reading_cover_cache_index_ready = false;
static uint16_t s_reading_cover_cache_index_count = 0U;
static reading_cover_cache_index_entry_t s_reading_cover_cache_index[READING_COVER_CACHE_INDEX_MAX];

static bool reading_cover_cache_dir_available(const char *dir)
{
    if (dir == NULL)
    {
        return false;
    }

    return app_tf_storage_ready();
}

static const char *const s_cover_cache_dirs[] = {
    "/books/COVERS",
    "/config/cache/covers",
};

static bool reading_cover_cache_mutex_ready_snapshot(void)
{
    bool ready;

    rt_enter_critical();
    ready = s_reading_cover_cache_mutex_ready;
    rt_exit_critical();

    return ready;
}

static rt_err_t reading_cover_cache_ensure_mutex(void)
{
    rt_err_t result = RT_EOK;

    if (reading_cover_cache_mutex_ready_snapshot())
    {
        return RT_EOK;
    }

    rt_enter_critical();
    if (!s_reading_cover_cache_mutex_ready)
    {
        result = rt_mutex_init(&s_reading_cover_cache_mutex, "cover_cache", RT_IPC_FLAG_PRIO);
        if (result == RT_EOK)
        {
            s_reading_cover_cache_mutex_ready = true;
        }
    }
    rt_exit_critical();

    return result;
}

static bool reading_cover_cache_lock(void)
{
    if (reading_cover_cache_ensure_mutex() != RT_EOK)
    {
        return false;
    }

    return rt_mutex_take(&s_reading_cover_cache_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void reading_cover_cache_unlock(void)
{
    if (reading_cover_cache_mutex_ready_snapshot())
    {
        (void)rt_mutex_release(&s_reading_cover_cache_mutex);
    }
}

static reading_cover_cache_state_t reading_cover_cache_get_state_locked(const char *book_path,
                                                                       uint16_t width,
                                                                       uint16_t height);
static bool reading_cover_cache_load_image_locked(const char *book_path,
                                                uint16_t width,
                                                uint16_t height,
                                                lv_image_dsc_t *out_image);
static reading_cover_cache_state_t reading_cover_cache_build_locked(const char *book_path,
                                                                   uint16_t width,
                                                                   uint16_t height);
static bool reading_cover_read_exact(int fd, void *buffer, size_t size);

static bool reading_cover_make_fat_path(const char *path, char *out, size_t out_size)
{
    int written;

    if (path == NULL || out == NULL || out_size == 0U)
    {
        return false;
    }

    if (path[0] == '/')
    {
        written = rt_snprintf(out, out_size, "0:%s", path);
    }
    else
    {
        written = rt_snprintf(out, out_size, "0:/%s", path);
    }

    return written >= 0 && (size_t)written < out_size;
}

static FRESULT reading_cover_fopen(FIL *file, const char *path, BYTE mode)
{
    char fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (file == NULL || path == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_open(file, path, mode);
    if (result == FR_OK)
    {
        return result;
    }

    if (reading_cover_make_fat_path(path, fat_path, sizeof(fat_path)))
    {
        result = f_open(file, fat_path, mode);
    }
    return result;
}

static FRESULT reading_cover_fstat(const char *path, FILINFO *info)
{
    char fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (path == NULL || info == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_stat(path, info);
    if (result == FR_OK)
    {
        return result;
    }

    if (reading_cover_make_fat_path(path, fat_path, sizeof(fat_path)))
    {
        memset(info, 0, sizeof(*info));
        result = f_stat(fat_path, info);
    }
    return result;
}

static FRESULT reading_cover_fopendir(FATFS_DIR *dir, const char *path)
{
    char fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (dir == NULL || path == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_opendir(dir, path);
    if (result == FR_OK)
    {
        return result;
    }

    if (reading_cover_make_fat_path(path, fat_path, sizeof(fat_path)))
    {
        memset(dir, 0, sizeof(*dir));
        result = f_opendir(dir, fat_path);
    }
    return result;
}

static FRESULT reading_cover_fmkdir(const char *path)
{
    char fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (path == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_mkdir(path);
    if (result == FR_OK || result == FR_EXIST)
    {
        return result;
    }

    if (reading_cover_make_fat_path(path, fat_path, sizeof(fat_path)))
    {
        result = f_mkdir(fat_path);
    }
    return result;
}

static FRESULT reading_cover_funlink(const char *path)
{
    char fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (path == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_unlink(path);
    if (result == FR_OK)
    {
        return result;
    }

    if (reading_cover_make_fat_path(path, fat_path, sizeof(fat_path)))
    {
        result = f_unlink(fat_path);
    }
    return result;
}

static FRESULT reading_cover_frename(const char *old_path, const char *new_path)
{
    char old_fat_path[READING_COVER_CACHE_PATH_MAX];
    char new_fat_path[READING_COVER_CACHE_PATH_MAX];
    FRESULT result;

    if (old_path == NULL || new_path == NULL)
    {
        return FR_INVALID_OBJECT;
    }

    result = f_rename(old_path, new_path);
    if (result == FR_OK)
    {
        return result;
    }

    if (reading_cover_make_fat_path(old_path, old_fat_path, sizeof(old_fat_path)) &&
        reading_cover_make_fat_path(new_path, new_fat_path, sizeof(new_fat_path)))
    {
        result = f_rename(old_fat_path, new_fat_path);
    }
    return result;
}

static bool reading_cover_fread_exact(FIL *file, void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0U;

    if (file == NULL || (buffer == NULL && size > 0U))
    {
        return false;
    }

    while (done < size)
    {
        UINT read_size = 0U;
        size_t chunk = size - done;
        if (chunk > 4096U)
        {
            chunk = 4096U;
        }
        if (f_read(file, bytes + done, (UINT)chunk, &read_size) != FR_OK ||
            read_size == 0U)
        {
            return false;
        }
        done += (size_t)read_size;
    }
    return true;
}

static bool reading_cover_fwrite_exact(FIL *file, const void *buffer, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0U;

    if (file == NULL || (buffer == NULL && size > 0U))
    {
        return false;
    }

    while (done < size)
    {
        UINT written_size = 0U;
        size_t chunk = size - done;
        if (chunk > 4096U)
        {
            chunk = 4096U;
        }
        if (f_write(file, bytes + done, (UINT)chunk, &written_size) != FR_OK ||
            written_size == 0U)
        {
            return false;
        }
        done += (size_t)written_size;
    }
    return true;
}

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

static const char *reading_cover_stable_book_key(const char *book_path)
{
    const char *books_path;

    if (book_path == NULL)
    {
        return "";
    }

    books_path = strstr(book_path, "/books/");
    if (books_path != NULL)
    {
        return books_path + 1;
    }

    if (strncmp(book_path, "books/", 6U) == 0)
    {
        return book_path;
    }

    return book_path;
}

static bool reading_cover_source_key(const char *book_path, reading_cover_source_key_t *key)
{
    FILINFO info;
    FRESULT result;
    uint32_t hash = 2166136261UL;
    const char *stable_key;

    if (book_path == NULL || book_path[0] == '\0' || key == NULL)
    {
        return false;
    }

    memset(&info, 0, sizeof(info));
    result = reading_cover_fstat(book_path, &info);
    if (result != FR_OK || (info.fattrib & AM_DIR) != 0U)
    {
        rt_kprintf("cover_cache: source stat failed path=%s result=%d attr=0x%02x\n",
                   book_path,
                   (int)result,
                   (unsigned int)info.fattrib);
        return false;
    }

    memset(key, 0, sizeof(*key));
    stable_key = reading_cover_stable_book_key(book_path);
    key->source_size = (uint32_t)info.fsize;
    key->source_mtime = 0U;
    hash = reading_cover_hash_update(hash, stable_key, strlen(stable_key));
    hash = reading_cover_hash_update(hash, &key->source_size, sizeof(key->source_size));
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
            (void)reading_cover_fmkdir(buffer);
            *cursor = '/';
        }
        ++cursor;
    }
    (void)reading_cover_fmkdir(buffer);
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
        FILINFO info;
        FRESULT result;
        int written;

        if (!reading_cover_cache_dir_available(s_cover_cache_dirs[i]))
        {
            continue;
        }

        written = rt_snprintf(buffer, buffer_size, "%s/%s", s_cover_cache_dirs[i], filename);
        if (written < 0 || (size_t)written >= buffer_size)
        {
            buffer[0] = '\0';
            continue;
        }

        memset(&info, 0, sizeof(info));
        result = reading_cover_fstat(buffer, &info);
        if (result == FR_OK && (info.fattrib & AM_DIR) == 0U)
        {
            return true;
        }
    }

    buffer[0] = '\0';
    return false;
}

static bool reading_cover_name_has_suffix(const char *name, const char *suffix)
{
    size_t name_len;
    size_t suffix_len;

    if (name == NULL || suffix == NULL)
    {
        return false;
    }

    name_len = strlen(name);
    suffix_len = strlen(suffix);
    return name_len >= suffix_len &&
           strcmp(name + name_len - suffix_len, suffix) == 0;
}

static bool reading_cover_cache_header_valid(const reading_cover_cache_header_t *header,
                                             off_t file_size)
{
    uint32_t expected_data_size;

    if (header == NULL ||
        header->magic != READING_COVER_CACHE_MAGIC ||
        header->version != READING_COVER_CACHE_VERSION ||
        header->format != READING_COVER_CACHE_FORMAT_RGB565 ||
        header->width == 0U ||
        header->height == 0U)
    {
        return false;
    }

    expected_data_size = (uint32_t)header->width * (uint32_t)header->height * 2U;
    return header->data_size == expected_data_size &&
           file_size == (off_t)(sizeof(*header) + expected_data_size);
}

static void reading_cover_cache_index_add(const reading_cover_cache_header_t *header,
                                          const char *path)
{
    reading_cover_cache_index_entry_t *entry;

    if (header == NULL || path == NULL ||
        s_reading_cover_cache_index_count >= READING_COVER_CACHE_INDEX_MAX)
    {
        return;
    }

    entry = &s_reading_cover_cache_index[s_reading_cover_cache_index_count++];
    memset(entry, 0, sizeof(*entry));
    entry->source_size = header->source_size;
    entry->source_mtime = header->source_mtime;
    entry->width = header->width;
    entry->height = header->height;
    rt_snprintf(entry->path, sizeof(entry->path), "%s", path);
}

static void reading_cover_cache_index_scan_dir(const char *dir_path)
{
    FATFS_DIR dir;
    FILINFO info;
    FRESULT result;

    if (dir_path == NULL || s_reading_cover_cache_index_count >= READING_COVER_CACHE_INDEX_MAX)
    {
        return;
    }

    memset(&dir, 0, sizeof(dir));
    memset(&info, 0, sizeof(info));

    result = reading_cover_fopendir(&dir, dir_path);
    if (result != FR_OK)
    {
        rt_kprintf("cover_cache: index opendir failed path=%s result=%d\n", dir_path, (int)result);
        return;
    }

    while (s_reading_cover_cache_index_count < READING_COVER_CACHE_INDEX_MAX)
    {
        char path[READING_COVER_CACHE_PATH_MAX];
        reading_cover_cache_header_t header;
        FIL file;
        FSIZE_t file_size;
        int written;

        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            rt_kprintf("cover_cache: index readdir failed path=%s result=%d\n", dir_path, (int)result);
            break;
        }
        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((info.fattrib & AM_DIR) != 0U || !reading_cover_name_has_suffix(info.fname, ".CVR"))
        {
            continue;
        }

        written = rt_snprintf(path, sizeof(path), "%s/%s", dir_path, info.fname);
        if (written < 0 || (size_t)written >= sizeof(path))
        {
            continue;
        }

        if (reading_cover_fopen(&file, path, FA_READ) != FR_OK)
        {
            continue;
        }

        memset(&header, 0, sizeof(header));
        file_size = f_size(&file);
        if (reading_cover_fread_exact(&file, &header, sizeof(header)) &&
            reading_cover_cache_header_valid(&header, (off_t)file_size))
        {
            reading_cover_cache_index_add(&header, path);
        }
        (void)f_close(&file);
    }

    (void)f_closedir(&dir);
}

static void reading_cover_cache_ensure_index_locked(void)
{
    size_t i;

    if (s_reading_cover_cache_index_ready)
    {
        return;
    }

    s_reading_cover_cache_index_count = 0U;
    memset(s_reading_cover_cache_index, 0, sizeof(s_reading_cover_cache_index));
    for (i = 0U; i < sizeof(s_cover_cache_dirs) / sizeof(s_cover_cache_dirs[0]); ++i)
    {
        if (!reading_cover_cache_dir_available(s_cover_cache_dirs[i]))
        {
            continue;
        }

        reading_cover_cache_index_scan_dir(s_cover_cache_dirs[i]);
    }
    s_reading_cover_cache_index_ready = true;
}

static bool reading_cover_find_compatible_index_locked(const reading_cover_source_key_t *source,
                                                       uint16_t width,
                                                       uint16_t height,
                                                       char *path,
                                                       size_t path_size)
{
    uint16_t i;

    if (source == NULL || path == NULL || path_size == 0U)
    {
        return false;
    }

    reading_cover_cache_ensure_index_locked();
    for (i = 0U; i < s_reading_cover_cache_index_count; ++i)
    {
        const reading_cover_cache_index_entry_t *entry = &s_reading_cover_cache_index[i];

        if (entry->source_size == source->source_size &&
            entry->source_mtime == source->source_mtime &&
            entry->width == width &&
            entry->height == height &&
            entry->path[0] != '\0')
        {
            rt_snprintf(path, path_size, "%s", entry->path);
            return true;
        }
    }

    return false;
}

static bool reading_cover_make_write_path(char *buffer,
                                          size_t buffer_size,
                                          const char *filename)
{
    char temp_path[READING_COVER_CACHE_PATH_MAX];
    size_t i;
    int written;

    if (buffer == NULL || buffer_size == 0U || filename == NULL)
    {
        return false;
    }

    for (i = 0U; i < sizeof(s_cover_cache_dirs) / sizeof(s_cover_cache_dirs[0]); ++i)
    {
        FIL test_file;
        FRESULT result;

        if (!reading_cover_cache_dir_available(s_cover_cache_dirs[i]))
        {
            continue;
        }

        reading_cover_ensure_dir_tree(s_cover_cache_dirs[i]);

        written = rt_snprintf(temp_path,
                              sizeof(temp_path),
                              "%s/%s%s",
                              s_cover_cache_dirs[i],
                              filename,
                              READING_COVER_CACHE_TEMP_SUFFIX);
        if (written < 0 || (size_t)written >= sizeof(temp_path))
        {
            continue;
        }

        (void)reading_cover_funlink(temp_path);
        result = reading_cover_fopen(&test_file, temp_path, FA_WRITE | FA_CREATE_ALWAYS);
        if (result == FR_OK)
        {
            (void)f_close(&test_file);
            (void)reading_cover_funlink(temp_path);

            written = rt_snprintf(buffer, buffer_size, "%s/%s", s_cover_cache_dirs[i], filename);
            if (written < 0 || (size_t)written >= buffer_size)
            {
                return false;
            }

            return true;
        }
        rt_kprintf("cover_cache: write path test failed path=%s result=%d\n",
                   temp_path,
                   (int)result);
    }

    buffer[0] = '\0';
    return false;
}

static bool reading_cover_sync_parent_dir(const char *path)
{
    LV_UNUSED(path);
    return true;
}

static void reading_cover_format_cover_filename(uint32_t dimension_key,
                                                uint16_t width,
                                                uint16_t height,
                                                char *buffer,
                                                size_t buffer_size)
{
    rt_snprintf(buffer,
                buffer_size,
                "%08lX.CVR",
                (unsigned long)dimension_key);
    LV_UNUSED(width);
    LV_UNUSED(height);
}

static void reading_cover_format_state_filename(uint32_t source_key,
                                                char *buffer,
                                                size_t buffer_size)
{
    rt_snprintf(buffer, buffer_size, "%08lX.STA", (unsigned long)source_key);
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
    char backup_path[READING_COVER_CACHE_PATH_MAX];
    int written;

    if (temp_path == NULL || target_path == NULL)
    {
        return false;
    }

    if (reading_cover_frename(temp_path, target_path) == FR_OK)
    {
        return true;
    }

    written = rt_snprintf(backup_path,
                          sizeof(backup_path),
                          "%s%s",
                          target_path,
                          READING_COVER_CACHE_BACKUP_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(backup_path))
    {
        (void)reading_cover_funlink(temp_path);
        return false;
    }

    (void)reading_cover_funlink(backup_path);
    if (reading_cover_frename(target_path, backup_path) != FR_OK)
    {
        (void)reading_cover_funlink(temp_path);
        return false;
    }

    if (reading_cover_frename(temp_path, target_path) == FR_OK)
    {
        (void)reading_cover_funlink(backup_path);
        return true;
    }

    if (reading_cover_frename(backup_path, target_path) != FR_OK)
    {
        rt_kprintf("cover_cache: rollback failed path=%s backup=%s\n", target_path, backup_path);
    }
    (void)reading_cover_funlink(temp_path);
    return false;
}

static bool reading_cover_cache_file_exists(const reading_cover_source_key_t *source,
                                            uint16_t width,
                                            uint16_t height)
{
    char filename[48];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_cache_header_t header;
    FIL file;
    FSIZE_t file_size;
    uint32_t dimension_key;
    uint32_t expected_data_size;

    if (source == NULL)
    {
        return false;
    }

    expected_data_size = (uint32_t)width * (uint32_t)height * 2U;
    dimension_key = reading_cover_dimension_key(source, width, height);
    reading_cover_format_cover_filename(dimension_key, width, height, filename, sizeof(filename));
    if (!reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        if (!reading_cover_find_compatible_index_locked(source, width, height, path, sizeof(path)))
        {
            return false;
        }
    }

    if (reading_cover_fopen(&file, path, FA_READ) != FR_OK)
    {
        rt_kprintf("cover_cache: verify open failed %s\n", path);
        return false;
    }

    file_size = f_size(&file);
    if ((off_t)file_size != (off_t)(sizeof(header) + expected_data_size))
    {
        rt_kprintf("cover_cache: verify size invalid path=%s size=%lu expected=%lu\n",
                   path,
                   (unsigned long)file_size,
                   (unsigned long)(sizeof(header) + expected_data_size));
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return false;
    }

    memset(&header, 0, sizeof(header));
    if (!reading_cover_fread_exact(&file, &header, sizeof(header)) ||
        !reading_cover_cache_header_valid(&header, (off_t)file_size) ||
        header.width != width ||
        header.height != height ||
        header.source_size != source->source_size ||
        header.source_mtime != source->source_mtime)
    {
        rt_kprintf("cover_cache: verify header invalid path=%s magic=0x%08lx ver=%u fmt=%u wh=%ux%u src=%lu/%lu expected=%lu/%lu\n",
                   path,
                   (unsigned long)header.magic,
                   (unsigned int)header.version,
                   (unsigned int)header.format,
                   (unsigned int)header.width,
                   (unsigned int)header.height,
                   (unsigned long)header.source_size,
                   (unsigned long)header.source_mtime,
                   (unsigned long)source->source_size,
                   (unsigned long)source->source_mtime);
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return false;
    }

    (void)f_close(&file);
    return true;
}

static reading_cover_cache_state_t reading_cover_read_state_file(const reading_cover_source_key_t *source)
{
    char filename[32];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_state_record_t record;
    FIL file;

    if (source == NULL)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    reading_cover_format_state_filename(source->key, filename, sizeof(filename));
    if (!reading_cover_find_existing_path(path, sizeof(path), filename))
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    if (reading_cover_fopen(&file, path, FA_READ) != FR_OK)
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    memset(&record, 0, sizeof(record));
    if (!reading_cover_fread_exact(&file, &record, sizeof(record)))
    {
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return READING_COVER_CACHE_UNKNOWN;
    }
    (void)f_close(&file);

    if (record.magic != READING_COVER_STATE_MAGIC ||
        record.version != READING_COVER_CACHE_VERSION ||
        record.source_size != source->source_size ||
        record.source_mtime != source->source_mtime)
    {
        (void)reading_cover_funlink(path);
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

    (void)reading_cover_funlink(path);
    return READING_COVER_CACHE_UNKNOWN;
}

static void reading_cover_write_state_file(const reading_cover_source_key_t *source,
                                           reading_cover_cache_state_t state)
{
    char filename[32];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_state_record_t record;
    FIL file;

    if (source == NULL ||
        (state != READING_COVER_CACHE_NO_COVER && state != READING_COVER_CACHE_FAILED))
    {
        return;
    }

    reading_cover_format_state_filename(source->key, filename, sizeof(filename));
    if (!reading_cover_make_write_path(path, sizeof(path), filename))
    {
        rt_kprintf("cover_cache: no writable state path filename=%s state=%d\n",
                   filename,
                   (int)state);
        return;
    }

    memset(&record, 0, sizeof(record));
    record.magic = READING_COVER_STATE_MAGIC;
    record.version = READING_COVER_CACHE_VERSION;
    record.state = (uint16_t)state;
    record.source_size = source->source_size;
    record.source_mtime = source->source_mtime;

    if (reading_cover_fopen(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return;
    }
    if (!reading_cover_fwrite_exact(&file, &record, sizeof(record)) ||
        f_sync(&file) != FR_OK)
    {
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return;
    }

    if (f_close(&file) != FR_OK)
    {
        (void)reading_cover_funlink(path);
        return;
    }
    if (!reading_cover_sync_parent_dir(path))
    {
        rt_kprintf("cover_cache: sync parent dir failed path=%s\n", path);
    }
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
        (void)reading_cover_funlink(path);
    }
}

static reading_cover_cache_state_t reading_cover_cache_get_state_locked(const char *book_path,
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

static bool reading_cover_cache_load_image_locked(const char *book_path,
                                                uint16_t width,
                                                uint16_t height,
                                                lv_image_dsc_t *out_image)
{
    reading_cover_source_key_t source;
    char filename[48];
    char path[READING_COVER_CACHE_PATH_MAX];
    reading_cover_cache_header_t header;
    uint8_t *pixels = NULL;
    FIL file;

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
        if (!reading_cover_find_compatible_index_locked(&source, width, height, path, sizeof(path)))
        {
            return false;
        }
    }

    if (reading_cover_fopen(&file, path, FA_READ) != FR_OK)
    {
        return false;
    }

    memset(&header, 0, sizeof(header));
    if (!reading_cover_fread_exact(&file, &header, sizeof(header)) ||
        !reading_cover_cache_header_valid(&header, (off_t)(sizeof(header) + header.data_size)) ||
        header.width != width ||
        header.height != height ||
        header.source_size != source.source_size ||
        header.source_mtime != source.source_mtime ||
        header.data_size != (uint32_t)width * (uint32_t)height * 2U)
    {
        (void)f_close(&file);
        return false;
    }

    pixels = (uint8_t *)lv_malloc(header.data_size);
    if (pixels == NULL)
    {
        (void)f_close(&file);
        return false;
    }

    if (!reading_cover_fread_exact(&file, pixels, header.data_size))
    {
        (void)f_close(&file);
        lv_free(pixels);
        return false;
    }
    (void)f_close(&file);

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
    reading_cover_cache_header_t header;
    FIL file;

    LV_UNUSED(book_path);

    if (source == NULL ||
        image == NULL ||
        image->data == NULL ||
        image->data_size != (uint32_t)width * (uint32_t)height * 2U)
    {
        rt_kprintf("cover_cache: invalid image write expected=%lu actual=%lu\n",
                   (unsigned long)((uint32_t)width * (uint32_t)height * 2U),
                   (unsigned long)(image != NULL ? image->data_size : 0U));
        return false;
    }

    reading_cover_format_cover_filename(reading_cover_dimension_key(source, width, height),
                                        width,
                                        height,
                                        filename,
                                        sizeof(filename));
    if (!reading_cover_make_write_path(path, sizeof(path), filename))
    {
        rt_kprintf("cover_cache: no writable cache path filename=%s\n", filename);
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

    if (reading_cover_fopen(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        rt_kprintf("cover_cache: open write failed %s\n", path);
        return false;
    }

    if (!reading_cover_fwrite_exact(&file, &header, sizeof(header)) ||
        !reading_cover_fwrite_exact(&file, image->data, image->data_size))
    {
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return false;
    }

    if (f_sync(&file) != FR_OK)
    {
        (void)f_close(&file);
        (void)reading_cover_funlink(path);
        return false;
    }

    if (f_close(&file) != FR_OK)
    {
        (void)reading_cover_funlink(path);
        return false;
    }

    if (!reading_cover_sync_parent_dir(path))
    {
        rt_kprintf("cover_cache: sync parent dir failed path=%s\n", path);
    }

    if (!reading_cover_cache_file_exists(source, width, height))
    {
        rt_kprintf("cover_cache: post-write verify failed path=%s\n", path);
        return false;
    }

    if (s_reading_cover_cache_index_ready)
    {
        reading_cover_cache_index_add(&header, path);
    }

    rt_kprintf("cover_cache: wrote %s size=%lu\n",
               path,
               (unsigned long)image->data_size);
    return true;
}

static reading_cover_cache_state_t reading_cover_cache_build_locked(const char *book_path,
                                                                   uint16_t width,
                                                                   uint16_t height)
{
    reading_cover_source_key_t source;
    char cover_internal_path[READING_EPUB_MAX_INTERNAL_PATH];
    lv_image_dsc_t image;
    reading_cover_cache_state_t state;
    reading_epub_cover_result_t cover_result;

    if (!reading_cover_source_key(book_path, &source) || width == 0U || height == 0U)
    {
        return READING_COVER_CACHE_FAILED;
    }

    state = reading_cover_cache_get_state_locked(book_path, width, height);
    if (state == READING_COVER_CACHE_READY ||
        state == READING_COVER_CACHE_NO_COVER)
    {
        return state;
    }

    memset(&image, 0, sizeof(image));
    cover_internal_path[0] = '\0';
    cover_result = reading_epub_find_cover_image_result(book_path, cover_internal_path, sizeof(cover_internal_path));
    if (cover_result != READING_EPUB_COVER_FOUND)
    {
        if (cover_result == READING_EPUB_COVER_NOT_FOUND)
        {
            rt_kprintf("cover_cache: no epub cover path=%s\n", book_path);
            reading_cover_write_state_file(&source, READING_COVER_CACHE_NO_COVER);
            return READING_COVER_CACHE_NO_COVER;
        }

        rt_kprintf("cover_cache: epub cover probe failed result=%d path=%s\n",
                   (int)cover_result,
                   book_path);
        reading_cover_write_state_file(&source, READING_COVER_CACHE_FAILED);
        return READING_COVER_CACHE_FAILED;
    }

    rt_kprintf("cover_cache: decode start path=%s internal=%s\n", book_path, cover_internal_path);
    if (!reading_epub_decode_image(book_path, cover_internal_path, width, height, &image))
    {
        rt_kprintf("cover_cache: decode unavailable path=%s internal=%s\n", book_path, cover_internal_path);
        reading_cover_write_state_file(&source, READING_COVER_CACHE_NO_COVER);
        return READING_COVER_CACHE_NO_COVER;
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

reading_cover_cache_state_t reading_cover_cache_get_state(const char *book_path,
                                                          uint16_t width,
                                                          uint16_t height)
{
    reading_cover_cache_state_t state = READING_COVER_CACHE_UNKNOWN;

    if (!reading_cover_cache_lock())
    {
        return READING_COVER_CACHE_UNKNOWN;
    }

    state = reading_cover_cache_get_state_locked(book_path, width, height);
    reading_cover_cache_unlock();

    return state;
}

bool reading_cover_cache_load_image(const char *book_path,
                                   uint16_t width,
                                   uint16_t height,
                                   lv_image_dsc_t *out_image)
{
    bool result = false;

    if (!reading_cover_cache_lock())
    {
        return false;
    }

    result = reading_cover_cache_load_image_locked(book_path, width, height, out_image);
    reading_cover_cache_unlock();

    return result;
}

reading_cover_cache_state_t reading_cover_cache_build(const char *book_path,
                                                     uint16_t width,
                                                     uint16_t height)
{
    reading_cover_cache_state_t state = READING_COVER_CACHE_UNKNOWN;

    if (!reading_cover_cache_lock())
    {
        return READING_COVER_CACHE_FAILED;
    }

    state = reading_cover_cache_build_locked(book_path, width, height);
    reading_cover_cache_unlock();

    return state;
}
