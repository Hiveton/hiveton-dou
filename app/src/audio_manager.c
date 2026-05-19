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
    bool static_ipc;
} s_manager;

static struct rt_mutex s_audio_manager_mutex;
static struct rt_semaphore s_audio_manager_sem;

static bool audio_owner_is_valid(audio_owner_t owner)
{
    return owner >= AUDIO_OWNER_NONE && owner <= AUDIO_OWNER_RECORDER;
}

static bool audio_manager_lock(void)
{
    if (!s_manager.initialized || s_manager.mutex == RT_NULL)
    {
        return false;
    }

    return rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER) == RT_EOK;
}

static void audio_manager_unlock(void)
{
    if (!s_manager.initialized || s_manager.mutex == RT_NULL)
    {
        return;
    }

    (void)rt_mutex_release(s_manager.mutex);
}

static audio_state_callback_t audio_manager_copy_state_callback(void)
{
    audio_state_callback_t callback = RT_NULL;

    if (!s_manager.initialized || s_manager.mutex == RT_NULL) {
        return RT_NULL;
    }

    if (!audio_manager_lock())
    {
        return RT_NULL;
    }

    callback = s_manager.state_cb;
    audio_manager_unlock();
    return callback;
}

static void audio_manager_notify_state(audio_owner_t owner, audio_owner_t old_owner)
{
    audio_state_callback_t callback = audio_manager_copy_state_callback();

    if (callback) {
        callback(owner, old_owner);
    }
}

static bool audio_manager_snapshot_state(audio_owner_t *owner, bool *is_initialized)
{
    if (owner == RT_NULL)
    {
        return false;
    }

    if (!audio_manager_lock())
    {
        return false;
    }

    if (is_initialized != RT_NULL)
    {
        *is_initialized = s_manager.initialized;
    }

    *owner = s_manager.initialized ? s_manager.current_owner : AUDIO_OWNER_NONE;
    audio_manager_unlock();

    return true;
}

int audio_manager_init(void)
{
    if (s_manager.initialized) {
        return 0;
    }
    
    if (rt_mutex_init(&s_audio_manager_mutex, "audio_mu", RT_IPC_FLAG_FIFO) != RT_EOK) {
        LOG_E("Failed to init mutex");
        return -RT_ERROR;
    }

    if (rt_sem_init(&s_audio_manager_sem, "audio_se", 1, RT_IPC_FLAG_FIFO) != RT_EOK) {
        rt_mutex_detach(&s_audio_manager_mutex);
        LOG_E("Failed to init sem");
        return -RT_ERROR;
    }

    s_manager.mutex = &s_audio_manager_mutex;
    s_manager.sem = &s_audio_manager_sem;
    s_manager.static_ipc = true;
    
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
        if (rt_mutex_take(s_manager.mutex, RT_WAITING_FOREVER) == RT_EOK)
        {
            s_manager.initialized = false;
            s_manager.current_owner = AUDIO_OWNER_NONE;
            s_manager.state_cb = RT_NULL;
            (void)rt_mutex_release(s_manager.mutex);
        }
        else
        {
            s_manager.initialized = false;
            s_manager.current_owner = AUDIO_OWNER_NONE;
            s_manager.state_cb = RT_NULL;
        }
    }
    else
    {
        s_manager.initialized = false;
        s_manager.current_owner = AUDIO_OWNER_NONE;
        s_manager.state_cb = RT_NULL;
    }
    
    if (s_manager.mutex) {
        if (s_manager.static_ipc) {
            rt_mutex_detach(s_manager.mutex);
        } else {
            rt_mutex_delete(s_manager.mutex);
        }
        s_manager.mutex = NULL;
    }
    
    if (s_manager.sem) {
        if (s_manager.static_ipc) {
            rt_sem_detach(s_manager.sem);
        } else {
            rt_sem_delete(s_manager.sem);
        }
        s_manager.sem = NULL;
    }
    s_manager.static_ipc = false;
}

bool audio_acquire(audio_owner_t owner, audio_req_mode_t mode)
{
    if (!s_manager.initialized || !audio_owner_is_valid(owner) || owner == AUDIO_OWNER_NONE) {
        return false;
    }
    
    if (!audio_manager_lock())
    {
        return false;
    }
    
    /* 检查是否已是当前拥有者 */
    if (s_manager.current_owner == owner) {
        audio_manager_unlock();
        return true;
    }
    
    /* 检查优先级 */
    if (s_manager.current_owner != AUDIO_OWNER_NONE) {
        if (s_priority_table[owner] <= s_priority_table[s_manager.current_owner]) {
            /* 优先级不够 */
            LOG_W("Priority too low: %d vs %d", owner, s_manager.current_owner);
            audio_manager_unlock();
            return false;
        }
        
        /* 需要等待 */
        if (mode == AUDIO_REQ_BLOCKING) {
            audio_manager_unlock();
            
            /* 等待当前使用者释放 */
            rt_err_t result = rt_sem_take(s_manager.sem, rt_tick_from_millisecond(5000));
            if (result != RT_EOK) {
                LOG_W("Acquire timeout");
                return false;
            }
            
            if (!audio_manager_lock())
            {
                return false;
            }

            if (s_manager.current_owner != AUDIO_OWNER_NONE) {
                LOG_I("Ownership preempted by higher priority");
                audio_manager_unlock();
                return false;
            }
        } else {
            audio_manager_unlock();
            return false;
        }
    }
    
    /* 获取音频 */
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = owner;
    
    /* 占用信号量 */
    if (rt_sem_trytake(s_manager.sem) != RT_EOK)
    {
        s_manager.current_owner = AUDIO_OWNER_NONE;
        audio_manager_unlock();
        return false;
    }
    
    audio_manager_unlock();
    
    LOG_I("Audio acquired: %d -> %d", old_owner, owner);
    
    /* 通知状态变化 */
    audio_manager_notify_state(owner, old_owner);
    
    return true;
}

void audio_release(audio_owner_t owner)
{
    if (!s_manager.initialized) {
        return;
    }
    
    if (!audio_manager_lock())
    {
        return;
    }
    
    if (s_manager.current_owner != owner) {
        LOG_W("Release mismatch: current=%d, release=%d", 
              s_manager.current_owner, owner);
        audio_manager_unlock();
        return;
    }
    
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = AUDIO_OWNER_NONE;
    
    /* 释放信号量，允许其他人获取 */
    rt_sem_release(s_manager.sem);
    
    audio_manager_unlock();
    
    LOG_I("Audio released: %d", old_owner);
    
    /* 通知状态变化 */
    audio_manager_notify_state(AUDIO_OWNER_NONE, old_owner);
}

audio_owner_t audio_force_acquire(audio_owner_t owner)
{
    if (!s_manager.initialized || !audio_owner_is_valid(owner) || owner == AUDIO_OWNER_NONE || s_manager.mutex == RT_NULL) {
        return AUDIO_OWNER_NONE;
    }
    
    if (!audio_manager_lock())
    {
        return AUDIO_OWNER_NONE;
    }
    
    audio_owner_t old_owner = s_manager.current_owner;
    s_manager.current_owner = owner;
    
    /* 占用信号量 */
    rt_sem_trytake(s_manager.sem);
    
    audio_manager_unlock();
    
    LOG_I("Audio force acquired: %d -> %d", old_owner, owner);
    
    /* 通知状态变化 */
    audio_manager_notify_state(owner, old_owner);
    
    return old_owner;
}

audio_owner_t audio_get_current_owner(void)
{
    audio_owner_t owner = AUDIO_OWNER_NONE;

    bool initialized = false;
    if (!audio_manager_snapshot_state(&owner, &initialized) || !initialized)
    {
        return AUDIO_OWNER_NONE;
    }

    return owner;
}

bool audio_is_available(void)
{
    bool available = false;
    audio_owner_t owner = AUDIO_OWNER_NONE;
    bool initialized = false;

    if (!audio_manager_snapshot_state(&owner, &initialized) || !initialized)
    {
        return false;
    }

    available = (owner == AUDIO_OWNER_NONE);
    return available;
}

void audio_register_state_callback(audio_state_callback_t callback)
{
    if (!s_manager.initialized || s_manager.mutex == RT_NULL) {
        s_manager.state_cb = callback;
        return;
    }

    if (!audio_manager_lock())
    {
        return;
    }

    s_manager.state_cb = callback;
    audio_manager_unlock();
}

bool audio_try_preempt(audio_owner_t preemptor)
{
    if (!s_manager.initialized || !audio_owner_is_valid(preemptor) || preemptor == AUDIO_OWNER_NONE) {
        return false;
    }

    if (!audio_manager_lock())
    {
        return false;
    }
    
    if (s_manager.current_owner == AUDIO_OWNER_NONE) {
        /* 无人使用，直接获取 */
        s_manager.current_owner = preemptor;
        if (rt_sem_trytake(s_manager.sem) != RT_EOK)
        {
            s_manager.current_owner = AUDIO_OWNER_NONE;
            audio_manager_unlock();
            return false;
        }
        audio_manager_unlock();
        
        audio_manager_notify_state(preemptor, AUDIO_OWNER_NONE);
        return true;
    }
    
    if (s_manager.current_owner == preemptor) {
        /* 已是拥有者 */
        audio_manager_unlock();
        return true;
    }
    
    /* 检查优先级 */
    if (s_priority_table[preemptor] > s_priority_table[s_manager.current_owner]) {
        /* 可以抢占 */
        audio_owner_t old_owner = s_manager.current_owner;
        s_manager.current_owner = preemptor;
        (void)rt_sem_trytake(s_manager.sem);
        audio_manager_unlock();
        
        LOG_I("Audio preempted: %d -> %d", old_owner, preemptor);
        
        audio_manager_notify_state(preemptor, old_owner);
        return true;
    }
    
    audio_manager_unlock();
    return false;
}
