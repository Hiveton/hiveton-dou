#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "drv_lcd_private.h"
#include "mem_section.h"
#include "bf0_pm.h"
#include "drv_gpio.h"
#include "gui_app_pm.h"
#include "lv_display.h"
#include "lv_obj_pos.h"
#include "littlevgl2rtt.h"
#define DISPLAY_COLOR 0x00FF0000   // red color
#define IDLE_TIME_LIMIT  (5000)
static struct rt_device_graphic_info lcd_info;
static rt_device_t lcd_device = RT_NULL;

L2_NON_RET_BSS_SECT_BEGIN(frambuf)
// Use a smaller framebuffer, e.g., 1/4 of screen size
#define FRAMEBUFFER_WIDTH  (LCD_HOR_RES_MAX/2)
#define FRAMEBUFFER_HEIGHT (LCD_VER_RES_MAX/2)
L2_NON_RET_BSS_SECT(frambuf, ALIGN(64) static char frambuffer[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 3]);
L2_NON_RET_BSS_SECT_END


// Convert RGB888 to specific format color
static uint32_t make_color(uint16_t cf, uint32_t rgb888)
{
    uint8_t r, g, b;

    r = (rgb888 >> 16) & 0xFF;
    g = (rgb888 >> 8) & 0xFF;
    b = (rgb888 >> 0) & 0xFF;

    switch (cf)
    {
    case RTGRAPHIC_PIXEL_FORMAT_RGB565:
        return ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3));
    case RTGRAPHIC_PIXEL_FORMAT_RGB888:
        return ((((r) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((b) & 0xFF));
    default:
        return 0;
    }
}

// Fill color to buffer
static void fill_color(uint8_t *buf, uint32_t width, uint32_t height,
                       uint16_t cf, uint32_t ARGB8888)
{
    uint8_t pixel_size;

    if (RTGRAPHIC_PIXEL_FORMAT_RGB565 == cf)
    {
        pixel_size = 2;
    }
    else if (RTGRAPHIC_PIXEL_FORMAT_RGB888 == cf)
    {
        pixel_size = 3;
    }
    else
    {
        RT_ASSERT(0);
    }

    uint32_t i, j, k, c;
    c = make_color(cf, ARGB8888);
    for (i = 0; i < height; i++)
    {
        for (j = 0; j < width; j++)
        {
            for (k = 0; k < pixel_size; k++)
            {
                *buf++ = (c >> (k << 3)) & 0xFF;
            }
        }
    }
}

// Drawing function
static void draw(rt_device_t lcd_device)
{
    fill_color((uint8_t *)frambuffer, lcd_info.width, lcd_info.height,
               (16 == lcd_info.bits_per_pixel) ? RTGRAPHIC_PIXEL_FORMAT_RGB565 : RTGRAPHIC_PIXEL_FORMAT_RGB888,
               DISPLAY_COLOR);

    // Set drawing window to full screen
    rt_graphix_ops(lcd_device)->set_window(0, 0, lcd_info.width - 1, lcd_info.height - 1);
    rt_graphix_ops(lcd_device)->draw_rect((const char *)frambuffer, 0, 0, lcd_info.width - 1, lcd_info.height - 1);

    rt_kprintf("Screen updated at tick: %d\n", rt_tick_get());
}

static void lcd_init()
{
    rt_err_t result;
    lcd_device = rt_device_find("lcd");
    RT_ASSERT(lcd_device);
    result = rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_INFO, &lcd_info);
    RT_ASSERT(result == RT_EOK);
    // Set framebuffer format
    uint16_t cf;
    if (16 == lcd_info.bits_per_pixel)
        cf = RTGRAPHIC_PIXEL_FORMAT_RGB565;
    else if (24 == lcd_info.bits_per_pixel)
        cf = RTGRAPHIC_PIXEL_FORMAT_RGB888;
    else
    {
        rt_kprintf("Unsupported pixel format\n");
        rt_device_close(lcd_device);
        RT_ASSERT(0);
    }
    rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BUF_FORMAT, &cf);
}

static void lcd_display_task()
{
    if (lcd_device != RT_NULL) draw(lcd_device);
    else rt_kprintf("LCD device not initialized\n");
    lv_display_trigger_activity(NULL);
    while (1)
    {
#ifdef GUI_APP_PM
        uint32_t inactive_time = lv_display_get_inactive_time(NULL);
        if (lv_display_get_inactive_time(NULL) > IDLE_TIME_LIMIT)
        {
            gui_pm_fsm(GUI_PM_ACTION_SLEEP);
            rt_kprintf("no input, inactive time: %d ms\n", inactive_time);
        }
        if (gui_is_force_close())
        {
            rt_kprintf("in force_close\n");
            bool lcd_drawing;
            rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_BUSY,
                              &lcd_drawing);
            if (!lcd_drawing)
            {
                rt_kprintf("no input:%d\n", lv_display_get_inactive_time(NULL));
                gui_suspend();
                if (lcd_device != RT_NULL) draw(lcd_device);
                rt_kprintf("ui resume");
                /* force screen to redraw */
                lv_obj_invalidate(lv_screen_active());
                /* reset activity timer */
                lv_display_trigger_activity(NULL);
            }
        }
#endif
        rt_thread_mdelay(10);
    }
}
static void button_event_handler(int32_t pin, button_action_t action)
{
    rt_kprintf("in button handle\n");
    lv_display_trigger_activity(NULL);
    gui_pm_fsm(GUI_PM_ACTION_WAKEUP); // wake up gui
    static button_action_t last_action = BUTTON_RELEASED;
    if (last_action == action)
        return;
    last_action = action;
    if (action == BUTTON_PRESSED)
    {

        rt_kprintf("pressed\r\n");

    }
    else if (action == BUTTON_RELEASED)
    {
        rt_kprintf("released\r\n");
    }
}
static void wakeup_button_init(void)
{
    static int initialized = 0;
    rt_kprintf("wakeup_button_init\n");
    if (initialized == 0)
    {
        button_cfg_t cfg2;
        cfg2.pin = BSP_KEY1_PIN;
        cfg2.active_state = 1;
        cfg2.mode = PIN_MODE_INPUT;
        cfg2.button_handler = button_event_handler;
        int32_t id2 = button_init(&cfg2);
        RT_ASSERT(SF_EOK == button_enable(id2));
        RT_ASSERT(id2 >= 0);
        initialized = 1;
    }
}
static void pm_event_handler(gui_pm_event_type_t event)
{
    switch (event)
    {
    case GUI_PM_EVT_SUSPEND:
    {
        lv_timer_enable(false);
        break;
    }
    case GUI_PM_EVT_RESUME:
    {
        lv_timer_enable(true);
        break;
    }
    default:
    {
        RT_ASSERT(0);
    }
    }
}

static void pm_ui_init()
{
    littlevgl2rtt_init("lcd");
    lcd_init();
#ifdef GUI_APP_PM
    wakeup_button_init();
    gui_ctx_init();
    gui_pm_init(lcd_device, pm_event_handler);
#endif
}
int main(void)
{
    HAL_LPAON_Sleep();
    pm_ui_init();
    lcd_display_task();
    while (1)
    {
        /*main loop*/
        rt_thread_mdelay(10000);
    }

    return RT_EOK;
}