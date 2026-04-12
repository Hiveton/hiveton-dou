#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "rtthread.h"
#include "rtdevice.h"

#include "aw32001_debug.h"

#define AW32001_I2C_BUS_NAME "i2c2"
#define AW32001_DEBUG_POLL_INTERVAL_MS 1000U
#define AW32001_DEBUG_TEXT_BUFFER_SIZE 96U
#define AW32001_I2C_ADDRESS                      0x49
#define AW32001_REG_SYS_STATUS                   0x08
#define AW32001_REG_FAULT                        0x09
#define AW32001_REG_POWERON_CONF                 0x01

static char s_status_text[AW32001_DEBUG_TEXT_BUFFER_SIZE] = "AW32001: init...";
static rt_tick_t s_last_poll_tick = 0U;

static void aw32001_debug_set_charge_enable(struct rt_i2c_bus_device *i2c_bus, bool enable)
{
    uint8_t reg = 0U;
    rt_size_t size;

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_POWERON_CONF, 8, &reg, 1);
    if (size < 1U)
    {
        return;
    }

    if (enable)
    {
        reg &= ~(1U << 3);
    }
    else
    {
        reg |= (1U << 3);
    }

    (void)rt_i2c_mem_write(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_POWERON_CONF, 8, &reg, 1);
}

void aw32001_debug_ensure_charge_enabled(void)
{
    rt_device_t bus;
    struct rt_i2c_bus_device *i2c_bus = RT_NULL;

    bus = (rt_device_t)rt_i2c_bus_device_find(AW32001_I2C_BUS_NAME);
    if (bus == RT_NULL)
    {
        return;
    }
    i2c_bus = (struct rt_i2c_bus_device *)bus;
    aw32001_debug_set_charge_enable(i2c_bus, true);
}

static void aw32001_debug_set_text(const char *text)
{
    if (text != NULL)
    {
        rt_strncpy(s_status_text, text, sizeof(s_status_text) - 1U);
        s_status_text[sizeof(s_status_text) - 1U] = '\0';
    }
}

void aw32001_debug_init(void)
{
    s_last_poll_tick = 0U;
    aw32001_debug_set_text("AW32001: 等待检测");
}

void aw32001_debug_poll_once(void)
{
    rt_tick_t now;
    rt_tick_t now_ms;
    uint8_t charge_status = 0U;
    uint8_t fault_status = 0U;
    rt_device_t bus;
    char text[AW32001_DEBUG_TEXT_BUFFER_SIZE];
    const char *state_text;
    bool charge_enabled = false;
    const char *charge_enable_text;
    uint8_t poweron_cfg = 0U;
    struct rt_i2c_bus_device *i2c_bus = RT_NULL;
    rt_size_t size;

    now = rt_tick_get();
    now_ms = rt_tick_from_millisecond(AW32001_DEBUG_POLL_INTERVAL_MS);
    if ((now_ms != 0U) && ((now - s_last_poll_tick) < now_ms))
    {
        return;
    }
    s_last_poll_tick = now;

    bus = (rt_device_t)rt_i2c_bus_device_find(AW32001_I2C_BUS_NAME);
    if (bus == RT_NULL)
    {
        aw32001_debug_set_text("AW32001: 未检测到I2C2");
        return;
    }
    i2c_bus = (struct rt_i2c_bus_device *)bus;

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_SYS_STATUS, 8, &charge_status, 1);
    if (size < 1U)
    {
        aw32001_debug_set_text("AW32001: 状态读失败");
        return;
    }
    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_FAULT, 8, &fault_status, 1);
    if (size < 1U)
    {
        aw32001_debug_set_text("AW32001: 故障读失败");
        return;
    }

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_POWERON_CONF, 8, &poweron_cfg, 1);
    if (size < 1U)
    {
        aw32001_debug_set_text("AW32001: 配置读失败");
        return;
    }
    if ((poweron_cfg & (1U << 3)) != 0U)
    {
        aw32001_debug_set_charge_enable(i2c_bus, true);
        size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_POWERON_CONF, 8, &poweron_cfg, 1);
        if (size < 1U)
        {
            aw32001_debug_set_text("AW32001: 配置复位后读失败");
            return;
        }
    }
    charge_enabled = ((poweron_cfg & (1U << 3)) == 0U);
    charge_enable_text = charge_enabled ? "开" : "关";

    charge_status = (charge_status & 0x18U) >> 3;

    switch (charge_status)
    {
    case 0:
        if (!charge_enabled)
        {
            state_text = "未充电(手动关闭)";
        }
        else
        {
            state_text = "未充电(待充电条件)";
        }
        break;
    case 1:
        state_text = "预充电";
        break;
    case 2:
        state_text = "充电中";
        break;
    case 3:
        state_text = "已充满";
        break;
    default:
        state_text = "未知";
        break;
    }

    if (fault_status == 0U)
    {
        rt_snprintf(text,
                    sizeof(text),
                    "AW32001: 已连接 | 充电:%s | 状态:%s | 故障:无",
                    charge_enable_text,
                    state_text);
    }
    else
    {
        rt_snprintf(text,
                    sizeof(text),
                    "AW32001: 已连接 | 充电:%s | 状态:%s | 故障:0x%02X",
                    charge_enable_text,
                    state_text,
                    (unsigned int)fault_status);
    }

    aw32001_debug_set_text(text);
}

void aw32001_debug_get_status_text(char *buffer, size_t buffer_size)
{
    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return;
    }

    (void)rt_strncpy(buffer, s_status_text, buffer_size - 1U);
    buffer[buffer_size - 1U] = '\0';
}
