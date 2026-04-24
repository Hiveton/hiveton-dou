#ifndef APP_SRC_CONFIG_APP_CONFIG_H
#define APP_SRC_CONFIG_APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_VERSION 1U

typedef enum
{
    APP_CONFIG_NETWORK_MODE_4G = 0,
    APP_CONFIG_NETWORK_MODE_BT = 1,
} app_config_network_mode_t;

typedef enum
{
    APP_CONFIG_LANGUAGE_ZH_CN = 0,
    APP_CONFIG_LANGUAGE_EN_US = 1,
} app_config_language_t;

typedef struct
{
    uint32_t version;
    struct
    {
        app_config_network_mode_t network_mode;
        uint8_t auto_connect;
    } boot;
    struct
    {
        uint32_t brightness;
        uint32_t standby_timeout_sec;
    } display;
    struct
    {
        app_config_language_t language;
    } ui;
    struct
    {
        uint8_t use_system_font;
        char font_path[256];
        uint32_t font_size;
        uint32_t line_space;
    } reading;
    struct
    {
        uint32_t music_volume;
        uint32_t ai_volume;
    } audio;
    struct
    {
        char path[256];
    } wallpaper;
    struct
    {
        uint8_t auto_resume;
    } ai;
    struct
    {
        uint8_t auto_refresh;
    } weather;
} app_config_t;

rt_err_t app_config_init(void);
rt_err_t app_config_load(void);
rt_err_t app_config_save(void);
void app_config_reset_to_defaults(void);
const app_config_t *app_config_get(void);
void app_config_get_snapshot(app_config_t *out);
bool app_config_is_loaded_from_file(void);
bool app_config_is_dirty(void);

app_config_network_mode_t app_config_get_boot_network_mode(void);
void app_config_set_boot_network_mode(app_config_network_mode_t mode);

bool app_config_get_boot_auto_connect(void);
void app_config_set_boot_auto_connect(bool enabled);

uint32_t app_config_get_display_brightness(void);
void app_config_set_display_brightness(uint32_t brightness);

uint32_t app_config_get_display_standby_timeout_sec(void);
void app_config_set_display_standby_timeout_sec(uint32_t timeout_sec);

app_config_language_t app_config_get_ui_language(void);
void app_config_set_ui_language(app_config_language_t language);

bool app_config_get_reading_use_system_font(void);
void app_config_set_reading_use_system_font(bool enabled);

void app_config_get_reading_font_path(char *out, size_t out_size);
rt_err_t app_config_set_reading_font_path(const char *path);

uint32_t app_config_get_reading_font_size(void);
void app_config_set_reading_font_size(uint32_t font_size);

uint32_t app_config_get_reading_line_space(void);
void app_config_set_reading_line_space(uint32_t line_space);

uint32_t app_config_get_audio_music_volume(void);
void app_config_set_audio_music_volume(uint32_t volume);

uint32_t app_config_get_audio_ai_volume(void);
void app_config_set_audio_ai_volume(uint32_t volume);

void app_config_get_wallpaper_path(char *out, size_t out_size);
rt_err_t app_config_set_wallpaper_path(const char *path);

bool app_config_get_ai_auto_resume(void);
void app_config_set_ai_auto_resume(bool enabled);

bool app_config_get_weather_auto_refresh(void);
void app_config_set_weather_auto_refresh(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* APP_SRC_CONFIG_APP_CONFIG_H */
