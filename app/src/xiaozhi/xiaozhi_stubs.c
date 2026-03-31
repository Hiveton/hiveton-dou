/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rtthread.h"
#include "bts2_type.h"
#include "ui/ui_dispatch.h"
#include "xiaozhi_service.h"

rt_tick_t last_listen_tick = 0;
uint8_t Initiate_disconnection_flag = 0;
BOOL first_pan_connected = FALSE;

/* UI占位符函数
 * 当前项目的小智页面通过 xiaozhi_service 的 UI 回调刷新，
 * 这里把旧接口桥接到服务层，避免 activation 等提示被空实现吞掉。 */
void xiaozhi_ui_chat_status(const char* text)
{
    (void)text;
}

void xiaozhi_ui_chat_output(const char* text)
{
    if (text != RT_NULL && text[0] != '\0')
    {
        xiaozhi_service_notify_chat_output(text);
    }
}

void xiaozhi_ui_standby_chat_output(const char* text)
{
    if (text != RT_NULL && text[0] != '\0')
    {
        xiaozhi_service_notify_chat_output(text);
    }
}

void xiaozhi_ui_update_emoji(const char* emoji)
{
    if (emoji != RT_NULL && emoji[0] != '\0')
    {
        xiaozhi_service_notify_emoji(emoji);
    }
}

void xiaozhi_ui_update_standby_emoji(const char* emoji)
{
    if (emoji != RT_NULL && emoji[0] != '\0')
    {
        xiaozhi_service_notify_emoji(emoji);
    }
}

void xiaozhi_ui_tts_output(const char* text)
{
    if (text != RT_NULL && text[0] != '\0')
    {
        xiaozhi_service_notify_tts_output(text);
    }
}
void xiaozhi_ui_reinit_audio(void) {}
void xiaozhi_ui_update_confirm_button_event(int event) { (void)event; }
void xiaozhi_ui_update_ble(const char* status) { (void)status; }
void xiaozhi_ui_update_latest_version(char *version)
{
    if (version != RT_NULL)
    {
        rt_kprintf("xz: latest version available %s\n", version);
    }
}

/* 屏幕切换占位符 */
void ui_switch_to_xiaozhi_screen(void)
{
    ui_dispatch_request_screen_switch(UI_SCREEN_AI_DOU);
}
void ui_swith_to_standby_screen(void)
{
    ui_dispatch_request_screen_switch(UI_SCREEN_HOME);
}
void gui_pm_fsm(void) {}

/* 屏幕对象占位符 */
void* standby_screen = NULL;
void* main_container = NULL;
