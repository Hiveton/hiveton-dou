/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtdevice.h>
#include <rthw.h>
#include "SEGGER_RTT.h"

static rt_bool_t is_idle_hook_set;
static struct rt_serial_device segger_serial;
static void segger_idle_hook(void)
{
    rt_hw_serial_isr(&segger_serial, RT_SERIAL_EVENT_RX_IND);
}

static rt_err_t segger_control(struct rt_serial_device *serial, int cmd, void *arg)
{
    RT_ASSERT(serial != RT_NULL);
    RT_ASSERT(arg != RT_NULL);

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CLR_INT:
        if (is_idle_hook_set)
        {
            rt_thread_idle_delhook(segger_idle_hook);
            is_idle_hook_set = RT_FALSE;
        }
        break;

    case RT_DEVICE_CTRL_SET_INT:
        if (!is_idle_hook_set)
        {
            rt_thread_idle_sethook(segger_idle_hook);
            is_idle_hook_set = RT_TRUE;
        }
        break;
    }

    return RT_EOK;
}

static int segger_putc(struct rt_serial_device *serial, char c)
{
    RT_ASSERT(serial != RT_NULL);

    return SEGGER_RTT_Write(0, &c, 1);
}

static int segger_getc(struct rt_serial_device *serial)
{
    char ch;

    RT_ASSERT(serial != RT_NULL);

    if (SEGGER_RTT_Read(0, &ch, 1) == 1)
    {
        return ch;
    }
    else
    {
        return -1;
    }
}

static const struct rt_uart_ops segger_uart_ops =
{
    NULL,
    segger_control,
    segger_putc,
    segger_getc,
};

int hw_segger_init(void)
{
    segger_serial.ops = &segger_uart_ops;
    segger_serial.config.bufsz = RT_SERIAL_RB_BUFSZ;

    /* register segger rtt as serial device */
    rt_hw_serial_register(&segger_serial,
                          PKG_SERIAL_DEVICE_NAME,
                          RT_DEVICE_FLAG_RDWR  | RT_DEVICE_FLAG_INT_RX,
                          NULL);

    return 0;
}
