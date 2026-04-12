#include <stdbool.h>
#include <stdint.h>

#include "rtthread.h"
#include "rtdevice.h"

#include "bq27220_monitor.h"
#include "xiaozhi/xiaozhi_ui.h"

#define BQ27220_MONITOR_THREAD_STACK_SIZE 2048
#define BQ27220_MONITOR_THREAD_PRIORITY 21
#define BQ27220_MONITOR_THREAD_TICK 10
#define BQ27220_MONITOR_POLL_MS 5000U
#define BQ27220_MONITOR_BATTERY_SCAN_MS 60000U

#define BQ27220_I2C_BUS_NAME "i2c2"
#define BQ27220_I2C_ADDRESS 0x55
#define BQ27220_REG_VOLTAGE 0x08
#define BQ27220_REG_BATTERY_STATUS 0x0A
#define BQ27220_REG_CURRENT 0x0C
#define BQ27220_REG_AVERAGE_POWER 0x24
#define BQ27220_REG_STATE_OF_CHARGE 0x2C

#define AW32001_I2C_ADDRESS 0x49
#define AW32001_REG_SYS_STATUS 0x08

static struct rt_thread s_bq27220_monitor_thread;
static rt_uint8_t s_bq27220_monitor_stack[BQ27220_MONITOR_THREAD_STACK_SIZE];
static bool s_bq27220_monitor_started = false;
static char s_bq27220_status_text[96] = "BQ27220: 等待检测";
static int s_bq27220_last_soc = -1;
static int s_bq27220_last_charge = -1;

static void bq27220_monitor_set_status_text(const char *text)
{
    if ((text != RT_NULL) && (rt_strcmp(s_bq27220_status_text, text) != 0))
    {
        rt_strncpy(s_bq27220_status_text, text, sizeof(s_bq27220_status_text) - 1U);
        s_bq27220_status_text[sizeof(s_bq27220_status_text) - 1U] = '\0';
    }
}

static bool bq27220_monitor_read_u16(struct rt_i2c_bus_device *i2c_bus,
                                     uint8_t slave_addr,
                                     uint8_t reg,
                                     uint16_t *value)
{
    uint8_t raw[2];
    rt_size_t size;

    if ((i2c_bus == RT_NULL) || (value == RT_NULL))
    {
        return false;
    }

    size = rt_i2c_mem_read(i2c_bus, slave_addr, reg, 8, raw, sizeof(raw));
    if (size < sizeof(raw))
    {
        return false;
    }

    *value = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    return true;
}

static bool bq27220_monitor_read_s16(struct rt_i2c_bus_device *i2c_bus,
                                     uint8_t slave_addr,
                                     uint8_t reg,
                                     int16_t *value)
{
    uint16_t raw = 0U;

    if ((value == RT_NULL) || !bq27220_monitor_read_u16(i2c_bus, slave_addr, reg, &raw))
    {
        return false;
    }

    *value = (int16_t)raw;
    return true;
}

static bool bq27220_monitor_read_aw_charge_state(struct rt_i2c_bus_device *i2c_bus,
                                                 uint8_t *charge_state)
{
    rt_size_t size;

    if ((i2c_bus == RT_NULL) || (charge_state == RT_NULL))
    {
        return false;
    }

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_SYS_STATUS, 8, charge_state, 1);
    if (size < 1U)
    {
        return false;
    }

    *charge_state = (uint8_t)((*charge_state & 0x18U) >> 3);
    return true;
}

static void bq27220_monitor_thread_entry(void *parameter)
{
    struct rt_i2c_bus_device *i2c_bus = RT_NULL;
    rt_tick_t last_battery_scan_tick = 0U;

    (void)parameter;

    while (1)
    {
        rt_device_t bus = (rt_device_t)rt_i2c_bus_device_find(BQ27220_I2C_BUS_NAME);

        if (bus != RT_NULL)
        {
            uint16_t battery_status = 0U;
            uint8_t aw_charge_state = 0U;
            rt_tick_t now_tick = rt_tick_get();
            bool need_battery_scan = false;

            i2c_bus = (struct rt_i2c_bus_device *)bus;

            if ((last_battery_scan_tick == 0U) ||
                ((now_tick - last_battery_scan_tick) >= rt_tick_from_millisecond(BQ27220_MONITOR_BATTERY_SCAN_MS)))
            {
                need_battery_scan = true;
                last_battery_scan_tick = now_tick;
            }

            if (need_battery_scan)
            {
                uint16_t soc = 0U;
                uint16_t voltage = 0U;
                int16_t current = 0;
                int16_t average_power = 0;
                char text[96];

                if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_STATE_OF_CHARGE, &soc))
                {
                    if (soc > 100U)
                    {
                        soc = 100U;
                    }

                    if (s_bq27220_last_soc != (int)soc)
                    {
                        s_bq27220_last_soc = (int)soc;
                        xiaozhi_ui_update_battery_percent((uint8_t)soc);
                    }

                    if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_VOLTAGE, &voltage) &&
                        bq27220_monitor_read_s16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_CURRENT, &current) &&
                        bq27220_monitor_read_s16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_AVERAGE_POWER, &average_power))
                    {
                        rt_snprintf(text,
                                    sizeof(text),
                                    "BQ27220: %u%% %.2fV %+dmA %+dmW",
                                    (unsigned int)soc,
                                    ((float)voltage) / 1000.0f,
                                    (int)current,
                                    (int)average_power);
                    }
                    else if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_BATTERY_STATUS, &battery_status))
                    {
                        rt_snprintf(text,
                                    sizeof(text),
                                    "BQ27220: %u%% | 状态字:0x%04X | 实时量读取失败",
                                    (unsigned int)soc,
                                    (unsigned int)battery_status);
                    }
                    else
                    {
                        rt_snprintf(text,
                                    sizeof(text),
                                    "BQ27220: %u%% | 电压电流功率读取失败",
                                    (unsigned int)soc);
                    }
                    bq27220_monitor_set_status_text(text);
                }
                else
                {
                    bq27220_monitor_set_status_text("BQ27220: 电量读取失败");
                }
            }

            if (bq27220_monitor_read_aw_charge_state(i2c_bus, &aw_charge_state))
            {
                int charge_now = ((aw_charge_state == 1U) || (aw_charge_state == 2U)) ? 1 : 0;

                if (s_bq27220_last_charge != charge_now)
                {
                    s_bq27220_last_charge = charge_now;
                    xiaozhi_ui_update_charge_status((uint8_t)charge_now);
                }
            }
            else if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_BATTERY_STATUS, &battery_status))
            {
                int charge_now = ((battery_status & (1U << 6)) == 0U) ? 1 : 0;

                if (s_bq27220_last_charge != charge_now)
                {
                    s_bq27220_last_charge = charge_now;
                    xiaozhi_ui_update_charge_status((uint8_t)charge_now);
                }
            }
        }

        rt_thread_mdelay(BQ27220_MONITOR_POLL_MS);
    }
}

void bq27220_monitor_start(void)
{
    if (s_bq27220_monitor_started)
    {
        return;
    }

    if (rt_thread_init(&s_bq27220_monitor_thread,
                       "bq27220",
                       bq27220_monitor_thread_entry,
                       RT_NULL,
                       s_bq27220_monitor_stack,
                       sizeof(s_bq27220_monitor_stack),
                       BQ27220_MONITOR_THREAD_PRIORITY,
                       BQ27220_MONITOR_THREAD_TICK) == RT_EOK)
    {
        rt_thread_startup(&s_bq27220_monitor_thread);
        s_bq27220_monitor_started = true;
    }
}

void bq27220_monitor_get_status_text(char *buffer, rt_size_t buffer_size)
{
    if ((buffer == RT_NULL) || (buffer_size == 0U))
    {
        return;
    }

    rt_strncpy(buffer, s_bq27220_status_text, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
}
