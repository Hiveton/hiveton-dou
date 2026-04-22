/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "xiaozhi_service.h"
#include "rtthread.h"
#include "ulog.h"
#include <string.h>
#include "xiaozhi_websocket.h"
#include "xiaozhi_audio.h"
#include "kws/app_recorder_process.h"
#include "xiaozhi_client_public.h"
#include "network/net_manager.h"
#include "audio_manager.h"
#include "mem_section.h"

#define DBG_TAG "xz_svc"
#define DBG_LVL LOG_LVL_INFO

/* 线程配置 */
#define XZ_SERVICE_THREAD_STACK_SIZE (16 * 1024)
#define XZ_SERVICE_THREAD_PRIORITY 22
#define XZ_SERVICE_LOOP_INTERVAL_MS 500
#define XZ_SERVICE_RESPONSE_TIMEOUT_MS 5000
#define XZ_SERVICE_RECONNECT_INTERVAL_MS 3000
#define XZ_SERVICE_GREETING_RETRY_MS 1000
#define XZ_SERVICE_CONNECT_TIMEOUT_MS 12000
#define XZ_SERVICE_SESSION_READY_TIMEOUT_MS 5000

/* 全局状态 */
static volatile xz_service_state_t s_state = XZ_SERVICE_IDLE;
static volatile bool s_initialized = false;
static volatile bool s_kws_enabled = true;
static xz_service_ui_callbacks_t s_ui_cbs = {0};
static struct rt_thread s_service_thread;
#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(xz_service_thread_stack)
static uint8_t s_thread_stack[XZ_SERVICE_THREAD_STACK_SIZE];
L2_RET_BSS_SECT_END
#else
static uint8_t s_thread_stack[XZ_SERVICE_THREAD_STACK_SIZE] L2_RET_BSS_SECT(xz_service_thread_stack);
#endif

/* 事件定义 */
#define XZ_EVT_INIT 0x01
#define XZ_EVT_DEINIT 0x02
#define XZ_EVT_START_LISTEN 0x04
#define XZ_EVT_STOP_LISTEN 0x08
#define XZ_EVT_ABORT 0x10
#define XZ_EVT_KWS_TRIGGER 0x20
#define XZ_EVT_GREETING 0x40
#define XZ_EVT_RECONNECT 0x80

static struct rt_event s_event_obj;
static struct rt_mutex s_mutex_obj;
static rt_event_t s_event = NULL;
static rt_mutex_t s_mutex = NULL;
static volatile bool s_pending_greeting = false;
static volatile bool s_waiting_server_reply = false;
static volatile bool s_connect_in_progress = false;
static rt_tick_t s_waiting_reply_tick = 0;
static rt_tick_t s_connect_start_tick = 0;
static rt_tick_t s_last_reconnect_tick = 0;
static rt_tick_t s_last_greeting_retry_tick = 0;
static rt_tick_t s_last_connect_notice_tick = 0;
static rt_tick_t s_session_wait_start_tick = 0;
static bool s_reconnect_notice_sent = false;
static net_manager_service_state_t s_last_network_state = (net_manager_service_state_t)-1;
static bool s_last_network_ready = false;

static void set_state(xz_service_state_t new_state);
static void xiaozhi_service_try_pending_greeting(void);

static void clear_waiting_server_reply(void)
{
    s_waiting_server_reply = false;
    s_waiting_reply_tick = 0;
}

static void mark_waiting_server_reply(void)
{
    s_waiting_server_reply = true;
    s_waiting_reply_tick = rt_tick_get();
}

static void clear_connect_attempt_state(void)
{
    s_connect_in_progress = false;
    s_connect_start_tick = 0;
    s_session_wait_start_tick = 0;
}

static void reset_reconnect_state(void)
{
    s_last_reconnect_tick = 0;
    s_last_connect_notice_tick = 0;
    clear_connect_attempt_state();
    s_reconnect_notice_sent = false;
}

static void reset_greeting_retry_state(void)
{
    s_last_greeting_retry_tick = 0;
}

static void reset_network_notice_state(void)
{
    s_last_network_state = (net_manager_service_state_t)-1;
    s_last_network_ready = false;
}

static void xiaozhi_service_ensure_audio_ready(void)
{
    if (!net_manager_can_run_ai())
    {
        return;
    }

    if (audio_get_current_owner() != AUDIO_OWNER_XIAOZHI)
    {
        if (!audio_try_preempt(AUDIO_OWNER_XIAOZHI))
        {
            LOG_W("Failed to acquire audio for xiaozhi");
            return;
        }
    }

    if (!xz_audio_is_inited())
    {
        xz_audio_init();
    }
}

static void xiaozhi_service_release_audio_when_idle(void)
{
    if (s_state == XZ_SERVICE_READY &&
        audio_get_current_owner() == AUDIO_OWNER_XIAOZHI)
    {
        audio_release(AUDIO_OWNER_XIAOZHI);
    }
}

static const char *network_unavailable_text(net_manager_service_state_t state)
{
    switch (state)
    {
    case NET_MANAGER_SERVICE_OFFLINE:
        return "网络未就绪：4G未驻网或蓝牙网络未连接";
    case NET_MANAGER_SERVICE_RADIO_READY:
        return "4G正在搜网，等待注册到运营商";
    case NET_MANAGER_SERVICE_LINK_READY:
        return "网络链路已连接，等待DNS就绪";
    case NET_MANAGER_SERVICE_DNS_READY:
        return "DNS已就绪，等待互联网检测";
    case NET_MANAGER_SERVICE_INTERNET_READY:
        return "网络已连接，正在自动重连小智";
    default:
        return "网络状态未知，等待自动重连小智";
    }
}

static void xiaozhi_service_sync_network_notice(bool network_ready, bool force_notice)
{
    net_manager_service_state_t net_state = net_manager_get_service_state();
    bool state_changed = (s_last_network_state != net_state) ||
                         (s_last_network_ready != network_ready);
    bool recovered = (!s_last_network_ready) || force_notice;
    bool should_notice_recovered =
        ((!s_last_network_ready && s_reconnect_notice_sent) || force_notice);

    if (network_ready)
    {
        if (should_notice_recovered)
        {
            if (s_ui_cbs.on_chat_output)
            {
                s_ui_cbs.on_chat_output("网络已恢复，正在自动重连小智...");
            }
        }

        if (should_notice_recovered)
        {
            LOG_I("Network recovered, state=%d", (int)net_state);
        }

        if (recovered)
        {
            reset_reconnect_state();
        }
    }
    else
    {
        if (force_notice || state_changed)
        {
            const char *notice = network_unavailable_text(net_state);

            LOG_W("Network unavailable, waiting for auto reconnect (state=%d)",
                  (int)net_state);
            if (s_ui_cbs.on_error)
            {
                s_ui_cbs.on_error(notice);
            }
            if (s_ui_cbs.on_chat_output)
            {
                s_ui_cbs.on_chat_output(notice);
            }
        }

        s_reconnect_notice_sent = true;
    }

    s_last_network_state = net_state;
    s_last_network_ready = network_ready;
}

static void xiaozhi_service_notice_connecting(bool force_notice)
{
    rt_tick_t now = rt_tick_get();

    if (!force_notice &&
        s_last_connect_notice_tick != 0 &&
        (now - s_last_connect_notice_tick) <
            rt_tick_from_millisecond(XZ_SERVICE_RECONNECT_INTERVAL_MS))
    {
        return;
    }

    s_last_connect_notice_tick = now;
    if (s_ui_cbs.on_chat_output)
    {
        s_ui_cbs.on_chat_output("正在连接小智，请稍候...");
    }
}

static void xiaozhi_service_mark_connect_attempt(bool force_notice)
{
    s_connect_in_progress = true;
    s_connect_start_tick = rt_tick_get();
    s_session_wait_start_tick = 0;
    clear_waiting_server_reply();
    xiaozhi_service_notice_connecting(force_notice);
    set_state(XZ_SERVICE_INITING);
}

static void xiaozhi_service_finish_connect_attempt(bool success)
{
    clear_connect_attempt_state();
    clear_waiting_server_reply();
    if (!success)
    {
        s_last_reconnect_tick = rt_tick_get();
    }
}

static bool xiaozhi_service_connect_attempt_timed_out(rt_tick_t now)
{
    if (!s_connect_in_progress || s_connect_start_tick == 0)
    {
        return false;
    }

    return (now - s_connect_start_tick) >=
           rt_tick_from_millisecond(XZ_SERVICE_CONNECT_TIMEOUT_MS);
}

static void xiaozhi_service_fail_connect_attempt(const char *reason,
                                                 bool notify_error)
{
    if (reason != NULL)
    {
        LOG_W("%s", reason);
    }

    xiaozhi_service_finish_connect_attempt(false);

    if (notify_error && reason != NULL && s_ui_cbs.on_error)
    {
        s_ui_cbs.on_error(reason);
    }

    if (s_initialized &&
        s_state != XZ_SERVICE_IDLE &&
        s_state != XZ_SERVICE_CLOSING)
    {
        set_state(XZ_SERVICE_READY);
    }
}

static void xiaozhi_service_watchdog_tick(void)
{
    rt_tick_t now = rt_tick_get();
    bool network_ready;

    if (!s_initialized || s_state == XZ_SERVICE_IDLE || s_state == XZ_SERVICE_CLOSING)
    {
        reset_reconnect_state();
        return;
    }

    network_ready = net_manager_can_run_ai() ? true : false;
    xiaozhi_service_sync_network_notice(network_ready, false);
    if (!network_ready)
    {
        if (xz_websocket_is_connected())
        {
            xz_websocket_disconnect();
        }
        if (s_state == XZ_SERVICE_INITING || s_connect_in_progress)
        {
            xiaozhi_service_finish_connect_attempt(false);
            set_state(XZ_SERVICE_READY);
        }
        else
        {
            clear_waiting_server_reply();
        }
        return;
    }

    if (s_waiting_server_reply &&
        (now - s_waiting_reply_tick) >=
            rt_tick_from_millisecond(XZ_SERVICE_RESPONSE_TIMEOUT_MS))
    {
        LOG_W("Server response timeout");
        clear_waiting_server_reply();
        if (s_ui_cbs.on_error) {
            s_ui_cbs.on_error("小智超过 5 秒没有反馈，已结束等待");
        }
        set_state(XZ_SERVICE_READY);
    }

    if (xz_websocket_is_connected())
    {
        if (xz_websocket_get_session_id() != NULL)
        {
            xiaozhi_service_ensure_audio_ready();
            xiaozhi_service_sync_network_notice(true, false);
            reset_reconnect_state();
            clear_waiting_server_reply();
            if (s_state == XZ_SERVICE_INITING)
            {
                set_state(XZ_SERVICE_READY);
            }
            xiaozhi_service_try_pending_greeting();
            return;
        }

        if (!s_connect_in_progress)
        {
            s_connect_in_progress = true;
            s_connect_start_tick = now;
        }
        if (s_state != XZ_SERVICE_INITING)
        {
            set_state(XZ_SERVICE_INITING);
        }

        if (s_session_wait_start_tick == 0)
        {
            s_session_wait_start_tick = now;
            xiaozhi_service_notice_connecting(false);
            return;
        }

        if ((now - s_session_wait_start_tick) <
                rt_tick_from_millisecond(XZ_SERVICE_SESSION_READY_TIMEOUT_MS) &&
            !xiaozhi_service_connect_attempt_timed_out(now))
        {
            xiaozhi_service_notice_connecting(false);
            return;
        }

        LOG_W("WebSocket connected but session is not ready");
        xz_websocket_disconnect();
        xiaozhi_service_fail_connect_attempt("小智握手超时，将稍后自动重试", true);
        return;
    }

    if (s_connect_in_progress)
    {
        if (xiaozhi_service_connect_attempt_timed_out(now))
        {
            xz_websocket_disconnect();
            xiaozhi_service_fail_connect_attempt("小智连接超时，将稍后自动重试", true);
        }
        else
        {
            xiaozhi_service_notice_connecting(false);
        }
        return;
    }

    xiaozhi_service_notice_connecting(false);

    if ((s_last_reconnect_tick != 0) &&
        (now - s_last_reconnect_tick) <
            rt_tick_from_millisecond(XZ_SERVICE_RECONNECT_INTERVAL_MS))
    {
        return;
    }

    s_last_reconnect_tick = now;
    LOG_I("Attempting websocket auto reconnect");
    xiaozhi_service_mark_connect_attempt(false);

    if (xz_websocket_connect() == 0)
    {
        if (xz_websocket_get_session_id() != NULL)
        {
            LOG_I("Auto reconnect succeeded");
            reset_reconnect_state();
            clear_waiting_server_reply();
            set_state(XZ_SERVICE_READY);
            xiaozhi_service_try_pending_greeting();
            if (s_ui_cbs.on_chat_output) {
                s_ui_cbs.on_chat_output("小智已自动重连");
            }
        }
        else
        {
            LOG_I("Auto reconnect connected, waiting for session");
            s_session_wait_start_tick = rt_tick_get();
            xiaozhi_service_notice_connecting(false);
        }
    }
    else
    {
        xiaozhi_service_fail_connect_attempt("小智连接失败，将稍后自动重试", false);
    }
}

/* 设置状态 */
static void set_state(xz_service_state_t new_state)
{
    if (s_state != new_state) {
        xz_service_state_t old_state = s_state;

        s_state = new_state;
        LOG_I("State: %d -> %d", old_state, new_state);
        
        if (s_ui_cbs.on_state_change) {
            s_ui_cbs.on_state_change(new_state);
        }
        
        /* 触发UI表情更新 */
        if (s_ui_cbs.on_emoji_change) {
            switch (new_state) {
            case XZ_SERVICE_READY:
                s_ui_cbs.on_emoji_change("neutral");
                break;
            case XZ_SERVICE_LISTENING:
                s_ui_cbs.on_emoji_change("listening");
                break;
            case XZ_SERVICE_SPEAKING:
                s_ui_cbs.on_emoji_change("speaking");
                break;
            default:
                break;
            }
        }
    }

    xiaozhi_service_release_audio_when_idle();
}

/* 唤醒词触发回调 */
static void kws_trigger_callback(void)
{
    if (s_initialized && s_kws_enabled && s_state == XZ_SERVICE_READY) {
        LOG_I("KWS triggered");
        rt_event_send(s_event, XZ_EVT_KWS_TRIGGER);
    }
}

/* WebSocket状态回调 */
static void ws_state_callback(bool connected)
{
    if (connected) {
        LOG_I("WebSocket connected");
        clear_waiting_server_reply();
        if (xz_websocket_get_session_id() != NULL) {
            reset_reconnect_state();
            if (s_state == XZ_SERVICE_INITING) {
                set_state(XZ_SERVICE_READY);
            }
        } else {
            if (!s_connect_in_progress) {
                s_connect_in_progress = true;
                s_connect_start_tick = rt_tick_get();
            }
            if (s_state != XZ_SERVICE_INITING) {
                set_state(XZ_SERVICE_INITING);
            }
        }
    } else {
        LOG_W("WebSocket disconnected");
        xiaozhi_service_finish_connect_attempt(false);
        if (s_state != XZ_SERVICE_IDLE) {
            set_state(XZ_SERVICE_READY);
        }
    }
}

/* 对话输出回调 */
static void ws_chat_output_callback(const char* text)
{
    LOG_I("Chat: %s", text);
    if (s_ui_cbs.on_chat_output) {
        s_ui_cbs.on_chat_output(text);
    }
}

/* TTS输出回调 */
static void ws_tts_output_callback(const char* text)
{
    LOG_I("TTS: %s", text);
    if (s_ui_cbs.on_tts_output) {
        s_ui_cbs.on_tts_output(text);
    }
}

static void request_greeting_when_ready(void)
{
    if (s_event == NULL)
    {
        return;
    }

    s_pending_greeting = true;
    reset_greeting_retry_state();
    rt_event_send(s_event, XZ_EVT_GREETING);
}

static void xiaozhi_service_try_pending_greeting(void)
{
    rt_tick_t now;

    if (!s_pending_greeting || s_event == NULL)
    {
        return;
    }

    if (s_state != XZ_SERVICE_READY)
    {
        return;
    }

    if (xz_websocket_get_session_id() == NULL)
    {
        return;
    }

    now = rt_tick_get();
    if (s_last_greeting_retry_tick != 0 &&
        (now - s_last_greeting_retry_tick) <
            rt_tick_from_millisecond(XZ_SERVICE_GREETING_RETRY_MS))
    {
        return;
    }

    s_last_greeting_retry_tick = now;
    rt_event_send(s_event, XZ_EVT_GREETING);
}

/* 服务线程入口 */
static void xiaozhi_service_thread(void *parameter)
{
    rt_uint32_t evt;
    
    (void)parameter;
    
    LOG_I("Service thread started");
    
    while (1) {
        if (rt_event_recv(s_event, XZ_EVT_INIT | XZ_EVT_DEINIT | 
                         XZ_EVT_START_LISTEN | XZ_EVT_STOP_LISTEN | 
                         XZ_EVT_ABORT | XZ_EVT_KWS_TRIGGER |
                         XZ_EVT_GREETING | XZ_EVT_RECONNECT,
                         RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                         rt_tick_from_millisecond(XZ_SERVICE_LOOP_INTERVAL_MS),
                         &evt) == RT_EOK) {
            
            if (evt & XZ_EVT_INIT) {
                if (s_state != XZ_SERVICE_IDLE) {
                    LOG_W("Already initialized");
                    continue;
                }
                
                /* 抢占音频资源（小智最高优先级） */
                if (!audio_try_preempt(AUDIO_OWNER_XIAOZHI)) {
                    LOG_E("Failed to acquire audio");
                    if (s_ui_cbs.on_error) {
                        s_ui_cbs.on_error("音频资源被占用");
                    }
                    clear_connect_attempt_state();
                    s_initialized = false;
                    set_state(XZ_SERVICE_IDLE);
                    continue;
                }
                
                set_state(XZ_SERVICE_INITING);

                /* 检查网络
                 * 4G/蓝牙切换、PPP协商期间可能短暂不可用。
                 * 这里不要直接退出到 IDLE，否则后续网络 ready 不会自动续上。
                 */
                if (!net_manager_can_run_ai()) {
                    LOG_E("Network not available");
                    xiaozhi_service_sync_network_notice(false, true);
                    set_state(XZ_SERVICE_READY);
                    continue;
                }
                
                /* 初始化WebSocket */
                xiaozhi_service_mark_connect_attempt(true);
                if (xz_websocket_connect() != 0) {
                    LOG_E("WebSocket connect failed");
                    if (net_manager_can_run_ai()) {
                        xiaozhi_service_notice_connecting(true);
                    } else {
                        xiaozhi_service_sync_network_notice(false, true);
                    }
                    xiaozhi_service_fail_connect_attempt("小智连接失败，将稍后自动重试", false);
                    continue;
                }
                
                /* 唤醒词功能暂时禁用以节省内存 */
                LOG_I("KWS disabled for memory saving");
                
                if (xz_websocket_get_session_id() != NULL)
                {
                    xiaozhi_service_ensure_audio_ready();
                    clear_waiting_server_reply();
                    reset_reconnect_state();
                    xiaozhi_service_sync_network_notice(true, false);
                    set_state(XZ_SERVICE_READY);
                    xiaozhi_service_try_pending_greeting();
                    LOG_I("Service initialized");
                }
                else
                {
                    LOG_I("WebSocket connected, waiting for session");
                    s_session_wait_start_tick = rt_tick_get();
                    xiaozhi_service_notice_connecting(false);
                }
            }
            
            if (evt & XZ_EVT_DEINIT) {
                LOG_I("Deinitializing...");
                
                set_state(XZ_SERVICE_CLOSING);
                
                /* 停止唤醒词 */
                kws_demo_stop();
                
                /* 停止聆听 */
                xz_mic_close(NULL);
                
                /* 关闭WebSocket */
                xz_websocket_disconnect();
                
                /* 关闭音频 */
                xz_speaker_close(NULL);
                
                /* 释放音频资源 */
                if (audio_get_current_owner() == AUDIO_OWNER_XIAOZHI)
                {
                    audio_release(AUDIO_OWNER_XIAOZHI);
                }
                
                set_state(XZ_SERVICE_IDLE);
                s_initialized = false;
                s_pending_greeting = false;
                reset_greeting_retry_state();
                clear_waiting_server_reply();
                reset_reconnect_state();
                reset_network_notice_state();
                LOG_I("Service deinitialized");
            }
            
            if (evt & XZ_EVT_KWS_TRIGGER) {
                if (s_state == XZ_SERVICE_READY) {
                    LOG_I("Auto start listening (KWS)");
                    rt_event_send(s_event, XZ_EVT_START_LISTEN);
                }
            }
            
            if (evt & XZ_EVT_START_LISTEN) {
                if (s_state == XZ_SERVICE_READY || s_state == XZ_SERVICE_SPEAKING) {
                    const char *session_id = xz_websocket_get_session_id();

                    if (session_id == NULL) {
                        LOG_W("WebSocket session not ready");
                        if (s_ui_cbs.on_error) {
                            s_ui_cbs.on_error("连接尚未就绪，正在自动连接小智");
                        }
                        if (net_manager_can_run_ai()) {
                            xiaozhi_service_notice_connecting(true);
                        } else {
                            xiaozhi_service_sync_network_notice(false, true);
                        }
                        rt_event_send(s_event, XZ_EVT_RECONNECT);
                        set_state(XZ_SERVICE_READY);
                        continue;
                    }

                    LOG_I("Start listening");
                    xiaozhi_service_ensure_audio_ready();
                    
                    /* 中止当前播放 */
                    if (s_state == XZ_SERVICE_SPEAKING) {
                        LOG_I("Barge-in: abort current speaking before listening");
                        xz_websocket_begin_barge_in();
                        ws_send_speak_abort(NULL, NULL, 0);
                        xz_speaker_abort(NULL);
                        clear_waiting_server_reply();
                        if (s_ui_cbs.on_chat_output) {
                            s_ui_cbs.on_chat_output("已打断当前回答，开始重新聆听...");
                        }
                    }
                    
                    set_state(XZ_SERVICE_LISTENING);
                    xz_mic_open(NULL);
                    
                    /* 发送开始聆听消息到服务器 */
                    ws_send_listen_start(NULL, NULL, kListeningModeManualStop);
                }
            }
            
            if (evt & XZ_EVT_STOP_LISTEN) {
                if (s_state == XZ_SERVICE_LISTENING) {
                    const char *session_id = xz_websocket_get_session_id();

                    LOG_I("Stop listening");
                    xz_mic_stop_capture(NULL);
                    if (session_id == NULL) {
                        LOG_W("WebSocket session lost before listen stop");
                        xz_mic_close(NULL);
                        if (s_ui_cbs.on_error) {
                            s_ui_cbs.on_error("连接已断开，正在自动重连小智");
                        }
                        clear_waiting_server_reply();
                        rt_event_send(s_event, XZ_EVT_RECONNECT);
                        set_state(XZ_SERVICE_READY);
                        continue;
                    }
                    if (xz_mic_flush_pending(NULL, 300) != RT_EOK) {
                        LOG_W("Mic flush timeout before listen stop");
                    }
                    ws_send_listen_stop(NULL, NULL);
                    xz_mic_close(NULL);
                    mark_waiting_server_reply();
                }
            }
            
            if (evt & XZ_EVT_ABORT) {
                LOG_I("Abort speaking");
                xz_websocket_begin_barge_in();
                xz_speaker_abort(NULL);
                ws_send_speak_abort(NULL, NULL, 0);
                clear_waiting_server_reply();
                set_state(XZ_SERVICE_READY);
            }

            if (evt & XZ_EVT_GREETING) {
                if (!s_pending_greeting) {
                    continue;
                }

                if (s_state == XZ_SERVICE_INITING) {
                    continue;
                }

                if (s_state != XZ_SERVICE_READY) {
                    LOG_I("Greeting deferred, state=%d", s_state);
                    continue;
                }

                if (xz_websocket_get_session_id() == NULL) {
                    LOG_I("Greeting deferred, session not ready");
                    xiaozhi_service_watchdog_tick();
                    continue;
                }

                if (xz_websocket_send_detected("你好小智")) {
                    s_pending_greeting = false;
                    reset_greeting_retry_state();
                    mark_waiting_server_reply();
                    if (s_ui_cbs.on_chat_output) {
                        s_ui_cbs.on_chat_output("已主动问候小智，等待回应...");
                    }
                } else {
                    LOG_W("Greeting send failed");
                    if (s_ui_cbs.on_error) {
                        s_ui_cbs.on_error("主动问候发送失败");
                    }
                    xiaozhi_service_watchdog_tick();
                }
            }

            if (evt & XZ_EVT_RECONNECT) {
                xiaozhi_service_watchdog_tick();
            }
        }
        else
        {
            xiaozhi_service_watchdog_tick();
        }
    }
}

/* 初始化小智服务 */
int xiaozhi_service_init(void)
{
    rt_err_t result;
    
    if (s_initialized) {
        LOG_W("Already initialized");
        return 0;
    }
    
    /* 创建同步对象 */
    if (!s_event) {
        result = rt_event_init(&s_event_obj, "xz_svc_evt", RT_IPC_FLAG_FIFO);
        if (result != RT_EOK) {
            LOG_E("Failed to init event: %d", result);
            return result;
        }
        s_event = &s_event_obj;
    }
    
    if (!s_mutex) {
        result = rt_mutex_init(&s_mutex_obj, "xz_svc_mtx", RT_IPC_FLAG_FIFO);
        if (result != RT_EOK) {
            LOG_E("Failed to init mutex: %d", result);
            return result;
        }
        s_mutex = &s_mutex_obj;
    }
    
    /* 创建线程（首次） */
    if (s_service_thread.stat == RT_THREAD_INIT) {
        result = rt_thread_init(&s_service_thread, "xz_svc",
                               xiaozhi_service_thread, NULL,
                               s_thread_stack, XZ_SERVICE_THREAD_STACK_SIZE,
                               XZ_SERVICE_THREAD_PRIORITY, 10);
        if (result != RT_EOK) {
            LOG_E("Failed to init thread: %d", result);
            return result;
        }
        
        result = rt_thread_startup(&s_service_thread);
        if (result != RT_EOK) {
            LOG_E("Failed to start thread: %d", result);
            return result;
        }
    }
    
    s_initialized = true;
    
    /* 触发初始化 */
    rt_event_send(s_event, XZ_EVT_INIT);
    
    return 0;
}

/* 反初始化 */
void xiaozhi_service_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    rt_event_send(s_event, XZ_EVT_DEINIT);
    
    /* 等待状态变为IDLE */
    int timeout = 100; /* 10秒超时 */
    while (s_state != XZ_SERVICE_IDLE && timeout-- > 0) {
        rt_thread_mdelay(100);
    }
    
    if (s_state != XZ_SERVICE_IDLE) {
        LOG_E("Deinit timeout");
    }
    
    /* 清理回调 */
    memset(&s_ui_cbs, 0, sizeof(s_ui_cbs));
}

/* 开始聆听 */
int xiaozhi_service_start_listening(void)
{
    if (!s_initialized || !s_event) {
        return -RT_ERROR;
    }
    
    rt_event_send(s_event, XZ_EVT_START_LISTEN);
    return 0;
}

/* 停止聆听 */
void xiaozhi_service_stop_listening(void)
{
    if (!s_initialized || !s_event) {
        return;
    }
    
    rt_event_send(s_event, XZ_EVT_STOP_LISTEN);
}

/* 中止对话 */
void xiaozhi_service_abort_speaking(void)
{
    if (!s_initialized || !s_event) {
        return;
    }
    
    rt_event_send(s_event, XZ_EVT_ABORT);
}

void xiaozhi_service_request_greeting(void)
{
    if (!s_initialized) {
        return;
    }

    if (!xz_websocket_get_session_id()) {
        if (net_manager_can_run_ai()) {
            xiaozhi_service_notice_connecting(true);
        } else {
            xiaozhi_service_sync_network_notice(false, true);
        }
    }

    request_greeting_when_ready();
}

/* 获取状态 */
xz_service_state_t xiaozhi_service_get_state(void)
{
    return s_state;
}

/* 检查是否运行 */
bool xiaozhi_service_is_running(void)
{
    return s_initialized && s_state != XZ_SERVICE_IDLE;
}

/* 注册UI回调 */
void xiaozhi_service_register_ui_callbacks(const xz_service_ui_callbacks_t* callbacks)
{
    if (s_mutex) {
        rt_mutex_take(s_mutex, RT_WAITING_FOREVER);
    }
    
    if (callbacks) {
        memcpy(&s_ui_cbs, callbacks, sizeof(xz_service_ui_callbacks_t));
    } else {
        memset(&s_ui_cbs, 0, sizeof(s_ui_cbs));
    }
    
    if (s_mutex) {
        rt_mutex_release(s_mutex);
    }
}

/* 设置KWS使能 */
void xiaozhi_service_set_kws_enable(bool enable)
{
    s_kws_enabled = enable;
    
    if (s_initialized) {
        if (enable && s_state == XZ_SERVICE_READY) {
            kws_demo();
        } else {
            kws_demo_stop();
        }
    }
}

/* 获取KWS使能 */
bool xiaozhi_service_get_kws_enable(void)
{
    return s_kws_enabled;
}

/* 获取会话ID */
const char* xiaozhi_service_get_session_id(void)
{
    if (!s_initialized || s_state == XZ_SERVICE_IDLE) {
        return NULL;
    }
    
    return xz_websocket_get_session_id();
}

void xiaozhi_service_notify_state(xz_service_state_t state)
{
    if (!s_initialized || state == XZ_SERVICE_IDLE) {
        return;
    }

    if (state == XZ_SERVICE_INITING) {
        if (!s_connect_in_progress) {
            s_connect_in_progress = true;
            s_connect_start_tick = rt_tick_get();
            s_session_wait_start_tick = 0;
        }
        xiaozhi_service_notice_connecting(false);
    }

    if (state == XZ_SERVICE_READY || state == XZ_SERVICE_SPEAKING) {
        clear_waiting_server_reply();
    }

    if (state == XZ_SERVICE_READY &&
        xz_websocket_get_session_id() != NULL)
    {
        xiaozhi_service_ensure_audio_ready();
    }

    if (state == XZ_SERVICE_READY &&
        s_connect_in_progress &&
        xz_websocket_is_connected() &&
        xz_websocket_get_session_id() == NULL) {
        LOG_I("Keep service connecting until session is ready");
        state = XZ_SERVICE_INITING;
    }
    else if (state == XZ_SERVICE_READY && s_connect_in_progress) {
        xiaozhi_service_finish_connect_attempt(
            xz_websocket_get_session_id() != NULL ? true : false);
    }

    set_state(state);

    if (state == XZ_SERVICE_READY && s_pending_greeting) {
        rt_event_send(s_event, XZ_EVT_GREETING);
    }
}

void xiaozhi_service_notify_chat_output(const char* text)
{
    if (!s_initialized || text == NULL) {
        return;
    }

    clear_waiting_server_reply();
    ws_chat_output_callback(text);
}

void xiaozhi_service_notify_tts_output(const char* text)
{
    if (!s_initialized || text == NULL) {
        return;
    }

    clear_waiting_server_reply();
    ws_tts_output_callback(text);
}

void xiaozhi_service_notify_emoji(const char* emoji_name)
{
    if (!s_initialized || emoji_name == NULL) {
        return;
    }

    if (s_ui_cbs.on_emoji_change) {
        s_ui_cbs.on_emoji_change(emoji_name);
    }
}

void xiaozhi_service_notify_error(const char* error_msg)
{
    if (!s_initialized || error_msg == NULL) {
        return;
    }

    if (s_state == XZ_SERVICE_INITING || s_connect_in_progress) {
        xiaozhi_service_finish_connect_attempt(false);
        set_state(XZ_SERVICE_READY);
    }

    if (s_ui_cbs.on_error) {
        s_ui_cbs.on_error(error_msg);
    }
}
