#include "ui/home_screen.h"

#include <string.h>

#include "ui.h"
#include "ui_helpers.h"
#include "ui_runtime_adapter.h"

static const uint8_t s_transparent_img_data[] = {0x00};
static rt_bool_t s_ui_initialized = RT_FALSE;

RT_WEAK const lv_image_dsc_t ble = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t ble_close = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t ble_icon_img = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t ble_icon_img_close = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t network_icon_img = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t network_icon_img_close = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t cdian2 = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

RT_WEAK const lv_image_dsc_t no_power2 = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = 1, .h = 1, .stride = 1, .reserved_2 = 0},
    .data_size = sizeof(s_transparent_img_data),
    .data = s_transparent_img_data,
    .reserved = NULL,
};

static void ensure_ui_initialized(void)
{
    if (s_ui_initialized)
    {
        return;
    }

    ui_init();
    s_ui_initialized = RT_TRUE;
}

rt_err_t home_screen_build(lv_event_cb_t home_event_cb,
                           bool enable_home_event,
                           home_screen_bundle_t *bundle)
{
    lv_obj_t *screen;
    const xiaozhi_home_screen_refs_t *refs;

    if (bundle == RT_NULL)
    {
        return -RT_EINVAL;
    }

    memset(bundle, 0, sizeof(*bundle));
    ensure_ui_initialized();
    ui_runtime_ensure_home_screen();

    screen = ui_runtime_get_home_screen();
    refs = ui_home_screen_refs_get();
    bundle->home_screen = screen;
    if (refs != RT_NULL)
    {
        bundle->home_refs = *refs;
    }
    bundle->home_refs.screen = screen;
    bundle->standby_screen = RT_NULL;

    if (enable_home_event && home_event_cb != NULL && screen != NULL)
    {
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(screen, home_event_cb, LV_EVENT_ALL, NULL);
    }

    return RT_EOK;
}

lv_font_t *home_screen_font_get(uint16_t size)
{
    return ui_font_get_actual(size);
}

lv_font_t *home_screen_title_font_get(uint16_t size)
{
    return ui_font_get_actual(size);
}
