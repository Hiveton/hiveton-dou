#ifndef APP_SRC_READING_READING_COVER_CACHE_H
#define APP_SRC_READING_READING_COVER_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    READING_COVER_CACHE_UNKNOWN = 0,
    READING_COVER_CACHE_READY,
    READING_COVER_CACHE_NO_COVER,
    READING_COVER_CACHE_FAILED,
} reading_cover_cache_state_t;

reading_cover_cache_state_t reading_cover_cache_get_state(const char *book_path,
                                                          uint16_t width,
                                                          uint16_t height);

bool reading_cover_cache_load_image(const char *book_path,
                                    uint16_t width,
                                    uint16_t height,
                                    lv_image_dsc_t *out_image);

reading_cover_cache_state_t reading_cover_cache_build(const char *book_path,
                                                      uint16_t width,
                                                      uint16_t height);

void reading_cover_cache_release_image(lv_image_dsc_t *image);

#ifdef __cplusplus
}
#endif

#endif
