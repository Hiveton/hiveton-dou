#include "reading_state.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dfs_posix.h"

#define READING_STATE_FILE_NAME "reading_state.cfg"
#define READING_STATE_TEMP_SUFFIX ".tmp"
#define READING_STATE_STORAGE_PATH_MAX 320U
#define READING_STATE_LINE_MAX 2048U

typedef struct
{
    reading_book_state_t books[READING_STATE_MAX_BOOKS];
    uint16_t count;
    uint32_t sequence_counter;
    bool dirty;
    bool loaded;
    char storage_path[READING_STATE_STORAGE_PATH_MAX];
} reading_state_db_t;

static const char *const s_reading_state_config_dirs[] = {
    "/config",
    "/tf/config",
    "/sd/config",
    "/sd0/config",
    "config",
};

static reading_state_db_t s_reading_state_db;
static struct rt_mutex s_reading_state_mutex;
static bool s_reading_state_mutex_ready = false;
static char s_reading_state_line[READING_STATE_LINE_MAX];

static rt_err_t reading_state_ensure_mutex(void)
{
    rt_err_t result;

    if (s_reading_state_mutex_ready)
    {
        return RT_EOK;
    }

    result = rt_mutex_init(&s_reading_state_mutex, "rdstate", RT_IPC_FLAG_PRIO);
    if (result == RT_EOK)
    {
        s_reading_state_mutex_ready = true;
    }

    return result;
}

static bool reading_state_lock(void)
{
    if (reading_state_ensure_mutex() != RT_EOK)
    {
        return false;
    }

    return rt_mutex_take(&s_reading_state_mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void reading_state_unlock(void)
{
    if (s_reading_state_mutex_ready)
    {
        (void)rt_mutex_release(&s_reading_state_mutex);
    }
}

static void reading_state_reset_content_locked(void)
{
    memset(s_reading_state_db.books, 0, sizeof(s_reading_state_db.books));
    s_reading_state_db.count = 0U;
    s_reading_state_db.sequence_counter = 0U;
    s_reading_state_db.dirty = false;
    s_reading_state_db.loaded = true;
}

static bool reading_state_copy_path(char *out, size_t out_size, const char *path)
{
    size_t len;

    if (out == NULL || out_size == 0U || path == NULL || path[0] == '\0')
    {
        return false;
    }

    len = strlen(path);
    if (len >= out_size)
    {
        return false;
    }

    memcpy(out, path, len + 1U);
    return true;
}

static void reading_state_set_title_from_path(reading_book_state_t *book)
{
    const char *base;
    const char *dot;
    size_t len;

    if (book == NULL)
    {
        return;
    }

    base = strrchr(book->path, '/');
    if (base != NULL)
    {
        base++;
    }
    else
    {
        base = book->path;
    }

    if (base[0] == '\0')
    {
        base = book->path;
    }

    dot = strrchr(base, '.');
    if (dot != NULL && dot > base)
    {
        len = (size_t)(dot - base);
    }
    else
    {
        len = strlen(base);
    }

    if (len == 0U)
    {
        base = book->path;
        len = strlen(base);
    }

    if (len >= READING_STATE_TITLE_MAX)
    {
        len = READING_STATE_TITLE_MAX - 1U;
    }

    memcpy(book->title, base, len);
    book->title[len] = '\0';
}

static int reading_state_find_index_locked(const char *path)
{
    uint16_t i;

    if (path == NULL || path[0] == '\0')
    {
        return -1;
    }

    for (i = 0U; i < s_reading_state_db.count; ++i)
    {
        if (strcmp(s_reading_state_db.books[i].path, path) == 0)
        {
            return (int)i;
        }
    }

    return -1;
}

static bool reading_state_is_older(const reading_book_state_t *candidate, const reading_book_state_t *current)
{
    int cmp;

    if (current == NULL)
    {
        return true;
    }

    if (candidate->last_read_at != current->last_read_at)
    {
        return candidate->last_read_at < current->last_read_at;
    }

    if (candidate->open_count != current->open_count)
    {
        return candidate->open_count < current->open_count;
    }

    cmp = strcmp(candidate->path, current->path);
    return cmp > 0;
}

static bool reading_state_is_newer(const reading_book_state_t *candidate, const reading_book_state_t *current)
{
    int cmp;

    if (current == NULL)
    {
        return true;
    }

    if (candidate->last_read_at != current->last_read_at)
    {
        return candidate->last_read_at > current->last_read_at;
    }

    if (candidate->open_count != current->open_count)
    {
        return candidate->open_count > current->open_count;
    }

    cmp = strcmp(candidate->path, current->path);
    return cmp < 0;
}

static int reading_state_find_oldest_non_favorite_locked(void)
{
    const reading_book_state_t *oldest = NULL;
    int oldest_index = -1;
    uint16_t i;

    for (i = 0U; i < s_reading_state_db.count; ++i)
    {
        if (s_reading_state_db.books[i].favorite != 0U)
        {
            continue;
        }

        if (reading_state_is_older(&s_reading_state_db.books[i], oldest))
        {
            oldest = &s_reading_state_db.books[i];
            oldest_index = (int)i;
        }
    }

    return oldest_index;
}

static void reading_state_remove_index_locked(uint16_t index)
{
    uint16_t i;

    if (index >= s_reading_state_db.count)
    {
        return;
    }

    for (i = index + 1U; i < s_reading_state_db.count; ++i)
    {
        s_reading_state_db.books[i - 1U] = s_reading_state_db.books[i];
    }

    s_reading_state_db.count--;
    memset(&s_reading_state_db.books[s_reading_state_db.count], 0, sizeof(s_reading_state_db.books[s_reading_state_db.count]));
}

static int reading_state_alloc_index_locked(const char *path)
{
    int evict_index;
    uint16_t index;

    if (path == NULL || path[0] == '\0' || strlen(path) >= READING_STATE_PATH_MAX)
    {
        return -1;
    }

    if (s_reading_state_db.count >= READING_STATE_MAX_BOOKS)
    {
        evict_index = reading_state_find_oldest_non_favorite_locked();
        if (evict_index < 0)
        {
            return -1;
        }
        reading_state_remove_index_locked((uint16_t)evict_index);
    }

    index = s_reading_state_db.count++;
    memset(&s_reading_state_db.books[index], 0, sizeof(s_reading_state_db.books[index]));
    if (!reading_state_copy_path(s_reading_state_db.books[index].path, sizeof(s_reading_state_db.books[index].path), path))
    {
        s_reading_state_db.count--;
        memset(&s_reading_state_db.books[index], 0, sizeof(s_reading_state_db.books[index]));
        return -1;
    }
    reading_state_set_title_from_path(&s_reading_state_db.books[index]);

    return (int)index;
}

static uint32_t reading_state_next_sequence_locked(void)
{
    uint16_t i;

    if (s_reading_state_db.sequence_counter == UINT32_MAX)
    {
        for (i = 0U; i < s_reading_state_db.count; ++i)
        {
            s_reading_state_db.books[i].last_read_at = (uint32_t)i + 1U;
        }
        s_reading_state_db.sequence_counter = (uint32_t)s_reading_state_db.count;
    }

    s_reading_state_db.sequence_counter++;
    if (s_reading_state_db.sequence_counter == 0U)
    {
        s_reading_state_db.sequence_counter = 1U;
    }

    return s_reading_state_db.sequence_counter;
}

static bool reading_state_join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int written;

    if (out == NULL || out_size == 0U || dir == NULL || name == NULL)
    {
        return false;
    }

    written = snprintf(out, out_size, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= out_size)
    {
        return false;
    }

    return true;
}

static bool reading_state_can_read_path(const char *path)
{
    int fd;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return false;
    }

    (void)close(fd);
    return true;
}

static bool reading_state_find_existing_path_locked(char *out, size_t out_size)
{
    size_t i;
    char path[READING_STATE_STORAGE_PATH_MAX];

    if (out == NULL || out_size == 0U)
    {
        return false;
    }

    if (s_reading_state_db.storage_path[0] != '\0' &&
        reading_state_can_read_path(s_reading_state_db.storage_path) &&
        reading_state_copy_path(out, out_size, s_reading_state_db.storage_path))
    {
        return true;
    }

    for (i = 0U; i < (sizeof(s_reading_state_config_dirs) / sizeof(s_reading_state_config_dirs[0])); ++i)
    {
        if (!reading_state_join_path(path, sizeof(path), s_reading_state_config_dirs[i], READING_STATE_FILE_NAME))
        {
            continue;
        }

        if (reading_state_can_read_path(path) && reading_state_copy_path(out, out_size, path))
        {
            return true;
        }
    }

    return false;
}

static rt_err_t reading_state_write_all(int fd, const char *data, size_t len)
{
    size_t total = 0U;

    if (data == NULL && len > 0U)
    {
        return -RT_EINVAL;
    }

    while (total < len)
    {
        ssize_t written = write(fd, data + total, len - total);
        if (written <= 0)
        {
            return -RT_EIO;
        }
        total += (size_t)written;
    }

    return RT_EOK;
}

static rt_err_t reading_state_write_cstr(int fd, const char *data)
{
    if (data == NULL)
    {
        return -RT_EINVAL;
    }

    return reading_state_write_all(fd, data, strlen(data));
}

static rt_err_t reading_state_write_escaped(int fd, const char *data)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *cursor;
    char out[3];
    rt_err_t result;

    if (data == NULL)
    {
        return -RT_EINVAL;
    }

    cursor = (const unsigned char *)data;
    while (*cursor != '\0')
    {
        if (*cursor < 0x20U || *cursor == 0x7FU || *cursor == '%' || *cursor == '\t')
        {
            out[0] = '%';
            out[1] = hex[(*cursor >> 4U) & 0x0FU];
            out[2] = hex[*cursor & 0x0FU];
            result = reading_state_write_all(fd, out, sizeof(out));
        }
        else
        {
            result = reading_state_write_all(fd, (const char *)cursor, 1U);
        }

        if (result != RT_EOK)
        {
            return result;
        }
        cursor++;
    }

    return RT_EOK;
}

static rt_err_t reading_state_write_book(int fd, const reading_book_state_t *book)
{
    int written;
    rt_err_t result;

    result = reading_state_write_cstr(fd, "book\t");
    if (result != RT_EOK)
    {
        return result;
    }

    result = reading_state_write_escaped(fd, book->path);
    if (result != RT_EOK)
    {
        return result;
    }

    result = reading_state_write_cstr(fd, "\t");
    if (result != RT_EOK)
    {
        return result;
    }

    result = reading_state_write_escaped(fd, book->title);
    if (result != RT_EOK)
    {
        return result;
    }

    written = snprintf(s_reading_state_line,
                       sizeof(s_reading_state_line),
                       "\t%u\t%u\t%lu\t%lu\t%u\t%u\t%u\t%u\t%lu\t%lu\n",
                       (unsigned int)book->type,
                       (unsigned int)book->favorite,
                       (unsigned long)book->last_read_at,
                       (unsigned long)book->open_count,
                       (unsigned int)book->page_index,
                       (unsigned int)book->chapter_index,
                       (unsigned int)book->total_pages_hint,
                       (unsigned int)book->chapter_pages_hint,
                       (unsigned long)book->file_size,
                       (unsigned long)book->file_mtime);
    if (written < 0 || (size_t)written >= sizeof(s_reading_state_line))
    {
        return -RT_EFULL;
    }

    return reading_state_write_all(fd, s_reading_state_line, (size_t)written);
}

static rt_err_t reading_state_write_file_locked(int fd)
{
    int written;
    rt_err_t result;
    uint16_t i;

    written = snprintf(s_reading_state_line,
                       sizeof(s_reading_state_line),
                       "reading_state_v1\nsequence=%lu\n",
                       (unsigned long)s_reading_state_db.sequence_counter);
    if (written < 0 || (size_t)written >= sizeof(s_reading_state_line))
    {
        return -RT_EFULL;
    }

    result = reading_state_write_all(fd, s_reading_state_line, (size_t)written);
    if (result != RT_EOK)
    {
        return result;
    }

    for (i = 0U; i < s_reading_state_db.count; ++i)
    {
        result = reading_state_write_book(fd, &s_reading_state_db.books[i]);
        if (result != RT_EOK)
        {
            return result;
        }
    }

    return RT_EOK;
}

static rt_err_t reading_state_save_to_path_locked(const char *path)
{
    char temp_path[READING_STATE_STORAGE_PATH_MAX];
    int written;
    int fd;
    rt_err_t result;

    if (path == NULL || path[0] == '\0')
    {
        return -RT_EINVAL;
    }

    written = snprintf(temp_path, sizeof(temp_path), "%s%s", path, READING_STATE_TEMP_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(temp_path))
    {
        return -RT_EFULL;
    }

    fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        return -RT_EIO;
    }

    result = reading_state_write_file_locked(fd);
    if (close(fd) != 0 && result == RT_EOK)
    {
        result = -RT_EIO;
    }

    if (result != RT_EOK)
    {
        (void)unlink(temp_path);
        return result;
    }

    if (rename(temp_path, path) == 0)
    {
        return RT_EOK;
    }

    (void)unlink(path);
    if (rename(temp_path, path) == 0)
    {
        return RT_EOK;
    }

    (void)unlink(temp_path);
    return -RT_EIO;
}

static rt_err_t reading_state_save_locked(void)
{
    size_t i;
    rt_err_t result;
    char path[READING_STATE_STORAGE_PATH_MAX];

    if (s_reading_state_db.storage_path[0] != '\0')
    {
        result = reading_state_save_to_path_locked(s_reading_state_db.storage_path);
        if (result == RT_EOK)
        {
            s_reading_state_db.dirty = false;
            return RT_EOK;
        }
    }

    for (i = 0U; i < (sizeof(s_reading_state_config_dirs) / sizeof(s_reading_state_config_dirs[0])); ++i)
    {
        if (!reading_state_join_path(path, sizeof(path), s_reading_state_config_dirs[i], READING_STATE_FILE_NAME))
        {
            continue;
        }

        result = reading_state_save_to_path_locked(path);
        if (result == RT_EOK)
        {
            (void)reading_state_copy_path(s_reading_state_db.storage_path, sizeof(s_reading_state_db.storage_path), path);
            s_reading_state_db.dirty = false;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

static int reading_state_read_line(int fd, char *line, size_t line_size)
{
    size_t len = 0U;
    bool got_data = false;
    char ch;

    if (line == NULL || line_size == 0U)
    {
        return -RT_EINVAL;
    }

    while (true)
    {
        ssize_t read_len = read(fd, &ch, 1U);
        if (read_len < 0)
        {
            return -RT_EIO;
        }
        if (read_len == 0)
        {
            break;
        }

        got_data = true;
        if (ch == '\n')
        {
            break;
        }
        if (ch == '\r')
        {
            continue;
        }
        if (len + 1U >= line_size)
        {
            return -RT_EFULL;
        }

        line[len++] = ch;
    }

    if (!got_data && len == 0U)
    {
        return 0;
    }

    line[len] = '\0';
    return 1;
}

static int reading_state_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool reading_state_decode_field(const char *in, char *out, size_t out_size)
{
    size_t in_pos = 0U;
    size_t out_pos = 0U;

    if (in == NULL || out == NULL || out_size == 0U)
    {
        return false;
    }

    while (in[in_pos] != '\0')
    {
        unsigned char ch;

        if (in[in_pos] == '%')
        {
            int hi;
            int lo;

            if (in[in_pos + 1U] == '\0' || in[in_pos + 2U] == '\0')
            {
                return false;
            }

            hi = reading_state_hex_value(in[in_pos + 1U]);
            lo = reading_state_hex_value(in[in_pos + 2U]);
            if (hi < 0 || lo < 0)
            {
                return false;
            }
            ch = (unsigned char)((hi << 4) | lo);
            in_pos += 3U;
        }
        else
        {
            ch = (unsigned char)in[in_pos++];
        }

        if (ch == '\0' || ch == '\r' || ch == '\n')
        {
            return false;
        }
        if (out_pos + 1U >= out_size)
        {
            return false;
        }

        out[out_pos++] = (char)ch;
    }

    out[out_pos] = '\0';
    return true;
}

static bool reading_state_parse_u32(const char *text, uint32_t *out)
{
    uint32_t value = 0U;

    if (text == NULL || text[0] == '\0' || out == NULL)
    {
        return false;
    }

    while (*text != '\0')
    {
        uint32_t digit;

        if (*text < '0' || *text > '9')
        {
            return false;
        }

        digit = (uint32_t)(*text - '0');
        if (value > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }
        value = (value * 10U) + digit;
        text++;
    }

    *out = value;
    return true;
}

static bool reading_state_parse_u16(const char *text, uint16_t *out)
{
    uint32_t value;

    if (!reading_state_parse_u32(text, &value) || value > 0xFFFFU)
    {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool reading_state_parse_u8(const char *text, uint8_t *out)
{
    uint32_t value;

    if (!reading_state_parse_u32(text, &value) || value > 0xFFU)
    {
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

static size_t reading_state_split_tabs(char *text, char **fields, size_t max_fields)
{
    char *cursor;
    size_t count = 0U;

    if (text == NULL || fields == NULL || max_fields == 0U)
    {
        return 0U;
    }

    cursor = text;
    while (true)
    {
        char *tab;

        if (count >= max_fields)
        {
            return max_fields + 1U;
        }

        fields[count++] = cursor;
        tab = strchr(cursor, '\t');
        if (tab == NULL)
        {
            break;
        }

        *tab = '\0';
        cursor = tab + 1;
    }

    return count;
}

static bool reading_state_parse_book_line_locked(char *line)
{
    enum
    {
        FIELD_PATH = 0,
        FIELD_TITLE,
        FIELD_TYPE,
        FIELD_FAVORITE,
        FIELD_LAST_READ_AT,
        FIELD_OPEN_COUNT,
        FIELD_PAGE_INDEX,
        FIELD_CHAPTER_INDEX,
        FIELD_TOTAL_PAGES_HINT,
        FIELD_CHAPTER_PAGES_HINT,
        FIELD_FILE_SIZE,
        FIELD_FILE_MTIME,
        FIELD_COUNT
    };
    char *fields[FIELD_COUNT];
    reading_book_state_t entry;
    size_t field_count;
    int index;

    field_count = reading_state_split_tabs(line, fields, FIELD_COUNT);
    if (field_count != FIELD_COUNT)
    {
        return false;
    }

    memset(&entry, 0, sizeof(entry));
    if (!reading_state_decode_field(fields[FIELD_PATH], entry.path, sizeof(entry.path)) ||
        !reading_state_decode_field(fields[FIELD_TITLE], entry.title, sizeof(entry.title)) ||
        !reading_state_parse_u8(fields[FIELD_TYPE], &entry.type) ||
        !reading_state_parse_u8(fields[FIELD_FAVORITE], &entry.favorite) ||
        !reading_state_parse_u32(fields[FIELD_LAST_READ_AT], &entry.last_read_at) ||
        !reading_state_parse_u32(fields[FIELD_OPEN_COUNT], &entry.open_count) ||
        !reading_state_parse_u16(fields[FIELD_PAGE_INDEX], &entry.page_index) ||
        !reading_state_parse_u16(fields[FIELD_CHAPTER_INDEX], &entry.chapter_index) ||
        !reading_state_parse_u16(fields[FIELD_TOTAL_PAGES_HINT], &entry.total_pages_hint) ||
        !reading_state_parse_u16(fields[FIELD_CHAPTER_PAGES_HINT], &entry.chapter_pages_hint) ||
        !reading_state_parse_u32(fields[FIELD_FILE_SIZE], &entry.file_size) ||
        !reading_state_parse_u32(fields[FIELD_FILE_MTIME], &entry.file_mtime))
    {
        return false;
    }

    if (entry.path[0] == '\0' || entry.favorite > 1U)
    {
        return false;
    }
    if (entry.type != READING_BOOK_TYPE_TXT && entry.type != READING_BOOK_TYPE_EPUB)
    {
        entry.type = READING_BOOK_TYPE_UNKNOWN;
    }
    if (entry.title[0] == '\0')
    {
        reading_state_set_title_from_path(&entry);
    }

    index = reading_state_find_index_locked(entry.path);
    if (index < 0)
    {
        index = reading_state_alloc_index_locked(entry.path);
        if (index < 0)
        {
            return false;
        }
    }

    s_reading_state_db.books[index] = entry;
    if (s_reading_state_db.sequence_counter < entry.last_read_at)
    {
        s_reading_state_db.sequence_counter = entry.last_read_at;
    }

    return true;
}

static rt_err_t reading_state_parse_file_locked(int fd)
{
    rt_err_t result = RT_EOK;

    reading_state_reset_content_locked();

    while (true)
    {
        int line_result = reading_state_read_line(fd, s_reading_state_line, sizeof(s_reading_state_line));
        uint32_t sequence;

        if (line_result < 0)
        {
            result = (rt_err_t)line_result;
            break;
        }
        if (line_result == 0)
        {
            break;
        }

        if (s_reading_state_line[0] == '\0' || s_reading_state_line[0] == '#')
        {
            continue;
        }
        if (strcmp(s_reading_state_line, "reading_state_v1") == 0)
        {
            continue;
        }
        if (strncmp(s_reading_state_line, "sequence=", 9U) == 0)
        {
            if (!reading_state_parse_u32(s_reading_state_line + 9U, &sequence))
            {
                result = -RT_ERROR;
                break;
            }
            if (s_reading_state_db.sequence_counter < sequence)
            {
                s_reading_state_db.sequence_counter = sequence;
            }
            continue;
        }
        if (strncmp(s_reading_state_line, "book\t", 5U) == 0)
        {
            if (!reading_state_parse_book_line_locked(s_reading_state_line + 5U))
            {
                result = -RT_ERROR;
                break;
            }
            continue;
        }

        result = -RT_ERROR;
        break;
    }

    s_reading_state_db.dirty = false;
    s_reading_state_db.loaded = true;
    return result;
}

static rt_err_t reading_state_load_from_path_locked(const char *path)
{
    int fd;
    rt_err_t result;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return -RT_ERROR;
    }

    result = reading_state_parse_file_locked(fd);
    if (close(fd) != 0 && result == RT_EOK)
    {
        result = -RT_EIO;
    }

    if (result != RT_EOK)
    {
        reading_state_reset_content_locked();
    }

    return result;
}

rt_err_t reading_state_init(void)
{
    rt_err_t result;

    result = reading_state_ensure_mutex();
    if (result != RT_EOK)
    {
        return result;
    }

    return reading_state_reload();
}

rt_err_t reading_state_reload(void)
{
    char path[READING_STATE_STORAGE_PATH_MAX];
    rt_err_t result = RT_EOK;
    bool found;

    if (!reading_state_lock())
    {
        return -RT_ERROR;
    }

    found = reading_state_find_existing_path_locked(path, sizeof(path));
    if (found)
    {
        (void)reading_state_copy_path(s_reading_state_db.storage_path, sizeof(s_reading_state_db.storage_path), path);
        result = reading_state_load_from_path_locked(path);
    }
    else
    {
        s_reading_state_db.storage_path[0] = '\0';
        reading_state_reset_content_locked();
        (void)reading_state_save_locked();
    }

    reading_state_unlock();
    return result;
}

rt_err_t reading_state_save(void)
{
    rt_err_t result;

    if (!reading_state_lock())
    {
        return -RT_ERROR;
    }

    result = reading_state_save_locked();
    reading_state_unlock();
    return result;
}

void reading_state_save_deferred(void)
{
    if (!reading_state_lock())
    {
        return;
    }

    s_reading_state_db.dirty = true;
    reading_state_unlock();
}

void reading_state_flush_deferred(void)
{
    if (!reading_state_lock())
    {
        return;
    }

    if (s_reading_state_db.dirty)
    {
        (void)reading_state_save_locked();
    }

    reading_state_unlock();
}

bool reading_state_get(const char *path, reading_book_state_t *out)
{
    int index;

    if (path == NULL || out == NULL)
    {
        return false;
    }

    if (!reading_state_lock())
    {
        return false;
    }

    index = reading_state_find_index_locked(path);
    if (index >= 0)
    {
        *out = s_reading_state_db.books[index];
    }

    reading_state_unlock();
    return index >= 0;
}

bool reading_state_touch_open(const char *path, reading_book_type_t type, uint32_t file_size, uint32_t file_mtime, reading_book_state_t *out)
{
    int index;
    reading_book_state_t *book;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!reading_state_lock())
    {
        return false;
    }

    index = reading_state_find_index_locked(path);
    if (index < 0)
    {
        index = reading_state_alloc_index_locked(path);
    }
    if (index < 0)
    {
        reading_state_unlock();
        return false;
    }

    book = &s_reading_state_db.books[index];
    if (type == READING_BOOK_TYPE_TXT || type == READING_BOOK_TYPE_EPUB)
    {
        book->type = (uint8_t)type;
    }
    else
    {
        book->type = READING_BOOK_TYPE_UNKNOWN;
    }
    book->file_size = file_size;
    book->file_mtime = file_mtime;
    book->last_read_at = reading_state_next_sequence_locked();
    if (book->open_count < UINT32_MAX)
    {
        book->open_count++;
    }
    if (book->title[0] == '\0')
    {
        reading_state_set_title_from_path(book);
    }
    if (out != NULL)
    {
        *out = *book;
    }
    s_reading_state_db.dirty = true;

    reading_state_unlock();
    return true;
}

bool reading_state_update_progress(const char *path, uint16_t chapter_index, uint16_t page_index, uint16_t total_pages_hint, uint16_t chapter_pages_hint)
{
    int index;
    reading_book_state_t *book;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!reading_state_lock())
    {
        return false;
    }

    index = reading_state_find_index_locked(path);
    if (index < 0)
    {
        index = reading_state_alloc_index_locked(path);
    }
    if (index < 0)
    {
        reading_state_unlock();
        return false;
    }

    book = &s_reading_state_db.books[index];
    book->chapter_index = chapter_index;
    book->page_index = page_index;
    book->total_pages_hint = total_pages_hint;
    book->chapter_pages_hint = chapter_pages_hint;
    book->last_read_at = reading_state_next_sequence_locked();
    if (book->title[0] == '\0')
    {
        reading_state_set_title_from_path(book);
    }
    s_reading_state_db.dirty = true;

    reading_state_unlock();
    return true;
}

bool reading_state_set_favorite(const char *path, bool favorite)
{
    int index;
    reading_book_state_t *book;
    uint8_t favorite_value;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!reading_state_lock())
    {
        return false;
    }

    index = reading_state_find_index_locked(path);
    if (index < 0)
    {
        if (!favorite)
        {
            reading_state_unlock();
            return true;
        }
        index = reading_state_alloc_index_locked(path);
    }
    if (index < 0)
    {
        reading_state_unlock();
        return false;
    }

    book = &s_reading_state_db.books[index];
    favorite_value = favorite ? 1U : 0U;
    if (book->favorite != favorite_value)
    {
        book->favorite = favorite_value;
        s_reading_state_db.dirty = true;
    }

    reading_state_unlock();
    return true;
}

bool reading_state_is_favorite(const char *path)
{
    int index;
    bool result = false;

    if (path == NULL || path[0] == '\0')
    {
        return false;
    }

    if (!reading_state_lock())
    {
        return false;
    }

    index = reading_state_find_index_locked(path);
    if (index >= 0)
    {
        result = s_reading_state_db.books[index].favorite != 0U;
    }

    reading_state_unlock();
    return result;
}

uint16_t reading_state_collect_recent(reading_book_state_t *out, uint16_t max_count)
{
    bool selected[READING_STATE_MAX_BOOKS];
    uint16_t copied = 0U;
    uint16_t limit;

    if (out == NULL || max_count == 0U)
    {
        return 0U;
    }

    if (!reading_state_lock())
    {
        return 0U;
    }

    memset(selected, 0, sizeof(selected));
    limit = max_count;
    if (limit > READING_STATE_MAX_BOOKS)
    {
        limit = READING_STATE_MAX_BOOKS;
    }

    while (copied < limit)
    {
        const reading_book_state_t *best = NULL;
        int best_index = -1;
        uint16_t i;

        for (i = 0U; i < s_reading_state_db.count; ++i)
        {
            if (selected[i] || s_reading_state_db.books[i].last_read_at == 0U)
            {
                continue;
            }
            if (reading_state_is_newer(&s_reading_state_db.books[i], best))
            {
                best = &s_reading_state_db.books[i];
                best_index = (int)i;
            }
        }

        if (best_index < 0)
        {
            break;
        }

        out[copied++] = s_reading_state_db.books[best_index];
        selected[best_index] = true;
    }

    reading_state_unlock();
    return copied;
}

uint16_t reading_state_collect_favorites(reading_book_state_t *out, uint16_t max_count)
{
    bool selected[READING_STATE_MAX_BOOKS];
    uint16_t copied = 0U;
    uint16_t limit;

    if (out == NULL || max_count == 0U)
    {
        return 0U;
    }

    if (!reading_state_lock())
    {
        return 0U;
    }

    memset(selected, 0, sizeof(selected));
    limit = max_count;
    if (limit > READING_STATE_MAX_BOOKS)
    {
        limit = READING_STATE_MAX_BOOKS;
    }

    while (copied < limit)
    {
        const reading_book_state_t *best = NULL;
        int best_index = -1;
        uint16_t i;

        for (i = 0U; i < s_reading_state_db.count; ++i)
        {
            if (selected[i] || s_reading_state_db.books[i].favorite == 0U)
            {
                continue;
            }
            if (reading_state_is_newer(&s_reading_state_db.books[i], best))
            {
                best = &s_reading_state_db.books[i];
                best_index = (int)i;
            }
        }

        if (best_index < 0)
        {
            break;
        }

        out[copied++] = s_reading_state_db.books[best_index];
        selected[best_index] = true;
    }

    reading_state_unlock();
    return copied;
}
