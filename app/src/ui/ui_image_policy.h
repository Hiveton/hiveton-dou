#ifndef UI_IMAGE_POLICY_H
#define UI_IMAGE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define UI_IMAGE_GRAY4_CONTRAST_PERCENT 110

static inline uint8_t ui_image_gray4_apply_contrast(uint8_t gray)
{
    int32_t centered = (int32_t)gray - 128;
    int32_t adjusted = 128 + (centered * UI_IMAGE_GRAY4_CONTRAST_PERCENT) / 100;

    if (adjusted < 0)
    {
        adjusted = 0;
    }
    else if (adjusted > 255)
    {
        adjusted = 255;
    }

    return (uint8_t)adjusted;
}

static inline uint8_t ui_image_gray4_level_from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t gray = (uint8_t)(((uint16_t)(r * 30U + g * 59U + b * 11U)) / 100U);
    uint8_t level;

    gray = ui_image_gray4_apply_contrast(gray);
    level = (uint8_t)(((uint16_t)gray * 3U + 127U) / 255U);
    if (level > 3U)
    {
        level = 3U;
    }

    return level;
}

static inline uint8_t ui_image_gray4_level_to_u8(uint8_t level)
{
    static const uint8_t gray_levels[4] = {0U, 85U, 170U, 255U};

    if (level > 3U)
    {
        level = 3U;
    }

    return gray_levels[level];
}

static inline uint8_t ui_image_gray4_level_to_i4_palette(uint8_t level)
{
    static const uint8_t palette_index[4] = {15U, 10U, 5U, 0U};

    if (level > 3U)
    {
        level = 3U;
    }

    return palette_index[level];
}

static inline bool ui_image_page_transition_requires_full(bool current_has_image,
                                                          bool target_has_image)
{
    return current_has_image || target_has_image;
}

static inline bool ui_image_page_enter_requires_full(bool first_render,
                                                     bool refresh_pending)
{
    return first_render || refresh_pending;
}

#endif
