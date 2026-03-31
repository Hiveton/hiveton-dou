/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef XIAOZHI_SERVICE_H
#define XIAOZHI_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 小智服务状态
 */
typedef enum {
    XZ_SERVICE_IDLE = 0,        /* 空闲（未初始化） */
    XZ_SERVICE_INITING,         /* 初始化中 */
    XZ_SERVICE_READY,           /* 就绪（等待唤醒或按钮） */
    XZ_SERVICE_LISTENING,       /* 聆听中 */
    XZ_SERVICE_SPEAKING,        /* 播放中 */
    XZ_SERVICE_CLOSING          /* 关闭中 */
} xz_service_state_t;

/**
 * @brief UI回调函数表
 */
typedef struct {
    void (*on_state_change)(xz_service_state_t state);
    void (*on_chat_output)(const char* text);
    void (*on_tts_output)(const char* text);
    void (*on_emoji_change)(const char* emoji_name);
    void (*on_error)(const char* error_msg);
} xz_service_ui_callbacks_t;

/**
 * @brief 初始化小智后台服务
 * @return 0 成功，非0 失败
 * @note 建议在系统启动阶段调用一次，之后后台常驻运行
 */
int xiaozhi_service_init(void);

/**
 * @brief 反初始化小智服务（完全释放）
 * @note 常驻后台模式下一般不需要调用，仅保留给调试或特殊场景
 */
void xiaozhi_service_deinit(void);

/**
 * @brief 开始聆听（手动触发）
 * @return 0 成功
 */
int xiaozhi_service_start_listening(void);

/**
 * @brief 停止聆听
 */
void xiaozhi_service_stop_listening(void);

/**
 * @brief 中止当前对话
 */
void xiaozhi_service_abort_speaking(void);

/**
 * @brief 请求一次主动问候
 * @note 常用于进入 AI 小豆页面后，自动向小智发起一次唤醒
 */
void xiaozhi_service_request_greeting(void);

/**
 * @brief 获取当前服务状态
 */
xz_service_state_t xiaozhi_service_get_state(void);

/**
 * @brief 检查服务是否运行中
 */
bool xiaozhi_service_is_running(void);

/**
 * @brief 注册UI回调
 * @param callbacks 回调函数表，NULL表示取消注册
 */
void xiaozhi_service_register_ui_callbacks(const xz_service_ui_callbacks_t* callbacks);

/**
 * @brief 设置唤醒词使能
 * @param enable true 启用，false 禁用
 */
void xiaozhi_service_set_kws_enable(bool enable);

/**
 * @brief 检查唤醒词是否使能
 */
bool xiaozhi_service_get_kws_enable(void);

/**
 * @brief 获取当前会话ID
 * @return 会话ID字符串，无会话返回NULL
 */
const char* xiaozhi_service_get_session_id(void);

/**
 * @brief WebSocket/MQTT 层向服务层回灌实时状态
 */
void xiaozhi_service_notify_state(xz_service_state_t state);
void xiaozhi_service_notify_chat_output(const char* text);
void xiaozhi_service_notify_tts_output(const char* text);
void xiaozhi_service_notify_emoji(const char* emoji_name);
void xiaozhi_service_notify_error(const char* error_msg);

#ifdef __cplusplus
}
#endif

#endif /* XIAOZHI_SERVICE_H */
