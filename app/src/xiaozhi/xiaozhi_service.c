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

static rt_event_t s_event = NULL;
static rt_mutex_t s_mutex = NULL;
static volatile bool s_pending_greeting = false;
static volatile bool s_waiting_server_reply = false;
static rt_tick_t s_waiting_reply_tick = 0;
static rt_tick_t s_last_reconnect_tick = 0;
static bool s_reconnect_notice_sent = false;

static void set_state(xz_service_state_t new_state);

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

static void reset_reconnect_state(void)
{
    s_last_reconnect_tick = 0;
    s_reconnect_notice_sent = false;
}

static void xiaozhi_service_watchdog_tick(void)
{
    rt_tick_t now = rt_tick_get();

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

    if (!s_initialized || s_state == XZ_SERVICE_IDLE || s_state == XZ_SERVICE_CLOSING)
    {
        reset_reconnect_state();
        return;
    }

    if (xz_websocket_is_connected())
    {
        reset_reconnect_state();
        return;
    }

    if (check_internet_access() != 1)
    {
        if (!s_reconnect_notice_sent)
        {
            LOG_W("Network unavailable, waiting for auto reconnect");
            if (s_ui_cbs.on_error) {
                s_ui_cbs.on_error("网络已断开，等待自动重连");
            }
            s_reconnect_notice_sent = true;
        }
        return;
    }

    if ((s_last_reconnect_tick != 0) &&
        (now - s_last_reconnect_tick) <
            rt_tick_from_millisecond(XZ_SERVICE_RECONNECT_INTERVAL_MS))
    {
        return;
    }

    s_last_reconnect_tick = now;
    LOG_I("Attempting websocket auto reconnect");
    if (s_ui_cbs.on_chat_output) {
        s_ui_cbs.on_chat_output("网络已恢复，正在自动重连小智...");
    }
    set_state(XZ_SERVICE_INITING);

    if (xz_websocket_connect() == 0)
    {
        LOG_I("Auto reconnect succeeded");
        reset_reconnect_state();
        clear_waiting_server_reply();
        set_state(XZ_SERVICE_READY);
        if (s_ui_cbs.on_chat_output) {
            s_ui_cbs.on_chat_output("小智已自动重连");
        }
    }
    else
    {
        LOG_W("Auto reconnect failed");
        set_state(XZ_SERVICE_READY);
    }
}

/* 设置状态 */
static void set_state(xz_service_state_t new_state)
{
    if (s_state != new_state) {
        s_state = new_state;
        LOG_I("State: %d -> %d", s_state, new_state);
        
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
        reset_reconnect_state();
        if (s_state == XZ_SERVICE_INITING) {
            set_state(XZ_SERVICE_READY);
        }
    } else {
        LOG_W("WebSocket disconnected");
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
                         XZ_EVT_GREETING,
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
                    s_initialized = false;
                    set_state(XZ_SERVICE_IDLE);
                    continue;
                }
                
                set_state(XZ_SERVICE_INITING);
                
                /* 检查网络 */
                if (check_internet_access() != 1) {
                    LOG_E("Network not available");
                    audio_release(AUDIO_OWNER_XIAOZHI);
                    if (s_ui_cbs.on_error) {
                        s_ui_cbs.on_error("网络未连接");
                    }
                    s_initialized = false;
                    set_state(XZ_SERVICE_IDLE);
                    continue;
                }
                
                /* 初始化音频 */
                xz_ws_audio_init();
                
                /* 初始化WebSocket */
                if (xz_websocket_connect() != 0) {
                    LOG_E("WebSocket connect failed");
                    audio_release(AUDIO_OWNER_XIAOZHI);
                    s_initialized = false;
                    set_state(XZ_SERVICE_IDLE);
                    if (s_ui_cbs.on_error) {
                        s_ui_cbs.on_error("服务器连接失败");
                    }
                    continue;
                }
                
                /* 唤醒词功能暂时禁用以节省内存 */
                LOG_I("KWS disabled for memory saving");
                
                clear_waiting_server_reply();
                reset_reconnect_state();
                set_state(XZ_SERVICE_READY);
                LOG_I("Service initialized");
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
                audio_release(AUDIO_OWNER_XIAOZHI);
                
                set_state(XZ_SERVICE_IDLE);
                s_initialized = false;
                s_pending_greeting = false;
                clear_waiting_server_reply();
                reset_reconnect_state();
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
                            s_ui_cbs.on_error("连接尚未就绪，请稍后再试");
                        }
                        set_state(XZ_SERVICE_READY);
                        continue;
                    }

                    LOG_I("Start listening");
                    
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
                            s_ui_cbs.on_error("连接已断开，请重新开始");
                        }
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
                    continue;
                }

                if (xz_websocket_send_detected("你好小智")) {
                    s_pending_greeting = false;
                    mark_waiting_server_reply();
                    if (s_ui_cbs.on_chat_output) {
                        s_ui_cbs.on_chat_output("已主动问候小智，等待回应...");
                    }
                } else {
                    LOG_W("Greeting send failed");
                    if (s_ui_cbs.on_error) {
                        s_ui_cbs.on_error("主动问候发送失败");
                    }
                }
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
        s_event = rt_event_create("xz_svc_evt", RT_IPC_FLAG_FIFO);
        if (!s_event) {
            LOG_E("Failed to create event");
            return -RT_ENOMEM;
        }
    }
    
    if (!s_mutex) {
        s_mutex = rt_mutex_create("xz_svc_mtx", RT_IPC_FLAG_FIFO);
        if (!s_mutex) {
            LOG_E("Failed to create mutex");
            return -RT_ENOMEM;
        }
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

    if (state == XZ_SERVICE_READY || state == XZ_SERVICE_SPEAKING) {
        clear_waiting_server_reply();
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

    if (s_ui_cbs.on_error) {
        s_ui_cbs.on_error(error_msg);
    }
}
