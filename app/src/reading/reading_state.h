#ifndef READING_STATE_H
#define READING_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rtthread.h"

#define READING_STATE_MAX_BOOKS 64U
#define READING_STATE_PATH_MAX 256U
#define READING_STATE_TITLE_MAX 128U

typedef enum
{
    READING_BOOK_TYPE_UNKNOWN = 0,
    READING_BOOK_TYPE_TXT = 1,
    READING_BOOK_TYPE_EPUB = 2
} reading_book_type_t;

typedef struct
{
    char path[READING_STATE_PATH_MAX];
    char title[READING_STATE_TITLE_MAX];
    uint8_t type;
    uint8_t favorite;
    uint32_t last_read_at;
    uint32_t open_count;
    uint16_t page_index;
    uint16_t chapter_index;
    uint16_t total_pages_hint;
    uint16_t chapter_pages_hint;
    uint32_t file_size;
    uint32_t file_mtime;
} reading_book_state_t;

rt_err_t reading_state_init(void);
rt_err_t reading_state_reload(void);
rt_err_t reading_state_save(void);
void reading_state_save_deferred(void);
void reading_state_flush_deferred(void);
bool reading_state_get(const char *path, reading_book_state_t *out);
bool reading_state_touch_open(const char *path, reading_book_type_t type, uint32_t file_size, uint32_t file_mtime, reading_book_state_t *out);
bool reading_state_update_progress(const char *path, uint16_t chapter_index, uint16_t page_index, uint16_t total_pages_hint, uint16_t chapter_pages_hint);
bool reading_state_set_favorite(const char *path, bool favorite);
bool reading_state_is_favorite(const char *path);
uint16_t reading_state_collect_recent(reading_book_state_t *out, uint16_t max_count);
uint16_t reading_state_collect_favorites(reading_book_state_t *out, uint16_t max_count);

#endif
