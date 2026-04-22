#include "cat1_modem.h"

#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "at.h"
#include "drv_gpio.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "netif/ppp/ppp.h"
#include "netif/ppp/pppapi.h"
#include "netif/ppp/pppos.h"
#include "rtdevice.h"
#include "network/net_manager.h"
#include "app_watchdog.h"
#include "mem_section.h"

#define DBG_TAG "cat1"

#define CAT1_MODEM_EVENT_ONLINE_REQ  (1U << 0)
#define CAT1_MODEM_EVENT_OFFLINE_REQ (1U << 1)
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifndef CAT1_MODEM_UART_NAME
#define CAT1_MODEM_UART_NAME "uart2"
#endif

#ifndef CAT1_MODEM_BAUDRATE
#define CAT1_MODEM_BAUDRATE 115200
#endif

#ifndef CAT1_MODEM_HIGH_BAUDRATE
#define CAT1_MODEM_HIGH_BAUDRATE 500000
#endif

#ifdef CAT1_MODEM_ENABLE_HIGH_BAUD
#undef CAT1_MODEM_ENABLE_HIGH_BAUD
#endif
#define CAT1_MODEM_ENABLE_HIGH_BAUD 0

#ifndef CAT1_MODEM_RECV_BUF_SIZE
#define CAT1_MODEM_RECV_BUF_SIZE 2048
#endif

#ifndef CAT1_MODEM_SEND_BUF_SIZE
#define CAT1_MODEM_SEND_BUF_SIZE 256
#endif

#ifndef CAT1_MODEM_RESP_BUF_SIZE
#define CAT1_MODEM_RESP_BUF_SIZE 2048
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

#define CAT1_MODEM_AT_PAUSED 0

#ifndef CAT1_MODEM_DETAIL_VERBOSE
#define CAT1_MODEM_DETAIL_VERBOSE 0
#endif

#ifndef CAT1_MODEM_RAW_VERBOSE
#define CAT1_MODEM_RAW_VERBOSE CAT1_MODEM_DETAIL_VERBOSE
#endif

#ifndef CAT1_MODEM_PROFILE_VERBOSE
#define CAT1_MODEM_PROFILE_VERBOSE CAT1_MODEM_DETAIL_VERBOSE
#endif

#ifndef CAT1_MODEM_DIAG_VERBOSE
#define CAT1_MODEM_DIAG_VERBOSE CAT1_MODEM_DETAIL_VERBOSE
#endif

#ifndef CAT1_MODEM_REG_DIAG_INTERVAL
#define CAT1_MODEM_REG_DIAG_INTERVAL 10
#endif

#ifndef CAT1_MODEM_REG_VERBOSE
#define CAT1_MODEM_REG_VERBOSE CAT1_MODEM_DETAIL_VERBOSE
#endif

#if CAT1_MODEM_RAW_VERBOSE
#define CAT1_RAW_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define CAT1_RAW_LOG(...) do { } while (0)
#endif

#if CAT1_MODEM_PROFILE_VERBOSE
#define CAT1_PROFILE_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define CAT1_PROFILE_LOG(...) do { } while (0)
#endif

#if CAT1_MODEM_DIAG_VERBOSE
#define CAT1_DIAG_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define CAT1_DIAG_LOG(...) do { } while (0)
#endif

#if CAT1_MODEM_REG_VERBOSE
#define CAT1_REG_LOG(...) rt_kprintf(__VA_ARGS__)
#else
#define CAT1_REG_LOG(...) do { } while (0)
#endif

#define CAT1_MODEM_THREAD_STACK_SIZE 4096
#define CAT1_MODEM_THREAD_PRIORITY   16
#define CAT1_MODEM_THREAD_TICK       10
#define CAT1_MODEM_PPP_RX_STACK_SIZE 2048
#define CAT1_MODEM_PPP_RX_PRIORITY   15
#define CAT1_MODEM_PPP_RX_TICK       10
#define CAT1_MODEM_PPP_RX_BUF_SIZE   512
#define CAT1_MODEM_PPP_CONNECT_TIMEOUT_MS 30000U
#define CAT1_MODEM_POLL_INTERVAL_MS  5000U
#define CAT1_MODEM_POWER_STABLE_MS   200U
#define CAT1_MODEM_REPOWER_DELAY_MS  1000U
#define CAT1_MODEM_BAUD_SWITCH_SETTLE_MS 150U
#define CAT1_MODEM_POWERKEY_PULSE_MS 800U
#define CAT1_MODEM_BOOT_WAIT_MS      10000U
#define CAT1_MODEM_AT_REPOWER_THRESHOLD 5U
#define CAT1_MODEM_NETWORK_TIME_MAX_AGE_MS 21600000U
#define CAT1_MODEM_HIGH_BAUD_FAIL_LIMIT 2U
#define CAT1_MODEM_RX_DRAIN_TIMEOUT_MS 120U
#define CAT1_MODEM_RX_DRAIN_IDLE_MS    20U
#define CAT1_MODEM_CONFIG_FILE_NAME "cat1.cfg"

typedef enum
{
    CAT1_MODEM_SERIAL_AT = 0,
    CAT1_MODEM_SERIAL_SWITCHING,
    CAT1_MODEM_SERIAL_PPP_DATA,
} cat1_modem_serial_mode_t;

static struct rt_thread s_cat1_thread;
static struct rt_thread s_cat1_ppp_rx_thread;
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(cat1_modem_thread_stacks)
static rt_uint8_t s_cat1_stack[CAT1_MODEM_THREAD_STACK_SIZE];
static rt_uint8_t s_cat1_ppp_rx_stack[CAT1_MODEM_PPP_RX_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
static rt_uint8_t s_cat1_stack[CAT1_MODEM_THREAD_STACK_SIZE]
    L2_RET_BSS_SECT(cat1_modem_stack);
static rt_uint8_t s_cat1_ppp_rx_stack[CAT1_MODEM_PPP_RX_STACK_SIZE]
    L2_RET_BSS_SECT(cat1_modem_ppp_rx_stack);
#endif
static rt_event_t s_cat1_event = RT_NULL;
static volatile rt_uint8_t s_cat1_ready = 0U;
static volatile rt_uint8_t s_online_requested = 0U;
static volatile rt_uint8_t s_client_ready = 0U;
static volatile rt_uint8_t s_power_enabled = 0U;
static volatile rt_uint8_t s_boot_latched = 0U;
static volatile rt_uint8_t s_at_failures = 0U;
static volatile rt_uint32_t s_detected_baud = CAT1_MODEM_BAUDRATE;
static volatile rt_uint8_t s_ppp_session_started = 0U;
static volatile rt_uint8_t s_ppp_session_up = 0U;
static volatile rt_uint8_t s_ppp_connecting = 0U;
static volatile rt_uint8_t s_ppp_rx_active = 0U;
static volatile rt_uint8_t s_ppp_recover_requested = 0U;
static volatile rt_uint8_t s_ppp_stopping = 0U;
static volatile rt_uint8_t s_high_baud_blocked = 0U;
static rt_uint8_t s_high_baud_fail_count = 0U;
static volatile rt_uint8_t s_high_baud_config_save_pending = 0U;
static volatile rt_uint8_t s_serial_mode = CAT1_MODEM_SERIAL_AT;
static volatile rt_uint8_t s_network_time_valid = 0U;
static time_t s_network_time = 0;
static rt_tick_t s_network_time_tick = 0U;
static rt_uint8_t s_ppp_pending_rx[CAT1_MODEM_PPP_RX_BUF_SIZE];
static rt_size_t s_ppp_pending_rx_len = 0U;
static rt_tick_t s_ppp_start_tick = 0U;
static rt_device_t s_uart_device = RT_NULL;
static ppp_pcb *s_ppp_pcb = RT_NULL;
static struct netif s_ppp_netif;
static char s_cat1_status_text[96] = "4G: 待首页启动";

static int cat1_modem_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...);
static void cat1_modem_dump_response(at_response_t resp);
static void cat1_modem_mark_ready(bool ready);
static void cat1_modem_reset_runtime_state(void);
static void cat1_modem_shutdown_module(void);
static void cat1_modem_wait_for_request(void);
static rt_err_t cat1_modem_uart_open(void);
static void cat1_modem_uart_close(void);
static int cat1_modem_raw_command(rt_device_t uart_device,
                                  const char *cmd,
                                  char *rx_buf,
                                  rt_size_t rx_buf_size,
                                  rt_uint32_t timeout_ms);
static int cat1_modem_wait_registered_raw(rt_device_t uart_device);
static int cat1_modem_configure_pdp_raw(rt_device_t uart_device);
static int cat1_modem_start_ppp_session(rt_device_t uart_device);
static void cat1_modem_stop_ppp_session(rt_bool_t close_uart);
static void cat1_modem_free_ppp_pcb(rt_bool_t close_first);
static rt_err_t cat1_modem_switch_uart_baud(rt_uint32_t target_baud);
static void cat1_modem_uart_drain_rx(rt_device_t uart_device,
                                      rt_uint32_t timeout_ms,
                                      const char *reason);
static rt_err_t cat1_modem_restore_default_baud(const char *reason);
static void cat1_modem_ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx);
static u32_t cat1_modem_pppos_output_cb(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx);
static void cat1_modem_ppp_rx_thread_entry(void *parameter);
static void cat1_modem_repower_module(void);
static rt_bool_t cat1_modem_handle_at_failures(const char *reason);
static rt_bool_t cat1_modem_offline_requested(void);

static void cat1_modem_set_status_text(const char *text)
{
    if (text == RT_NULL)
    {
        return;
    }

    rt_snprintf(s_cat1_status_text, sizeof(s_cat1_status_text), "%s", text);
}

static bool cat1_modem_config_dir_exists(const char *path)
{
    struct stat info;

    if (path == RT_NULL)
    {
        return false;
    }

    if (stat(path, &info) != 0)
    {
        return false;
    }

    return (info.st_mode & S_IFDIR) != 0;
}

static bool cat1_modem_build_config_path(char *buffer,
                                         size_t buffer_size,
                                         bool ensure_dir)
{
    static const char *const config_dirs[] = {
        "/config",
        "/tf/config",
        "/sd/config",
        "/sd0/config",
        "config",
    };
    const char *config_dir = RT_NULL;
    size_t i;

    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return false;
    }

    for (i = 0; i < sizeof(config_dirs) / sizeof(config_dirs[0]); ++i)
    {
        if (cat1_modem_config_dir_exists(config_dirs[i]))
        {
            config_dir = config_dirs[i];
            break;
        }
    }

    if (config_dir == RT_NULL)
    {
        if (!ensure_dir)
        {
            buffer[0] = '\0';
            return false;
        }

        mkdir("/config", 0);
        config_dir = "/config";
    }

    rt_snprintf(buffer, buffer_size, "%s/%s", config_dir, CAT1_MODEM_CONFIG_FILE_NAME);
    return true;
}

static void cat1_modem_save_high_baud_config(void)
{
#if CAT1_MODEM_ENABLE_HIGH_BAUD
    char path[96];
    const char *value;
    int fd;

    if (!cat1_modem_build_config_path(path, sizeof(path), true))
    {
        return;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
    {
        return;
    }

    value = s_high_baud_blocked ? "high_baud=blocked\n" : "high_baud=auto\n";
    (void)write(fd, value, rt_strlen(value));
    close(fd);
#endif
}

static void cat1_modem_request_high_baud_config_save(void)
{
#if CAT1_MODEM_ENABLE_HIGH_BAUD
    s_high_baud_config_save_pending = 1U;
    if (s_cat1_event != RT_NULL)
    {
        rt_event_send(s_cat1_event, CAT1_MODEM_EVENT_ONLINE_REQ);
    }
#endif
}

static void cat1_modem_load_high_baud_config(void)
{
#if CAT1_MODEM_ENABLE_HIGH_BAUD
    char path[96];
    char buffer[48];
    int fd;
    int length;

    if (!cat1_modem_build_config_path(path, sizeof(path), false))
    {
        return;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        return;
    }

    length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (length <= 0)
    {
        return;
    }

    buffer[length] = '\0';
    if (strstr(buffer, "high_baud=blocked") != RT_NULL)
    {
        s_high_baud_blocked = 1U;
        s_high_baud_fail_count = CAT1_MODEM_HIGH_BAUD_FAIL_LIMIT;
    }
#else
    s_high_baud_blocked = 0U;
    s_high_baud_fail_count = 0U;
#endif
}

static void cat1_modem_record_high_baud_failure(const char *reason)
{
#if CAT1_MODEM_ENABLE_HIGH_BAUD
    rt_bool_t newly_blocked = RT_FALSE;

    if (s_high_baud_fail_count < 255U)
    {
        ++s_high_baud_fail_count;
    }

    if (s_high_baud_fail_count >= CAT1_MODEM_HIGH_BAUD_FAIL_LIMIT &&
        s_high_baud_blocked == 0U)
    {
        s_high_baud_blocked = 1U;
        newly_blocked = RT_TRUE;
        cat1_modem_request_high_baud_config_save();
    }

    if (newly_blocked)
    {
        LOG_W("CAT1 high baud disabled after failures=%u reason=%s",
              (unsigned int)s_high_baud_fail_count,
              reason != RT_NULL ? reason : "unknown");
    }
#else
    (void)reason;
#endif
}

static rt_bool_t cat1_modem_offline_requested(void)
{
    return s_online_requested == 0U ? RT_TRUE : RT_FALSE;
}

static void cat1_modem_reset_runtime_state(void)
{
    s_client_ready = 0U;
    s_at_failures = 0U;
    s_boot_latched = 0U;
    s_detected_baud = CAT1_MODEM_BAUDRATE;
    s_ppp_pending_rx_len = 0U;
    s_ppp_recover_requested = 0U;
    s_serial_mode = CAT1_MODEM_SERIAL_AT;
    s_network_time_valid = 0U;
    s_network_time = 0;
    s_network_time_tick = 0U;
}

static rt_bool_t cat1_modem_raw_at_allowed(void)
{
    if (s_serial_mode == CAT1_MODEM_SERIAL_PPP_DATA ||
        s_ppp_rx_active != 0U ||
        s_ppp_session_started != 0U ||
        s_ppp_connecting != 0U ||
        s_ppp_session_up != 0U)
    {
        return RT_FALSE;
    }

    return RT_TRUE;
}

static void cat1_modem_uart_drain_rx(rt_device_t uart_device,
                                      rt_uint32_t timeout_ms,
                                      const char *reason)
{
    rt_uint8_t scratch[64];
    rt_tick_t deadline;
    rt_tick_t last_rx_tick;
    rt_size_t drained = 0U;

    if (uart_device == RT_NULL)
    {
        return;
    }

    deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    last_rx_tick = rt_tick_get();
    while (rt_tick_get() < deadline)
    {
        rt_size_t read_len = rt_device_read(uart_device, -1, scratch, sizeof(scratch));

        if (read_len > 0U)
        {
            drained += read_len;
            last_rx_tick = rt_tick_get();
            continue;
        }

        if ((rt_tick_get() - last_rx_tick) >=
            rt_tick_from_millisecond(CAT1_MODEM_RX_DRAIN_IDLE_MS))
        {
            break;
        }

        rt_thread_mdelay(5);
    }

    if (drained > 0U &&
        (reason == RT_NULL || strcmp(reason, "AT command") != 0))
    {
        CAT1_RAW_LOG("cat1_uart: drained %u bytes before %s\n",
                   (unsigned int)drained,
                   reason != RT_NULL ? reason : "serial switch");
    }
}

static void cat1_modem_wait_for_request(void)
{
    if (s_cat1_event != RT_NULL)
    {
        rt_uint32_t events = 0U;
        rt_event_recv(s_cat1_event,
                      CAT1_MODEM_EVENT_ONLINE_REQ | CAT1_MODEM_EVENT_OFFLINE_REQ,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER,
                      &events);
        (void)events;
    }
    else
    {
        rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
    }
}

static rt_size_t cat1_modem_raw_trim_chunk(char *buf, rt_size_t len)
{
    rt_size_t start = 0;
    rt_size_t end = len;

    if (buf == RT_NULL || len == 0U)
    {
        return 0U;
    }

    while (start < end && (unsigned char)buf[start] == 0U)
    {
        ++start;
    }

    while (end > start && (unsigned char)buf[end - 1U] == 0U)
    {
        --end;
    }

    if (end <= start)
    {
        return 0U;
    }

    if (start > 0U)
    {
        rt_memmove(buf, buf + start, end - start);
    }

    return end - start;
}

static rt_size_t cat1_modem_raw_complete_length(const char *rx_buf, rt_size_t total)
{
    const char *p;
    const char *line_end;

    if (rx_buf == RT_NULL || total == 0U)
    {
        return 0U;
    }

    p = strstr(rx_buf, "\r\nOK\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 6);
    }

    p = strstr(rx_buf, "\nOK\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 5);
    }

    p = strstr(rx_buf, "\r\nERROR\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 9);
    }

    p = strstr(rx_buf, "+CME ERROR:");
    if (p != RT_NULL)
    {
        line_end = p;
        while ((rt_size_t)(line_end - rx_buf) < total &&
               *line_end != '\n' &&
               *line_end != '\0')
        {
            ++line_end;
        }
        if ((rt_size_t)(line_end - rx_buf) < total && *line_end == '\n')
        {
            ++line_end;
        }
        return (rt_size_t)(line_end - rx_buf);
    }

    p = strstr(rx_buf, "CONNECT\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 9);
    }

    p = strstr(rx_buf, "\r\nRDY\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 7);
    }

    p = strstr(rx_buf, "\nRDY\r\n");
    if (p != RT_NULL)
    {
        return (rt_size_t)((p - rx_buf) + 6);
    }

    return total;
}

static rt_bool_t cat1_modem_raw_response_complete(const char *rx_buf)
{
    return cat1_modem_raw_complete_length(rx_buf,
                                          rx_buf != RT_NULL ? rt_strlen(rx_buf) : 0U) > 0U &&
           (strstr(rx_buf, "OK") != RT_NULL ||
            strstr(rx_buf, "ERROR") != RT_NULL ||
            strstr(rx_buf, "+CME ERROR:") != RT_NULL ||
            strstr(rx_buf, "CONNECT") != RT_NULL ||
            strstr(rx_buf, "RDY") != RT_NULL);
}

static rt_size_t cat1_modem_raw_exchange(rt_device_t uart_device,
                                         const char *cmd,
                                         char *rx_buf,
                                         rt_size_t rx_buf_size,
                                         rt_uint32_t timeout_ms)
{
    rt_tick_t deadline;
    rt_size_t total = 0;

    if (!cat1_modem_raw_at_allowed())
    {
        if (rx_buf != RT_NULL && rx_buf_size > 0U)
        {
            rx_buf[0] = '\0';
        }
        return 0U;
    }

    cat1_modem_uart_drain_rx(uart_device, CAT1_MODEM_RX_DRAIN_TIMEOUT_MS, "AT command");
    (void)rt_device_write(uart_device, 0, cmd, rt_strlen(cmd));

    deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    rt_memset(rx_buf, 0, rx_buf_size);
    while (rt_tick_get() < deadline && total < rx_buf_size - 1)
    {
        if (cat1_modem_offline_requested())
        {
            break;
        }

        rt_size_t read_len = rt_device_read(uart_device, -1, &rx_buf[total], rx_buf_size - 1 - total);
        app_watchdog_pet();
        if (read_len > 0)
        {
            rt_size_t full_total;
            rt_size_t complete_len;

            read_len = cat1_modem_raw_trim_chunk(&rx_buf[total], read_len);
            if (read_len == 0U)
            {
                rt_thread_mdelay(20);
                continue;
            }

            total += read_len;
            rx_buf[total] = '\0';
            if (cat1_modem_raw_response_complete(rx_buf))
            {
                full_total = total;
                complete_len = cat1_modem_raw_complete_length(rx_buf, total);
                if (complete_len < full_total &&
                    strstr(rx_buf, "CONNECT") != RT_NULL)
                {
                    s_ppp_pending_rx_len = full_total - complete_len;
                    if (s_ppp_pending_rx_len > sizeof(s_ppp_pending_rx))
                    {
                        s_ppp_pending_rx_len = sizeof(s_ppp_pending_rx);
                    }
                    rt_memcpy(s_ppp_pending_rx,
                              &rx_buf[complete_len],
                              s_ppp_pending_rx_len);
                }
                total = complete_len;
                break;
            }
            continue;
        }
        rt_thread_mdelay(20);
    }

    rx_buf[total] = '\0';
    return total;
}

static void cat1_modem_raw_dump(const char *prefix, const char *rx_buf, rt_size_t total)
{
#if CAT1_MODEM_RAW_VERBOSE
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
#else
    (void)prefix;
    (void)rx_buf;
    (void)total;
#endif
}

static int cat1_modem_raw_cme_code(const char *rx_buf)
{
    const char *line = strstr(rx_buf, "+CME ERROR:");
    int code = -1;

    if (line != RT_NULL && sscanf(line, "+CME ERROR: %d", &code) == 1)
    {
        return code;
    }

    return -1;
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

        if (cat1_modem_offline_requested())
        {
            return -RT_EBUSY;
        }

        rt_thread_mdelay(80);
        if (cat1_modem_offline_requested())
        {
            return -RT_EBUSY;
        }

        CAT1_PROFILE_LOG("cat1_raw: baud=%u profile `%s` try=%u\n",
                   (unsigned int)baud_rate,
                   cmd,
                   (unsigned int)(attempt + 1U));
        total = cat1_modem_raw_exchange(uart_device, cmd, rx_buf, sizeof(rx_buf), timeout_ms);
        cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
        if (cat1_modem_raw_has_success(rx_buf))
        {
            return RT_EOK;
        }

        if (cat1_modem_offline_requested())
        {
            return -RT_EBUSY;
        }
    }

    return -RT_ERROR;
}

static void cat1_modem_raw_diag_command(rt_device_t uart_device,
                                        const char *cmd,
                                        const char *label,
                                        rt_uint32_t timeout_ms)
{
#if CAT1_MODEM_DIAG_VERBOSE
    char rx_buf[256];
    int result;

    result = cat1_modem_raw_command(uart_device, cmd, rx_buf, sizeof(rx_buf), timeout_ms);
    if (result == RT_EOK)
    {
        CAT1_DIAG_LOG("cat1_diag[%s]: %s\n", label, rx_buf);
    }
    else if (rx_buf[0] != '\0')
    {
        CAT1_DIAG_LOG("cat1_diag[%s]: result=%d payload=%s\n", label, result, rx_buf);
    }
    else
    {
        CAT1_DIAG_LOG("cat1_diag[%s]: result=%d\n", label, result);
    }
#else
    (void)uart_device;
    (void)cmd;
    (void)label;
    (void)timeout_ms;
#endif
}

static rt_err_t cat1_modem_raw_apply_profile(rt_device_t uart_device, rt_uint32_t baud_rate)
{
    struct
    {
        const char *cmd;
    } profile_cmds[] = {
        { "AT\r" },
        { "ATE0\r" },
        { "AT+IFC=0,0\r" },
        { "AT\r" },
    };
    rt_size_t i;

    for (i = 0; i < sizeof(profile_cmds) / sizeof(profile_cmds[0]); ++i)
    {
        int result = cat1_modem_raw_expect_ok(uart_device, baud_rate, profile_cmds[i].cmd, 1000U, 3U);

        if (result == -RT_EBUSY)
        {
            return -RT_EBUSY;
        }

        if (result != RT_EOK)
        {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

static rt_err_t cat1_modem_uart_open(void)
{
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_err_t open_result;

    if (s_uart_device == RT_NULL)
    {
        s_uart_device = rt_device_find(CAT1_MODEM_UART_NAME);
        if (s_uart_device == RT_NULL)
        {
            rt_kprintf("cat1_uart: uart device %s not found\n", CAT1_MODEM_UART_NAME);
            return -RT_ENOSYS;
        }
    }

    config.baud_rate = s_detected_baud;
    rt_device_control(s_uart_device, RT_DEVICE_CTRL_CONFIG, &config);

    open_result = rt_device_open(s_uart_device, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    if (open_result != RT_EOK && open_result != -RT_EBUSY)
    {
        rt_kprintf("cat1_uart: open %s failed: %d\n", CAT1_MODEM_UART_NAME, open_result);
        s_uart_device = RT_NULL;
        return open_result;
    }

    rt_thread_mdelay(50);
    return RT_EOK;
}

static void cat1_modem_uart_close(void)
{
    if (s_uart_device != RT_NULL)
    {
        rt_device_close(s_uart_device);
        s_uart_device = RT_NULL;
    }
}

static int cat1_modem_raw_command(rt_device_t uart_device,
                                  const char *cmd,
                                  char *rx_buf,
                                  rt_size_t rx_buf_size,
                                  rt_uint32_t timeout_ms)
{
    rt_size_t total;

    if (uart_device == RT_NULL || cmd == RT_NULL || rx_buf == RT_NULL || rx_buf_size == 0U)
    {
        return -RT_ERROR;
    }

    if (!cat1_modem_raw_at_allowed())
    {
        rx_buf[0] = '\0';
        return -RT_EBUSY;
    }

    total = cat1_modem_raw_exchange(uart_device, cmd, rx_buf, rx_buf_size, timeout_ms);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (total == 0U)
    {
        if (cat1_modem_offline_requested())
        {
            return -RT_EBUSY;
        }

        ++s_at_failures;
        return -RT_ETIMEOUT;
    }

    if (strstr(rx_buf, "ERROR") != RT_NULL || strstr(rx_buf, "FAIL") != RT_NULL)
    {
        int cme_code = cat1_modem_raw_cme_code(rx_buf);

        if (cme_code >= 0)
        {
            if (cme_code == 14)
            {
                return -RT_EBUSY;
            }
            return -RT_ERROR;
        }

        ++s_at_failures;
        return -RT_ERROR;
    }

    s_at_failures = 0U;
    return RT_EOK;
}

static int cat1_modem_wait_registered_raw(rt_device_t uart_device)
{
    char rx_buf[256];
    int i;

    (void)cat1_modem_raw_command(uart_device, "AT+CEREG=2\r", rx_buf, sizeof(rx_buf), 2000U);
    (void)cat1_modem_raw_command(uart_device, "AT+CGREG=2\r", rx_buf, sizeof(rx_buf), 2000U);
    (void)cat1_modem_raw_command(uart_device, "AT+CREG=2\r", rx_buf, sizeof(rx_buf), 2000U);

    for (i = 0; i < 40; ++i)
    {
        rt_bool_t need_extra_diag = ((i == 0) || (((i + 1) % CAT1_MODEM_REG_DIAG_INTERVAL) == 0)) ? RT_TRUE : RT_FALSE;
        int stat = 0;
        int cfun = -1;
        int cgatt = -1;
        int rssi = -1;
        int ber = -1;
        int cpin_result;
        const char *line = RT_NULL;

        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }
        app_watchdog_pet();

        cat1_modem_set_status_text("4G: 检查SIM卡");
        cpin_result = cat1_modem_raw_command(uart_device, "AT+CPIN?\r", rx_buf, sizeof(rx_buf), 3000U);
        if (cpin_result != RT_EOK)
        {
            int cme_code = cat1_modem_raw_cme_code(rx_buf);

            if (cpin_result == -RT_EBUSY || cme_code == 14)
            {
                CAT1_REG_LOG("cat1_reg: SIM busy, retry=%d\n", i + 1);
                cat1_modem_set_status_text("4G: SIM忙");
            }
            else if (cme_code == 13)
            {
                CAT1_REG_LOG("cat1_reg: SIM failure, retry=%d\n", i + 1);
                cat1_modem_set_status_text("4G: SIM异常");
            }
            else
            {
                CAT1_REG_LOG("cat1_reg: CPIN query failed, retry=%d\n", i + 1);
                cat1_modem_set_status_text("4G: SIM未就绪");
            }
            rt_thread_mdelay(1000);
            continue;
        }
        line = strstr(rx_buf, "+CPIN:");
        if (line != RT_NULL)
        {
            CAT1_REG_LOG("cat1_reg: %s\n", line);
        }
        if (strstr(rx_buf, "READY") == RT_NULL)
        {
            CAT1_REG_LOG("cat1_reg: SIM not READY, retry=%d\n", i + 1);
            cat1_modem_set_status_text("4G: SIM未就绪");
            rt_thread_mdelay(1000);
            continue;
        }

        {
            int cfun_result = cat1_modem_raw_command(uart_device, "AT+CFUN?\r", rx_buf, sizeof(rx_buf), 3000U);

            if (cfun_result == -RT_EBUSY || cat1_modem_offline_requested())
            {
                cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
                return -RT_EBUSY;
            }

            if (cfun_result == RT_EOK)
            {
                line = strstr(rx_buf, "+CFUN:");
                if (line != RT_NULL)
                {
                    CAT1_REG_LOG("cat1_reg: %s\n", line);
                    if (sscanf(line, "+CFUN: %d", &cfun) == 1)
                    {
                        CAT1_REG_LOG("cat1_reg: CFUN=%d\n", cfun);
                    }
                }
            }
        }

        cat1_modem_set_status_text("4G: 等待网络注册");
        if (cat1_modem_raw_command(uart_device, "AT+CEREG?\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+CEREG:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
                if (sscanf(line, "+CEREG: %*d,%d", &stat) == 1)
                {
                    CAT1_REG_LOG("cat1_reg: CEREG stat=%d\n", stat);
                    if (stat == 1 || stat == 5)
                    {
                        cat1_modem_set_status_text("4G: LTE已注册");
                        return RT_EOK;
                    }
                }
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+CREG?\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+CREG:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+CGREG?\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+CGREG:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
                if (sscanf(line, "+CGREG: %*d,%d", &stat) == 1)
                {
                    CAT1_REG_LOG("cat1_reg: CGREG stat=%d\n", stat);
                }
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+CGATT?\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+CGATT:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
                if (sscanf(line, "+CGATT: %d", &cgatt) == 1)
                {
                    CAT1_REG_LOG("cat1_reg: CGATT=%d\n", cgatt);
                }
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+QNWINFO\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+QNWINFO:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+COPS?\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+COPS:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (cat1_modem_raw_command(uart_device, "AT+CSQ\r", rx_buf, sizeof(rx_buf), 3000U) == RT_EOK)
        {
            line = strstr(rx_buf, "+CSQ:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
                if (sscanf(line, "+CSQ: %d,%d", &rssi, &ber) == 2)
                {
                    CAT1_REG_LOG("cat1_reg: CSQ rssi=%d ber=%d\n", rssi, ber);
                }
            }
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        if (need_extra_diag)
        {
            cat1_modem_raw_diag_command(uart_device, "AT+QCCID\r", "QCCID", 3000U);
            cat1_modem_raw_diag_command(uart_device, "AT+CIMI\r", "CIMI", 3000U);
            cat1_modem_raw_diag_command(uart_device, "AT+QENG=\"servingcell\"\r", "QENG", 3000U);
            cat1_modem_raw_diag_command(uart_device, "AT+CEER\r", "CEER", 3000U);
        }
        if (cat1_modem_offline_requested())
        {
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        cat1_modem_set_status_text("4G: 搜网中");
        app_watchdog_pet();
        rt_thread_mdelay(1000);
    }

    cat1_modem_set_status_text("4G: 注册超时");
    return -RT_ETIMEOUT;
}

static int cat1_modem_configure_pdp_raw(rt_device_t uart_device)
{
    char rx_buf[256];
    int result;

    if (cat1_modem_offline_requested())
    {
        cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
        return -RT_EBUSY;
    }

    cat1_modem_set_status_text("4G: 配置APN");
    result = cat1_modem_raw_command(uart_device,
                                    "AT+CGDCONT=1,\"IP\",\"" CAT1_MODEM_APN "\"\r",
                                    rx_buf,
                                    sizeof(rx_buf),
                                    5000U);
    if (result == -RT_EBUSY || cat1_modem_offline_requested())
    {
        return -RT_EBUSY;
    }

    if (result != RT_EOK ||
        strstr(rx_buf, "OK") == RT_NULL)
    {
        cat1_modem_set_status_text("4G: APN配置失败");
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void cat1_modem_capture_network_time(rt_device_t uart_device)
{
    char rx_buf[128];
    const char *line;
    int yy;
    int mm;
    int dd;
    int hour;
    int minute;
    int second;
    char tz_sign = '+';
    int tz_quarters = 0;
    struct tm tm_value;
    time_t parsed_time;
    int module_offset_seconds = 8 * 3600;

    if (uart_device == RT_NULL)
    {
        return;
    }

    if (cat1_modem_raw_command(uart_device, "AT+CCLK?\r",
                               rx_buf, sizeof(rx_buf), 3000U) != RT_EOK)
    {
        return;
    }

    line = strstr(rx_buf, "+CCLK:");
    if (line == RT_NULL)
    {
        return;
    }

    if (sscanf(line,
               "+CCLK: \"%d/%d/%d,%d:%d:%d%c%d\"",
               &yy,
               &mm,
               &dd,
               &hour,
               &minute,
               &second,
               &tz_sign,
               &tz_quarters) < 6)
    {
        return;
    }

    if (yy < 24 || mm < 1 || mm > 12 || dd < 1 || dd > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
    {
        return;
    }

    rt_memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = yy + 100;
    tm_value.tm_mon = mm - 1;
    tm_value.tm_mday = dd;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;
    parsed_time = mktime(&tm_value);
    if (parsed_time <= 1000000000)
    {
        return;
    }

    if (tz_quarters > 0)
    {
        module_offset_seconds = tz_quarters * 15 * 60;
        if (tz_sign == '-')
        {
            module_offset_seconds = -module_offset_seconds;
        }
        parsed_time += (8 * 3600) - module_offset_seconds;
    }

    s_network_time = parsed_time;
    s_network_time_tick = rt_tick_get();
    s_network_time_valid = 1U;
    rt_kprintf("cat1_time: cached %ld\n", (long)s_network_time);
}

static rt_err_t cat1_modem_switch_uart_baud(rt_uint32_t target_baud)
{
    char cmd[32];
    rt_uint32_t old_baud;
    rt_err_t result;

    if (s_uart_device == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (!cat1_modem_raw_at_allowed())
    {
        return -RT_EBUSY;
    }

    old_baud = s_detected_baud;
    if (old_baud == target_baud)
    {
        return RT_EOK;
    }

    s_serial_mode = CAT1_MODEM_SERIAL_SWITCHING;
    cat1_modem_uart_drain_rx(s_uart_device, CAT1_MODEM_RX_DRAIN_TIMEOUT_MS, "baud switch");
    rt_snprintf(cmd, sizeof(cmd), "AT+IPR=%u\r", (unsigned int)target_baud);
    CAT1_RAW_LOG("cat1_raw: switch baud %u -> %u\n",
               (unsigned int)old_baud,
               (unsigned int)target_baud);
    (void)rt_device_write(s_uart_device, 0, cmd, rt_strlen(cmd));
    rt_thread_mdelay(CAT1_MODEM_BAUD_SWITCH_SETTLE_MS);

    cat1_modem_uart_close();
    s_detected_baud = target_baud;

    if (cat1_modem_uart_open() != RT_EOK)
    {
        s_detected_baud = CAT1_MODEM_BAUDRATE;
        s_serial_mode = CAT1_MODEM_SERIAL_AT;
        return -RT_ERROR;
    }

    cat1_modem_uart_drain_rx(s_uart_device, CAT1_MODEM_RX_DRAIN_TIMEOUT_MS, "baud settle");
    s_serial_mode = CAT1_MODEM_SERIAL_AT;
    result = cat1_modem_raw_apply_profile(s_uart_device, target_baud);
    if (result != RT_EOK)
    {
        if (target_baud != CAT1_MODEM_BAUDRATE)
        {
            (void)cat1_modem_restore_default_baud("profile");
        }
        else
        {
            cat1_modem_uart_close();
            s_detected_baud = CAT1_MODEM_BAUDRATE;
        }
        return result == -RT_EBUSY ? -RT_EBUSY : -RT_ERROR;
    }

    CAT1_RAW_LOG("cat1_raw: baud switched to %u\n", (unsigned int)target_baud);
    return RT_EOK;
}

static rt_err_t cat1_modem_restore_default_baud(const char *reason)
{
    char cmd[32];

    if (s_uart_device != RT_NULL)
    {
        s_serial_mode = CAT1_MODEM_SERIAL_SWITCHING;
        cat1_modem_uart_drain_rx(s_uart_device,
                                  CAT1_MODEM_RX_DRAIN_TIMEOUT_MS,
                                  "baud fallback");
        rt_snprintf(cmd, sizeof(cmd), "AT+IPR=%u\r", (unsigned int)CAT1_MODEM_BAUDRATE);
        (void)rt_device_write(s_uart_device, 0, cmd, rt_strlen(cmd));
        rt_thread_mdelay(CAT1_MODEM_BAUD_SWITCH_SETTLE_MS);
    }

    cat1_modem_uart_close();
    s_detected_baud = CAT1_MODEM_BAUDRATE;
    s_serial_mode = CAT1_MODEM_SERIAL_AT;

    if (cat1_modem_uart_open() != RT_EOK)
    {
        CAT1_RAW_LOG("cat1_raw: fallback open %u failed after %s\n",
                   (unsigned int)CAT1_MODEM_BAUDRATE,
                   reason != RT_NULL ? reason : "high baud failure");
        return -RT_ERROR;
    }

    cat1_modem_uart_drain_rx(s_uart_device, CAT1_MODEM_RX_DRAIN_TIMEOUT_MS, "baud fallback settle");
    if (cat1_modem_raw_apply_profile(s_uart_device, CAT1_MODEM_BAUDRATE) != RT_EOK)
    {
        CAT1_RAW_LOG("cat1_raw: fallback profile %u failed after %s\n",
                   (unsigned int)CAT1_MODEM_BAUDRATE,
                   reason != RT_NULL ? reason : "high baud failure");
        cat1_modem_uart_close();
        return -RT_ERROR;
    }

    CAT1_RAW_LOG("cat1_raw: fallback baud restored to %u\n",
               (unsigned int)CAT1_MODEM_BAUDRATE);
    return RT_EOK;
}

static u32_t cat1_modem_pppos_output_cb(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    (void)pcb;
    (void)ctx;

    if (s_uart_device == RT_NULL || data == RT_NULL || len == 0U)
    {
        return 0U;
    }

    return (u32_t)rt_device_write(s_uart_device, 0, data, len);
}

static void cat1_modem_ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    (void)ctx;

    if (err_code == PPPERR_NONE)
    {
        const ip_addr_t *dns_server = dns_getserver(0);

        s_ppp_session_up = 1U;
        s_ppp_connecting = 0U;
        cat1_modem_mark_ready(true);
        cat1_modem_set_status_text("4G: DNS已就绪");
        rt_kprintf("cat1_ppp: connected\n");
        if (pcb != RT_NULL && ppp_netif(pcb) != RT_NULL)
        {
            rt_kprintf("cat1_ppp: ip=%s\n", ipaddr_ntoa(&ppp_netif(pcb)->ip_addr));
            rt_kprintf("cat1_ppp: gw=%s\n", ipaddr_ntoa(&ppp_netif(pcb)->gw));
        }
        if (dns_server != RT_NULL)
        {
            rt_kprintf("cat1_ppp: dns1=%s\n", ipaddr_ntoa(dns_server));
        }
#if CAT1_MODEM_ENABLE_HIGH_BAUD
        if (s_detected_baud == CAT1_MODEM_HIGH_BAUDRATE)
        {
            s_high_baud_fail_count = 0U;
            s_high_baud_blocked = 0U;
            cat1_modem_request_high_baud_config_save();
        }
        else if (s_detected_baud == CAT1_MODEM_BAUDRATE &&
                 s_high_baud_blocked == 0U)
        {
            s_high_baud_fail_count = 0U;
            cat1_modem_request_high_baud_config_save();
        }
#endif
        return;
    }

    s_ppp_session_up = 0U;
    s_ppp_connecting = 0U;
    cat1_modem_mark_ready(false);
    rt_kprintf("cat1_ppp: link error=%d\n", err_code);

    if (s_ppp_stopping || !s_online_requested)
    {
        return;
    }

    switch (err_code)
    {
    case PPPERR_CONNECT:
        cat1_modem_set_status_text("4G: PPP链路断开");
        break;
    case PPPERR_PEERDEAD:
        cat1_modem_set_status_text("4G: PPP超时");
        break;
    case PPPERR_PROTOCOL:
        cat1_modem_set_status_text("4G: PPP协议失败");
        break;
    default:
        cat1_modem_set_status_text("4G: PPP异常，准备重连");
        break;
    }

    s_ppp_recover_requested = 1U;
}

static void cat1_modem_ppp_rx_thread_entry(void *parameter)
{
    rt_uint8_t rx_buf[CAT1_MODEM_PPP_RX_BUF_SIZE];

    (void)parameter;

    while (1)
    {
        if (!s_ppp_rx_active || s_uart_device == RT_NULL || s_ppp_pcb == RT_NULL)
        {
            rt_thread_mdelay(20);
            continue;
        }

        if (s_serial_mode != CAT1_MODEM_SERIAL_PPP_DATA)
        {
            rt_thread_mdelay(20);
            continue;
        }

        {
            rt_size_t read_len = rt_device_read(s_uart_device, -1, rx_buf, sizeof(rx_buf));
            if (read_len > 0U)
            {
                (void)pppos_input_tcpip(s_ppp_pcb, rx_buf, (int)read_len);
                continue;
            }
        }

        rt_thread_mdelay(10);
    }
}

static int cat1_modem_start_ppp_session(rt_device_t uart_device)
{
    char rx_buf[128];
    int result;
    rt_bool_t connected = RT_FALSE;

    if (cat1_modem_offline_requested())
    {
        cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
        return -RT_EBUSY;
    }

    s_ppp_pending_rx_len = 0U;
    cat1_modem_set_status_text("4G: PPP拨号中");
    result = cat1_modem_raw_command(uart_device, "AT+CGDATA=\"PPP\",1\r", rx_buf, sizeof(rx_buf), 8000U);
    if (result == -RT_EBUSY || cat1_modem_offline_requested())
    {
        return -RT_EBUSY;
    }

    if (result == RT_EOK && strstr(rx_buf, "CONNECT") != RT_NULL)
    {
        connected = RT_TRUE;
    }
    else
    {
        result = cat1_modem_raw_command(uart_device, "ATD*99***1#\r", rx_buf, sizeof(rx_buf), 15000U);
        if (result == -RT_EBUSY || cat1_modem_offline_requested())
        {
            return -RT_EBUSY;
        }

        if (result == RT_EOK && strstr(rx_buf, "CONNECT") != RT_NULL)
        {
            connected = RT_TRUE;
        }
    }

    if (!connected)
    {
        cat1_modem_set_status_text("4G: PPP拨号失败");
        return -RT_ERROR;
    }

    s_serial_mode = CAT1_MODEM_SERIAL_PPP_DATA;
    if (s_ppp_pcb == RT_NULL)
    {
        rt_memset(&s_ppp_netif, 0, sizeof(s_ppp_netif));
        s_ppp_pcb = pppapi_pppos_create(&s_ppp_netif,
                                        cat1_modem_pppos_output_cb,
                                        cat1_modem_ppp_status_cb,
                                        RT_NULL);
        if (s_ppp_pcb == RT_NULL)
        {
            s_serial_mode = CAT1_MODEM_SERIAL_AT;
            cat1_modem_set_status_text("4G: PPP创建失败");
            return -RT_ENOMEM;
        }
        ppp_set_usepeerdns(s_ppp_pcb, 1);
    }

    if (cat1_modem_offline_requested())
    {
        s_serial_mode = CAT1_MODEM_SERIAL_AT;
        cat1_modem_free_ppp_pcb(RT_FALSE);
        return -RT_EBUSY;
    }

    s_ppp_stopping = 0U;
    s_ppp_recover_requested = 0U;
    s_ppp_session_started = 1U;
    s_ppp_session_up = 0U;
    s_ppp_connecting = 1U;
    s_ppp_start_tick = rt_tick_get();
    s_ppp_rx_active = 1U;
    cat1_modem_set_status_text("4G: PPP协商中");
    (void)pppapi_set_default(s_ppp_pcb);
    if (pppapi_connect(s_ppp_pcb, 0) != ERR_OK)
    {
        s_ppp_rx_active = 0U;
        s_ppp_session_started = 0U;
        s_ppp_connecting = 0U;
        s_serial_mode = CAT1_MODEM_SERIAL_AT;
        cat1_modem_free_ppp_pcb(RT_FALSE);
        cat1_modem_set_status_text("4G: PPP启动失败");
        return -RT_ERROR;
    }

    if (cat1_modem_offline_requested())
    {
        cat1_modem_stop_ppp_session(RT_FALSE);
        return -RT_EBUSY;
    }

    if (s_ppp_pending_rx_len > 0U)
    {
        (void)pppos_input_tcpip(s_ppp_pcb,
                                s_ppp_pending_rx,
                                (int)s_ppp_pending_rx_len);
        s_ppp_pending_rx_len = 0U;
    }

    return RT_EOK;
}

static void cat1_modem_free_ppp_pcb(rt_bool_t close_first)
{
    ppp_pcb *pcb = s_ppp_pcb;

    if (pcb == RT_NULL)
    {
        return;
    }

    s_ppp_rx_active = 0U;
    if (close_first)
    {
        (void)pppapi_close(pcb, 1);
        rt_thread_mdelay(200);
        return;
    }

    if (pppapi_free(pcb) != ERR_OK)
    {
        rt_kprintf("cat1_ppp: free failed, keep pcb for reuse\n");
        return;
    }

    s_ppp_pcb = RT_NULL;
    rt_memset(&s_ppp_netif, 0, sizeof(s_ppp_netif));
}

static void cat1_modem_stop_ppp_session(rt_bool_t close_uart)
{
    rt_bool_t close_first = (s_ppp_session_started != 0U ||
                             s_ppp_connecting != 0U ||
                             s_ppp_session_up != 0U) ? RT_TRUE : RT_FALSE;

    s_ppp_stopping = 1U;
    s_ppp_rx_active = 0U;
    s_ppp_connecting = 0U;
    s_ppp_session_up = 0U;
    s_ppp_session_started = 0U;
    s_ppp_recover_requested = 0U;
    cat1_modem_mark_ready(false);
    s_serial_mode = CAT1_MODEM_SERIAL_AT;

    cat1_modem_free_ppp_pcb(close_first);

    s_ppp_stopping = 0U;
    if (close_uart)
    {
        cat1_modem_uart_close();
    }
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
    rt_err_t apply_result;

    uart_device = rt_device_find(CAT1_MODEM_UART_NAME);
    if (uart_device == RT_NULL)
    {
        CAT1_RAW_LOG("cat1_raw: uart device %s not found\n", CAT1_MODEM_UART_NAME);
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
        CAT1_RAW_LOG("cat1_raw: open %s failed: %d\n", CAT1_MODEM_UART_NAME, open_result);
        return open_result;
    }

    config.baud_rate = CAT1_MODEM_BAUDRATE;
    rt_device_control(uart_device, RT_DEVICE_CTRL_CONFIG, &config);
    rt_thread_mdelay(50);

    if (cat1_modem_offline_requested())
    {
        if (opened_here)
        {
            rt_device_close(uart_device);
        }
        return -RT_EBUSY;
    }

    CAT1_RAW_LOG("cat1_raw: baud=%u send AT\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    total = cat1_modem_raw_exchange(uart_device, at_cmd, rx_buf, sizeof(rx_buf), 800U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (cat1_modem_raw_has_success(rx_buf))
    {
        s_detected_baud = CAT1_MODEM_BAUDRATE;
        apply_result = cat1_modem_raw_apply_profile(uart_device, CAT1_MODEM_BAUDRATE);
        if (apply_result == -RT_EBUSY)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return -RT_EBUSY;
        }

        if (apply_result == RT_EOK)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return RT_EOK;
        }
    }

    if (cat1_modem_offline_requested())
    {
        if (opened_here)
        {
            rt_device_close(uart_device);
        }
        return -RT_EBUSY;
    }

    CAT1_RAW_LOG("cat1_raw: baud=%u send ATQ0V1\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    rescue_total = cat1_modem_raw_exchange(uart_device, rescue_cmd, rx_buf, sizeof(rx_buf), 300U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, rescue_total);

    if (cat1_modem_offline_requested())
    {
        if (opened_here)
        {
            rt_device_close(uart_device);
        }
        return -RT_EBUSY;
    }

    CAT1_RAW_LOG("cat1_raw: baud=%u retry AT\\r\n", (unsigned int)CAT1_MODEM_BAUDRATE);
    total = cat1_modem_raw_exchange(uart_device, at_cmd, rx_buf, sizeof(rx_buf), 800U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (cat1_modem_raw_has_success(rx_buf))
    {
        s_detected_baud = CAT1_MODEM_BAUDRATE;
        apply_result = cat1_modem_raw_apply_profile(uart_device, CAT1_MODEM_BAUDRATE);
        if (apply_result == -RT_EBUSY)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return -RT_EBUSY;
        }

        if (apply_result == RT_EOK)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return RT_EOK;
        }
    }

#if !CAT1_MODEM_ENABLE_HIGH_BAUD
    if (opened_here)
    {
        rt_device_close(uart_device);
    }
#endif
#if CAT1_MODEM_ENABLE_HIGH_BAUD
    config.baud_rate = CAT1_MODEM_HIGH_BAUDRATE;
    rt_device_control(uart_device, RT_DEVICE_CTRL_CONFIG, &config);
    rt_thread_mdelay(50);

    CAT1_RAW_LOG("cat1_raw: baud=%u retry AT\\r\n", (unsigned int)CAT1_MODEM_HIGH_BAUDRATE);
    total = cat1_modem_raw_exchange(uart_device, at_cmd, rx_buf, sizeof(rx_buf), 800U);
    cat1_modem_raw_dump("cat1_raw:", rx_buf, total);
    if (cat1_modem_raw_has_success(rx_buf))
    {
        s_detected_baud = CAT1_MODEM_HIGH_BAUDRATE;
        apply_result = cat1_modem_raw_apply_profile(uart_device, CAT1_MODEM_HIGH_BAUDRATE);
        if (apply_result == -RT_EBUSY)
        {
            if (opened_here)
            {
                rt_device_close(uart_device);
            }
            return -RT_EBUSY;
        }

        if (apply_result == RT_EOK)
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
#endif

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
    s_detected_baud = CAT1_MODEM_BAUDRATE;
    cat1_modem_set_status_text("4G: 准备重新上电");
}

static void cat1_modem_repower_module(void)
{
    LOG_W("CAT1 power cycle start");
    cat1_modem_stop_ppp_session(RT_TRUE);
    cat1_modem_mark_ready(false);
    cat1_modem_reset_runtime_state();
    cat1_modem_set_status_text("4G: 断电重启中");
    cat1_modem_power_enable(RT_FALSE);
    rt_thread_mdelay(CAT1_MODEM_REPOWER_DELAY_MS);
    if (!s_online_requested)
    {
        cat1_modem_set_status_text("4G: 已关闭");
        return;
    }

    cat1_modem_power_on_sequence();
    s_boot_latched = 1U;
    s_at_failures = 0U;
}

static void cat1_modem_shutdown_module(void)
{
    rt_bool_t need_power_off;

    need_power_off = (s_ppp_session_started != 0U) ||
                     (s_ppp_rx_active != 0U) ||
                     (s_uart_device != RT_NULL) ||
                     (s_boot_latched != 0U) ||
                     (s_power_enabled != 0U);
    if (need_power_off)
    {
        cat1_modem_stop_ppp_session(RT_TRUE);
    }

    cat1_modem_mark_ready(false);
    cat1_modem_reset_runtime_state();
    if (s_power_enabled)
    {
        cat1_modem_set_status_text("4G: 断电关闭中");
        cat1_modem_power_enable(RT_FALSE);
    }

    cat1_modem_set_status_text("4G: 已关闭");
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

    resp = at_create_resp(CAT1_MODEM_RESP_BUF_SIZE, 0, timeout);
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

    if (!cat1_modem_raw_at_allowed())
    {
        return -RT_EBUSY;
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

    resp = at_create_resp(CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
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

        if (cat1_modem_offline_requested())
        {
            at_delete_resp(resp);
            cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
            return -RT_EBUSY;
        }

        cat1_modem_set_status_text("4G: 检查SIM卡");
        if (cat1_modem_exec_cmd(client, resp, "AT+CPIN?") < 0)
        {
            CAT1_REG_LOG("cat1_reg: CPIN query failed, retry=%d\n", i + 1);
            cat1_modem_set_status_text("4G: SIM未就绪");
            rt_thread_mdelay(1000);
            continue;
        }
        line = at_resp_get_line_by_kw(resp, "+CPIN:");
        if (line != RT_NULL)
        {
            CAT1_REG_LOG("cat1_reg: %s\n", line);
        }
        if (at_resp_get_line_by_kw(resp, "READY") == RT_NULL)
        {
            CAT1_REG_LOG("cat1_reg: SIM not READY, retry=%d\n", i + 1);
            cat1_modem_set_status_text("4G: SIM未就绪");
            rt_thread_mdelay(1000);
            continue;
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CFUN?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CFUN:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CFUN:", "+CFUN: %d", &cfun) >= 0)
            {
                CAT1_REG_LOG("cat1_reg: CFUN=%d\n", cfun);
            }
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        cat1_modem_set_status_text("4G: 等待网络注册");
        if (cat1_modem_exec_cmd(client, resp, "AT+CEREG?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CEREG:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CEREG:", "+CEREG:%*d,%d", &stat) >= 0)
            {
                if (stat == 1 || stat == 5)
                {
                    cat1_modem_set_status_text("4G: LTE已注册");
                    at_delete_resp(resp);
                    return RT_EOK;
                }
                CAT1_REG_LOG("cat1_reg: CEREG stat=%d\n", stat);
            }
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CGREG?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CGREG:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CGREG:", "+CGREG:%*d,%d", &stat) >= 0)
            {
                if (stat == 1 || stat == 5)
                {
                    cat1_modem_set_status_text("4G: 分组域已注册");
                    at_delete_resp(resp);
                    return RT_EOK;
                }
                CAT1_REG_LOG("cat1_reg: CGREG stat=%d\n", stat);
            }
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CGATT?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CGATT:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CGATT:", "+CGATT: %d", &cgatt) >= 0)
            {
                CAT1_REG_LOG("cat1_reg: CGATT=%d\n", cgatt);
            }
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+QNWINFO") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+QNWINFO:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
        }

        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+COPS?") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+COPS:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
        }

        at_resp_set_info(resp, 256, 0, 3000);
        if (cat1_modem_exec_cmd(client, resp, "AT+CSQ") == RT_EOK)
        {
            line = at_resp_get_line_by_kw(resp, "+CSQ:");
            if (line != RT_NULL)
            {
                CAT1_REG_LOG("cat1_reg: %s\n", line);
            }
            if (at_resp_parse_line_args_by_kw(resp, "+CSQ:", "+CSQ: %d,%d", &rssi, &ber) >= 0)
            {
                CAT1_REG_LOG("cat1_reg: CSQ rssi=%d ber=%d\n", rssi, ber);
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

    resp = at_create_resp(CAT1_MODEM_RESP_BUF_SIZE, 0, 5000);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    if (cat1_modem_offline_requested())
    {
        at_delete_resp(resp);
        cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
        return -RT_EBUSY;
    }

    cat1_modem_set_status_text("4G: 配置APN");
    (void)cat1_modem_exec_cmd(client, resp, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1", CAT1_MODEM_APN);
    at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 5000);
    (void)cat1_modem_exec_cmd(client, resp, "AT+CGDCONT=1,\"IP\",\"%s\"", CAT1_MODEM_APN);
    at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 15000);

    cat1_modem_set_status_text("4G: 激活数据链路");
    if (cat1_modem_exec_cmd(client, resp, "AT+QIACT=1") == RT_EOK &&
        at_resp_get_line_by_kw(resp, "OK") != RT_NULL)
    {
        result = RT_EOK;
    }
    else
    {
        at_resp_set_info(resp, CAT1_MODEM_RESP_BUF_SIZE, 0, 15000);
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

    resp = at_create_resp(CAT1_MODEM_RESP_BUF_SIZE, 0, 5000);
    if (resp == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    if (cat1_modem_offline_requested())
    {
        at_delete_resp(resp);
        cat1_modem_set_status_text("4G: 蓝牙连接，4G已停用");
        return -RT_EBUSY;
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
    bool previous_ready = (s_cat1_ready != 0U);

    s_cat1_ready = ready ? 1U : 0U;
    if (previous_ready != ready)
    {
        net_manager_notify_cat1_ready(ready);
    }
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
        if (s_high_baud_config_save_pending)
        {
            s_high_baud_config_save_pending = 0U;
            cat1_modem_save_high_baud_config();
        }

        if (!s_online_requested)
        {
            if (s_ppp_session_started != 0U ||
                s_ppp_rx_active != 0U ||
                s_uart_device != RT_NULL ||
                s_boot_latched != 0U ||
                s_power_enabled != 0U)
            {
                cat1_modem_set_status_text("4G: 正在关闭");
                cat1_modem_shutdown_module();
            }
            else
            {
                cat1_modem_mark_ready(false);
                cat1_modem_set_status_text("4G: 待首页启动");
            }
            cat1_modem_wait_for_request();
            continue;
        }

        if (s_ppp_recover_requested)
        {
            cat1_modem_stop_ppp_session(RT_TRUE);
            cat1_modem_repower_module();
            s_ppp_recover_requested = 0U;
            rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
            continue;
        }

        if (!s_cat1_ready && !s_ppp_session_started)
        {
            int at_ready_result;
            int register_result;
            int pdp_result;

            if (!s_boot_latched)
            {
                LOG_I("CAT1 online power cycle start");
                rt_kprintf("cat1: online power off %u ms before startup\n",
                           (unsigned int)CAT1_MODEM_REPOWER_DELAY_MS);
                cat1_modem_power_enable(RT_FALSE);
                rt_thread_mdelay(CAT1_MODEM_REPOWER_DELAY_MS);
                LOG_I("CAT1 online power-on sequence");
                rt_kprintf("cat1: online power-on sequence\n");
                cat1_modem_power_on_sequence();
                s_boot_latched = 1U;
            }

            at_ready_result = cat1_modem_raw_probe_at();
            if (at_ready_result != RT_EOK)
            {
                if (at_ready_result == -RT_EBUSY || cat1_modem_offline_requested())
                {
                    cat1_modem_shutdown_module();
                    continue;
                }

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

            if (cat1_modem_uart_open() != RT_EOK)
            {
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            s_at_failures = 0U;
            register_result = cat1_modem_wait_registered_raw(s_uart_device);
            if (register_result != RT_EOK)
            {
                if (register_result == -RT_EBUSY || cat1_modem_offline_requested())
                {
                    cat1_modem_shutdown_module();
                    continue;
                }

                LOG_W("CAT1 register timeout: %d", register_result);
                cat1_modem_mark_ready(false);
                cat1_modem_uart_close();
                if (cat1_modem_handle_at_failures("register"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            cat1_modem_capture_network_time(s_uart_device);

#if CAT1_MODEM_ENABLE_HIGH_BAUD
            if (s_high_baud_blocked)
            {
                if (s_detected_baud != CAT1_MODEM_BAUDRATE &&
                    cat1_modem_switch_uart_baud(CAT1_MODEM_BAUDRATE) != RT_EOK)
                {
                    LOG_W("CAT1 switch baud back to %u failed, repower",
                          (unsigned int)CAT1_MODEM_BAUDRATE);
                    cat1_modem_repower_module();
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                cat1_modem_set_status_text("4G: 高速失败，低速PPP");
            }
            else if (cat1_modem_switch_uart_baud(CAT1_MODEM_HIGH_BAUDRATE) != RT_EOK)
            {
                LOG_W("CAT1 switch baud to %u failed, restart at %u",
                      (unsigned int)CAT1_MODEM_HIGH_BAUDRATE,
                      (unsigned int)CAT1_MODEM_BAUDRATE);
                cat1_modem_set_status_text("4G: 高速串口失败，重启回低速");
                cat1_modem_record_high_baud_failure("switch");
                cat1_modem_repower_module();
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }
#else
            cat1_modem_set_status_text("4G: 使用低速PPP");
#endif

            pdp_result = cat1_modem_configure_pdp_raw(s_uart_device);
            if (pdp_result != RT_EOK)
            {
                if (pdp_result == -RT_EBUSY || cat1_modem_offline_requested())
                {
                    cat1_modem_shutdown_module();
                    continue;
                }

                LOG_W("CAT1 PPP PDP configure failed: %d", pdp_result);
                cat1_modem_mark_ready(false);
                cat1_modem_uart_close();
                if (cat1_modem_handle_at_failures("pdp"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
                rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                continue;
            }

            if (cat1_modem_start_ppp_session(s_uart_device) != RT_EOK)
            {
                if (cat1_modem_offline_requested())
                {
                    cat1_modem_shutdown_module();
                    continue;
                }

                LOG_W("CAT1 PPP start failed");
                cat1_modem_mark_ready(false);
                cat1_modem_uart_close();
                if (cat1_modem_handle_at_failures("ppp_start"))
                {
                    rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
                    continue;
                }
            }
        }
        else if (s_ppp_session_started && !s_ppp_session_up && s_ppp_connecting)
        {
            rt_tick_t elapsed = rt_tick_get() - s_ppp_start_tick;
            if (elapsed > rt_tick_from_millisecond(CAT1_MODEM_PPP_CONNECT_TIMEOUT_MS))
            {
                LOG_W("CAT1 PPP connect timeout");
#if CAT1_MODEM_ENABLE_HIGH_BAUD
                if (s_detected_baud == CAT1_MODEM_HIGH_BAUDRATE)
                {
                    cat1_modem_record_high_baud_failure("ppp_timeout");
                    LOG_W("CAT1 high baud PPP timeout, fallback to %u next cycle",
                          (unsigned int)CAT1_MODEM_BAUDRATE);
                }
#endif
                cat1_modem_set_status_text("4G: PPP协商超时");
                s_ppp_recover_requested = 1U;
            }
        }

        rt_thread_mdelay(CAT1_MODEM_POLL_INTERVAL_MS);
    }
}

rt_err_t cat1_modem_init(void)
{
    rt_err_t result;

    cat1_modem_pin_init();
    cat1_modem_load_high_baud_config();
    if (s_cat1_event == RT_NULL)
    {
        s_cat1_event = rt_event_create("cat1evt", RT_IPC_FLAG_FIFO);
        if (s_cat1_event == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }
#if CAT1_MODEM_AT_PAUSED
    s_online_requested = 0U;
    s_power_enabled = 0U;
    s_boot_latched = 0U;
    cat1_modem_mark_ready(false);
    cat1_modem_set_status_text("4G: AT已暂停");
#else
    s_online_requested = 0U;
    s_power_enabled = 0U;
    s_boot_latched = 0U;
    cat1_modem_mark_ready(false);
    cat1_modem_set_status_text("4G: 待启动");
    cat1_modem_power_enable(RT_FALSE);
#endif

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

    result = rt_thread_init(&s_cat1_ppp_rx_thread,
                            "cat1ppp",
                            cat1_modem_ppp_rx_thread_entry,
                            RT_NULL,
                            s_cat1_ppp_rx_stack,
                            sizeof(s_cat1_ppp_rx_stack),
                            CAT1_MODEM_PPP_RX_PRIORITY,
                            CAT1_MODEM_PPP_RX_TICK);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_thread_startup(&s_cat1_ppp_rx_thread);
    return RT_EOK;
}

rt_err_t cat1_modem_request_online(void)
{
#if CAT1_MODEM_AT_PAUSED
    s_online_requested = 0U;
    cat1_modem_mark_ready(false);
    cat1_modem_set_status_text("4G: AT已暂停");
    return RT_EOK;
#else
    if (net_manager_get_desired_mode() != NET_MANAGER_MODE_4G ||
        !net_manager_4g_enabled())
    {
        s_online_requested = 0U;
        cat1_modem_mark_ready(false);
        cat1_modem_set_status_text("4G: 非4G模式，拒绝启动");
        return -RT_EBUSY;
    }

    if (!s_online_requested)
    {
        LOG_I("CAT1 online requested");
        rt_kprintf("cat1: online requested\n");
    }
    s_online_requested = 1U;
    cat1_modem_set_status_text("4G: 启动入网流程");
    if (s_cat1_event != RT_NULL)
    {
        rt_event_send(s_cat1_event, CAT1_MODEM_EVENT_ONLINE_REQ);
    }
    return RT_EOK;
#endif
}

rt_err_t cat1_modem_request_offline(void)
{
    if (s_online_requested)
    {
        LOG_I("CAT1 offline requested");
    }
    s_online_requested = 0U;
    s_ppp_rx_active = 0U;
    s_ppp_connecting = 0U;
    s_ppp_recover_requested = 0U;
    cat1_modem_set_status_text("4G: 关闭请求已提交");
    if (s_cat1_event != RT_NULL)
    {
        rt_event_send(s_cat1_event, CAT1_MODEM_EVENT_OFFLINE_REQ);
    }
    return RT_EOK;
}

bool cat1_modem_is_ready(void)
{
    return s_cat1_ready != 0U;
}

bool cat1_modem_get_network_time(time_t *timestamp)
{
    rt_tick_t age_ticks;
    time_t adjusted_time;

    if (timestamp == RT_NULL || s_network_time_valid == 0U ||
        s_network_time <= 1000000000 || s_network_time_tick == 0U)
    {
        return false;
    }

    age_ticks = rt_tick_get() - s_network_time_tick;
    if (age_ticks > rt_tick_from_millisecond(CAT1_MODEM_NETWORK_TIME_MAX_AGE_MS))
    {
        return false;
    }

    adjusted_time = s_network_time + (time_t)(age_ticks / RT_TICK_PER_SECOND);
    if (adjusted_time <= 1000000000)
    {
        return false;
    }

    *timestamp = adjusted_time;
    return true;
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
    char rx_buf[128];
    int result;

    cat1_modem_pin_init();

    if (!cat1_modem_raw_at_allowed())
    {
        rt_kprintf("cat1_at: PPP data mode active, AT probe skipped\n");
        return;
    }

    result = cat1_modem_uart_open();
    if (result != RT_EOK)
    {
        rt_kprintf("cat1_at: uart open failed: %d\n", result);
        return;
    }

    rt_kprintf("cat1_at: send `AT`\n");
    result = cat1_modem_raw_command(s_uart_device, "AT\r", rx_buf, sizeof(rx_buf), 1500U);
    rt_kprintf("cat1_at: result=%d\n", result);
    if (strstr(rx_buf, "OK") != RT_NULL)
    {
        rt_kprintf("cat1_at: modem replied OK\n");
    }
    else
    {
        rt_kprintf("cat1_at: modem did not reply OK\n");
    }
}
MSH_CMD_EXPORT(cat1_at, send plain AT to CAT1 modem and print response);
