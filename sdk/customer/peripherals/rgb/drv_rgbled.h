/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DRV_RGBLED_H
#define DRV_RGBLED_H

#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup bsp_driver Driver IO
  * @{
  */

/** @defgroup drv_rgbled RGBLED driver
  * @ingroup bsp_driver
  * @brief RGBLED BSP driver header
  * @{
  */

/* RGB LED device control commands */
typedef enum {
    RGB_CMD_SET_SINGLE_COLOR = 0x10,    /*!< Set single LED color command */
    RGB_CMD_SET_MULTI_COLOR  = 0x11,    /*!< Set multiple LED colors command */
    RGB_CMD_GET_CAPABILITY   = 0x12     /*!< Get device capability command */
} rgb_cmd_t;

/* RGB LED configuration structures */

/**
 * @brief Multiple LED configuration structure
 * @note Used with RGB_CMD_SET_MULTI_COLOR command
 */
struct rt_rgbled_multi_configuration
{
    rt_uint16_t led_count;      /*!< Number of LEDs to control */
    rt_uint32_t *color_array;   /*!< Array of RGB colors for each LED */
};

/* RGB LED device capability structure */

/**
 * @brief RGB LED device capability information
 * @note Used with RGB_CMD_GET_CAPABILITY command
 */
struct rt_rgbled_capability
{
    rt_uint32_t max_led_count;  /*!< Maximum supported LED count */
    rt_uint32_t color_format;   /*!< Color format (0xRRGGBB) */
    rt_uint32_t features;       /*!< Device feature flags */
};

/* RGB LED color format definitions */
#define RGB_COLOR_FORMAT_RGB888    0x01    /*!< 24-bit RGB color format */

/* RGB LED feature flags */
#define RGB_FEATURE_SINGLE_COLOR   0x01    /*!< Single color setting supported */
#define RGB_FEATURE_MULTI_COLOR    0x02    /*!< Multiple color setting supported */
#define RGB_FEATURE_DMA_TRANSFER   0x04    /*!< DMA transfer supported */

/* RGB LED device name */
#ifndef RGBLED_DEVICE_NAME
#define RGBLED_DEVICE_NAME         "rgbled"
#endif

/* RGB LED common color definitions */
#define RGB_COLOR_BLACK            0x000000
#define RGB_COLOR_WHITE            0xFFFFFF
#define RGB_COLOR_RED              0xFF0000
#define RGB_COLOR_GREEN            0x00FF00
#define RGB_COLOR_BLUE             0x0000FF
#define RGB_COLOR_YELLOW           0xFFFF00
#define RGB_COLOR_CYAN             0x00FFFF
#define RGB_COLOR_MAGENTA          0xFF00FF

/* RGB LED color manipulation macros */
#define RGB_GET_RED(color)         (((color) & 0xFF0000) >> 16)
#define RGB_GET_GREEN(color)       (((color) & 0x00FF00) >> 8)
#define RGB_GET_BLUE(color)        ((color) & 0x0000FF)
#define RGB_MAKE_COLOR(r,g,b)      (((r) << 16) | ((g) << 8) | (b))

/* Function prototypes */

/**
 * @brief Helper function to find RGB LED device
 * @param name Device name (NULL for default name "rgbled")
 * @return Device handle on success, NULL on failure
 */
static inline rt_device_t rgb_find_device(const char *name)
{
    return rt_device_find(name ? name : RGBLED_DEVICE_NAME);
}

/* @} drv_rgbled */
/* @} bsp_driver */

#ifdef __cplusplus
}
#endif

#endif /* DRV_RGBLED_H */