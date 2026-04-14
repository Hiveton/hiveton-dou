/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <rtthread.h>
#include "lwip/api.h"
#include "lwip/tcpip.h"
#include "lwip/dns.h"
#include "lwip/apps/websocket_client.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/apps/mqtt.h"
#include "xiaozhi_mqtt.h"
#include "xiaozhi_websocket.h"
#include "bf0_hal.h"
#include "bts2_global.h"
#include "bts2_app_pan.h"
#include <cJSON.h>
#include "button.h"
#include "audio_server.h"
#include <webclient.h>
#include "./iot/iot_c_api.h"
#include "./mcp/mcp_api.h"
#include "lv_timer.h"
#include "lv_display.h"
#include "lv_obj_pos.h"
#include "lv_tiny_ttf.h"
#include "lv_obj.h"
#include "lv_label.h"
#include "lib_et_asr.h"
#include "../weather/weather.h"
#ifdef BSP_USING_PM
    #include "gui_app_pm.h"
#endif /* BSP_USING_PM */
#include "xiaozhi_client_public.h"
#include "xiaozhi_ui.h"
#include "xiaozhi_audio.h"
#include "audio_mem.h"
#include "network/net_manager.h"
#include "../sleep_manager.h"
#include "ui/ui_dispatch.h"
#include "xiaozhi_service.h"

/* PSRAM内存分配宏 */
#define XZ_PSRAM_ATTR   __attribute__((section(".psram_nocache")))

#define MAX_WSOCK_HDR_LEN 4096
#define SPEAKING_THRESHOLD_MS (5 * 60 * 1000) // 小智讲话时间阈值 

extern xz_audio_t *thiz;
extern lv_obj_t *main_container;
extern lv_obj_t *standby_screen;
extern uint8_t Initiate_disconnection_flag;
extern rt_mailbox_t g_ui_task_mb;
extern rt_tick_t last_listen_tick;


xiaozhi_ws_t g_xz_ws;
rt_mailbox_t g_button_event_mb;
enum DeviceState web_g_state;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(message) //6000地址
static char message[256];
L2_RET_BSS_SECT_END
#else
static char message[256] L2_RET_BSS_SECT(message);
#endif
static const char *mode_str[] = {"auto", "manual", "realtime"};
static const char *hello_message =
    "{"
    "\"type\":\"hello\","
    "\"version\": 3,"
#ifdef CONFIG_IOT_PROTOCOL_MCP
    "\"features\":{\"mcp\":true},"
#endif
    "\"transport\":\"websocket\","
    "\"audio_params\":{"
    "\"format\":\"opus\", \"sample_rate\":16000, \"channels\":1, "
    "\"frame_duration\":60"
    "}}";

// 倒计时动画
static lv_obj_t *countdown_screen = NULL;
static rt_thread_t countdown_thread = RT_NULL;
static bool  g_ota_verified = false;
bool shutdown_state = true;

typedef struct
{
    char code[7];
    bool is_activated;
    rt_sem_t sem;
} activation_context_t;

typedef struct
{
    char *url;
    char *token;
} websocket_context_t;

typedef struct
{
    char host[128];
    char path[256];
    u16_t port;
    int ssl_enabled;
} websocket_endpoint_t;

static activation_context_t g_activation_context;
static websocket_context_t g_websocket_context;
static rt_tick_t g_speaking_start_tick = 0;  // 讲话开始时间
static rt_tick_t g_total_speaking_time = 0;  // 累计讲话时间
static bool g_is_speaking = false;           // 是否正在讲话
static volatile rt_bool_t g_drop_interrupted_reply = RT_FALSE;

static int parse_ota_response(const char *response,
                              activation_context_t *active,
                              websocket_context_t *websocket,
                              char *server_message,
                              size_t server_message_size);

static void xz_websocket_end_barge_in_drop(void)
{
    if (g_drop_interrupted_reply)
    {
        rt_kprintf("Barge-in drop window closed\n");
        g_drop_interrupted_reply = RT_FALSE;
    }
}

void xz_websocket_begin_barge_in(void)
{
    g_drop_interrupted_reply = RT_TRUE;
    rt_kprintf("Barge-in drop window opened\n");
}

static void xz_report_prepare_error(const char *message, rt_bool_t interactive)
{
    const char *final_message = message;
    char ui_message[160];

    if (final_message == RT_NULL || final_message[0] == '\0')
    {
        final_message = "小智服务器未返回有效设备信息";
    }

    rt_kprintf("xiaozhi prepare failed: %s\n", final_message);
    xiaozhi_service_notify_error(final_message);

    if (interactive)
    {
        rt_snprintf(ui_message, sizeof(ui_message), "%s", final_message);
        xiaozhi_ui_chat_status("连接失败");
        xiaozhi_ui_chat_output(ui_message);
        xiaozhi_ui_standby_chat_output(ui_message);
        xiaozhi_ui_update_emoji("embarrassed");
    }
}

static void xz_set_error_text(char *buffer, size_t buffer_size, const char *text)
{
    if (buffer == RT_NULL || buffer_size == 0U)
    {
        return;
    }

    if (text == RT_NULL || text[0] == '\0')
    {
        buffer[0] = '\0';
        return;
    }

    rt_snprintf(buffer, buffer_size, "%s", text);
}

static void xz_show_activation_prompt(rt_bool_t interactive)
{
    char str_temp[256];

    she_bei_ma = 0;
    snprintf(str_temp, sizeof(str_temp),
             "设备未添加，请前往 xiaozhi.me 输入绑定码: \n %s \n ",
             g_activation_context.code);
    xiaozhi_ui_chat_output(str_temp);
    xiaozhi_ui_standby_chat_output(str_temp);

    if (interactive)
    {
        xiaozhi_ui_chat_status("等待绑定...");
    }
}

static int xz_wait_for_activation_completion(rt_bool_t interactive,
                                             char *error_text,
                                             size_t error_text_size)
{
    const rt_tick_t poll_wait = rt_tick_from_millisecond(3000);
    uint32_t retry = 120;
    char last_code[sizeof(g_activation_context.code)] = {0};

    if (g_activation_context.sem != RT_NULL)
    {
        while (rt_sem_take(g_activation_context.sem, 0) == RT_EOK)
        {
        }
    }

    while (retry-- > 0)
    {
        char *ota_version;
        char server_message[160] = {0};
        int parse_result;
        const char *ota_error;

        if (strncmp(last_code, g_activation_context.code, sizeof(last_code)) != 0)
        {
            rt_snprintf(last_code, sizeof(last_code), "%s",
                        g_activation_context.code);
            xz_show_activation_prompt(interactive);
        }
        else if (interactive)
        {
            xiaozhi_ui_chat_status("等待绑定...");
            xiaozhi_ui_chat_output("请在 xiaozhi.me 完成绑定，设备将自动继续连接");
        }

        if (g_activation_context.sem != RT_NULL &&
            rt_sem_take(g_activation_context.sem, poll_wait) == RT_EOK)
        {
            xz_set_error_text(error_text, error_text_size, "连接已取消");
            return -RT_EINTR;
        }

        ota_version = get_xiaozhi();
        if (ota_version == RT_NULL)
        {
            ota_error = get_xiaozhi_last_error();
            if (ota_error != RT_NULL && ota_error[0] != '\0')
            {
                xz_set_error_text(error_text, error_text_size, ota_error);
            }
            continue;
        }

        parse_result = parse_ota_response(ota_version,
                                          &g_activation_context,
                                          &g_websocket_context,
                                          server_message,
                                          sizeof(server_message));
        if (parse_result != RT_EOK)
        {
            xz_set_error_text(error_text, error_text_size,
                              server_message[0] != '\0' ? server_message :
                              "绑定状态检查失败");
            return parse_result;
        }

        if (!g_activation_context.is_activated)
        {
            she_bei_ma = 1;
            ui_dispatch_request_activity();
            return RT_EOK;
        }
    }

    xz_set_error_text(error_text, error_text_size,
                      "等待绑定超时，请完成绑定后重试");
    return -RT_ETIMEOUT;
}

static int xz_parse_websocket_url(const char *url, websocket_endpoint_t *endpoint,
                                  char *error_text, size_t error_text_size)
{
    const char *scheme_end;
    const char *host_begin;
    const char *host_end;
    const char *path_begin;
    const char *host_limit;
    const char *port_begin;
    size_t host_len;
    size_t path_len;
    u32_t port = 0;

    if (url == RT_NULL || endpoint == RT_NULL)
    {
        xz_set_error_text(error_text, error_text_size, "WebSocket 地址为空");
        return -RT_ERROR;
    }

    memset(endpoint, 0, sizeof(*endpoint));

    scheme_end = strstr(url, "://");
    if (scheme_end == RT_NULL)
    {
        xz_set_error_text(error_text, error_text_size, "WebSocket 地址格式无效");
        return -RT_ERROR;
    }

    if (strncmp(url, "wss://", 6) == 0)
    {
        endpoint->ssl_enabled = 1;
        endpoint->port = LWIP_IANA_PORT_HTTPS;
    }
    else if (strncmp(url, "ws://", 5) == 0)
    {
        endpoint->ssl_enabled = 0;
        endpoint->port = LWIP_IANA_PORT_HTTP;
    }
    else
    {
        xz_set_error_text(error_text, error_text_size, "仅支持 ws/wss 协议");
        return -RT_ERROR;
    }

    host_begin = scheme_end + 3;
    if (*host_begin == '\0')
    {
        xz_set_error_text(error_text, error_text_size, "WebSocket 地址缺少主机名");
        return -RT_ERROR;
    }

    path_begin = strchr(host_begin, '/');
    host_limit = path_begin != RT_NULL ? path_begin : host_begin + strlen(host_begin);
    host_end = host_limit;
    port_begin = strchr(host_begin, ':');
    if (port_begin != RT_NULL && port_begin < host_end)
    {
        host_end = port_begin;
    }

    host_len = (size_t)(host_end - host_begin);
    if (host_len == 0U || host_len >= sizeof(endpoint->host))
    {
        xz_set_error_text(error_text, error_text_size, "WebSocket 主机名无效或过长");
        return -RT_ERROR;
    }

    memcpy(endpoint->host, host_begin, host_len);
    endpoint->host[host_len] = '\0';

    if (port_begin != RT_NULL && port_begin < host_limit)
    {
        const char *p = port_begin + 1;

        if (*p == '\0')
        {
            xz_set_error_text(error_text, error_text_size, "WebSocket 端口为空");
            return -RT_ERROR;
        }

        while (p < host_limit)
        {
            if (*p < '0' || *p > '9')
            {
                xz_set_error_text(error_text, error_text_size, "WebSocket 端口格式无效");
                return -RT_ERROR;
            }

            port = (port * 10U) + (u32_t)(*p - '0');
            if (port > 65535U)
            {
                xz_set_error_text(error_text, error_text_size, "WebSocket 端口超出范围");
                return -RT_ERROR;
            }
            p++;
        }

        endpoint->port = (u16_t)port;
    }

    if (path_begin == RT_NULL)
    {
        endpoint->path[0] = '/';
        endpoint->path[1] = '\0';
    }
    else
    {
        path_len = strlen(path_begin);
        if (path_len >= sizeof(endpoint->path))
        {
            xz_set_error_text(error_text, error_text_size, "WebSocket 路径过长");
            return -RT_ERROR;
        }

        memcpy(endpoint->path, path_begin, path_len + 1U);
    }

    return RT_EOK;
}

static wsock_state_t *xz_ws_active_client(void)
{
    if (!g_xz_ws.is_connected)
    {
        return RT_NULL;
    }

    if (g_xz_ws.clnt.pcb == RT_NULL)
    {
        return RT_NULL;
    }

    return &g_xz_ws.clnt;
}

static const char *xz_ws_active_session_id(void)
{
    if (!g_xz_ws.is_connected || g_xz_ws.session_id[0] == '\0')
    {
        return RT_NULL;
    }

    return (const char *)g_xz_ws.session_id;
}

static rt_bool_t xz_ws_send_text_message(const char *msg)
{
    wsock_state_t *client;

    if (msg == RT_NULL)
    {
        return RT_FALSE;
    }

    client = xz_ws_active_client();
    if (client == RT_NULL)
    {
        rt_kprintf("websocket is not connected\n");
        return RT_FALSE;
    }

    if (wsock_write(client, msg, strlen(msg), OPCODE_TEXT) != ERR_OK)
    {
        rt_kprintf("websocket write failed\n");
        return RT_FALSE;
    }

    return RT_TRUE;
}


void parse_helLo(const u8_t *data, u16_t len);

void send_iot_descriptors(void)
{
    const char *desc = iot_get_descriptors_json();
    if (desc == NULL)
    {
        rt_kprintf("Failed to get IoT descriptors\n");
        return;
    }

    char msg[1024];
    snprintf(msg, sizeof(msg),
             "{\"session_id\":\"%s\",\"type\":\"iot\",\"update\":true,"
             "\"descriptors\":%s}",
             g_xz_ws.session_id, desc);

    rt_kprintf("Sending IoT descriptors:\n");
    rt_kprintf(msg);
    rt_kprintf("\n");
    if (g_xz_ws.is_connected == 1)
    {
        wsock_write(&g_xz_ws.clnt, msg, strlen(msg), OPCODE_TEXT);
    }
    else
    {
        rt_kprintf("websocket is not connected\n");
    }
}

void send_iot_states(void)
{
    const char *state = iot_get_states_json();
    if (state == NULL)
    {
        rt_kprintf("Failed to get IoT states\n");
        return;
    }

    char msg[1024];
    snprintf(msg, sizeof(msg),
             "{\"session_id\":\"%s\",\"type\":\"iot\",\"update\":true,"
             "\"states\":%s}",
             g_xz_ws.session_id, state);

    rt_kprintf("Sending IoT states:\n");
    rt_kprintf(msg);
    rt_kprintf("\n");
    if (g_xz_ws.is_connected == 1)
    {
        wsock_write(&g_xz_ws.clnt, msg, strlen(msg), OPCODE_TEXT);
    }
    else
    {
        rt_kprintf("websocket is not connected\n");
    }
}

void ws_send_speak_abort(void *ws, char *session_id, int reason)
{
    const char *active_session_id;

    (void)ws;
    (void)session_id;
    rt_kprintf("speak abort\n");
    active_session_id = xz_ws_active_session_id();
    if (active_session_id == RT_NULL)
    {
        rt_kprintf("websocket session is not ready\n");
        return;
    }

    rt_snprintf(message, 256, "{\"session_id\":\"%s\",\"type\":\"abort\"",
                active_session_id);
    if (reason)
        strcat(message, ",\"reason\":\"wake_word_detected\"}");
    else
        strcat(message, "}");

    xz_ws_send_text_message(message);
}

void ws_send_listen_start(void *ws, char *session_id, enum ListeningMode mode)
{
    const char *active_session_id;

    (void)ws;
    (void)session_id;
    rt_kprintf("listen start,mode=%d\n",mode);
    active_session_id = xz_ws_active_session_id();
    if (active_session_id == RT_NULL)
    {
        rt_kprintf("websocket session is not ready\n");
        return;
    }

    if (mode < kListeningModeAutoStop || mode > kListeningModeAlwaysOn)
    {
        mode = kListeningModeManualStop;
    }

    rt_snprintf(message, 256,
                "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":"
                "\"start\",\"mode\":\"%s\"}",
                active_session_id, mode_str[mode]);
    // rt_kputs("\r\n");
    // rt_kputs(message);
    // rt_kputs("\r\n");
    xz_ws_send_text_message(message);
}

void ws_send_listen_stop(void *ws, char *session_id)
{
    const char *active_session_id;

    (void)ws;
    (void)session_id;
    rt_kprintf("listen stop\n");
    active_session_id = xz_ws_active_session_id();
    if (active_session_id == RT_NULL)
    {
        rt_kprintf("websocket session is not ready\n");
        return;
    }

    rt_snprintf(
        message, 256,
        "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
        active_session_id);
    xz_ws_send_text_message(message);
}
void ws_send_hello(void *ws)
{
    if (g_xz_ws.is_connected == 1)
    {
        wsock_write((wsock_state_t *)ws, hello_message, strlen(hello_message),
                    OPCODE_TEXT);
    }
    else
    {
        rt_kprintf("websocket is not connected\n");
    }
}

rt_bool_t xz_websocket_send_detected(const char *wakeword)
{
    const char *active_session_id;
    const char *active_wakeword = wakeword;

    active_session_id = xz_ws_active_session_id();
    if (active_session_id == RT_NULL)
    {
        rt_kprintf("websocket session is not ready\n");
        return RT_FALSE;
    }

    if (active_wakeword == RT_NULL || active_wakeword[0] == '\0')
    {
        active_wakeword = "你好小智";
    }

    rt_snprintf(message, sizeof(message),
                "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"detected\",\"text\":\"%s\"}",
                active_session_id, active_wakeword);

    rt_kprintf("listen detected: %s\n", active_wakeword);
    return xz_ws_send_text_message(message);
}
void xz_audio_send_using_websocket(uint8_t *data, int len)
{
    if (g_xz_ws.is_connected == 1)
    {
        err_t err = wsock_write(&g_xz_ws.clnt, data, len, OPCODE_BINARY);
        // rt_kprintf("send audio = %d len=%d\n", err, len);
    }
    // else
    //     rt_kprintf("Websocket disconnected\n");
}

err_t my_wsapp_fn(int code, char *buf, size_t len)
{
    if (code == WS_CONNECT)
    {
        xz_websocket_end_barge_in_drop();
        rt_kprintf("websocket connected\n");
        int status = (uint16_t)(uint32_t)buf;
        if (status == 101) // wss setup success
        {
            rt_sem_release(g_xz_ws.sem);
            g_xz_ws.is_connected = 1;
            xiaozhi_service_notify_state(XZ_SERVICE_READY);
        }
    }
    else if (code == WS_DISCONNECT)
    {
        xz_websocket_end_barge_in_drop();
        if (!g_xz_ws.is_connected)
        {
            rt_sem_release(g_xz_ws.sem);
        }
        else
        {
            //  #ifdef BSP_USING_PM
            //             // 关闭 VAD
            //             if(thiz->vad_enabled)
            //             {
            //                 thiz->vad_enabled = false;
            //                 rt_kprintf("web_cloae,so vad_close\n");
            //             }
            //  #endif
            MCP_RGBLED_CLOSE();

            xiaozhi_ui_chat_status("休眠中...");
            xiaozhi_ui_chat_output("请按键或语音唤醒");
            xiaozhi_ui_standby_chat_output("小智已断开请按键唤醒");//待机界面
            xiaozhi_ui_update_emoji("sleepy");
            xiaozhi_ui_update_standby_emoji("sleepy");
            if (!xiaozhi_network_service_ready())
            {
                ui_swith_to_standby_screen();
            }
        }
        rt_kprintf("WebSocket closed\n");
        g_xz_ws.is_connected = 0;
        xiaozhi_service_notify_error("小智连接已断开");
        xiaozhi_service_notify_state(XZ_SERVICE_READY);
    }
    else if (code == WS_TEXT)
    {
        // 打印原始数据
        rt_kprintf("web send to me:\n");
        rt_kprintf("%.*s\n", (int)len, buf); // 打印接收到的文本数据
        parse_helLo(buf, len);
    }
    else
    {
        // Receive Audio Data
        if (g_drop_interrupted_reply)
        {
            rt_kprintf("drop interrupted reply audio len=%d\n", (int)len);
            return 0;
        }
        xz_audio_downlink(buf, len, NULL, 0);
    }
    return 0;
}
void xiaozhi2(int argc, char **argv);

static void xz_button_event_handler(int32_t pin, button_action_t action)
{
    rt_kprintf("in ws button handle\n");
    ui_dispatch_request_activity();
    sleep_manager_request_wakeup();
     rt_kprintf("in ws button handle2\n");
    // 如果当前处于KWS模式，则退出KWS模式
        if (g_kws_running) 
        {  
            rt_kprintf("KWS exit\n");
            g_kws_force_exit = 1;
        }
    static button_action_t last_action = BUTTON_RELEASED;
    if (last_action == action)
        return;
    last_action = action;

    if (action == BUTTON_PRESSED)
    {
        rt_kprintf("pressed\r\n");
        rt_kprintf("按键->对话");

        // 检查是否弹窗显示中

        xiaozhi_ui_update_confirm_button_event(1); // 模拟点击更新按钮

        if (ui_dispatch_get_active_screen() == UI_SCREEN_HOME)
        {
            ui_switch_to_xiaozhi_screen();
        }
    
        // 1. 检查是否处于睡眠状态（WebSocket未连接）
        if (!g_xz_ws.is_connected)
        {
            xiaozhi_ui_chat_status("连接小智...");
            xiaozhi2(0, RT_NULL);
        }
        else
        {
            // 2. 已唤醒，直接进入对话模式
            rt_mb_send(g_button_event_mb, BUTTON_EVENT_PRESSED);
            xiaozhi_ui_chat_status("聆听中...");
        }
    }
    else if (action == BUTTON_RELEASED)
    {
        sleep_manager_request_wakeup();
        rt_kprintf("released\r\n");
        // 仅在已唤醒时发送停止监听
        if (g_xz_ws.is_connected)
        {
            rt_mb_send(g_button_event_mb, BUTTON_EVENT_RELEASED);
            xiaozhi_ui_chat_status("待命中...");
        }
    }
}
#ifndef XIAOZHI_USING_MQTT
void simulate_button_pressed()
{
    rt_kprintf("ws simulate_button_pressed pressed\r\n");
    if(Initiate_disconnection_flag)//蓝牙主动断开不允许mic触发
    {
        rt_kprintf("Initiate_disconnection_flag\r\n");
        return;
    }
    xz_button_event_handler(BSP_KEY1_PIN, BUTTON_PRESSED);
}
void simulate_button_released()
{
    rt_kprintf("ws simulate_button_released released\r\n");
    if(Initiate_disconnection_flag)
    {
        return;
    }
    xz_button_event_handler(BSP_KEY1_PIN, BUTTON_RELEASED);
}
#endif

static void xz_button2_event_handler(int32_t pin, button_action_t action)
{
    if (action == BUTTON_PRESSED)
    {

        rt_kprintf("xz_button2_event_handler - pressed\n");

        xiaozhi_ui_update_confirm_button_event(0); // 模拟点击取消按钮

    }
    else if (action == BUTTON_LONG_PRESSED)
    {
        // 按下超过3秒，触发关机
        rt_kprintf("xz_button2_event_handler - long pressed\n");
        //检查设备是否已绑定设备码
        // if (g_activation_context.is_activated)
        // {
        //     rt_sem_release(g_activation_context.sem);
        // }

            // 长按3秒，直接发送关机消息到ui_task
        if (ui_dispatch_get_active_screen() != UI_SCREEN_HOME && g_activation_context.sem != RT_NULL)
        {
            rt_sem_release(g_activation_context.sem);
        }
        shutdown_state = false;
        sleep_manager_request_wakeup();
        rt_thread_mdelay(100);
        rt_mb_send(g_ui_task_mb, UI_EVENT_SHUTDOWN);
    }

    else if (action == BUTTON_RELEASED)
    {
        rt_kprintf("xz_button2_event_handler - released\n");
    }
}

void xz_ws_button_init(void) // Session key
{
    extern void app_buttons_init(void);
    app_buttons_init();
}
void xz_ws_button_init2(void)
{
    extern void app_buttons_init(void);
    app_buttons_init();
}
void xz_ws_audio_init()
{
    rt_kprintf("xz_audio_init\n");
    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, 8); // 设置音量
    xz_audio_decoder_encoder_open(1); // 打开音频解码器和编码器

}
void parse_helLo(const u8_t *data, u16_t len)
{
    cJSON *item = NULL;
    cJSON *root = NULL;
    rt_kprintf(data);
    rt_kprintf("--\r\n");
    root = cJSON_Parse(data); /*json_data 为MQTT的原始数据*/
    if (!root)
    {
        rt_kprintf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return;
    }

    char *type = cJSON_GetObjectItem(root, "type")->valuestring;
    rt_kprintf("type = %s\n", type);
    if (strcmp(type, "hello") == 0)
    {
        xz_websocket_end_barge_in_drop();
        char *session_id = cJSON_GetObjectItem(root, "session_id")->valuestring;
        rt_kprintf("session_id = %s\n", session_id);
        cJSON *audio_param = cJSON_GetObjectItem(root, "audio_params");
        char *sample_rate =
            cJSON_GetObjectItem(audio_param, "sample_rate")->valuestring;
        char *duration =
            cJSON_GetObjectItem(audio_param, "duration")->valuestring;
        g_xz_ws.sample_rate = atoi(sample_rate);
        g_xz_ws.frame_duration = atoi(duration);
        strncpy(g_xz_ws.session_id, session_id, 9);
        web_g_state = kDeviceStateIdle;
        
#ifndef CONFIG_IOT_PROTOCOL_MCP
        send_iot_descriptors(); // 发送iot描述
        send_iot_states();      // 发送iot状态
#endif// CONFIG_IOT_PROTOCOL_MCP
        xiaozhi_ui_chat_status("待命中...");
        xiaozhi_ui_chat_output("小智已连接!");
        xiaozhi_ui_update_emoji("neutral");
        xiaozhi_ui_update_standby_emoji("funny");
        xiaozhi_service_notify_state(XZ_SERVICE_READY);
        xiaozhi_service_notify_chat_output("小智已连接!");
        xiaozhi_service_notify_emoji("neutral");
        rt_kprintf("hello->对话\n");
        ui_switch_to_xiaozhi_screen();//切换到小智对话界面
#ifdef PKG_XIAOZHI_USING_AEC
        ws_send_listen_start(&g_xz_ws.clnt, g_xz_ws.session_id, kListeningModeAlwaysOn);
#endif
    }
    else if (strcmp(type, "goodbye") == 0)
    {
        xz_websocket_end_barge_in_drop();
        web_g_state = kDeviceStateUnknown;
        rt_kprintf("session ended\n");
        xiaozhi_ui_chat_status("睡眠中...");
        xiaozhi_ui_chat_output("等待唤醒...");
        xiaozhi_ui_update_emoji("sleep");
        xiaozhi_service_notify_state(XZ_SERVICE_READY);
        xiaozhi_service_notify_chat_output("等待唤醒...");
        xiaozhi_service_notify_emoji("sleep");
    }
    else if (strcmp(type, "stt") == 0)
    {
        xz_websocket_end_barge_in_drop();
        char *txt = cJSON_GetObjectItem(root, "text")->valuestring;
        xiaozhi_ui_chat_output(txt);
        xiaozhi_service_notify_chat_output(txt);
        last_listen_tick = rt_tick_get();
        web_g_state = kDeviceStateIdle;
        xiaozhi_ui_chat_status("正在思考...");
        xiaozhi_service_notify_state(XZ_SERVICE_READY);
    }
    else if (strcmp(type, "tts") == 0)
    {
        if (g_drop_interrupted_reply)
        {
            rt_kprintf("drop interrupted reply tts\n");
            cJSON_Delete(root);
            return;
        }

        char *txt = cJSON_GetObjectItem(root, "text")->valuestring;
        rt_kprintf(txt);
        rt_kprintf("--\r\n");

        char *state = cJSON_GetObjectItem(root, "state")->valuestring;

        if (strcmp(state, "start") == 0)
        {
            if (web_g_state == kDeviceStateIdle ||
                web_g_state == kDeviceStateListening)
            {
                web_g_state = kDeviceStateSpeaking;
                xz_speaker(1); // 打开扬声器
                xiaozhi_ui_chat_status("讲话中...");
                xiaozhi_service_notify_state(XZ_SERVICE_SPEAKING);

                // 开始累计讲话时间
                g_is_speaking = true;
                g_speaking_start_tick = rt_tick_get();
            }
        }
        else if (strcmp(state, "stop") == 0)
        {           
            // 计算本次讲话时间并累加
            if (g_is_speaking) 
            {
                rt_tick_t current_tick = rt_tick_get();
                rt_tick_t speaking_duration = current_tick - g_speaking_start_tick;
                g_total_speaking_time += speaking_duration;
                g_is_speaking = false;

                rt_kprintf("xiaozhi total_speaking_time: %d ticks\n", g_total_speaking_time);
                // 检查是否达到5分钟阈值
                if (g_total_speaking_time >= rt_tick_from_millisecond(SPEAKING_THRESHOLD_MS)) 
                {
                    rt_kprintf("Speaking time reached 5 minutes, reinitializing audio\n");
                    g_total_speaking_time = 0; // 重置累计时间
                    xiaozhi_ui_reinit_audio();
                }
            }
            
            web_g_state = kDeviceStateIdle;
            xz_speaker(0); // 关闭扬声器
            xiaozhi_ui_chat_status("待命中...");
            xiaozhi_service_notify_state(XZ_SERVICE_READY);
        }
        else if (strcmp(state, "sentence_start") == 0)
        {
            char *txt = cJSON_GetObjectItem(root, "text")->valuestring;
            // rt_kputs(txt);
            xiaozhi_ui_tts_output(txt); // 使用专用函数处理 tts 输出
            xiaozhi_service_notify_tts_output(txt);
        }
        else
        {
            rt_kprintf("Unkown test: %s\n", state);
        }
    }
    else if (strcmp(type, "llm") == 0)
    {
        if (g_drop_interrupted_reply)
        {
            rt_kprintf("drop interrupted reply emotion\n");
            cJSON_Delete(root);
            return;
        }

        rt_kprintf(cJSON_GetObjectItem(root, "emotion")->valuestring);
        xiaozhi_ui_update_emoji(
            cJSON_GetObjectItem(root, "emotion")->valuestring);
        xiaozhi_service_notify_emoji(
            cJSON_GetObjectItem(root, "emotion")->valuestring);
    }
    else if (strcmp(type, "iot") == 0)
    {
#ifndef CONFIG_IOT_PROTOCOL_MCP
        rt_kprintf("iot command\n");
        cJSON *commands = cJSON_GetObjectItem(root, "commands");
        // rt_kprintf("commands: %s\n", cJSON_Print(commands));
        for (int i = 0; i < cJSON_GetArraySize(commands); i++)
        {
            // rt_kprintf("command %d: %s\n", i,
            // cJSON_Print(cJSON_GetArrayItem(commands, i)));
            cJSON *cmd = cJSON_GetArrayItem(commands, i);
            // rt_kprintf("cmd: %s\n", cJSON_Print(cmd));
            char *cmd_str = cJSON_PrintUnformatted(cmd);
            // rt_kprintf("cmd_str: %s\n", cmd_str);
            if (cmd_str)
            {
                iot_invoke((uint8_t *)cmd_str, strlen(cmd_str));
                send_iot_states(); // 发送 IoT 状态
                rt_free(cmd_str);
            }
        }
#endif // 定义了MCP就不走IOT
    }
    else if (strcmp(type, "mcp") == 0)
    {
        rt_kprintf("mcp command\n");
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload && cJSON_IsObject(payload))
        {
            McpServer_ParseMessage(cJSON_PrintUnformatted(payload));
        }
    }
    else
    {
        rt_kprintf("Unkown type: %s\n", type);
    }

    cJSON_Delete(root); /*每次调用cJSON_Parse函数后，都要释放内存*/
}

static void svr_found_callback(const char *name, const ip_addr_t *ipaddr,
                               void *callback_arg)
{
    if (ipaddr != NULL)
    {
        rt_kprintf("DNS lookup succeeded, IP: %s\n", ipaddr_ntoa(ipaddr));
    }
}

static int xiaozhi_ws_connect_internal(rt_bool_t interactive)
{
    websocket_endpoint_t endpoint;
    char endpoint_error[96] = {0};

    xz_prepare_tls_allocator();

    if (!xiaozhi_network_service_ready())
    {
        xz_report_prepare_error("请在手机上开启网络共享后重新发起连接",
                                interactive);
        return -RT_ERROR;
    }

    if (g_activation_context.is_activated)
    {
        if (xz_wait_for_activation_completion(interactive,
                                              endpoint_error,
                                              sizeof(endpoint_error)) != RT_EOK)
        {
            xz_report_prepare_error(endpoint_error, interactive);
            return -RT_ERROR;
        }
    }
    // 检查 WebSocket 的 TCP 控制块状态是否为 CLOSED
    if (g_xz_ws.clnt.pcb != NULL && g_xz_ws.clnt.pcb->state != CLOSED)
    {
        rt_kprintf("WebSocket is not in CLOSED state, cannot reconnect\n");
        return -RT_EBUSY;
    }

    memset(&endpoint, 0, sizeof(endpoint));
    endpoint.ssl_enabled = 1;
    endpoint.port = LWIP_IANA_PORT_HTTPS;
    rt_snprintf(endpoint.host, sizeof(endpoint.host), "%s", XIAOZHI_HOST);
    rt_snprintf(endpoint.path, sizeof(endpoint.path), "%s", XIAOZHI_WSPATH);

    if (g_websocket_context.url != RT_NULL && g_websocket_context.url[0] != '\0')
    {
        rt_kprintf("Ignore OTA websocket url, use fixed endpoint: wss://%s%s\n",
                   endpoint.host, endpoint.path);
    }

    err_t err;
    uint32_t retry = 10;
    while (retry-- > 0)
    {
        const char *auth_token;

        if (g_xz_ws.sem == NULL)
            g_xz_ws.sem = rt_sem_create("xz_ws", 0, RT_IPC_FLAG_FIFO);

        wsock_init(&g_xz_ws.clnt, endpoint.ssl_enabled, 1,
                   my_wsapp_fn); // 初始化websocket,注册回调函数
        char *Client_Id = get_client_id();
        auth_token = (g_websocket_context.token != RT_NULL &&
                      g_websocket_context.token[0] != '\0')
                         ? g_websocket_context.token
                         : XIAOZHI_TOKEN;
        err = wsock_connect(
            &g_xz_ws.clnt, MAX_WSOCK_HDR_LEN, endpoint.host, endpoint.path,
            endpoint.port, auth_token, NULL,
            "Protocol-Version: 1\r\nDevice-Id: %s\r\nClient-Id: %s\r\n",
            get_mac_address(), Client_Id);
        rt_kprintf("Web socket connection %d\r\n", err);
        if (err == 0)
        {
            rt_kprintf("err = 0\n");
            if (RT_EOK == rt_sem_take(g_xz_ws.sem, 50000))
            {
                rt_kprintf("g_xz_ws.is_connected = %d\n", g_xz_ws.is_connected);
                if (g_xz_ws.is_connected)
                {
                    err = wsock_write(&g_xz_ws.clnt, hello_message,
                                      strlen(hello_message), OPCODE_TEXT);

                    rt_kprintf("Web socket write %d\r\n", err);
                    break;
                }
                else
                {
                    rt_kprintf("err = wsock_write_Web socket disconnected\r\n");
                }
            }
            else
            {
                rt_kprintf("Web socket connected timeout\r\n");
            }
        }
        else
        {
            rt_kprintf("Waiting ws_connect ready%d... \r\n", retry);
            if (interactive)
            {
                xiaozhi_ui_chat_output("小智连接失败!");
            }
            rt_thread_mdelay(1000);
            if (interactive)
            {
                ui_swith_to_standby_screen();
            }
        }
    }

    return g_xz_ws.is_connected ? RT_EOK : -RT_ETIMEOUT;
}

static int parse_ota_response(const char *response,
                              activation_context_t *active,
                              websocket_context_t *websocket,
                              char *server_message,
                              size_t server_message_size)
{
    cJSON *message_obj;
    cJSON *websocket_obj;
    cJSON *activation_obj;
    rt_bool_t has_ws_token = RT_FALSE;

    if (!response || !active || !websocket)
    {
        rt_kprintf("parse_ota_response: Invalid parameters\n");
        return -RT_ERROR;
    }

    cJSON *root = cJSON_Parse(response);
    if (!root)
    {
        rt_kprintf("parse_ota_response: Failed to parse JSON, error: [%s]\n",
                   cJSON_GetErrorPtr());
        return -RT_ERROR;
    }

    if (server_message != RT_NULL && server_message_size > 0U)
    {
        server_message[0] = '\0';
    }

    // 初始化结构体
    active->code[0] = '\0';
    active->is_activated = false;
    if (websocket->url)
    {
        audio_mem_free(websocket->url);
        websocket->url = NULL;
    }
    if (websocket->token)
    {
        audio_mem_free(websocket->token);
        websocket->token = NULL;
    }

    message_obj = cJSON_GetObjectItem(root, "message");
    if (message_obj && cJSON_IsString(message_obj) &&
        server_message != RT_NULL && server_message_size > 0U)
    {
        rt_snprintf(server_message, server_message_size, "%s",
                    message_obj->valuestring);
    }

    // 解析 websocket 部分
    websocket_obj = cJSON_GetObjectItem(root, "websocket");
    if (websocket_obj && cJSON_IsObject(websocket_obj))
    {
        cJSON *url_item = cJSON_GetObjectItem(websocket_obj, "url");
        if (url_item && cJSON_IsString(url_item))
        {
            size_t url_len = strlen(url_item->valuestring) + 1;
            websocket->url = (char *)audio_mem_malloc((uint32_t)url_len);
            if (websocket->url)
            {
                strncpy(websocket->url, url_item->valuestring, url_len);
                rt_kprintf("Websocket URL from OTA (ignored for connect): %s\n",
                           websocket->url);
            }
        }

        cJSON *token_item = cJSON_GetObjectItem(websocket_obj, "token");
        if (token_item && cJSON_IsString(token_item))
        {
            size_t token_len = strlen(token_item->valuestring) + 1;
            websocket->token = (char *)audio_mem_malloc((uint32_t)token_len);
            if (websocket->token)
            {
                strncpy(websocket->token, token_item->valuestring, token_len);
                rt_kprintf("Websocket Token: %s\n", websocket->token);
                has_ws_token = RT_TRUE;
            }
        }
    }

    // 解析 activation 部分（可能不存在）
    activation_obj = cJSON_GetObjectItem(root, "activation");
    if (activation_obj && cJSON_IsObject(activation_obj))
    {
        cJSON *code_item = cJSON_GetObjectItem(activation_obj, "code");
        if (code_item && cJSON_IsString(code_item))
        {
            strncpy(active->code, code_item->valuestring,
                    sizeof(active->code) - 1);
            active->is_activated = true;
            rt_kprintf("Activation code: %s\n", active->code);
        }
    }
    else
    {
        rt_kprintf("No activation section found, device is activated\n");
        active->is_activated = false;
    }

    if (!has_ws_token)
    {
        if (server_message != RT_NULL && server_message_size > 0U &&
            server_message[0] == '\0')
        {
            rt_snprintf(server_message, server_message_size,
                        "小智服务器未返回 websocket token");
        }
        cJSON_Delete(root);
        return -RT_ERROR;
    }

    cJSON_Delete(root);
    return RT_EOK;
}

static int xiaozhi_prepare_session(rt_bool_t interactive)
{
    char *my_ota_version;
    uint32_t retry = 10;
    uint32_t network_retry = 10;
    char last_error[160] = {0};

    if (g_activation_context.sem == RT_NULL)
    {
        g_activation_context.sem =
            rt_sem_create("activation_sem", 0, RT_IPC_FLAG_FIFO);
    }

    while (network_retry-- > 0 && !xiaozhi_network_service_ready())
    {
        if (interactive)
        {
            xiaozhi_ui_chat_status("网络检查中...");
            xiaozhi_ui_chat_output("等待网络就绪...");
        }
        rt_thread_mdelay(1000);
    }

    if (!xiaozhi_network_service_ready())
    {
        xz_report_prepare_error("请在手机上开启网络共享后重新发起连接",
                                interactive);
        return -RT_ERROR;
    }

    if (g_ota_verified)
    {
        rt_kprintf("OTA verification skipped, already verified\n");
        return RT_EOK;
    }

    while (retry-- > 0)
    {
        char server_message[160] = {0};
        int parse_result;

        if (interactive)
        {
            xiaozhi_ui_chat_output("正在网络准备...");
        }

        my_ota_version = get_xiaozhi();
        if (my_ota_version == RT_NULL)
        {
            const char *ota_error = get_xiaozhi_last_error();

            if (ota_error != RT_NULL && ota_error[0] != '\0')
            {
                rt_snprintf(last_error, sizeof(last_error), "%s", ota_error);
            }

            rt_kprintf("Waiting internet ready(%d)... \r\n", retry);
            if (interactive)
            {
                xiaozhi_ui_chat_status("等待网络...");
                xiaozhi_ui_chat_output(last_error[0] != '\0' ? last_error :
                                       "等待网络重新准备...");
                xiaozhi_ui_standby_chat_output(last_error[0] != '\0' ? last_error :
                                               "等待网络重新准备...");
            }
            rt_thread_mdelay(1000);
            continue;
        }

        rt_kprintf("my_ota_version = %s\n", my_ota_version);
        parse_result = parse_ota_response(my_ota_version,
                                          &g_activation_context,
                                          &g_websocket_context,
                                          server_message,
                                          sizeof(server_message));

        if (parse_result != RT_EOK)
        {
            xz_report_prepare_error(server_message, interactive);
            return -RT_ERROR;
        }

        if (g_activation_context.is_activated)
        {
            if (xz_wait_for_activation_completion(interactive,
                                                  last_error,
                                                  sizeof(last_error)) != RT_EOK)
            {
                xz_report_prepare_error(last_error, interactive);
                return -RT_ERROR;
            }
        }

        g_ota_verified = true;
        return RT_EOK;
    }

    xz_report_prepare_error(last_error[0] != '\0' ? last_error :
                            "请检查网络连接后重试",
                            interactive);
    return -RT_ERROR;
}

void xiaozhi2(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (xiaozhi_prepare_session(RT_TRUE) != RT_EOK)
    {
        return;
    }

    (void)xiaozhi_ws_connect_internal(RT_TRUE);
}
MSH_CMD_EXPORT(xiaozhi2, Get Xiaozhi)

/* WebSocket连接管理函数 */
int xz_websocket_connect(void)
{
    if (g_xz_ws.is_connected) {
        return 0; /* 已连接 */
    }

    if (xiaozhi_prepare_session(RT_FALSE) != RT_EOK)
    {
        return -1;
    }
    
    if (xiaozhi_ws_connect_internal(RT_FALSE) != RT_EOK)
    {
        return -1;
    }
    
    /* 等待连接建立 */
    int retry = 50;
    while (retry-- > 0 && !g_xz_ws.is_connected) {
        rt_thread_mdelay(100);
    }
    
    return g_xz_ws.is_connected ? 0 : -1;
}

rt_bool_t xz_websocket_is_connected(void)
{
    return g_xz_ws.is_connected ? RT_TRUE : RT_FALSE;
}

void xz_websocket_disconnect(void)
{
    if (!g_xz_ws.is_connected) {
        return;
    }
    
    /* 关闭WebSocket连接 */
    wsock_close(&g_xz_ws.clnt, WSOCK_RESULT_LOCAL_ABORT, ERR_OK);
    g_xz_ws.is_connected = 0;
    memset(g_xz_ws.session_id, 0, sizeof(g_xz_ws.session_id));
    xz_websocket_end_barge_in_drop();
}

const char* xz_websocket_get_session_id(void)
{
    if (!g_xz_ws.is_connected || g_xz_ws.session_id[0] == '\0') {
        return NULL;
    }
    return (const char*)g_xz_ws.session_id;
}

/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/
