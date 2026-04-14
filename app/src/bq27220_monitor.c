#include <stdbool.h>
#include <stdint.h>

#include "rtthread.h"
#include "rtdevice.h"

#include "bq27220_monitor.h"
#include "sleep_manager.h"
#include "ui/ui_dispatch.h"
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
#define BQ27220_REG_REMAINING_CAPACITY 0x10
#define BQ27220_REG_FULL_CHARGE_CAPACITY 0x12
#define BQ27220_REG_AVERAGE_POWER 0x24
#define BQ27220_REG_STATE_OF_CHARGE 0x2C
#define BQ27220_REG_OPERATION_STATUS 0x3A

#define AW32001_I2C_ADDRESS 0x49
#define AW32001_REG_SYS_STATUS 0x08
#define AW32001_REG_FAULT 0x09

static struct rt_thread s_bq27220_monitor_thread;
static rt_uint8_t s_bq27220_monitor_stack[BQ27220_MONITOR_THREAD_STACK_SIZE];
static bool s_bq27220_monitor_started = false;
static char s_bq27220_status_text[96] = "BQ27220: 等待检测";
static bq27220_power_snapshot_t s_bq27220_power_snapshot = {0};
static bool s_bq27220_power_snapshot_valid = false;
static bq27220_power_snapshot_t s_bq27220_last_published_power_snapshot = {0};

static bool bq27220_monitor_can_scan_battery(void)
{
    return !sleep_manager_is_sleeping();
}

static bool bq27220_monitor_power_snapshot_equal(const bq27220_power_snapshot_t *lhs,
                                                 const bq27220_power_snapshot_t *rhs)
{
    if ((lhs == RT_NULL) || (rhs == RT_NULL))
    {
        return false;
    }

    return lhs->valid == rhs->valid &&
           lhs->battery_percent == rhs->battery_percent &&
           lhs->charging == rhs->charging &&
           lhs->battery_status == rhs->battery_status &&
           lhs->operation_status == rhs->operation_status &&
           lhs->voltage_mv == rhs->voltage_mv &&
           lhs->current_ma == rhs->current_ma &&
           lhs->average_power_mw == rhs->average_power_mw &&
           lhs->battery_present == rhs->battery_present &&
           lhs->init_complete == rhs->init_complete &&
           lhs->smoothing_active == rhs->smoothing_active &&
           lhs->discharge_mode == rhs->discharge_mode &&
           lhs->aw_charge_state == rhs->aw_charge_state &&
           lhs->aw_fault_status == rhs->aw_fault_status;
}

static void bq27220_monitor_publish_power_snapshot(void)
{
    if (!s_bq27220_power_snapshot_valid)
    {
        return;
    }

    if (s_bq27220_last_published_power_snapshot.valid &&
        bq27220_monitor_power_snapshot_equal(&s_bq27220_last_published_power_snapshot,
                                             &s_bq27220_power_snapshot))
    {
        return;
    }

    s_bq27220_last_published_power_snapshot = s_bq27220_power_snapshot;
    s_bq27220_last_published_power_snapshot.valid = true;

    xiaozhi_ui_update_battery_percent(s_bq27220_power_snapshot.battery_percent);
    xiaozhi_ui_update_charge_status(s_bq27220_power_snapshot.charging ? 1U : 0U);
    ui_dispatch_request_status_refresh();
}

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

static bool bq27220_monitor_read_aw_fault_status(struct rt_i2c_bus_device *i2c_bus,
                                                 uint8_t *fault_status)
{
    rt_size_t size;

    if ((i2c_bus == RT_NULL) || (fault_status == RT_NULL))
    {
        return false;
    }

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_FAULT, 8, fault_status, 1);
    if (size < 1U)
    {
        return false;
    }

    return true;
}

static void bq27220_monitor_scan_battery_now(struct rt_i2c_bus_device *i2c_bus)
{
    uint16_t soc = 0U;
    uint16_t battery_status = 0U;
    uint16_t voltage = 0U;
    uint16_t remaining_capacity = 0U;
    uint16_t full_charge_capacity = 0U;
    uint16_t operation_status = 0U;
    int16_t current = 0;
    int16_t average_power = 0;
    char text[96];
    bool has_operation_status = false;
    bool init_complete = false;
    bool battery_present = false;
    bool smoothing_active = false;
    bool discharge_mode = false;

    if (i2c_bus == RT_NULL)
    {
        return;
    }

    if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_STATE_OF_CHARGE, &soc))
    {
        bq27220_power_snapshot_t power_snapshot;

        if (s_bq27220_power_snapshot_valid)
        {
            power_snapshot = s_bq27220_power_snapshot;
        }
        else
        {
            rt_memset(&power_snapshot, 0, sizeof(power_snapshot));
        }

        power_snapshot.valid = true;

        if (soc > 100U)
        {
            soc = 100U;
        }

        power_snapshot.battery_percent = (uint8_t)soc;

        has_operation_status = bq27220_monitor_read_u16(i2c_bus,
                                                        BQ27220_I2C_ADDRESS,
                                                        BQ27220_REG_OPERATION_STATUS,
                                                        &operation_status);
        if (has_operation_status)
        {
            init_complete = ((operation_status & (1U << 5)) != 0U);
            smoothing_active = ((operation_status & (1U << 6)) != 0U);
        }

        if (bq27220_monitor_read_u16(i2c_bus,
                                     BQ27220_I2C_ADDRESS,
                                     BQ27220_REG_BATTERY_STATUS,
                                     &battery_status))
        {
            battery_present = ((battery_status & (1U << 3)) != 0U);
            discharge_mode = ((battery_status & (1U << 0)) != 0U);
        }

        (void)bq27220_monitor_read_u16(i2c_bus,
                                       BQ27220_I2C_ADDRESS,
                                       BQ27220_REG_REMAINING_CAPACITY,
                                       &remaining_capacity);
        (void)bq27220_monitor_read_u16(i2c_bus,
                                       BQ27220_I2C_ADDRESS,
                                       BQ27220_REG_FULL_CHARGE_CAPACITY,
                                       &full_charge_capacity);

        if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_VOLTAGE, &voltage) &&
            bq27220_monitor_read_s16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_CURRENT, &current) &&
            bq27220_monitor_read_s16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_AVERAGE_POWER, &average_power))
        {
            power_snapshot.voltage_mv = voltage;
            power_snapshot.current_ma = current;
            power_snapshot.average_power_mw = average_power;

            rt_snprintf(text,
                        sizeof(text),
                        "BQ27220:%u%% %.2fV %+dmA R%u F%u I%d P%d S%d D%d",
                        (unsigned int)soc,
                        ((float)voltage) / 1000.0f,
                        (int)current,
                        (unsigned int)remaining_capacity,
                        (unsigned int)full_charge_capacity,
                        (int)init_complete,
                        (int)battery_present,
                        (int)smoothing_active,
                        (int)discharge_mode);
        }
        else
        {
            rt_snprintf(text,
                        sizeof(text),
                        "BQ27220:%u%% BS:%04X OS:%04X I%d P%d",
                        (unsigned int)soc,
                        (unsigned int)battery_status,
                        (unsigned int)operation_status,
                        (int)init_complete,
                        (int)battery_present);
        }

        power_snapshot.battery_status = battery_status;
        power_snapshot.operation_status = operation_status;
        power_snapshot.battery_present = battery_present;
        power_snapshot.init_complete = init_complete;
        power_snapshot.smoothing_active = smoothing_active;
        power_snapshot.discharge_mode = discharge_mode;

        s_bq27220_power_snapshot = power_snapshot;
        s_bq27220_power_snapshot_valid = true;
        bq27220_monitor_publish_power_snapshot();
        bq27220_monitor_set_status_text(text);
    }
    else
    {
        bq27220_monitor_set_status_text("BQ27220: 电量读取失败");
    }
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
            uint8_t aw_fault_status = 0U;
            rt_tick_t now_tick = rt_tick_get();
            bool need_battery_scan = false;

            i2c_bus = (struct rt_i2c_bus_device *)bus;

            if (!bq27220_monitor_can_scan_battery())
            {
                last_battery_scan_tick = 0U;
                rt_thread_mdelay(BQ27220_MONITOR_POLL_MS);
                continue;
            }

            if ((last_battery_scan_tick == 0U) ||
                ((now_tick - last_battery_scan_tick) >= rt_tick_from_millisecond(BQ27220_MONITOR_BATTERY_SCAN_MS)))
            {
                need_battery_scan = true;
                last_battery_scan_tick = now_tick;
            }

            if (need_battery_scan)
            {
                bq27220_monitor_scan_battery_now(i2c_bus);
            }

            if (bq27220_monitor_read_aw_charge_state(i2c_bus, &aw_charge_state))
            {
                bool snapshot_dirty = false;
                int charge_now = ((aw_charge_state == 1U) || (aw_charge_state == 2U)) ? 1 : 0;
                bool aw_fault_valid = bq27220_monitor_read_aw_fault_status(i2c_bus, &aw_fault_status);

                if (s_bq27220_power_snapshot_valid &&
                    (s_bq27220_power_snapshot.charging != (charge_now != 0)))
                {
                    s_bq27220_power_snapshot.charging = (charge_now != 0);
                    snapshot_dirty = true;
                }

                if (s_bq27220_power_snapshot_valid &&
                    (s_bq27220_power_snapshot.aw_charge_state != aw_charge_state))
                {
                    s_bq27220_power_snapshot.aw_charge_state = aw_charge_state;
                    snapshot_dirty = true;
                }

                if (aw_fault_valid &&
                    s_bq27220_power_snapshot_valid &&
                    (s_bq27220_power_snapshot.aw_fault_status != aw_fault_status))
                {
                    s_bq27220_power_snapshot.aw_fault_status = aw_fault_status;
                    snapshot_dirty = true;
                }

                if (snapshot_dirty)
                {
                    bq27220_monitor_scan_battery_now(i2c_bus);
                    bq27220_monitor_publish_power_snapshot();
                    last_battery_scan_tick = 0U;
                }
            }
            else if (bq27220_monitor_read_u16(i2c_bus, BQ27220_I2C_ADDRESS, BQ27220_REG_BATTERY_STATUS, &battery_status))
            {
                bool snapshot_dirty = false;
                int charge_now = ((battery_status & (1U << 6)) == 0U) ? 1 : 0;

                if (s_bq27220_power_snapshot_valid &&
                    (s_bq27220_power_snapshot.charging != (charge_now != 0)))
                {
                    s_bq27220_power_snapshot.charging = (charge_now != 0);
                    snapshot_dirty = true;
                }

                if (s_bq27220_power_snapshot_valid &&
                    (s_bq27220_power_snapshot.battery_status != battery_status))
                {
                    s_bq27220_power_snapshot.battery_status = battery_status;
                    snapshot_dirty = true;
                }

                if (snapshot_dirty)
                {
                    bq27220_monitor_scan_battery_now(i2c_bus);
                    bq27220_monitor_publish_power_snapshot();
                    last_battery_scan_tick = 0U;
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

void bq27220_monitor_get_power_snapshot(bq27220_power_snapshot_t *snapshot)
{
    if (snapshot == RT_NULL)
    {
        return;
    }

    if (!s_bq27220_power_snapshot_valid)
    {
        rt_memset(snapshot, 0, sizeof(*snapshot));
        return;
    }

    *snapshot = s_bq27220_power_snapshot;
}
