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
#define AW32001_REG_SOURCE_CTRL                  0x00
#define AW32001_REG_POWERON_CONF                 0x01
#define AW32001_REG_CHARGE_CURRENT_CTRL          0x02
#define AW32001_REG_CHARGE_VOLTAGE               0x04
#define AW32001_REG_TIMER_CTRL                   0x05
#define AW32001_REG_VOLT_CTRL                    0x07
#define AW32001_REG_SYS_STATUS                   0x08
#define AW32001_REG_FAULT                        0x09
#define AW32001_REG_ADD_FUNC_0                   0x0C

#define AW32001_MAX_SOURCE_CTRL                  0x8F
#define AW32001_MAX_CHARGE_CURRENT_CTRL          0x3F
#define AW32001_SAFE_CHARGE_VOLTAGE_4200MV       0xA3
#define AW32001_SAFE_SYS_VOLTAGE_4600MV          0x38
#define AW32001_ADD_FUNC_EN0P55                  0x80

static char s_status_text[AW32001_DEBUG_TEXT_BUFFER_SIZE] = "AW32001: init...";
static rt_tick_t s_last_poll_tick = 0U;
static bool s_aw32001_max_charge_configured = false;

static bool aw32001_debug_read_reg(struct rt_i2c_bus_device *i2c_bus, uint8_t reg_addr, uint8_t *value)
{
    rt_size_t size;

    if (i2c_bus == RT_NULL || value == NULL)
    {
        return false;
    }

    size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, reg_addr, 8, value, 1);
    return size >= 1U;
}

static bool aw32001_debug_write_reg(struct rt_i2c_bus_device *i2c_bus, uint8_t reg_addr, uint8_t value)
{
    rt_size_t size;

    if (i2c_bus == RT_NULL)
    {
        return false;
    }

    size = rt_i2c_mem_write(i2c_bus, AW32001_I2C_ADDRESS, reg_addr, 8, &value, 1);
    return size >= 1U;
}

static bool aw32001_debug_update_reg(struct rt_i2c_bus_device *i2c_bus,
                                     uint8_t reg_addr,
                                     uint8_t mask,
                                     uint8_t value)
{
    uint8_t reg = 0U;

    if (!aw32001_debug_read_reg(i2c_bus, reg_addr, &reg))
    {
        return false;
    }

    reg = (uint8_t)((reg & (uint8_t)(~mask)) | (value & mask));
    return aw32001_debug_write_reg(i2c_bus, reg_addr, reg);
}

static bool aw32001_debug_set_charge_enable(struct rt_i2c_bus_device *i2c_bus, bool enable)
{
    return aw32001_debug_update_reg(i2c_bus,
                                    AW32001_REG_POWERON_CONF,
                                    (uint8_t)(1U << 3),
                                    enable ? 0U : (uint8_t)(1U << 3));
}

static bool aw32001_debug_configure_max_charge_on_bus(struct rt_i2c_bus_device *i2c_bus)
{
    bool ok = true;

    if (i2c_bus == RT_NULL)
    {
        return false;
    }

    ok = aw32001_debug_update_reg(i2c_bus,
                                  AW32001_REG_TIMER_CTRL,
                                  (uint8_t)(3U << 5),
                                  0U) && ok;
    ok = aw32001_debug_update_reg(i2c_bus,
                                  AW32001_REG_POWERON_CONF,
                                  (uint8_t)((1U << 4) | (1U << 3)),
                                  0U) && ok;
    ok = aw32001_debug_write_reg(i2c_bus,
                                 AW32001_REG_SOURCE_CTRL,
                                 AW32001_MAX_SOURCE_CTRL) && ok;
    ok = aw32001_debug_update_reg(i2c_bus,
                                  AW32001_REG_ADD_FUNC_0,
                                  AW32001_ADD_FUNC_EN0P55,
                                  AW32001_ADD_FUNC_EN0P55) && ok;
    ok = aw32001_debug_write_reg(i2c_bus,
                                 AW32001_REG_CHARGE_CURRENT_CTRL,
                                 AW32001_MAX_CHARGE_CURRENT_CTRL) && ok;
    ok = aw32001_debug_write_reg(i2c_bus,
                                 AW32001_REG_CHARGE_VOLTAGE,
                                 AW32001_SAFE_CHARGE_VOLTAGE_4200MV) && ok;
    ok = aw32001_debug_write_reg(i2c_bus,
                                 AW32001_REG_VOLT_CTRL,
                                 AW32001_SAFE_SYS_VOLTAGE_4600MV) && ok;

    s_aw32001_max_charge_configured = ok;
    if (ok)
    {
        rt_kprintf("AW32001: max charge configured IIN=550mA ICHG=512mA VBAT=4.20V VSYS=4.60V\n");
    }
    else
    {
        rt_kprintf("AW32001: max charge configure failed\n");
    }
    return ok;
}

bool aw32001_debug_configure_max_charge(void)
{
    rt_device_t bus;

    bus = (rt_device_t)rt_i2c_bus_device_find(AW32001_I2C_BUS_NAME);
    if (bus == RT_NULL)
    {
        return false;
    }

    return aw32001_debug_configure_max_charge_on_bus((struct rt_i2c_bus_device *)bus);
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
    if (!aw32001_debug_configure_max_charge_on_bus(i2c_bus))
    {
        (void)aw32001_debug_set_charge_enable(i2c_bus, true);
    }
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
    uint8_t source_ctrl = 0U;
    uint8_t charge_current_ctrl = 0U;
    uint8_t add_func = 0U;
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
    if (!s_aw32001_max_charge_configured || ((poweron_cfg & (1U << 3)) != 0U))
    {
        (void)aw32001_debug_configure_max_charge_on_bus(i2c_bus);
        size = rt_i2c_mem_read(i2c_bus, AW32001_I2C_ADDRESS, AW32001_REG_POWERON_CONF, 8, &poweron_cfg, 1);
        if (size < 1U)
        {
            aw32001_debug_set_text("AW32001: 配置复位后读失败");
            return;
        }
    }
    charge_enabled = ((poweron_cfg & (1U << 3)) == 0U);
    charge_enable_text = charge_enabled ? "开" : "关";
    (void)aw32001_debug_read_reg(i2c_bus, AW32001_REG_SOURCE_CTRL, &source_ctrl);
    (void)aw32001_debug_read_reg(i2c_bus, AW32001_REG_CHARGE_CURRENT_CTRL, &charge_current_ctrl);
    (void)aw32001_debug_read_reg(i2c_bus, AW32001_REG_ADD_FUNC_0, &add_func);

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
                    "AW32001: %s %s IIN%u ICHG%u F0x%02X",
                    charge_enable_text,
                    state_text,
                    ((source_ctrl & 0x0FU) == 0x0FU && (add_func & AW32001_ADD_FUNC_EN0P55) != 0U) ? 550U : (50U + (unsigned int)(source_ctrl & 0x0FU) * 30U),
                    (unsigned int)(((charge_current_ctrl & 0x3FU) + 1U) * 8U),
                    (unsigned int)fault_status);
    }
    else
    {
        rt_snprintf(text,
                    sizeof(text),
                    "AW32001: %s %s IIN%u ICHG%u F0x%02X",
                    charge_enable_text,
                    state_text,
                    ((source_ctrl & 0x0FU) == 0x0FU && (add_func & AW32001_ADD_FUNC_EN0P55) != 0U) ? 550U : (50U + (unsigned int)(source_ctrl & 0x0FU) * 30U),
                    (unsigned int)(((charge_current_ctrl & 0x3FU) + 1U) * 8U),
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
