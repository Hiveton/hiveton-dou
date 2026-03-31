#include <stdbool.h>
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <rtdevice.h>
#include "board.h"
#include <time.h>
#include "littlevgl2rtt.h"
#include "lv_ex_data.h"
#include "button.h"
#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "cwm_motion.h"
#include "st_lsm6dsl_sensor_v1.h"


LV_IMG_DECLARE(img2);

#define SCREEN_WIDTH  390
#define SCREEN_HEIGHT 450
#define IDLE_TIME_LIMIT 10000  // Sleep after 10 seconds of inactivity
#define LCD_DEVICE_NAME "lcd"
#define TOUCH_DEVICE_NAME "i2c1"


extern cwm_gesture_info_t g_gesture_info; // Wrist raise detection

#define WRIST_DETECT_THREAD_STACK_SIZE (4096)
#define TIME_UI_THREAD_STACK_SIZE (2048)
static struct rt_thread wrist_detect_thread;
static struct rt_thread time_ui_thread;
static rt_uint8_t wrist_detect_thread_stack[WRIST_DETECT_THREAD_STACK_SIZE];
static rt_uint8_t time_ui_thread_stack[TIME_UI_THREAD_STACK_SIZE];

extern cwm_gesture_info_t *cwm_get_gesture_data(void);
extern void cwm_all_fifo_data_input(void *buf, int16_t len, uint64_t ts_ns);
extern int cyewee_algo_init(void);
extern uint64_t CWM_OS_GetTimeNs(void);
extern void CWM_process(void);  // Supplement declaration (original code not explicitly declared, avoid compilation warning)

// Sensor devices
rt_device_t lsm6d_dev, lsm6d_gyro_dev, lsm6d_step_dev;
#define LSM6DSL_NAME "lsm6dsl"
#define LSM6DSL_LSM6DSL_DEV_NAME "acce_"LSM6DSL_NAME
#define LSM6DSL_LSM6DSL_GYRO_DEV_NAME "gyro_"LSM6DSL_NAME
#define LSM6DSL_LSM6DSL_STEP_DEV_NAME "step_"LSM6DSL_NAME


// Time structure for tracking current time
typedef struct
{
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} AppTime;

typedef struct
{
    lv_obj_t *img_obj;
    lv_obj_t *time_label;
    AppTime current_time;  // Current time
    bool is_sleeping;      // Sleep state flag
    int32_t key1_button_handle;    // Button handle
    lv_timer_t *idle_check_timer;  // Idle detection timer
    lv_disp_t *disp;               // Display device (newly added, used to get inactivity time)
} SingleScreenLayout;

static SingleScreenLayout main_screen =
{
    .current_time = {9, 30, 0},  // Initial time set to 9:30:00
    .is_sleeping = false,
    .key1_button_handle = -1,
    .idle_check_timer = NULL,
    .disp = NULL  // Initialize to NULL
};

// Screen power control
static void lcd_power_control(bool on)
{
    rt_device_t lcd_dev = rt_device_find(LCD_DEVICE_NAME);
    // rt_device_t touch_dev = rt_device_find(TOUCH_DEVICE_NAME);
    if (lcd_dev)
    {
        if (on)
        {
            rt_device_control(lcd_dev, RTGRAPHIC_CTRL_POWERON, NULL);
            // rt_device_control(touch_dev, RTGRAPHIC_CTRL_POWERON, NULL);
            uint8_t brightness = 100;
            rt_device_control(lcd_dev, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness);

            lv_obj_invalidate(lv_scr_act());  // Refresh screen
        }
        else
        {

            rt_device_control(lcd_dev, RTGRAPHIC_CTRL_POWEROFF, NULL);
            // rt_device_control(touch_dev, RTGRAPHIC_CTRL_POWEROFF, NULL);
#ifdef BSP_USING_TOUCHD
            rt_device_t touch_device = rt_device_find("touch");
            if (touch_device)
            {
                rt_device_control(touch_device, RTGRAPHIC_CTRL_POWEROFF, NULL);
            }
#endif
        }
    }
}

// Enter sleep mode
static void enter_sleep_mode(void)
{
    if (main_screen.is_sleeping) return;

    rt_kprintf("Entering sleep mode...\n");
    lv_disp_trig_activity(main_screen.disp);
    main_screen.is_sleeping = true;
    lcd_power_control(false);  // Turn off screen


    rt_thread_suspend(&wrist_detect_thread);

    rt_pm_release(PM_SLEEP_MODE_IDLE);

}

// Wake up system
static void wake_up_system(void)
{
    if (!main_screen.is_sleeping) return;

    rt_kprintf("Waking up system...\n");
    lcd_power_control(true);   // Turn on screen
    lv_disp_trig_activity(main_screen.disp);
    rt_kprintf("Wakeup: reset inactive time to 0 ms\n");
    main_screen.is_sleeping = false;
    // Automatically reset screen inactivity time when waking up (maintained internally by lvgl, no manual handling required)
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    rt_thread_resume(&wrist_detect_thread);
}

// Update time label display
static void update_time_label(void)
{
    if (!main_screen.time_label || main_screen.is_sleeping) return;

    char time_str[9];  // Format HH:MM:SS
    sprintf(time_str, "%02d:%02d:%02d",
            main_screen.current_time.hour,
            main_screen.current_time.minute,
            main_screen.current_time.second);

    lv_label_set_text(main_screen.time_label, time_str);
}

// Time update timer callback function (continues counting even during sleep)
static void time_update_timer(lv_timer_t *timer)
{
    main_screen.current_time.second++;

    // Handle carry logic
    if (main_screen.current_time.second >= 60)
    {
        main_screen.current_time.second = 0;
        main_screen.current_time.minute++;

        if (main_screen.current_time.minute >= 60)
        {
            main_screen.current_time.minute = 0;
            main_screen.current_time.hour++;

            if (main_screen.current_time.hour >= 24)
            {
                main_screen.current_time.hour = 0;
            }
        }
    }

    update_time_label();  // Automatically update display when not sleeping
}

// Idle detection timer callback (core modification: using lv_disp_get_inactive_time)
static void idle_check_timer_cb(lv_timer_t *timer)
{
    if (main_screen.is_sleeping || !main_screen.disp) return;

    // Directly get screen inactivity time (unit: ms), no need to manually maintain last_activity_time
    uint32_t inactive_time = lv_disp_get_inactive_time(main_screen.disp);
    rt_kprintf("%d /n", inactive_time);
    // Sleep if inactivity time exceeds limit
    if (inactive_time >= IDLE_TIME_LIMIT)
    {
        enter_sleep_mode();
    }
}

// Button event handler (button operations are treated as activity, automatically resets lvgl inactivity time)
static void button_event_handler(int32_t pin, button_action_t action)
{
    rt_kprintf("Button event - pin: %d, action: %d\n", pin, action);

    if (action == BUTTON_PRESSED || action == BUTTON_CLICKED)
    {
        if (main_screen.is_sleeping)
        {
            wake_up_system();  // Wake up when sleeping
        }
        else
        {
            // Trigger screen activity when not sleeping (resets lvgl inactivity time)
            lv_disp_trig_activity(main_screen.disp);
        }
    }
}

// Button initialization function
static void key_button_init(void)
{
    button_cfg_t cfg;

    cfg.pin = BSP_KEY1_PIN;
    cfg.mode = PIN_MODE_INPUT;
#ifdef BSP_KEY1_ACTIVE_HIGH
    cfg.active_state = BUTTON_ACTIVE_HIGH;
#else
    cfg.active_state = BUTTON_ACTIVE_LOW;
#endif
    cfg.button_handler = button_event_handler;

    int32_t id = button_init(&cfg);
    if (id >= 0)
    {
        RT_ASSERT(SF_EOK == button_enable(id));
        main_screen.key1_button_handle = id;
        rt_kprintf("Button initialized successfully\n");
    }
    else
    {
        rt_kprintf("Button initialization failed\n");
    }
}

// Create UI interface (removed original touch event callback, initialize screen display device)
static void create_single_screen_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    if (!scr)
    {
        rt_kprintf("LVGL active screen not found!\n");
        return;
    }

    // 1. Get current screen display device (key: used for subsequent inactivity time retrieval)
    main_screen.disp = lv_disp_get_default();
    if (!main_screen.disp)
    {
        rt_kprintf("LVGL default display not found!\n");
        return;
    }

    // 2. Create background image
    main_screen.img_obj = lv_img_create(scr);
    if (!main_screen.img_obj)
    {
        rt_kprintf("Failed to create image object\n");
        return;
    }
    lv_img_set_src(main_screen.img_obj, &img2);
    lv_obj_align(main_screen.img_obj, LV_ALIGN_CENTER, 0, 0);

    // 3. Create time label
    main_screen.time_label = lv_label_create(scr);
    if (!main_screen.time_label)
    {
        rt_kprintf("Failed to create time label\n");
        return;
    }
    // Set time label style
    lv_obj_set_style_text_font(main_screen.time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(main_screen.time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(main_screen.time_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(main_screen.time_label, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(main_screen.time_label, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(main_screen.time_label, 8, LV_PART_MAIN);
    lv_obj_align(main_screen.time_label, LV_ALIGN_TOP_RIGHT, -100, 50);

    // 4. Initialize time display
    update_time_label();

    // 5. Create time update timer (1 second interval)
    lv_timer_create(time_update_timer, 1000, NULL);

    // 6. Create idle detection timer (500ms interval, detect inactivity time)
    main_screen.idle_check_timer = lv_timer_create(idle_check_timer_cb, 500, NULL);
}

// Wrist raise detection thread
void wrist_detect_task(void *parameter)
{
    gsensors_fifo_t algo_data_buffer = {0};  // Data buffer

    while (1)
    {
        // Read 10 groups of acceleration data in FIFO mode
        struct rt_sensor_data lsm6d_acce[10];
        rt_size_t res = rt_device_read(lsm6d_dev, 0, lsm6d_acce, 10);

        if (res > 0)
        {
            // Filter invalid data (all zeros) and fill buffer
            for (int i = 0; i < res; i++)
            {
                if (lsm6d_acce[i].data.acce.x == 0 &&
                        lsm6d_acce[i].data.acce.y == 0 &&
                        lsm6d_acce[i].data.acce.z == 0)
                {
                    rt_kprintf("Warning: Zero accelerometer data, skipping!\n");
                    continue;
                }
                // Convert to actual physical values (unit: m/s²)
                algo_data_buffer.buf[i].acce_data[0] = (float)lsm6d_acce[i].data.acce.x / 1000.0f * 9.807f;
                algo_data_buffer.buf[i].acce_data[1] = (float)lsm6d_acce[i].data.acce.y / 1000.0f * 9.807f;
                algo_data_buffer.buf[i].acce_data[2] = (float)lsm6d_acce[i].data.acce.z / 1000.0f * 9.807f;
            }

            algo_data_buffer.num = res;
            algo_data_buffer.time_ns = CWM_OS_GetTimeNs();

            // Algorithm data input and processing
            cwm_all_fifo_data_input((void *)algo_data_buffer.buf, algo_data_buffer.num, algo_data_buffer.time_ns);
            CWM_process();

            // Clear buffer
            rt_memset(&algo_data_buffer, 0, sizeof(algo_data_buffer));
        }
        else
        {
            rt_kprintf("Sensor read failed! res = %d\n", res);
            rt_thread_mdelay(1000);  // Extend retry interval when read fails
        }

        // Wrist raise/wrist lower action handling
        if (main_screen.disp != NULL)
        {
            if (g_gesture_info.gesture == 1)
            {
                rt_kprintf(">>> Wrist raise - Turn on screen <<<\n");
                wake_up_system();
                g_gesture_info.gesture = 0;  // Clear action flag
            }
            else if (g_gesture_info.gesture == 2)
            {
                rt_kprintf(">>> Wrist lower - Turn off screen <<<\n");
                enter_sleep_mode();

                g_gesture_info.gesture = 0;  // Clear action flag
            }
        }

        rt_thread_mdelay(200);  // 200ms sampling period
    }
}

// IO initialization (I2C pin configuration)
static void board_io_init(void)
{
    HAL_PIN_Set(PAD_PA40, I2C3_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA39, I2C3_SDA, PIN_PULLUP, 1);
}

// Sensor device open and configuration
static void open_and_config_devices(rt_device_t *lsm6d_dev, rt_device_t *lsm6d_gyro_dev, rt_device_t *lsm6d_step_dev)
{
    *lsm6d_dev = rt_device_find(LSM6DSL_LSM6DSL_DEV_NAME);
    if (*lsm6d_dev == RT_NULL)
    {
        rt_kprintf("Can't find accelerometer device: %s\n", LSM6DSL_LSM6DSL_DEV_NAME);
        return;
    }

    // Open device in read-only mode
    rt_err_t ret = rt_device_open(*lsm6d_dev, RT_DEVICE_FLAG_RDONLY);
    if (ret != RT_EOK)
    {
        rt_kprintf("Open accelerometer device failed! err: %d\n", ret);
        return;
    }

    // Configure sensor: 52Hz output rate + FIFO mode
    rt_device_control(*lsm6d_dev, RT_SENSOR_CTRL_SET_ODR, (void *)52);
    rt_device_control(*lsm6d_dev, RT_SENSOR_CTRL_SET_MODE, (void *)RT_SENSOR_MODE_FIFO);
    rt_kprintf("Accelerometer device opened: ODR=52Hz, Mode=FIFO\n");
}
// Main function
int main(void)
{
    rt_pm_request(PM_SLEEP_MODE_IDLE);

    HAL_LPAON_Sleep();
    // 1. Hardware initialization
    board_io_init();  // IO pin (I2C) initialization

    HAL_PIN_Set_Analog(PAD_PA21, 1);
    HAL_PIN_Set_Analog(PAD_PA31, 1);
    // 2. Sensor initialization
    struct rt_sensor_config cfg;
    cfg.intf.dev_name = "i2c3";
    cfg.intf.user_data = (void *)LSM6DSL_ADDR_DEFAULT;
    cfg.irq_pin.pin = RT_PIN_NONE;
    rt_hw_lsm6dsl_init(LSM6DSL_NAME, &cfg);
    open_and_config_devices(&lsm6d_dev, &lsm6d_gyro_dev, &lsm6d_step_dev);

    rt_kprintf("Hello world!\n");

    // 3. Create wrist detection thread
    rt_err_t thread_ret = rt_thread_init(&wrist_detect_thread,
                                         "wrist_detect",
                                         wrist_detect_task,
                                         NULL,
                                         &wrist_detect_thread_stack[0],
                                         WRIST_DETECT_THREAD_STACK_SIZE,
                                         25,
                                         10);
    if (thread_ret == RT_EOK)
    {
        rt_thread_startup(&wrist_detect_thread);
        rt_kprintf("Wrist detect thread started\n");
    }
    else
    {
        rt_kprintf("Failed to init wrist_detect_thread! err: %d\n", thread_ret);
    }

    // 4. LVGL initialization
    rt_err_t lv_ret = littlevgl2rtt_init(LCD_DEVICE_NAME);
    if (lv_ret != RT_EOK)
    {
        rt_kprintf("LVGL init failed! err: %d\n", lv_ret);
        return lv_ret;
    }
    lv_ex_data_pool_init();

    // 5. Button and UI initialization
    key_button_init();
    create_single_screen_ui();

    // 6. LVGL main loop
    while (1)
    {
        uint32_t ms = lv_task_handler();  // Handle LVGL tasks
        rt_thread_mdelay(ms + 500);
    }

    return RT_EOK;
}