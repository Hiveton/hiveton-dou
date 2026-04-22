/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include "lwip/api.h"
#include "lwip/dns.h"
#include "lwip/apps/websocket_client.h"
#include "lwip/apps/mqtt_priv.h"
#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"
#include "bf0_hal.h"
#include "bts2_global.h"
#include "bts2_app_pan.h"
#include "bts2_app_inc.h"
#include "ble_connection_manager.h"
#include "bt_connection_manager.h"
#include "xiaozhi_mqtt.h"
#include "xiaozhi_client_public.h"
#ifdef LWIP_ALTCP_TLS
    #include <lwip/altcp_tls.h>
#endif

#include <webclient.h>
#include <cJSON.h>
#include "audio_mem.h"
#include "bt_env.h"
#include "./iot/iot_c_api.h"
#include "./mcp/mcp_api.h"
#include "xiaozhi_ui.h"



xiaozhi_context_t g_xz_context;

#ifdef XIAOZHI_USING_MQTT
enum DeviceState mqtt_g_state;

#ifndef XIAOZHI_MQTT_DEBUG_LOG
#define XIAOZHI_MQTT_DEBUG_LOG 0
#endif

#define XZ_MQTT_LOG(...)                                                       \
    do                                                                         \
    {                                                                          \
        if (XIAOZHI_MQTT_DEBUG_LOG)                                            \
        {                                                                      \
            rt_kprintf(__VA_ARGS__);                                           \
        }                                                                      \
    } while (0)

static bool g_mqtt_drop_incoming_payload = false;
static char message[256];
static const char *hello_message =
    "{"
    "\"type\":\"hello\","
    "\"version\": 3,"
    "\"features\":{\"mcp\":true},"
    "\"transport\":\"udp\","
    "\"audio_params\":{"
    "\"format\":\"opus\", \"sample_rate\":16000, \"channels\":1, "
    "\"frame_duration\":60"
    "}}";

static const char *mode_str[] = {"auto", "manual", "realtime"};

static rt_tick_t g_speaking_start_tick = 0;  // 讲话开始时间
static rt_tick_t g_total_speaking_time = 0;  // 累计讲话时间
static bool g_is_speaking = false;           // 是否正在讲话
#define SPEAKING_THRESHOLD_MS (5 * 60 * 1000) // 5分钟阈值 (毫秒)

static void xz_mqtt_release_rx_buf(xiaozhi_context_t *ctx, xz_topic_buf_t *buf)
{
    if (!ctx || !buf)
    {
        return;
    }

    if (buf->buf)
    {
        audio_mem_free(buf->buf);
    }

    buf->buf = NULL;
    buf->total_len = 0;
    buf->used_len = 0;
    ctx->topic_buf_pool.rd_idx = (ctx->topic_buf_pool.rd_idx + 1) & 1;
}

static void xz_mqtt_drop_current_publish(const char *reason, u32_t total_len)
{
    g_mqtt_drop_incoming_payload = (total_len > 0);
    rt_kprintf("MQTT drop incoming publish: %s, len=%u\n", reason,
               (unsigned int)total_len);
}

static void xz_mqtt_disconnect_for_bad_payload(xiaozhi_context_t *ctx,
                                                const char *reason)
{
    rt_kprintf("MQTT disconnect malformed incoming payload: %s\n", reason);
    mqtt_g_state = kDeviceStateUnknown;
    if (ctx && mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_disconnect(&(ctx->clnt));
    }
}

static const char *xz_json_get_string(cJSON *obj, const char *name)
{
    if (!obj || !name)
    {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!item || !cJSON_IsString(item) || !item->valuestring)
    {
        return NULL;
    }

    return item->valuestring;
}

static bool xz_json_get_int(cJSON *obj, const char *name, int *value)
{
    if (!obj || !name || !value)
    {
        return false;
    }

    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!item)
    {
        return false;
    }

    if (cJSON_IsString(item) && item->valuestring)
    {
        *value = atoi(item->valuestring);
    }
    else if (cJSON_IsNumber(item))
    {
        *value = item->valueint;
    }
    else
    {
        return false;
    }

    return true;
}

static void xz_mqtt_handle_mcp_payload(cJSON *root)
{
    cJSON *payload = cJSON_GetObjectItem(root, "payload");
    if (!payload || !cJSON_IsObject(payload))
    {
        rt_kprintf("MQTT mcp payload missing object payload\n");
        return;
    }

    char *payload_json = cJSON_PrintUnformatted(payload);
    if (!payload_json)
    {
        rt_kprintf("MQTT mcp payload serialization failed\n");
        return;
    }

    McpServer_ParseMessage(payload_json);
    cJSON_free(payload_json);
}

void my_mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);
void my_mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len,
                              u8_t flags);
void my_mqtt_connection_cb(mqtt_client_t *client, void *arg,
                           mqtt_connection_status_t status)
{
    xiaozhi_context_t *ctx = (xiaozhi_context_t *)arg;
    rt_kprintf("my_mqtt_connection_cb:%d\n", status);
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        mqtt_set_inpub_callback(&(ctx->clnt), my_mqtt_incoming_publish_cb,
                                my_mqtt_incoming_data_cb, ctx);
        rt_sem_release(ctx->sem);
    }
    else
    {
        mqtt_g_state = kDeviceStateFatalError;
        rt_kprintf("MQTT connection failed, status: %d\n", status);
        // TODO: Reset MQTT parameters.
    }
}

static void mqtt_found_callback(const char *name, const ip_addr_t *ipaddr,
                                void *callback_arg)
{
    if (ipaddr != NULL)
    {
        xiaozhi_context_t *ctx = (xiaozhi_context_t *)callback_arg;
        rt_kprintf("DNS lookup succeeded, IP: %s\n", ipaddr_ntoa(ipaddr));
        memcpy(&(ctx->mqtt_addr), ipaddr, sizeof(ip_addr_t));
        rt_sem_release(ctx->sem);
    }
}

void my_mqtt_request_cb(void *arg, err_t err)
{
    xiaozhi_context_t *ctx = (xiaozhi_context_t *)arg;
    rt_kprintf("MQTT Request : %d\n", err);
}

void my_mqtt_request_cb2(void *arg, err_t err)
{
    xiaozhi_context_t *ctx = (xiaozhi_context_t *)arg;
    rt_kprintf("MQTT Request2 : %d\n", err);
    rt_sem_release(ctx->sem);
}

void my_mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    xiaozhi_context_t *ctx = (xiaozhi_context_t *)arg;
    xz_topic_buf_pool_t *topic_buf_pool;
    xz_topic_buf_t *buf;

    XZ_MQTT_LOG("MQTT incoming topic len: %u\n", (unsigned int)tot_len);

    topic_buf_pool = &ctx->topic_buf_pool;
    buf = &topic_buf_pool->buf[topic_buf_pool->wr_idx];
    if (buf->buf)
    {
        /* pool full */
        xz_mqtt_drop_current_publish("topic buffer full", tot_len);
        return;
    }

    if (tot_len == 0 || tot_len == (u32_t)-1)
    {
        xz_mqtt_drop_current_publish("invalid payload length", tot_len);
        return;
    }

    /* allocate buffer for incoming payload */
    buf->buf = audio_mem_malloc(tot_len + 1);
    if (!buf->buf)
    {
        buf->total_len = 0;
        buf->used_len = 0;
        xz_mqtt_drop_current_publish("payload allocation failed", tot_len);
        return;
    }

    buf->total_len = tot_len;
    buf->used_len = 0;

    topic_buf_pool->wr_idx = (topic_buf_pool->wr_idx + 1) & 1;
}
extern rt_tick_t last_listen_tick; 
void my_mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len,
                              u8_t flags)
{
    xiaozhi_context_t *ctx = (xiaozhi_context_t *)arg;
    xz_topic_buf_pool_t *topic_buf_pool;
    xz_topic_buf_t *buf;

    XZ_MQTT_LOG("MQTT incoming pub data: len=%u flags=%x\n",
                (unsigned int)len, flags);
    if (g_mqtt_drop_incoming_payload)
    {
        if (flags)
        {
            g_mqtt_drop_incoming_payload = false;
        }
        return;
    }

    topic_buf_pool = &ctx->topic_buf_pool;
    buf = &topic_buf_pool->buf[topic_buf_pool->rd_idx];
    if (!buf->buf)
    {
        g_mqtt_drop_incoming_payload = !flags;
        xz_mqtt_disconnect_for_bad_payload(ctx, "data without payload buffer");
        return;
    }

    if (len > buf->total_len || buf->used_len > buf->total_len - len)
    {
        g_mqtt_drop_incoming_payload = !flags;
        xz_mqtt_release_rx_buf(ctx, buf);
        xz_mqtt_disconnect_for_bad_payload(ctx, "fragment length overflow");
        return;
    }
    memcpy(buf->buf + buf->used_len, data, len);
    buf->used_len += len;
    if (!flags)
    {
        /* wait for last fragment */
        return;
    }

    if (buf->used_len != buf->total_len)
    {
        xz_mqtt_release_rx_buf(ctx, buf);
        xz_mqtt_disconnect_for_bad_payload(ctx, "fragment length mismatch");
        return;
    }

    buf->buf[buf->used_len] = '\0';
    cJSON *item = NULL;
    cJSON *root = NULL;
    root = cJSON_Parse(buf->buf); /*json_data 为MQTT的原始数据*/
    xz_mqtt_release_rx_buf(ctx, buf);
    if (!root)
    {
        rt_kprintf("MQTT incoming payload JSON parse failed\n");
        return;
    }

    char *type = (char *)xz_json_get_string(root, "type");
    if (!type)
    {
        rt_kprintf("MQTT incoming payload missing type\n");
        cJSON_Delete(root);
        return;
    }
    XZ_MQTT_LOG("MQTT incoming payload type: %s\n", type);
    if (strcmp(type, "hello") == 0)
    {
        cJSON *udp = cJSON_GetObjectItem(root, "udp");
        char *server = (char *)xz_json_get_string(udp, "server");
        char *key = (char *)xz_json_get_string(udp, "key");
        char *nonce = (char *)xz_json_get_string(udp, "nonce");
        int port = 0;


        cJSON *audio_param = cJSON_GetObjectItem(root, "audio_params");
        int sample_rate = 0;
        int duration = 0;
        char *session_id = (char *)xz_json_get_string(root, "session_id");
        if (!server || !key || !nonce || !xz_json_get_int(udp, "port", &port) ||
                !xz_json_get_int(audio_param, "sample_rate", &sample_rate) ||
                !xz_json_get_int(audio_param, "duration", &duration) ||
                !session_id)
        {
            rt_kprintf("MQTT hello payload missing required fields\n");
            cJSON_Delete(root);
            return;
        }

        ip4addr_aton(server, &(ctx->udp_addr));
        ctx->port = port;
        hex2data(key, ctx->key, 16);
        hex2data(nonce, ctx->nonce, 16);
        ctx->sample_rate = sample_rate;
        ctx->frame_duration = duration;

        strncpy(ctx->session_id, session_id, 9);
        mqtt_g_state = kDeviceStateIdle;
        xz_audio_init();
        mqtt_listen_start(&g_xz_context, kListeningModeAlwaysOn);
        xiaozhi_ui_chat_output("小智 已连接!");
        xiaozhi_ui_update_ble("open");
        xiaozhi_ui_chat_status("待命中...");
        xiaozhi_ui_update_emoji("neutral");
    }
    else if (strcmp(type, "goodbye") == 0)
    {
        mqtt_g_state = kDeviceStateUnknown;

        xiaozhi_ui_chat_output("等待唤醒...");
        xiaozhi_ui_chat_status("睡眠中...");
        xiaozhi_ui_update_emoji("sleep");
    }
    else if (strcmp(type, "tts") == 0)
    {
        char *state = (char *)xz_json_get_string(root, "state");
        if (!state)
        {
            rt_kprintf("MQTT tts payload missing state\n");
            cJSON_Delete(root);
            return;
        }

        if (strcmp(state, "start") == 0)
        {
            if (mqtt_g_state == kDeviceStateIdle ||
                mqtt_g_state == kDeviceStateListening)
            {
                mqtt_g_state = kDeviceStateSpeaking;
                xz_speaker(1);
                xiaozhi_ui_chat_status("讲话中...");

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
                    xiaozhi_ui_reinit_audio();     // 重新初始化音频
                }
            }
            mqtt_g_state = kDeviceStateIdle;
            xz_speaker(0);
            xiaozhi_ui_chat_status("待命中...");
        }
        else if (strcmp(state, "sentence_start") == 0)
        {
            char *txt = (char *)xz_json_get_string(root, "text");
            if (!txt)
            {
                rt_kprintf("MQTT tts payload missing text\n");
                cJSON_Delete(root);
                return;
            }
            // rt_kputs(txt);
            xiaozhi_ui_tts_output(txt); // 使用专用函数处理 tts 输出
        }
    }
    else if (strcmp(type, "stt") == 0)
    {
        char *txt = (char *)xz_json_get_string(root, "text");
        if (!txt)
        {
            rt_kprintf("MQTT stt payload missing text\n");
            cJSON_Delete(root);
            return;
        }
        xiaozhi_ui_chat_output(txt);
        last_listen_tick = rt_tick_get();
        mqtt_g_state = kDeviceStateSpeaking;
        xz_speaker(1);
    }
    else if (strcmp(type, "llm") ==0) // {"type":"llm", "text": "😊", "emotion": "smile"}

    {
        char *emotion = (char *)xz_json_get_string(root, "emotion");
        if (!emotion)
        {
            rt_kprintf("MQTT llm payload missing emotion\n");
            cJSON_Delete(root);
            return;
        }
        XZ_MQTT_LOG("MQTT llm emotion: %s\n", emotion);
        xiaozhi_ui_update_emoji(emotion);
    }
    else if (strcmp(type, "mcp") == 0)
    {
        rt_kprintf("mcp command\n");
        xz_mqtt_handle_mcp_payload(root);
    }
    else
    {
    }
    cJSON_Delete(root); /*每次调用cJSON_Parse函数后，都要释放内存*/
}

void mqtt_hello(xiaozhi_context_t *ctx)
{
    XZ_MQTT_LOG("Publish topic len: %u\n",
                (unsigned int)strlen(ctx->publish_topic));
    XZ_MQTT_LOG("hello payload len: %u\n",
                (unsigned int)strlen(hello_message));
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, hello_message,
                     strlen(hello_message), 0, 0, my_mqtt_request_cb, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

void mqtt_listen_start(xiaozhi_context_t *ctx, int mode)
{
    rt_snprintf(message, 256,
                "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":"
                "\"start\",\"mode\":\"%s\"}",
                ctx->session_id, mode_str[mode]);
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, message, strlen(message),
                     0, 0, my_mqtt_request_cb2, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

void mqtt_listen_stop(xiaozhi_context_t *ctx)
{
    rt_snprintf(
        message, 256,
        "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
        ctx->session_id);
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, message, strlen(message),
                     0, 0, my_mqtt_request_cb2, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

void mqtt_speak_abort(xiaozhi_context_t *ctx, int reason)
{
    rt_snprintf(message, 256, "{\"session_id\":\"%s\",\"type\":\"abort\"",
                ctx->session_id);
    if (reason)
        strcat(message, ",\"reason\":\"wake_word_detected\"}");
    else
        strcat(message, "}");
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, message, strlen(message),
                     0, 0, my_mqtt_request_cb2, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

void mqtt_wake_word_detected(xiaozhi_context_t *ctx, char *wakeword)
{
    rt_snprintf(message, 256,
                "{\"session_id\":\"%s\",\"type\":\"listen\", "
                "\"state\":\"detected\",\"text\":\"%s\"",
                ctx->session_id, wakeword);
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, message, strlen(message),
                     0, 0, my_mqtt_request_cb2, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

void mqtt_iot_descriptor(xiaozhi_context_t *ctx, char *descriptors)
{
    rt_snprintf(
        message, 256,
        "{\"session_id\":\"%s\",\"type\":\"iot\", \"descriptor\":\"%s\"",
        ctx->session_id, descriptors);
    LOCK_TCPIP_CORE();
    if (mqtt_client_is_connected(&(ctx->clnt)))
    {
        mqtt_publish(&(ctx->clnt), ctx->publish_topic, message, strlen(message),
                     0, 0, my_mqtt_request_cb2, ctx);
    }
    else
    {
        xiaozhi_ui_chat_status("mqtt断开");
        xiaozhi_ui_chat_output("请重启连接");
    }
    UNLOCK_TCPIP_CORE();
}

mqtt_client_t *mqtt_xiaozhi(xiaozhi_context_t *ctx)
{
    mqtt_client_t *clnt = &ctx->clnt;
    struct mqtt_connect_client_info_t *info = &ctx->info;
    err_t err;

    info->client_id = ctx->client_id;
    info->client_user = ctx->username;
    info->client_pass = ctx->password;
    info->keep_alive = 90;
    LOCK_TCPIP_CORE();
    err = dns_gethostbyname(ctx->endpoint, &ctx->mqtt_addr, mqtt_found_callback,
                            ctx);
    UNLOCK_TCPIP_CORE();
    if (err == ERR_OK)
    {

        rt_kprintf("mqtt_xiaozhi: DNS lookup succeeded, IP: %s\n",
                   ipaddr_ntoa(&(ctx->mqtt_addr)));
        rt_sem_release(ctx->sem);
    }
    if (err != ERR_OK && err != ERR_INPROGRESS)
    {
        rt_kprintf("Coud not find %s, please check PAN connection\n",
                   ctx->endpoint);
        clnt = NULL;
    }
    else if (RT_EOK == rt_sem_take(ctx->sem, 5000))
    {
        mqtt_g_state = kDeviceStateConnecting;
        // TODO free config when finish
        info->tls_config = altcp_tls_create_config_client(NULL, 0);
        LOCK_TCPIP_CORE();
        mqtt_client_connect(&(ctx->clnt), &(ctx->mqtt_addr),
                            LWIP_IANA_PORT_SECURE_MQTT, my_mqtt_connection_cb,
                            ctx, &ctx->info);
        UNLOCK_TCPIP_CORE();
        if (RT_EOK == rt_sem_take(ctx->sem, 10000))
        {
            mqtt_g_state = kDeviceStateIdle;
            LOCK_TCPIP_CORE();
            // ctx->info.tls_config = altcp_tls_create_config_client(NULL, 0);
            UNLOCK_TCPIP_CORE();
            mqtt_hello(ctx);
        }
        else
        {
            rt_kprintf("timeout\n");
            xiaozhi_ui_chat_output("Xiaozhi 连接超时请重启!");
            clnt = NULL;
        }
    }
    else
        clnt = NULL;

    return clnt;
}

static char *mqtt_dup_json_string(cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(item) || item->valuestring == RT_NULL)
    {
        rt_kprintf("MQTT HTTP config missing string field: %s\n", key);
        return RT_NULL;
    }

    size_t len = strlen(item->valuestring);
    char *copy = (char *)rt_malloc(len + 1);
    if (copy == RT_NULL)
    {
        rt_kprintf("MQTT HTTP config alloc failed: %s len=%u\n", key, (unsigned int)len);
        return RT_NULL;
    }
    memcpy(copy, item->valuestring, len + 1);
    return copy;
}

static void mqtt_free_context_config(xiaozhi_context_t *ctx)
{
    if (ctx->endpoint) { rt_free(ctx->endpoint); ctx->endpoint = RT_NULL; }
    if (ctx->client_id) { rt_free(ctx->client_id); ctx->client_id = RT_NULL; }
    if (ctx->username) { rt_free(ctx->username); ctx->username = RT_NULL; }
    if (ctx->password) { rt_free(ctx->password); ctx->password = RT_NULL; }
    if (ctx->publish_topic) { rt_free(ctx->publish_topic); ctx->publish_topic = RT_NULL; }
}

int mqtt_http_xiaozhi_data_parse(char *json_data)
{
    if (json_data == RT_NULL)
    {
        return -1;
    }

    cJSON *root = NULL;

    root = cJSON_Parse(json_data); /*json_data 为MQTT的原始数据*/
    if (!root)
    {
        rt_kprintf("MQTT HTTP data JSON parse failed\n");
        return -1;
    }

    cJSON *Presult = cJSON_GetObjectItem(root, "mqtt"); /*mqtt的键值对为数组，*/
    if (!cJSON_IsObject(Presult))
    {
        rt_kprintf("MQTT HTTP data missing mqtt object\n");
        cJSON_Delete(root);
        return -1;
    }

    char *endpoint = mqtt_dup_json_string(Presult, "endpoint");
    char *client_id = mqtt_dup_json_string(Presult, "client_id");
    char *username = mqtt_dup_json_string(Presult, "username");
    char *password = mqtt_dup_json_string(Presult, "password");
    char *publish_topic = mqtt_dup_json_string(Presult, "publish_topic");

    if (endpoint == RT_NULL || client_id == RT_NULL || username == RT_NULL ||
        password == RT_NULL || publish_topic == RT_NULL)
    {
        if (endpoint) { rt_free(endpoint); }
        if (client_id) { rt_free(client_id); }
        if (username) { rt_free(username); }
        if (password) { rt_free(password); }
        if (publish_topic) { rt_free(publish_topic); }
        cJSON_Delete(root);
        return -1;
    }

    mqtt_free_context_config(&g_xz_context);
    g_xz_context.endpoint = endpoint;
    g_xz_context.client_id = client_id;
    g_xz_context.username = username;
    g_xz_context.password = password;
    g_xz_context.publish_topic = publish_topic;

    rt_kprintf("MQTT config loaded: endpoint=%s, client_id_len=%u, "
               "username_len=%u, password=<redacted>, publish_topic_len=%u\n",
               g_xz_context.endpoint,
               (unsigned int)strlen(g_xz_context.client_id),
               (unsigned int)strlen(g_xz_context.username),
               (unsigned int)strlen(g_xz_context.publish_topic));
    mqtt_xiaozhi(&g_xz_context);
    cJSON_Delete(root); /*每次调用cJSON_Parse函数后，都要释放内存*/
    return 0;
}

void xiaozhi(int argc, char **argv)
{
    char *my_ota_version;
    uint32_t retry = 10;

    while (retry-- > 0)
    {
        my_ota_version = get_xiaozhi();

        if (g_xz_context.info.tls_config)
        {
            LOCK_TCPIP_CORE();
            mqtt_disconnect(&(g_xz_context.clnt));
            UNLOCK_TCPIP_CORE();
            if (g_xz_context.info.tls_config)
            {
                LOCK_TCPIP_CORE();
                altcp_tls_free_config(g_xz_context.info.tls_config);
                UNLOCK_TCPIP_CORE();
                g_xz_context.info.tls_config = NULL;
            }
        }

        if (my_ota_version)
        {
            if (g_xz_context.sem == NULL)
                g_xz_context.sem = rt_sem_create("xz_sem", 0, RT_IPC_FLAG_FIFO);
            mqtt_http_xiaozhi_data_parse(my_ota_version);
            break;
        }
        else
        {
            rt_kprintf("Waiting internet ready(%d)... \r\n", retry);
            rt_thread_mdelay(1000);
        }
    }
}
MSH_CMD_EXPORT(xiaozhi, Get Xiaozhi)
#endif

/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/
