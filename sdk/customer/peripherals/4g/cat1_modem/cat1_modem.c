#include "cat1_modem.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "at.h"
#include "drv_gpio.h"
#include "rtdevice.h"

#define DBG_TAG "cat1"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifndef CAT1_MODEM_UART_NAME
#define CAT1_MODEM_UART_NAME "uart2"
#endif

#ifndef CAT1_MODEM_BAUDRATE
#define CAT1_MODEM_BAUDRATE 115200
#endif

#ifndef CAT1_MODEM_RECV_BUF_SIZE
#define CAT1_MODEM_RECV_BUF_SIZE 512
#endif

#ifndef CAT1_MODEM_SEND_BUF_SIZE
#define CAT1_MODEM_SEND_BUF_SIZE 256
#endif

#ifndef CAT1_MODEM_POWERKEY_PIN
#define CAT1_MODEM_POWERKEY_PIN GET_PIN(1, 20)
#endif

#ifndef CAT1_MODEM_POWER_EN_PIN
#define CAT1_MODEM_POWER_EN_PIN GET_PIN(1, 9)
#endif

#ifndef CAT1_MODEM_APN
#define CAT1_MODEM_APN "CMNET"
#endif

#define CAT1_MODEM_THREAD_STACK_SIZE 4096
#define CAT1_MODEM_THREAD_PRIORITY   16
#define CAT1_MODEM_THREAD_TICK       10
#define CAT1_MODEM_POLL_INTERVAL_MS  5000U
#define CAT1_MODEM_POWER_STABLE_MS   200U
#define CAT1_MODEM_REPOWER_DELAY_MS  1000U
#define CAT1_MODEM_POWERKEY_PULSE_MS 800U
#define CAT1_MODEM_BOOT_WAIT_MS      10000U
#define CAT1_MODEM_AT_REPOWER_THRESHOLD 5U

static struct rt_thread s_cat1_thread;
static rt_uint8_t s_cat1_stack[CAT1_MODEM_THREAD_STACK_SIZE];
static volatile rt_uint8_t s_cat1_ready = 0U;
static volatile rt_uint8_t s_online_requested = 0U;
static volatile rt_uint8_t s_client_ready = 0U;
static volatile rt_uint8_t s_power_enabled = 0U;
static volatile rt_uint8_t s_boot_latched = 0U;
static volatile rt_uint8_t s_at_failures = 0U;
static volatile rt_uint32_t s_detected_baud = CAT1_MODEM_BAUDRATE;
static char s_cat1_status_text[96] = "4G: 待首页启动";

static int cat1_modem_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...);
static void cat1_modem_dump_response(at_response_t resp);
static void cat1_modem_mark_ready(bool ready);
static void cat1_modem_repower_module(void);
static rt_bool_t cat1_modem_handle_at_failures(const char *reason);

static void cat1_modem_set_status_text(const char *text)
{
    if (text == RT_NULL)
    {
        return;
    }

    rt_snprintf(s_cat1_status_text, sizeof(s_cat1_status_text), "%s", text);
}

static rt_size_t cat1_modem_raw_exchange(rt_device_t uart_device,
                                         const char *cmd,
                                         char *rx_buf,
                                         rt_size_t rx_buf_size,
                                         rt_uint32_t timeout_ms)
{
    rt_tick_t deadline;
    rt_size_t total = 0;

    while (rt_device_read(uart_device, -1, rx_buf, rx_buf_size) > 0)
    {
    }

    (void)rt_device_write(uart_device, 0, cmd, rt_strlen(cmd));

    deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    rt_memset(rx_buf, 0, rx_buf_size);
    while (rt_tick_get() < deadline && total < rx_buf_size - 1)
    {
        rt_size_t read_len = rt_device_read(uart_device, -1, &rx_buf[total], rx_buf_size - 1 - total);
        if (read_len > 0)
        {
            total += read_len;
            continue;
        }
        rt_thread_mdelay(20);
    }

    rx_buf[total] = '\0';
    return total;
}

static void cat1_modem_raw_dump(const char *prefix, const char *rx_buf, rt_size_t total)
{
    rt_kprintf("%s recv_len=%d data=`%s`\n", prefix, (int)total, rx_buf);
    if (total > 0)
    {
        rt_size_t i;
        rt_kprintf("%s recv_hex=", prefix);
        for (i = 0; i < total; ++i)
        {
            rt_kprintf("%02X", (unsigned char)rx_buf[i]);
            if (i + 1U < total)
            {
                rt_kprintf(" ");
            }
        }
        rt_kprintf("\n");

        rt_kprintf("%s recv_ascii=", prefix);
        for (i = 0; i < total; ++i)
        {
            unsigned char ch = (unsigned char)rx_buf[i];
            if (ch >= 32U && ch <= 126U)
            {
                rt_kprintf("%c", ch);
            }
            else
            {
                rt_kprintf(".");
            }
        }
        rt_kprintf("\n");
    }
}

static rt_bool_t cat1_modem_raw_has_success(const char *rx_buf)
{
    const char *p = rx_buf;

    if (strstr(rx_buf, "OK") != RT_NULL)
    {
        return RT_TRUE;
    }

    while (*p != '\0')
    {
        while (*p == '\r' || *p == '\n' || *p == ' ')
        {
            ++p;
        }
        if (*p == '0' && (p[1] == '\0' || p[1] == '\r' || p[1] == '\n' || p[1] == ' '))
        {
            return RT_TRUE;
        }
        while (*p != '\0' && *p != '\r' && *p != '\n')
        {
            ++p;
        }
    }

    return RT_FALSE;
}

static rt_err_t cat1_modem_raw_expect_ok(rt_device_t uart_device,
                                         rt_uint32_t baud_rate,
                                         const char *cmd,
                                         rt_uint32_t timeout_ms,
                                         rt_uint32_t retries)
{
    char rx_buf[64];
    rt_uint32_t attempt;

    for (attempt = 0; attempt < retries; ++attempt)
    {
        rt_size_t total;

        rt_thread_mdelay(80);
        rt_kprintf("cat1_raw: baud=%u profile `%s` try=%u\n",
                   (unsigned int)baud_rate,
                   cmd,
                   (unsigned int)(attempt + 1U));
        total = cat1_modem_raw_exchange(uart_device, cmd, rx_buf, sizeof(rx_buf), timeout_ms);
        cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
        if (cat1_modem_raw_has_success(rx_buf))
        {
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

static rt_err_t cat1_modem_raw_apply_profile(rt_device_t uart_device, rt_uint32_t baud_rate)
{
    struct
    {
        const char *cmd;
    } profile_cmds[] = {
        { "ATQ0V1\r" },
        { "AT\r" },
        { "ATE0\r" },
        { "ATV1\r" },
        { "ATQ0\r" },
        { "AT+IFC=0,0\r" },
        { "AT\r" },
    };
    rt_size_t i;

    for (i = 0; i < sizeof(profile_cmds) / sizeof(profile_cmds[0]); ++i)
    {
        if (cat1_modem_raw_expect_ok(uart_device, baud_rate, profile_cmds[i].cmd, 1000U, 3U) != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

static int cat1_modem_raw_probe_at(void)
{
    rt_device_t uart_device;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t open_result;
    rt_bool_t opened_here = RT_FALSE;
    char rx_buf[64];
    const char at_cmd[] = "AT\r";
    const char rescue_cmd[] = "ATQ0V1\r";
    rt_size_t total;
    rt_size_t rescue_total;

    uart_device = rt_device_find(CAT1_MODEM_UART_NAME);
    if (uart_device == RT_NULL)
    {
        rt_kprintf("cat1_raw: uart device %s not found\n", CAT1_MODEM_UART_NAME);
        return -RT_ENOSYS;
    }

    config.baud_rate = CAT1_MODEM_BAUDRATE;
    rt_device_control(uart_device, RT_DEVICE_CTRL_CONFIG, &config);

    open_result = rt_device_open(uart_device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (open_result == RT_EOK)
    {
        opened_here = RT_TRUE;
    }
    else if (open_result != -RT_EBUSY)
    {
        rt_kprintf("cat1_raw: open %s failed: %d\n", CAT1_MODEM_UART_NAME, open_result);
        return open_result;
    }

    config.baud_rate = CAT1_MODEM_BAUDRATE;
    rt_device_control(uart_device, RT_DEVICE_CTRL_CONFIG, &config);
    rt_thread_mdelay(50);

    rt_kprintf("cat1_raw: baud=%u send AT\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    total = cat1_modem_raw_exchange(uart_device, at_cmd, rx_buf, sizeof(rx_buf), 800U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (cat1_modem_raw_has_success(rx_buf))
    {
        s_detected_baud = CAT1_MODEM_BAUDRATE;
        if (cat1_modem_raw_apply_profile(uart_device, CAT1_MODEM_BAUDRATE) == RT_EOK)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return RT_EOK;
        }
    }

    rt_kprintf("cat1_raw: baud=%u send ATQ0V1\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    rescue_total = cat1_modem_raw_exchange(uart_device, rescue_cmd, rx_buf, sizeof(rx_buf), 300U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, rescue_total);

    rt_kprintf("cat1_raw: baud=%u retry AT\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    total = cat1_modem_raw_exchange(uart_device, at_cmd, rx_buf, sizeof(rx_buf), 800U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (cat1_modem_raw_has_success(rx_buf))
    {
        s_detected_baud = CAT1_MODEM_BAUDRATE;
        if (cat1_modem_raw_apply_profile(uart_device, CAT1_MODEM_BAUDRATE) == RT_EOK)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return RT_EOK;
        }
    }

    if (opened_here)
    {
        rt_device_close(uart_device);
    }

    return -RT_ETIMEOUT;
}

static void cat1_modem_pin_init(void)
{
    rt_pin_mode(CAT1_MODEM_POWER_EN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(CAT1_MODEM_POWER_EN_PIN, PIN_HIGH);
    s_power_enabled = 1U;
    rt_pin_mode(CAT1_MODEM_POWERKEY_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(CAT1_MODEM_POWERKEY_PIN, PIN_HIGH);
}

static void cat1_modem_power_enable(rt_bool_t enable)
{
    rt_pin_write(CAT1_MODEM_POWER_EN_PIN, enable ? PIN_HIGH : PIN_LOW);
    s_power_enabled = enable ? 1U : 0U;
    rt_thread_mdelay(CAT1_MODEM_POWER_STABLE_MS);
}

static void cat1_modem_powerkey_pulse(void)
{
    rt_kprintf("cat1: pwrkey assert for %u ms\n", CAT1_MODEM_POWERKEY_PULSE_MS);
    rt_pin_write(CAT1_MODEM_POWERKEY_PIN, PIN_LOW);
    rt_thread_mdelay(CAT1_MODEM_POWERKEY_PULSE_MS);
    rt_pin_write(CAT1_MODEM_POWERKEY_PIN, PIN_HIGH);
    rt_kprintf("cat1: wait boot %u ms\n", CAT1_MODEM_BOOT_WAIT_MS);
    rt_thread_mdelay(CAT1_MODEM_BOOT_WAIT_MS);
}

static void cat1_modem_power_on_sequence(void)
{
    if (!s_power_enabled)
    {
        LOG_I("CAT1 power enable");
        cat1_modem_power_enable(RT_TRUE);
    }

    cat1_modem_set_status_text("4G: 模组上电中");
    cat1_modem_powerkey_pulse();
}

static void cat1_modem_reset_boot_state(void)
{
    s_boot_latched = 0U;
    s_at_failures = 0U;
    cat1_modem_set_status_text("4G: 准备重新上电");
}

static void cat1_modem_repower_module(void)
{
    LOG_W("CAT1 power cycle start");
    cat1_modem_mark_ready(false);
    s_client_ready = 0U;
    cat1_modem_reset_boot_state();
    cat1_modem_set_status_text("4G: 断电重启中");
    cat1_modem_power_enable(RT_FALSE);
    rt_thread_mdelay(CAT1_MODEM_REPOWER_DELAY_MS);
    cat1_modem_power_on_sequence();
    s_boot_latched = 1U;
    s_at_failures = 0U;
}

static rt_bool_t cat1_modem_handle_at_failures(const char *reason)
{
    if (s_at_failures > CAT1_MODEM_AT_REPOWER_THRESHOLD)
    {
        LOG_W("CAT1 repower after AT failures=%u reason=%s",
              (unsigned int)s_at_failures,
              reason != RT_NULL ? reason : "unknown");
        cat1_modem_repower_module();
        return RT_TRUE;
    }

    return RT_FALSE;
}

static rt_err_t cat1_modem_ensure_client(void)
{
    at_client_t client;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t ret;

    if (s_client_ready)
    {
        return RT_EOK;
    }

    ret = at_client_init(CAT1_MODEM_UART_NAME,
                         CAT1_MODEM_RECV_BUF_SIZE,
                         CAT1_MODEM_SEND_BUF_SIZE);
    if ((ret != RT_EOK) && (ret != -RT_EBUSY))
    {
        LOG_E("at_client_init failed: %d", ret);
        return ret;
    }

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        LOG_E("at client %s not found", CAT1_MODEM_UART_NAME);
        return -RT_ERROR;
    }

    config.baud_rate = s_detected_baud;
    rt_device_control(client->device, RT_DEVICE_CTRL_CONFIG, &config);
    s_client_ready = 1U;
    cat1_modem_set_status_text("4G: AT客户端已连接");
    return RT_EOK;
}

static int cat1_modem_exec_simple(const char *cmd,
                                  const char *kw1,
                                  const char *kw2,
                                  rt_int32_t timeout)
{
    at_client_t client;
    at_response_t resp;
    int result;

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        return -RT_ERROR;
    }

    resp = at_create_resp(256, 0, timeout);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    result = cat1_modem_exec_cmd(client, resp, "%s", cmd);
    if (result < 0)
    {
        at_delete_resp(resp);
        return result;
    }

    if ((kw1 != RT_NULL && at_resp_get_line_by_kw(resp, kw1) == RT_NULL) &&
        (kw2 != RT_NULL && at_resp_get_line_by_kw(resp, kw2) == RT_NULL))
    {
        at_delete_resp(resp);
        return -RT_ERROR;
    }

    at_delete_resp(resp);
    return RT_EOK;
}

static int cat1_modem_try_at_ready(void)
{
    rt_kprintf("cat1: probe AT\n");

    if (cat1_modem_exec_simple("AT", "OK", RT_NULL, 1000) == RT_EOK)
    {
        (void)cat1_modem_exec_simple("ATE0", "OK", RT_NULL, 1000);
        return RT_EOK;
    }

    return -RT_ETIMEOUT;
}

static int cat1_modem_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...)
{
    va_list args;
    int result = RT_EOK;
    rt_size_t send_len;

    if (client == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(client->lock, RT_WAITING_FOREVER);

    client->resp_status = AT_RESP_OK;
    if (resp != RT_NULL)
    {
        resp->buf_len = 0;
        resp->line_counts = 0;
    }

    client->resp = resp;
    rt_sem_control(client->resp_notice, RT_IPC_CMD_RESET, RT_NULL);

    va_start(args, cmd_expr);
    send_len = vsnprintf(client->send_buf, client->send_bufsz - 1, cmd_expr, args);
    va_end(args);

    if (send_len >= client->send_bufsz - 1)
    {
        send_len = client->send_bufsz - 2;
    }

    client->send_buf[send_len++] = '\r';
    client->send_buf[send_len] = '\0';
    client->last_cmd_len = send_len - 1;

    if (rt_device_write(client->device, 0, client->send_buf, send_len) != send_len)
    {
        ++s_at_failures;
        client->resp = RT_NULL;
        rt_mutex_release(client->lock);
        return -RT_ERROR;
    }

    if (resp != RT_NULL)
    {
        if (rt_sem_take(client->resp_notice, resp->timeout) != RT_EOK)
        {
            LOG_W("execute command (%.*s) timeout (%d ticks)!",
                  client->last_cmd_len,
                  client->send_buf,
                  resp->timeout);
            client->resp_status = AT_RESP_TIMEOUT;
            ++s_at_failures;
            result = -RT_ETIMEOUT;
        }
        else if (client->resp_status != AT_RESP_OK)
        {
            LOG_E("execute command (%.*s) failed!",
                  client->last_cmd_len,
                  client->send_buf);
            ++s_at_failures;
            result = -RT_ERROR;
        }
    }

    if (result == RT_EOK)
    {
        s_at_failures = 0U;
    }

    client->resp = RT_NULL;
    rt_mutex_release(client->lock);

    return result;
}

static int cat1_modem_wait_registered(void)
{
    at_client_t client;
    at_response_t resp;
    int i;

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        return -RT_ERROR;
    }

    resp = at_create_resp(256, 0, 3000);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    for (i = 0; i < 20; ++i)
    {
        int stat = 0;
        int cfun = -1;
        int cgatt = -1;
        int rssi = -1;
        int ber = -1;
        const char *line = RT_NULL;

        cat1_modem_set_status_text("4G: 检查SIM卡");
        if (cat1_modem_exec_cmd(client, resp, "AT+CPIN?") < 0)
        {
            rt_kprintf("cat1_reg: CPIN query failed, retry=%d\n", i + 1);
            cat1_modem_set_status_text("4G: SIM未就绪");
            rt_thread_mdelay(1000);
            continue;
        }
        line = at_resp_get_line_by_kw(resp, "+CPIN:");
        if (line != RT_NULL)
        {
            rt_kprintf("cat1_reg: %s\n", line);
        }
        if (at_resp_get_line_by_kw(resp, "READY") == RT_NULL)
        {
            rt_kprintf("cat1_reg: SIM not READY, retry=%d\n", i + 1);
            cat1_modem_set_status_text("4G: SIM未就绪");
            rt_thread_mdelay(1000);
            continue;
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CFUN?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CFUN:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CFUN:", "+CFUN: %d", &cfun) >= 0)
            {
                rt_kprintf("cat1_reg: CFUN=%d\n", cfun);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        cat1_modem_set_status_text("4G: 等待网络注册");
        if (cat1_modem_exec_cmd(client, resp, "AT+CEREG?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CEREG:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CEREG:", "+CEREG:%*d,%d", &stat) >= 0)
            {
                if (stat == 1 || stat == 5)
                {
                    cat1_modem_set_status_text("4G: LTE已注册");
                    at_delete_resp(resp);
                    return RT_EOK;
                }
                rt_kprintf("cat1_reg: CEREG stat=%d\n", stat);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CGREG?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CGREG:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CGREG:", "+CGREG:%*d,%d", &stat) >= 0)
            {
                if (stat == 1 || stat == 5)
                {
                    cat1_modem_set_status_text("4G: 分组域已注册");
                    at_delete_resp(resp);
                    return RT_EOK;
                }
                rt_kprintf("cat1_reg: CGREG stat=%d\n", stat);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CGATT?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CGATT:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CGATT:", "+CGATT: %d", &cgatt) >= 0)
            {
                rt_kprintf("cat1_reg: CGATT=%d\n", cgatt);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+QNWINFO") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+QNWINFO:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+COPS?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+COPS:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CSQ") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CSQ:");
            if (line != RT_NULL)
            {
                rt_kprintf("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CSQ:", "+CSQ: %d,%d", &rssi, &ber) >= 0)
            {
                rt_kprintf("cat1_reg: CSQ rssi=%d ber=%d\n", rssi, ber);
            }
        }

        cat1_modem_set_status_text("4G: 搜网中");
        rt_thread_mdelay(1000);
    }

    at_delete_resp(resp);
    cat1_modem_set_status_text("4G: 注册超时");
    return -RT_ETIMEOUT;
}

static int cat1_modem_activate_pdp(void)
{
    at_client_t client;
    at_response_t resp;
    int result = -RT_ERROR;

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        return -RT_ERROR;
    }

    resp = at_create_resp(256, 0, 5000);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    cat1_modem_set_status_text("4G: 配置APN");
    (void)cat1_modem_exec_cmd(client, resp, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1", CAT1_MODEM_APN);
    at_resp_set_info(resp, 256, 0, 5000);
    (void)cat1_modem_exec_cmd(client, resp, "AT+CGDCONT=1,\"IP\",\"%s\"", CAT1_MODEM_APN);
    at_resp_set_info(resp, 256, 0, 15000);

    cat1_modem_set_status_text("4G: 激活数据链路");
    if (cat1_modem_exec_cmd(client, resp, "AT+QIACT=1") == RT_EOK &&
        at_resp_get_line_by_kw(resp, "OK") != RT_NULL)
    {
        result = RT_EOK;
    }
    else
    {
        at_resp_set_info(resp, 256, 0, 15000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CGACT=1,1") == RT_EOK &&
            at_resp_get_line_by_kw(resp, "OK") != RT_NULL)
        {
            result = RT_EOK;
        }
    }

    at_delete_resp(resp);
    if (result != RT_EOK)
    {
        cat1_modem_set_status_text("4G: 数据链路激活失败");
    }
    return result;
}

static int cat1_modem_check_ip_ready(void)
{
    at_client_t client;
    at_response_t resp;
    const char *line;
    int result = -RT_ERROR;
    int context_id = 0;
    int context_state = 0;
    int context_type = 0;
    char ip_addr[64];

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        return -RT_ERROR;
    }

    resp = at_create_resp(256, 0, 5000);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    cat1_modem_set_status_text("4G: 获取IP地址");
    if (cat1_modem_exec_cmd(client, resp, "AT+QIACT?") == RT_EOK)
    {
        line = at_resp_get_line_by_kw(resp, "+QIACT:");
        if (line != RT_NULL)
        {
            rt_kprintf("cat1_pdp: %s\n", line);
        }
        if (line != RT_NULL &&
            at_resp_parse_line_args_by_kw(resp,
                                          "+QIACT:",
                                          "+QIACT: %d,%d,%d,\"%63[^\"]\"",
                                          &context_id,
                                          &context_state,
                                          &context_type,
                                          ip_addr) >= 0 &&
            context_id == 1 &&
            context_state == 1 &&
            strcmp(ip_addr, "0.0.0.0") != 0)
        {
            result = RT_EOK;
        }
    }

    at_delete_resp(resp);
    return result;
}

static void cat1_modem_mark_ready(bool ready)
{
    s_cat1_ready = ready ? 1U : 0U;
    if (ready)
    {
        cat1_modem_set_status_text("4G: 已联网");
    }
}

static void cat1_modem_dump_response(at_response_t resp)
{
    rt_size_t line_index;

    if (resp == RT_NULL)
    {
        return;
    }

    rt_kprintf("cat1_at: line_count=%d\n", (int)resp->line_counts);
    for (line_index = 1; line_index <= resp->line_counts; ++line_index)
    {
        const char *line = at_resp_get_line(resp, line_index);
        if (line != RT_NULL)
        {
            rt_kprintf("cat1_at: [%d] %s\n", (int)line_index, line);
        }
    }
}

static void cat1_modem_thread_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        if (!s_online_requested)
        {
            cat1_modem_mark_ready(false);
            cat1_modem_set_status_text("4G: 待首页启动");
            rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
            continue;
        }

        if (!s_cat1_ready)
        {
            int at_ready_result;
            int register_result;
            int pdp_result;
            int ip_ready_result;

            if (!s_boot_latched)
            {
                LOG_I("CAT1 boot sequence start");
                rt_kprintf("cat1: boot sequence start\n");
                cat1_modem_power_on_sequence();
                s_boot_latched = 1U;
            }

            at_ready_result = cat1_modem_raw_probe_at();
            if (at_ready_result != RT_EOK)
            {
                cat1_modem_set_status_text("4G: 模组无响应");
                LOG_W("CAT1 AT not ready: %d", at_ready_result);
                ++s_at_failures;
                if (cat1_modem_handle_at_failures("raw_probe"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            if (cat1_modem_ensure_client() != RT_EOK)
            {
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            s_at_failures = 0U;
            cat1_modem_set_status_text("4G: 关闭回显");
            (void)cat1_modem_exec_simple("ATE0", "OK", RT_NULL, 1000);

            register_result = cat1_modem_wait_registered();
            if (register_result != RT_EOK)
            {
                LOG_W("CAT1 register timeout: %d", register_result);
                cat1_modem_mark_ready(false);
                if (cat1_modem_handle_at_failures("register"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            pdp_result = cat1_modem_activate_pdp();
            if (pdp_result != RT_EOK)
            {
                LOG_W("CAT1 PDP activate failed: %d", pdp_result);
                cat1_modem_mark_ready(false);
                if (cat1_modem_handle_at_failures("pdp"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            ip_ready_result = cat1_modem_check_ip_ready();
            if (ip_ready_result == RT_EOK)
            {
                LOG_I("CAT1 modem network ready");
                cat1_modem_mark_ready(true);
            }
            else
            {
                cat1_modem_set_status_text("4G: 未获取到IP");
                LOG_W("CAT1 modem IP not ready: %d", ip_ready_result);
                cat1_modem_mark_ready(false);
                if (cat1_modem_handle_at_failures("ip_check"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
            }
        }
        else if (cat1_modem_check_ip_ready() != RT_EOK)
        {
            cat1_modem_set_status_text("4G: IP已丢失");
            LOG_W("CAT1 modem lost IP");
            cat1_modem_mark_ready(false);
            if (cat1_modem_handle_at_failures("ip_lost"))
            {
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }
        }

        rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
    }
}

rt_err_t cat1_modem_init(void)
{
    rt_err_t result;

    cat1_modem_pin_init();
    LOG_I("CAT1 power rail enabled at boot");
    rt_kprintf("cat1: power rail enabled at boot\n");
    LOG_I("CAT1 startup power-on sequence");
    rt_kprintf("cat1: startup power-on sequence\n");
    cat1_modem_power_on_sequence();
    s_boot_latched = 1U;

    result = rt_thread_init(&s_cat1_thread,
                            "cat1",
                            cat1_modem_thread_entry,
                            RT_NULL,
                            s_cat1_stack,
                            sizeof(s_cat1_stack),
                            CAT1_MODEM_THREAD_PRIORITY,
                            CAT1_MODEM_THREAD_TICK);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_thread_startup(&s_cat1_thread);
    return RT_EOK;
}

rt_err_t cat1_modem_request_online(void)
{
    if (!s_online_requested)
    {
        LOG_I("CAT1 online requested");
        rt_kprintf("cat1: online requested\n");
    }
    s_online_requested = 1U;
    cat1_modem_set_status_text("4G: 启动入网流程");
    return RT_EOK;
}

rt_err_t cat1_modem_request_offline(void)
{
    if (s_online_requested)
    {
        LOG_I("CAT1 offline requested");
    }
    s_online_requested = 0U;
    cat1_modem_mark_ready(false);
    cat1_modem_set_status_text("4G: 已停止入网");
    return RT_EOK;
}

bool cat1_modem_is_ready(void)
{
    return s_cat1_ready != 0U;
}

void cat1_modem_get_status_text(char *buffer, rt_size_t buffer_size)
{
    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return;
    }

    rt_snprintf(buffer, buffer_size, "%s", s_cat1_status_text);
}

static void cat1_at(void)
{
    at_client_t client;
    at_response_t resp;
    int result;

    cat1_modem_pin_init();

    result = cat1_modem_ensure_client();
    if (result != RT_EOK)
    {
        rt_kprintf("cat1_at: at client init failed: %d\n", result);
        return;
    }

    client = at_client_get(CAT1_MODEM_UART_NAME);
    if (client == RT_NULL)
    {
        rt_kprintf("cat1_at: client %s not found\n", CAT1_MODEM_UART_NAME);
        return;
    }

    resp = at_create_resp(256, 0, 1500);
    if (resp == RT_NULL)
    {
        rt_kprintf("cat1_at: create resp failed\n");
        return;
    }

    rt_kprintf("cat1_at: send `AT`\n");
    result = cat1_modem_exec_cmd(client, resp, "AT");
    rt_kprintf("cat1_at: result=%d\n", result);
    cat1_modem_dump_response(resp);

    if (at_resp_get_line_by_kw(resp, "OK") != RT_NULL)
    {
        rt_kprintf("cat1_at: modem replied OK\n");
    }
    else
    {
        rt_kprintf("cat1_at: modem did not reply OK\n");
    }

    at_delete_resp(resp);
}
MSH_CMD_EXPORT(cat1_at, send plain AT to CAT1 modem and print response);
