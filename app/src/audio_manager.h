/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 音频资源拥有者
 */
typedef enum {
    AUDIO_OWNER_NONE = 0,
    AUDIO_OWNER_XIAOZHI,       /* 小智AI对话 */
    AUDIO_OWNER_MUSIC,         /* 音乐播放 */
    AUDIO_OWNER_RECORDER,      /* 录音功能 */
} audio_owner_t;

/**
 * @brief 音频请求模式
 */
typedef enum {
    AUDIO_REQ_BLOCKING = 0,    /* 阻塞等待 */
    AUDIO_REQ_NONBLOCKING,     /* 非阻塞，立即返回 */
} audio_req_mode_t;

/**
 * @brief 初始化音频管理器
 * @return 0 成功
 */
int audio_manager_init(void);

/**
 * @brief 反初始化音频管理器
 */
void audio_manager_deinit(void);

/**
 * @brief 请求音频使用权
 * @param owner 请求者
 * @param mode 请求模式
 * @return true 成功获取，false 失败
 * @note 阻塞模式下会等待当前使用者释放
 */
bool audio_acquire(audio_owner_t owner, audio_req_mode_t mode);

/**
 * @brief 释放音频使用权
 * @param owner 释放者（必须与acquire时的owner一致）
 */
void audio_release(audio_owner_t owner);

/**
 * @brief 强制获取音频（中断当前使用）
 * @param owner 新的使用者
 * @return 之前的使用者
 */
audio_owner_t audio_force_acquire(audio_owner_t owner);

/**
 * @brief 获取当前音频拥有者
 */
audio_owner_t audio_get_current_owner(void);

/**
 * @brief 检查音频是否可用
 */
bool audio_is_available(void);

/**
 * @brief 注册状态变化回调
 * @param callback 回调函数，参数为(new_owner, old_owner)
 */
typedef void (*audio_state_callback_t)(audio_owner_t new_owner, audio_owner_t old_owner);
void audio_register_state_callback(audio_state_callback_t callback);

/**
 * @brief 优先级抢占（小智最高优先级）
 * @return true 抢占成功
 */
bool audio_try_preempt(audio_owner_t preemptor);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MANAGER_H */
