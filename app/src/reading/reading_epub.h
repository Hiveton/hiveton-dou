#ifndef APP_SRC_READING_READING_EPUB_H
#define APP_SRC_READING_READING_EPUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READING_EPUB_MAX_INTERNAL_PATH 192U

typedef enum
{
    READING_EPUB_BLOCK_TEXT = 0,
    READING_EPUB_BLOCK_IMAGE,
} reading_epub_block_type_t;

typedef struct
{
    reading_epub_block_type_t type;
    uint32_t text_start;
    uint32_t text_end;
    uint16_t image_index;
} reading_epub_block_t;

typedef struct
{
    char internal_path[READING_EPUB_MAX_INTERNAL_PATH];
} reading_epub_image_ref_t;

typedef struct
{
    char internal_path[READING_EPUB_MAX_INTERNAL_PATH];
} reading_epub_spine_item_t;

bool reading_epub_build_index(const char *epub_path,
                              reading_epub_spine_item_t *items,
                              uint16_t max_item_count,
                              uint16_t *item_count_out,
                              char *error_buffer,
                              size_t error_buffer_size);

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
                               size_t error_buffer_size);

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
                       size_t error_buffer_size);

bool reading_epub_decode_image(const char *epub_path,
                               const char *internal_path,
                               uint16_t max_width,
                               uint16_t max_height,
                               lv_image_dsc_t *out_image);

void reading_epub_release_image(lv_image_dsc_t *image);

#ifdef __cplusplus
}
#endif

#endif
