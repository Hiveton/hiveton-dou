/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_manager.h"
#include "rtthread.h"
#include "ulog.h"
#include "audio_server.h"

#define DBG_TAG "audio_mgr"
#define DBG_LVL LOG_LVL_INFO

/* 优先级：小智 > 录音 > 音乐 */
static const int s_priority_table[] = {
    [AUDIO_OWNER_NONE] = 0,
    [AUDIO_OWNER_XIAOZHI] = 3,
    [AUDIO_OWNER_RECORDER] = 2,
    [AUDIO_OWNER_MUSIC] = 1,
};

static struct {
    volatile audio_owner_t current_owner;
    rt_mutex_t mutex;
    rt_sem_t sem;
    audio_state_callback_t state_cb;
    bool initialized;
} s_manager;

int audio_manager_init(void)
{
    if (s_manager.initialized) {
        return 0;
    }
    
    s_manager.mutex = rt_mutex_create("audio_mutex", RT_IPC_FLAG_FIFO);
    if (!s_manager.mutex) {
        LOG_E("Failed to create mutex");
        return -RT_ENOMEM;
    }
    
    s_manager.sem = rt_sem_create("audio_sem", 1, RT_IPC_FLAG_FIFO);
    if (!s_manager.sem) {
        rt_mutex_delete(s_manager.mutex);
        LOG_E("Failed to create sem");
        return -RT_ENOMEM;
    }
    
    s_manager.current_owner = AUDIO_OWNER_NONE;
    s_manager.initialized = true;
    
    LOG_I("Audio manager initialized");
    return 0;
}

void audio_manager_deinit(void)
{
    if (!s_manager.initialized) {
        return;
    }
    
    if (s_manager.mutex) {
        rt_mutex_delete(s_manager.mutex);
        s_manager.mutex = NULL;
    }
    
    if (s_manager.sem) {
        rt_sem_delete(s_manager.sem);
        s_manager.sem = NULL;
    }
    
    s_manager.current_owner = AUDIO_OWNER_NONE;
    s_manager.initialized = false;
}

bool audio_acquire(audio_owner_t owner, audio_req_mode_t mode)
{
    if (!s_manager.initialized || owner == AUDIO_OWNER_NONE) {
        return false;
    }
    
    rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER);
    
    /* 检查是否已是当前拥有者 */
    if (s_manager.current_owner == owner) {
        rt_mutex_release(s_manager.mutex);
        return true;
    }
    
    /* 检查优先级 */
    if (s_manager.current_owner != AUDIO_OWNER_NONE) {
        if (s_priority_table[owner] <= s_priority_table[s_manager.current_owner]) {
            /* 优先级不够 */
            LOG_W("Priority too low: %d vs %d", owner, s_manager.current_owner);
            rt_mutex_release(s_manager.mutex);
            return false;
        }
        
        /* 需要等待 */
        if (mode == AUDIO_REQ_BLOCKING) {
            rt_mutex_release(s_manager.mutex);
            /* 等待当前使用者释放 */
            rt_err_t result = rt_sem_take(s_manager.sem, rt_tick_from_millisecond(5000));
            if (result != RT_EOK) {
                LOG_W("Acquire timeout");
                return false;
            }
            rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER);
        } else {
            rt_mutex_release(s_manager.mutex);
            return false;
        }
    }
    
    /* 获取音频 */
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = owner;
    
    /* 占用信号量 */
    rt_sem_trytake(s_manager.sem);
    
    rt_mutex_release(s_manager.mutex);
    
    LOG_I("Audio acquired: %d -> %d", old_owner, owner);
    
    /* 通知状态变化 */
    if (s_manager.state_cb) {
        s_manager.state_cb(owner, old_owner);
    }
    
    return true;
}

void audio_release(audio_owner_t owner)
{
    if (!s_manager.initialized) {
        return;
    }
    
    rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER);
    
    if (s_manager.current_owner != owner) {
        LOG_W("Release mismatch: current=%d, release=%d", 
              s_manager.current_owner, owner);
        rt_mutex_release(s_manager.mutex);
        return;
    }
    
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = AUDIO_OWNER_NONE;
    
    /* 释放信号量，允许其他人获取 */
    rt_sem_release(s_manager.sem);
    
    rt_mutex_release(s_manager.mutex);
    
    LOG_I("Audio released: %d", old_owner);
    
    /* 通知状态变化 */
    if (s_manager.state_cb) {
        s_manager.state_cb(AUDIO_OWNER_NONE, old_owner);
    }
}

audio_owner_t audio_force_acquire(audio_owner_t owner)
{
    if (!s_manager.initialized) {
        return AUDIO_OWNER_NONE;
    }
    
    rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER);
    
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = owner;
    
    /* 占用信号量 */
    rt_sem_trytake(s_manager.sem);
    
    rt_mutex_release(s_manager.mutex);
    
    LOG_I("Audio force acquired: %d -> %d", old_owner, owner);
    
    /* 通知状态变化 */
    if (s_manager.state_cb) {
        s_manager.state_cb(owner, old_owner);
    }
    
    return old_owner;
}

audio_owner_t audio_get_current_owner(void)
{
    return s_manager.current_owner;
}

bool audio_is_available(void)
{
    return s_manager.initialized && s_manager.current_owner == AUDIO_OWNER_NONE;
}

void audio_register_state_callback(audio_state_callback_t callback)
{
    s_manager.state_cb = callback;
}

bool audio_try_preempt(audio_owner_t preemptor)
{
    if (!s_manager.initialized || preemptor == AUDIO_OWNER_NONE) {
        return false;
    }
    
    rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER);
    
    if (s_manager.current_owner == AUDIO_OWNER_NONE) {
        /* 无人使用，直接获取 */
        s_manager.current_owner = preemptor;
        rt_sem_trytake(s_manager.sem);
        rt_mutex_release(s_manager.mutex);
        
        if (s_manager.state_cb) {
            s_manager.state_cb(preemptor, AUDIO_OWNER_NONE);
        }
        return true;
    }
    
    if (s_manager.current_owner == preemptor) {
        /* 已是拥有者 */
        rt_mutex_release(s_manager.mutex);
        return true;
    }
    
    /* 检查优先级 */
    if (s_priority_table[preemptor] > s_priority_table[s_manager.current_owner]) {
        /* 可以抢占 */
        audio_owner_t old_owner = s_manager.current_owner;
        s_manager.current_owner = preemptor;
        rt_mutex_release(s_manager.mutex);
        
        LOG_I("Audio preempted: %d -> %d", old_owner, preemptor);
        
        if (s_manager.state_cb) {
            s_manager.state_cb(preemptor, old_owner);
        }
        return true;
    }
    
    rt_mutex_release(s_manager.mutex);
    return false;
}
