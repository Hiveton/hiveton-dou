#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "drv_lcd_private.h"
#include "mem_section.h"
#include "bf0_pm.h"
#include "drv_gpio.h"
#include "gui_app_pm.h"

rt_device_t lcd_device;
// Define test frame buffer
#define TEST_FRAMEBUFFER
// Display duration: 5 seconds
#define DISPLAY_DURATION_MS 5000
// Refresh interval: 1 second
#define DISPLAY_INTERVAL_MS 1000

// Wakeup flag
static volatile int wakeup_flag = 0;

// Event object
static rt_event_t display_event;

// Default wakeup key pin definition (please modify according to actual hardware)
#ifndef BSP_KEY_WAKEUP_PIN
    #define BSP_KEY_WAKEUP_PIN BSP_KEY1_PIN
#endif

// LCD device information structure
static struct rt_device_graphic_info lcd_info;
// Display start time
static rt_tick_t display_start_time = 0;
// Global LCD device pointer
static rt_device_t lcd_device_global = RT_NULL;
// System sleep state flag
static int system_sleeping = 0;

#ifdef TEST_FRAMEBUFFER
    // Frame buffer memory section start
    L2_NON_RET_BSS_SECT_BEGIN(frambuf)
    // Use smaller frame buffer, e.g., 1/4 of screen size
    #define FRAMEBUFFER_WIDTH  (LCD_HOR_RES_MAX/2)
    #define FRAMEBUFFER_HEIGHT (LCD_VER_RES_MAX/2)
    // Define frame buffer, 64-byte aligned
    L2_NON_RET_BSS_SECT(frambuf, ALIGN(64) static char frambuffer[FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * 3]);
    // Frame buffer memory section end
    L2_NON_RET_BSS_SECT_END
#else
    #ifdef  PSRAM_BASE
        // Ensure PSRAM buffer has correct alignment
        const char *frambuffer = (const char *)BSP_USING_PSRAM;
    #else
        // Use system RAM as frame buffer
        const char *frambuffer = (const char *)HPSYS_RAM0_BASE;
    #endif /* PSRAM_BASE */
#endif

// Convert RGB888 to specific format color
static uint32_t make_color(uint16_t cf, uint32_t rgb888)
{
    uint8_t r, g, b;

    // Extract RGB components
    r = (rgb888 >> 16) & 0xFF;
    g = (rgb888 >> 8) & 0xFF;
    b = (rgb888 >> 0) & 0xFF;

    // Convert color according to pixel format
    switch (cf)
    {
    case RTGRAPHIC_PIXEL_FORMAT_RGB565:
        // Convert to RGB565 format
        return ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3));
    case RTGRAPHIC_PIXEL_FORMAT_RGB888:
        // Convert to RGB888 format
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

    // Determine pixel size according to pixel format
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
    // Generate color value
    c = make_color(cf, ARGB8888);
    // Fill entire buffer
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

// Generate random color
static uint32_t rand_color(void)
{
    // Use system tick to generate pseudo-random color
    return (rt_tick_get() * 0x12345) & 0xFFFFFF;
}

// Drawing function
static void draw(rt_device_t lcd_device)
{
    // Fill entire screen with random color
#ifdef TEST_FRAMEBUFFER
    fill_color((uint8_t *)frambuffer, lcd_info.width, lcd_info.height,
               (16 == lcd_info.bits_per_pixel) ? RTGRAPHIC_PIXEL_FORMAT_RGB565 : RTGRAPHIC_PIXEL_FORMAT_RGB888,
               rand_color());
#endif /* TEST_FRAMEBUFFER */

    // Set drawing window to full screen
    rt_graphix_ops(lcd_device)->set_window(0, 0, lcd_info.width - 1, lcd_info.height - 1);

    // Synchronously draw entire screen
    rt_graphix_ops(lcd_device)->draw_rect((const char *)frambuffer, 0, 0, lcd_info.width - 1, lcd_info.height - 1);

    rt_kprintf("The screen is updated at tick %d.\n", rt_tick_get());
}

// Key interrupt callback function
static void key_wakeup_callback(void *args)
{
    // If system is sleeping
    if (system_sleeping)
    {
        rt_kprintf("The key is pressed, waking up the system...\n");
        // Request idle mode
        rt_pm_request(PM_SLEEP_MODE_IDLE);
        // Send event notification instead of setting flag
        rt_event_send(display_event, 0x01);
        // Clear sleep flag
        system_sleeping = 0;
    }
}

// Initialize key wakeup function
static void init_key_wakeup(void)
{
    int8_t wakeup_pin;
    uint16_t gpio_pin;
    GPIO_TypeDef *gpio;

    // Get GPIO instance and pin
    gpio = GET_GPIO_INSTANCE(BSP_KEY_WAKEUP_PIN);
    gpio_pin = GET_GPIOx_PIN(BSP_KEY_WAKEUP_PIN);

    // Query wakeup pin
    wakeup_pin = HAL_HPAON_QueryWakeupPin(gpio, gpio_pin);

    RT_ASSERT(wakeup_pin >= 0);
    // Enable key wakeup function, double edge trigger
    pm_enable_pin_wakeup(wakeup_pin, AON_PIN_MODE_DOUBLE_EDGE);

    // Configure key GPIO
    rt_pin_mode(34, PIN_MODE_INPUT); // Configure as input mode
    rt_pin_attach_irq(34, PIN_IRQ_MODE_RISING_FALLING, key_wakeup_callback, RT_NULL); // Configure interrupt callback function, rising and falling edge trigger
    rt_pin_irq_enable(34, PIN_IRQ_ENABLE);
}

// Enter sleep mode function - LCD will turn off
static void enter_sleep_mode(rt_device_t lcd_device)
{
    const uint8_t idle_mode_on = 1;
    rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_MODE, (void *)&idle_mode_on);
    rt_kprintf("The LCD is turned off, entering sleep mode...\n");
    // Set sleep flag
    system_sleeping = 1;
}
static void lcd_display_task(void *parameter)
{
    rt_device_t lcd_device = (rt_device_t)parameter;
    rt_err_t result;

    // Save global LCD device pointer
    lcd_device_global = lcd_device;

    // Create event object
    display_event = rt_event_create("display", RT_IPC_FLAG_FIFO);
    RT_ASSERT(display_event);

    // Open LCD device
    result = rt_device_open(lcd_device, RT_DEVICE_OFLAG_RDWR);
    RT_ASSERT(result == RT_EOK);

    // Get LCD information
    if (rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_INFO, &lcd_info) != RT_EOK)
    {
        rt_kprintf("Failed to obtain LCD information\n");
        rt_device_close(lcd_device);
        return;
    }
    // Set frame buffer format
    uint16_t cf;
    if (16 == lcd_info.bits_per_pixel)
        cf = RTGRAPHIC_PIXEL_FORMAT_RGB565;
    else if (24 == lcd_info.bits_per_pixel)
        cf = RTGRAPHIC_PIXEL_FORMAT_RGB888;
    else
    {
        rt_kprintf("Unsupported pixel format\n");
        rt_device_close(lcd_device);
        return;
    }
    rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BUF_FORMAT, &cf);

    while (1)
    {
        // Record display start time
        display_start_time = rt_tick_get();

        // Main loop: draw once per second, continue for specified time
        while (1)
        {
            rt_tick_t current_time = rt_tick_get();
            rt_tick_t elapsed_time = current_time - display_start_time;

            // Check if specified time has elapsed
            if (elapsed_time >= rt_tick_from_millisecond(DISPLAY_DURATION_MS))
            {
                rt_kprintf("The display period has ended, entering sleep mode...\n");
                break;
            }
            draw(lcd_device);

            // Delay 1 second
            rt_thread_mdelay(1000);
        }
        // Enter sleep mode
        enter_sleep_mode(lcd_device);
        // Release idle mode request
        rt_pm_release(PM_SLEEP_MODE_IDLE);
        // Wait for wakeup event
        rt_uint32_t event_flag;
        rt_err_t result = rt_event_recv(display_event, 0x01,
                                        RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                        RT_WAITING_FOREVER, &event_flag);
    }
}

// Initialization function
int lcd_simple_display_init(void)
{
    // Find LCD device
    lcd_device = rt_device_find("lcd");
    RT_ASSERT(lcd_device);
    // Start LCD display task
    lcd_display_task(lcd_device);
    return 0;
}

void draw_time(void *parameter)
{
    rt_device_t lcd_device = (rt_device_t)parameter;
    draw(lcd_device);
}
//Add timed wake-up refresh,Refresh the screen once every minute.
static void set_rtc_wakeup(void)
{
    pm_enable_rtc_wakeup();
    rt_timer_t rtc_timer;
    rtc_timer = rt_timer_create("rtc_wk",
                                draw_time,
                                lcd_device,
                                rt_tick_from_millisecond(60000),
                                RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_HARD_TIMER);

    if (rtc_timer != RT_NULL)
    {
        rt_timer_start(rtc_timer);
    }
}
int main(void)
{
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    // Enter low power sleep
    HAL_LPAON_Sleep();
    //HAL_PIN_Set(PAD_PA00, GPIO_A0, PIN_PULLUP, 1);
    // Initialize key wakeup function
    init_key_wakeup();
    // Initialize GUI power management
    gui_ctx_init();
    // Initialize display task
    BSP_GPIO_Set(21, 0, 1);
    if (lcd_simple_display_init() != 0)
    {
        rt_kprintf("Initialization of display task failed.\n");
    }
    // Main thread can perform other tasks or enter idle state
    while (1)
    {
        rt_thread_mdelay(10000);
    }

    return RT_EOK;
}