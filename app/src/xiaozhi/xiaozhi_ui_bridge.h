/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef XIAOZHI_UI_BRIDGE_H
#define XIAOZHI_UI_BRIDGE_H

#include "xiaozhi_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化UI桥接层
 */
void xiaozhi_ui_bridge_init(void);

/**
 * @brief 显示对话状态
 * @param text 状态文本
 */
void xiaozhi_ui_show_chat_status(const char* text);

/**
 * @brief 显示对话输出
 * @param text 对话内容
 */
void xiaozhi_ui_show_chat_output(const char* text);

/**
 * @brief 显示TTS输出
 * @param text TTS文本
 */
void xiaozhi_ui_show_tts_output(const char* text);

/**
 * @brief 更新表情
 * @param emoji 表情名称
 */
void xiaozhi_ui_update_emoji(const char* emoji);

/**
 * @brief 显示错误信息
 * @param error 错误文本
 */
void xiaozhi_ui_show_error(const char* error);

#ifdef __cplusplus
}
#endif

#endif /* XIAOZHI_UI_BRIDGE_H */
