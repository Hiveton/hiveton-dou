/**
  ******************************************************************************
  * @file   gsensors_service.c
  * @author Sifli software development team
  * @brief sensor service source.
 *
  ******************************************************************************
*/
/**
 * @attention
 * Copyright (c) 2019 - 2024,  Sifli Technology
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Sifli integrated circuit
 *    in a product or a software update for such product, must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Sifli nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Sifli integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY SIFLI TECHNOLOGY "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SIFLI TECHNOLOGY OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "rtthread.h"
#include <board.h>
#include "cwm_motion.h"
// #include "sensor_service.h"
// #include "sensor.h"
#include "cwm_lib.h"
#include "stdio.h"
// #include "gsensor_algo.h"
// #include "sensor_reg.h"
// #include "lcpu_service.h"
// #ifdef BSP_BLE_SIBLES
//     #include "ble_service.h"
// #endif

#define cywee_alloc rt_malloc
#define cywee_free  rt_free

// #ifndef BSP_USING_PC_SIMULATOR
//     //#include "ble_connection_manager.h"
//     #include "simulate_signal.h"
// #endif

//#define CWM_LOG_SHOW    // only debug cywee lib to open it

#define APP_TIMER_MAX_CNT_VAL   0xFFFFFFFF
#define TIMER_CLOCK_FREQ        1000
// #define STEP_SEND_SECOND        1
#define INPUT_DATA_WITH_FIFO_MODE
#define SIGNAL_FIFO_COUNT   10
#define ALGO_USING_TEST 1

// #if ALGO_USING_TEST
// static const char *REQ_SENS_NAME[] = {"acc", "gyro", "mag", "baro", "temp", "hr", "gnss", "ob", "oc", "accany"};
// #endif

// typedef struct
// {
//     void *ptr;
//     rt_list_t list;
// } cwm_malloc_node_t;



// static gsensor_pedo_info_t      g_pedo_info         = {0};
cwm_gesture_info_t       g_gesture_info      = {0};//姿势数据
// static cwm_seden_info_t         g_seden_info        = {0};
// static cwm_sleep_data_t         *g_sleep_buf        = NULL;
// static cwm_biking_info_t        g_biking_data       = {0};
// static cwm_freetraining_info_t  g_freetraining_info = {0};
// static gsensor_pedo_info_t      app_step_info       = {0};

static uint64_t                 cwm_tickLast            = 0;
static uint64_t                 cwm_elpsedTime_us       = 0;
// static int                      cwm_req_sensor_state    = 0;

// static uint8_t                  flag_sporting           = 0;
// static uint16_t                 activity_mode_status;
// static uint16_t                 inactivity_mode_status;

// static uint8_t                  g_stop_sleep_count   = 0;
// static uint8_t                  g_sleep_buf_num      = 0;
// static uint8_t                  g_wakeup_type        = CWM_WAKEUP_EXIT;
static uint8_t                  fifo_send_start      = 0;

// static bool                     g_sleep_start = false;
// static bool                     g_seden_start = false;
static bool                     g_cwm_inited  = false;

static SettingControl_t         scl;
// static struct rt_mutex          g_malloc_list_mutex;
// static rt_list_t                cwm_malloc_list;

// static user_info_t              sensor_user_info;
// static sport_target_t           sensor_sport_target;
// static sedentary_info_t         sensor_sedentary;
static gesture_info_t           sensor_gesture = {0, 1, 0};//默认右手佩戴，抬腕使能
// static sleep_time_t             sensor_sleep_time;
// static uint32_t                 sensor_sport_peroid = 1000;


// static void gsensor_para_process(uint16_t msg_id, void *data);
// static void gsensor_algo_process(uint32_t peroid, gsensors_fifo_t *fifo);

static inline uint64_t CWM_calcElapsedTicks(uint64_t tickNow, uint64_t tickLast)
{
    if (tickNow >= tickLast)
        return tickNow - tickLast;
    else
        return APP_TIMER_MAX_CNT_VAL - tickLast + tickNow; //Nordic RTC resolution is 18 bit, APP_TIMER_MAX_CNT_VAL = 0x0003FFFF
}

static uint64_t CWM_platsystickTicksToUs(uint64_t tick)
{
    return (uint64_t)(tick * 1000000 / TIMER_CLOCK_FREQ);
}

// void CWM_systickUpdate(void)
// {
//     //To implement the CWM_OS_GetTimeNs(), this function needs to be triggered every second
//     uint64_t tickNow;
//     uint64_t delta_ticks;

//     tickNow = rt_tick_get();
//     delta_ticks = CWM_calcElapsedTicks(tickNow, cwm_tickLast);
//     cwm_elpsedTime_us += CWM_platsystickTicksToUs(delta_ticks);
//     cwm_tickLast = tickNow;
// }

// void cwm_add_node_to_malloc_list(void *ptr)
// {
//     rt_mutex_take(&g_malloc_list_mutex, RT_WAITING_FOREVER);
//     cwm_malloc_node_t *node = cywee_alloc(sizeof(cwm_malloc_node_t));
//     RT_ASSERT(node);
//     node->ptr = ptr;
//     rt_list_init(&node->list);
//     rt_list_insert_after(&cwm_malloc_list, &node->list);
//     rt_mutex_release(&g_malloc_list_mutex);
//     return;
// }

// void cwm_delete_malloc_node_by_addr(void *ptr)
// {
//     rt_list_t *pos, *next;
//     rt_mutex_take(&g_malloc_list_mutex, RT_WAITING_FOREVER);
//     rt_list_for_each_safe(pos, next, (&cwm_malloc_list))
//     {
//         cwm_malloc_node_t *node = rt_list_entry(pos, cwm_malloc_node_t, list);
//         if (node && (ptr == node->ptr))
//         {
//             //rt_kprintf("cwm_delete_malloc_node_by_addr:%p\n",ptr);
//             rt_list_remove(&node->list);
//             cywee_free(node);
//             rt_mutex_release(&g_malloc_list_mutex);
//             return;
//         }
//     }
//     rt_mutex_release(&g_malloc_list_mutex);
//     return;
// }

// static int cwm_delete_all_node_from_malloc_list(void)
// {
//     rt_list_t *pos, *next;
//     rt_mutex_take(&g_malloc_list_mutex, RT_WAITING_FOREVER);
//     rt_list_for_each_safe(pos, next, (&cwm_malloc_list))
//     {
//         cwm_malloc_node_t *node = rt_list_entry(pos, cwm_malloc_node_t, list);
//         if (node)
//         {
//             rt_list_remove(&node->list);
//             //rt_kprintf("cwm_delete_all_node_from_malloc_list:%p\n",node->ptr);
//             cywee_free(node->ptr);
//             cywee_free(node);
//         }
//     }
//     rt_mutex_release(&g_malloc_list_mutex);
//     return 0;
// }

// int cwm_malloc_list_init(void)
// {
//     rt_mutex_init(&g_malloc_list_mutex, "cwm_list", RT_IPC_FLAG_FIFO);
//     rt_list_init(&cwm_malloc_list);
//     return 0;
// }

// INIT_PREV_EXPORT(cwm_malloc_list_init);

void *CWM_OS_malloc(int size)
{
    void *ptr;
    RT_ASSERT(size > 0);
    ptr = cywee_alloc(size);
    return ptr;
}

void CWM_OS_free(void *ptr)
{
    if (ptr == NULL)
        return;
    cywee_free(ptr);
}

uint64_t CWM_OS_GetTimeNs(void)
{
    uint64_t tick;
    uint64_t delta_ticks;
    uint64_t timeNow_us;

    tick = rt_tick_get();
    delta_ticks = CWM_calcElapsedTicks(tick, cwm_tickLast);
    timeNow_us = cwm_elpsedTime_us + CWM_platsystickTicksToUs(delta_ticks);
    return timeNow_us * 1000;
}

int CWM_OS_dbgOutput(const char *format)
{
    rt_kprintf("%s\n", format);
    return 0;
}

// int CWM_OS_dbgPrintf(const char *format, ...)
// {
//     va_list argList;
//     int size = 0;
//     static char tBuffer[256];

//     va_start(argList, format);
//     size = vsnprintf(tBuffer, sizeof(tBuffer), format, argList);
//     va_end(argList);

//     //strcat(tBuffer, "\n");
//     CWM_OS_dbgOutput(tBuffer);
//     return size;
// }

OsAPI device_func =
{
    .malloc       = CWM_OS_malloc,
    .free         = CWM_OS_free,
    .GetTimeNs    = CWM_OS_GetTimeNs,
    .dbgOutput    = CWM_OS_dbgOutput,
};

// #ifdef CWM_LOG_SHOW
// static void cwm_open_log(void)
// {
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[3] = 8;   //8 close；9 open
//     scl.iData[4] = 5;
//     scl.iData[7] = -1;
//     CWM_SettingControl(SCL_LOG, &scl);
// }
// #endif

// static int cwm_get_req_sensor_state(int index)
// {
//     if (cwm_req_sensor_state & (1 << index))
//         return 1;
//     return 0;
// }

/* L and H diferent config */
// static void cwm_set_user_info(void)
// {
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = sensor_user_info.age;
//     scl.iData[2] = sensor_user_info.gender;
//     scl.iData[3] = sensor_user_info.weight;
//     scl.iData[4] = sensor_user_info.height;
//     scl.iData[5] = sensor_gesture.hand;/*0:right hand;1:left hand*/
//     CWM_SettingControl(SCL_USER_INFO, &scl);
// }


/* L and H comm */
// static void cwm_set_date_time(void)
// {
//     time_data_t *cur_time = service_current_time_get(false);
// //  rt_kprintf("algo init time %d %d %d %d %d %d\r\n",cur_time->year,cur_time->month,cur_time->day,cur_time->hour,cur_time->min,cur_time->second);
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = cur_time->year;
//     scl.iData[2] = cur_time->month;
//     scl.iData[3] = cur_time->day;
//     scl.iData[4] = cur_time->hour;
//     scl.iData[5] = cur_time->min;
//     CWM_SettingControl(SCL_DATE_TIME, &scl);
// }

// void cwm_set_activity_mode(uint16_t mode)/*1001~7001*/
// {
//     CWM_Sensor_Enable(IDX_ALGO_ACTIVITY_OUTPUT);
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = mode;
//     CWM_SettingControl(SCL_SET_ACTIVITY_MODE, &scl);

//     activity_mode_status = mode;
// }

// void cwm_algo_process(void)
// {
//     CWM_process2(20 * 1000);
// }

// void cwm_systick_update(void)
// {
//     CWM_systickUpdate();
// }

static void cwm_chip_vendor_config(void)
{
    rt_memset(&scl, 0, sizeof(scl));
    scl.iData[0] = 1;
    scl.iData[1] = 2; /*2: skip_auto_detect */
    //scl.iData[2] = 0;
    //scl.iData[3] = 0;
    CWM_SettingControl(SCL_CHIP_VENDOR_CONFIG, &scl);
}

// static void cwm_get_chip_info(void)
// {
//     char chipInfo[64] = {0};

//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = 1;
//     scl.iData[2] = (int)chipInfo;
//     scl.iData[3] = sizeof(chipInfo);
//     scl.iData[4] = 0;
//     scl.iData[5] = 0;
//     scl.iData[6] = 0;
//     CWM_SettingControl(SCL_GET_CHIP_INFO, &scl);

//     rt_kprintf("have_security = %d.%d ret_buff_size = %d chipInfo = %s\n", scl.iData[5], scl.iData[6], scl.iData[4], chipInfo);
//     rt_kprintf("chip_settings = %d, %d, %d\n", scl.iData[9], scl.iData[10], scl.iData[11]);
// }

// static void cwm_get_lib_info(void)
// {
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     CWM_SettingControl(SCL_GET_LIB_INFO, &scl);

//     rt_kprintf("version:%d.%d.%d.%d product:%d\n", scl.iData[1], scl.iData[2], scl.iData[3], scl.iData[4], scl.iData[5]);
// }

static void cwm_set_lib_debug(void)
{
    rt_memset(&scl, 0, sizeof(scl));
    scl.iData[0] = 1;
    scl.iData[1] = 0;
    CWM_SettingControl(SCL_LIB_DEBUG, &scl);
}

// static void cwm_inactivity_config(void)/*set sensor noise level*/
// {
// #if (defined ACC_USING_SC7A22) || (defined ACC_USING_SC7A20)
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = 1;
//     CWM_SettingControl(SCL_INACTIVITY_CONFIG, &scl);
// #endif
// }


//输出运动，健康数据
// static void cwm_activity_output_process(void *data)
// {
//     float *p = ((pSensorEVT_t)data)->fData;

//     if (ACTIVITY_MODE_NORMAL            == (uint32_t)p[0] \
//             || ACTIVITY_MODE_TREADMILL         == (uint32_t)p[0] \
//             || ACTIVITY_MODE_OUTDOOR_RUNNING   == (uint32_t)p[0] \
//             || ACTIVITY_MODE_INDOOR_RUN        == (uint32_t)p[0] \
//             || ACTIVITY_MODE_FITNESS_PEDOMETER == (uint32_t)p[0])
//     {
//         gsensor_pedo_main_info_t *main_info = &g_pedo_info.main_info;
//         g_pedo_info.active_mode             = (uint32_t)p[0];
//         main_info->step             = (uint32_t)p[1];
//         main_info->distance         = p[2];
//         main_info->cal              = p[3];
//         rt_kprintf("basic_step:%d,%.2f,%.2f\n", (uint32_t)p[1], p[2], p[3]);
//         main_info->active_type      = (uint8_t)p[4];
//         main_info->step_frequency   = p[5];
//         main_info->step_length      = p[6];
//         main_info->pace             = p[7];
//         rt_kprintf("total_step:%d,%.2f,%.2f,%d\n", g_pedo_info.main_info.step, g_pedo_info.main_info.distance, g_pedo_info.main_info.cal, g_pedo_info.main_info.active_type);
//     }
//     else if (ACTIVITY_MODE_NORMAL_EXTRA == (uint32_t)p[0])
//     {
//         gsensor_pedo_extra_info_t *extra_info = &g_pedo_info.extra_info;
//         g_pedo_info.active_mode         = (uint32_t)p[0];
//         extra_info->av_step_frequency   = p[1];
//         extra_info->av_step_length      = p[2];
//         extra_info->av_pace             = p[3];
//         extra_info->max_step_frequency  = p[4];
//         extra_info->max_pace            = p[5];
//         extra_info->max_vo2             = p[6];
//         extra_info->hrr_intensity       = p[7];
//         extra_info->hrr_zone            = p[8];
//         extra_info->uncalibration_distance = p[9];
//     }
//     else if (ACTIVITY_MODE_OUTDOOR_BIKING == (uint32_t)p[0])
//     {
//         cwm_biking_main_info_t *main_info = &g_biking_data.main_info;
//         main_info->latitude         = p[1] + p[2];
//         main_info->longitude        = p[3] + p[4];
//         main_info->distance         = p[5];
//         main_info->speed            = (uint16_t)p[6];
//         main_info->cur_elevation    = (uint16_t)p[7];
//         main_info->cur_slope        = (uint8_t)p[8];
//         main_info->cal              = p[9];
//         main_info->max_vo2          = p[10];
//         main_info->hrr_intensity    = p[11];
//         main_info->hrr_zone         = p[12];
//     }
//     else if (ACTIVITY_MODE_BIKING_EXTRA == (uint32_t)p[0])
//     {
//         cwm_biking_extra_info_t *extra_info = &g_biking_data.extra_info;
//         extra_info->av_speed        = (uint16_t)p[1];
//         extra_info->max_speed       = (uint16_t)p[2];
//         extra_info->elevation_up    = (uint16_t)p[3];
//         extra_info->elevation_down  = (uint16_t)p[4];
//     }
//     else if (ACTIVITY_MODE_FREETRAINING == (uint32_t)p[0])
//     {
//         g_freetraining_info.cal             = p[1];
//         g_freetraining_info.max_vo2         = p[2];
//         g_freetraining_info.hrr_intensity   = p[3];
//         g_freetraining_info.hrr_zone        = p[4];
//         g_freetraining_info.duration_sec    = p[5];
//     }
//     else if (ACTIVITY_MODE_WORKOUT_MACHINE == (uint32_t)p[0])
//     {
//         //need to write
//     }

// #if 0
//     if (((uint32_t)p[0] >= ACTIVITY_MODE_NORMAL) && ((uint32_t)p[0] <= ACTIVITY_MODE_OUTDOOR_RUNNING_OPDR))
//     {
//         ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_RT_SPORT_INFO_ID, &g_pedo_info, sizeof(gsensor_pedo_info_t));
//     }
// #endif
// }

/* only on LCPU */
// void cwm_set_sedentary(void)
// {
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = 60;
//     scl.iData[2] = 1;//open remind
//     scl.iData[3] = 5;
//     scl.iData[4] = 5;
//     CWM_SettingControl(SCL_SEDENTARY, &scl);
// }

// static void cwm_set_inactivity_mode(uint16_t mode)/*1: sedentray; 2: sleep;*/
// {
//     if (mode <= SET_INACTIVITY_MODE_NAP)
//     {
//         CWM_Sensor_Enable(ACTIVITY_MODE_NORMAL);
//         CWM_Sensor_Enable(IDX_ALGO_INACTIVITY_OUTPUT);
//         rt_memset(&scl, 0, sizeof(scl));
//         scl.iData[0] = 1;
//         scl.iData[1] = mode;
//         CWM_SettingControl(SCL_SET_INACTIVITY_MODE, &scl);
//     }
//     else
//     {
//         inactivity_mode_status = mode;
//     }
// }

// static void cwm_set_seden_data(void *data)
// {
//     float *p = ((pSensorEVT_t)data)->fData;

//     g_seden_info.trigger_count = (uint16_t)p[1];
//     g_seden_info.trigger_type  = (uint8_t)p[2];

//     rt_kprintf("cwm_set_seden_data %d %d\n", g_seden_info.trigger_count, g_seden_info.trigger_type);
// }

// static uint8_t cwm_set_sleep_data(void *data)
// {
//     float *p = ((pSensorEVT_t)data)->fData;

//     if (INACTIVITY_MODE_OUTPUT_SLEEPING == (uint16_t)p[0])/*sleep*/
//     {
//         if (g_sleep_buf_num == MAIN_INFO_NUM)
//         {
//             rt_kprintf("err:sleep main info too much\n");
//             g_sleep_buf_num--;
//         }
//         cwm_sleep_main_info_t *main_info = &g_sleep_buf->main_info[g_sleep_buf_num];

//         main_info->trigger_count    = (uint8_t)p[1];
//         main_info->sleep_status     = (uint8_t)p[2];
//         main_info->month            = (uint8_t)p[3];
//         main_info->day              = (uint8_t)p[4];
//         main_info->hour             = (uint8_t)p[5];
//         main_info->min              = (uint8_t)p[6];
//         //rt_kprintf("sleep main %d %d %d %d %d %d %d\n",main_info->trigger_count,
//         //                                               main_info->sleep_status,
//         //                                               main_info->month,
//         //                                               main_info->day,
//         //                                               main_info->hour,
//         //                                               main_info->min,
//         //                                               g_sleep_buf_num);
//         g_sleep_buf_num ++;
//     }
//     else if (INACTIVITY_MODE_OUTPUT_SLEEP_TIME == (uint16_t)p[0])/*accumulate time*/
//     {
//         cwm_sleep_accumulate_time_t *accumulate_time = &g_sleep_buf->accumulate_time;
//         accumulate_time->total_sleep_time   = (uint16_t)p[1];
//         accumulate_time->total_lsleep_time  = (uint16_t)p[2];
//         accumulate_time->total_dsleep_time  = (uint16_t)p[3];
//         accumulate_time->total_awake_time   = (uint16_t)p[4];
//         accumulate_time->total_eyemove_time = (uint16_t)p[5];
//         //rt_kprintf("sleep accumulate %d %d %d %d %d\n",accumulate_time->total_sleep_time,
//         //                                               accumulate_time->total_lsleep_time,
//         //                                               accumulate_time->total_dsleep_time,
//         //                                               accumulate_time->total_awake_time,
//         //                                               accumulate_time->total_eyemove_time);
//     }

//     return g_sleep_buf_num;
// }

// static void cwm_inactivity_output_process(void *data)
// {
//     pSensorEVT_t inactivity_data = (pSensorEVT_t)data;
//     if (inactivity_data->fData[0] == INACTIVITY_MODE_OUTPUT_SEDENTARY)
//     {
//         cwm_set_seden_data((void *)inactivity_data);
//     }
//     else if ((inactivity_data->fData[0] == INACTIVITY_MODE_OUTPUT_SLEEPING) || (inactivity_data->fData[0] == INACTIVITY_MODE_OUTPUT_SLEEP_TIME))
//     {
//         uint8_t array_num;
//         if (g_sleep_buf)
//             array_num = cwm_set_sleep_data((void *)inactivity_data);
//         else
//             rt_kprintf("sleep output err:%x\r\n", g_sleep_start);
//     }
// }

// gsensor_pedo_info_t *cwm_get_pedo_data(void)
// {
//     return &g_pedo_info;
// }

static void cwm_set_gesture_data(void *data)
{
    float *p = ((pSensorEVT_t)data)->fData;

    g_gesture_info.gesture = (uint8_t)p[1];
    g_gesture_info.counter = (uint32_t)p[2];
    rt_kprintf("set-- gesture:%d, counter:%d\r\n", g_gesture_info.gesture, g_gesture_info.counter);

}

cwm_gesture_info_t *cwm_get_gesture_data(void)
{
    rt_kprintf("gesture:%d %d\n", g_gesture_info.gesture, g_gesture_info.counter);
    return &g_gesture_info;
}

// void cwm_get_sleep_data_cmd(uint8_t cmd)
// {
//     rt_memset(&scl, 0, sizeof(scl));
//     scl.iData[0] = 1;
//     scl.iData[1] = cmd;//0：get sleep data; 1:get sleep data,out of sleep detective
//     CWM_SettingControl(SCL_REQ_SLEEPING_DATA, &scl);
// }

// static uint8_t cwm_seden_time_judgement(time_data_t *cur_time, sedentary_info_t *sedentary)
// {
//     static uint32_t cur_time_total_s = 0;
//     static uint32_t seden_time_total_start_s = 0;
//     static uint32_t seden_time_total_end_s = 0;

//     cur_time_total_s         = cur_time->hour * 3600 + cur_time->min * 60 + cur_time->second;
//     seden_time_total_start_s = sedentary->start_time_hour * 3600 + sedentary->start_time_min * 60;
//     seden_time_total_end_s   = sedentary->end_time_hour * 3600 + sedentary->end_time_min * 60;

//     if (seden_time_total_start_s >= seden_time_total_end_s)/* cross a day */
//     {
//         if (cur_time_total_s <= seden_time_total_end_s)
//         {
//             cur_time_total_s += 24 * 3600;
//         }
//         seden_time_total_end_s += 24 * 3600;
//     }
//     //rt_kprintf("2:cwm_seden_time:%d %d %d\n", cur_time_total_s, seden_time_total_start_s, seden_time_total_end_s);

//     if ((cur_time_total_s >= seden_time_total_start_s) && (cur_time_total_s <= seden_time_total_end_s))
//     {
//         return 1;
//     }

//     return 0;
// }

// static uint8_t cwm_sleep_time_judgement(time_data_t *cur_time, sleep_time_t *sleep)
// {
//     static uint32_t cur_time_total_s = 0;
//     static uint32_t sleep_time_total_start_s = 0;
//     static uint32_t sleep_time_total_end_s = 0;

//     cur_time_total_s         = cur_time->hour * 3600 + cur_time->min * 60 + cur_time->second;
//     sleep_time_total_start_s = sleep->sleep_start_time_h * 3600 + sleep->sleep_start_time_m * 60 + sleep->sleep_start_time_s;
//     sleep_time_total_end_s   = sleep->sleep_stop_time_h * 3600 + sleep->sleep_stop_time_m * 60 + sleep->sleep_stop_time_s;

//     if (sleep_time_total_start_s >= sleep_time_total_end_s)/* cross a day */
//     {
//         if (cur_time_total_s <= sleep_time_total_end_s)
//         {
//             cur_time_total_s += 24 * 3600;
//         }
//         sleep_time_total_end_s += 24 * 3600;
//     }

//     if ((cur_time_total_s >= sleep_time_total_start_s) && (cur_time_total_s <= sleep_time_total_end_s))
//     {
//         return 1;
//     }

//     return 0;
// }

// static uint8_t cwm_enable_seden(time_data_t *cur_time)
// {
//     bool time_h;
//     if (!cwm_sleep_time_judgement(cur_time, &sensor_sleep_time) && cwm_seden_time_judgement(cur_time, &sensor_sedentary) && g_seden_start == false)
//         // && g_wakeup_type == CWM_WAKEUP_EXIT
//     {
//         cwm_set_date_time();
//         cwm_set_sedentary();
//         cwm_set_inactivity_mode(SET_INACTIVITY_MODE_SEDENTARY);
//         g_seden_start = true;
//         return 1;
//     }
//     return 0;
// }

// static void cwm_disable_seden(void)
// {
//     if (g_seden_start == true)
//     {
//         CWM_Sensor_Disable(IDX_ALGO_INACTIVITY_OUTPUT);
//         g_seden_start = false;
//     }
// }

// void cwm_sleep_buf_malloc(void)
// {
//     if (g_sleep_buf == NULL)
//     {
//         g_sleep_buf = rt_calloc(1,  sizeof(cwm_sleep_data_t));
//         RT_ASSERT(g_sleep_buf);
//         rt_memset(g_sleep_buf, 0, sizeof(cwm_sleep_data_t));
//     }
// }

// void cwm_sleep_buf_free(void)
// {
//     if (g_sleep_buf != NULL)
//     {
//         rt_free(g_sleep_buf);
//         g_sleep_buf = NULL;
//     }
// }

// static void _cwm_start_sleeping_stop_seden(void)
// {
//     cwm_disable_seden();
//     cwm_sleep_buf_malloc();
//     cwm_set_inactivity_mode(SET_INACTIVITY_MODE_SLEEP);
//     cwm_set_date_time();/*syn clock*/
// }

// static void _cwm_stop_sleeping_start_seden(time_data_t *time)
// {
//     if (cwm_enable_seden(time))
//     {
//         cwm_sleep_buf_free();
//     }
//     return;
// }

// static void cwm_start_sleeping_stop_seden(void *time)
// {
//     time_data_t *start_time = (time_data_t *)time;

//     if (cwm_sleep_time_judgement(start_time, &sensor_sleep_time) && (g_sleep_start == false))
//     {
//         _cwm_start_sleeping_stop_seden();
//         g_sleep_start = true;
//         g_seden_start = false;
//         g_wakeup_type = CWM_WAKEUP_NORMAL;
//         /* rt_kprintf("cwm_start_sleeping_stop_seden %d %d %d %d %d %d\n", \
//             start_time->year, start_time->month, start_time->day, start_time->hour, start_time->min, start_time->second);*/
//     }
// }

// static void cwm_stop_sleeping_start_seden(void *time)
// {
//     time_data_t *start_time = (time_data_t *)time;

//     if (!cwm_sleep_time_judgement(start_time, &sensor_sleep_time))/* out of sleep */
//     {
//         if (g_sleep_start == true)
//         {
//             if (g_stop_sleep_count == 0)
//             {
//                 g_wakeup_type = CWM_WAKEUP_TIME_ARRIVED;
//                 cwm_get_sleep_data_cmd(1);
//             }
//             g_stop_sleep_count ++;
//             /*rt_kprintf("cwm_stop_sleeping_start_seden %d %d %d %d %d %d\n", \
//                 start_time->year, start_time->month, start_time->day, start_time->hour, start_time->min, start_time->second); */
//         }

//         if ((g_stop_sleep_count == 2) || ((g_sleep_start == false) && (g_seden_start == false)))/* out off sleep or nothing to do before*/
//         {
//             g_sleep_start = false;
//             g_wakeup_type = CWM_WAKEUP_EXIT;
//             _cwm_stop_sleeping_start_seden(time);
//             if (g_seden_start == true)
//             {
//                 g_stop_sleep_count = 0;
//                 /*rt_kprintf("cwm_stop_sleeping_start_seden2 %d %d %d %d %d %d\n", \
//                     start_time->year, start_time->month, start_time->day, start_time->hour, start_time->min, start_time->second); */
//             }
//             else
//             {
//                 /* rt_kprintf("111111:sleep stop time arrived,seden start time not\n"); */
//             }
//         }
//     }
// }

// static void cwm_sleeping_seden_algo(void)
// {
//     time_data_t *start_time = service_current_time_get(false);
//     cwm_stop_sleeping_start_seden((void *)start_time);
//     cwm_start_sleeping_stop_seden((void *)start_time);
// }

static void cwm_enable_gesture(void)
{
    rt_kprintf("cwm_enable_gesture %d %d %d\n", sensor_gesture.hand, sensor_gesture.raise_hand, sensor_gesture.turn_wrist);
    if (sensor_gesture.raise_hand)
        CWM_Sensor_Enable(IDX_ALGO_WATCH_HANDUP);
}

// static void cwm_disable_gesture(void)
// {
//     CWM_Sensor_Disable(IDX_ALGO_WATCH_HANDUP);
// }

static void cwm_ap_sensorListen(pSensorEVT_t sensorEVT)
{
    float *p = sensorEVT->fData;
    rt_kprintf("cwm_ap_sensorListen called. sensorType=%d, index=%d, timestamp_ns=%d\n",
               sensorEVT->sensorType, sensorEVT->index, sensorEVT->timestamp_ns);
    switch (sensorEVT->sensorType)
    {
//     case IDX_ACCEL:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f, %.4f\n", "accel",
//                    p[1], p[1], p[2]);
// #endif
//         break;
//     }

//     case IDX_GYRO:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f, %.4f\n", "gyro",
//                    p[1], p[1], p[2]);
// #endif
//         break;
//     }

//     case IDX_MAG:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f, %.4f,%.4f\n", "mag",
//                    p[1], p[1], p[2], p[3]);
// #endif
//         break;
//     }

//     case IDX_BARO:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f\n", "baro",
//                    p[0]);
// #endif
//         break;
//     }

//     case IDX_TEMP:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f\n", "temp",
//                    p[0]);
// #endif
//         break;
//     }

//     case IDX_HEARTRATE:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f\n", "heart rate",
//                    p[0], p[1]);
// #endif
//         break;
//     }

//     case IDX_GNSS:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f\n", "gnss",
//                    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
// #endif
//         break;
//     }

//     case IDX_OFFBODY_DETECT:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s: %.4f, %.4f, %.4f\n", "offbody",
//                    p[1], p[1], p[2]);
// #endif
//         break;
//     }

//     case IDX_REQUEST_SENSOR:
//     {
//         if (p[2] != 0)
//             cwm_req_sensor_state |= (1 << (int)p[1]);
//         else
//             cwm_req_sensor_state &= ~(1 << (int)p[1]);
// #if ALGO_USING_TEST
//         rt_kprintf("req_sensor input_sensor: %s(%.1f), switch: %.1f\n", \
//                    REQ_SENS_NAME[(int)p[1]], p[1], p[2]);
//         rt_kprintf("input_sensor now: acc=%d gyro=%d mag=%d baro=%d temp=%d hr=%d gnss=%d ob=%d oc=%d accany=%d\n", \
//                    cwm_get_req_sensor_state(0), cwm_get_req_sensor_state(1), cwm_get_req_sensor_state(2), cwm_get_req_sensor_state(3), cwm_get_req_sensor_state(4), \
//                    cwm_get_req_sensor_state(5), cwm_get_req_sensor_state(6), cwm_get_req_sensor_state(7), cwm_get_req_sensor_state(8), cwm_get_req_sensor_state(9));
// #endif
//         break;
//     }


//     case IDX_ALGO_ACTIVITY_OUTPUT:
//     {
// #if ALGO_USING_TEST
//         rt_kprintf("%s:%.1f, %.1f, %.1f, %.1f, %.1f\n", "activity_output",
//                    p[0], p[1], p[2], p[3], p[4]);
// #endif
//         cwm_activity_output_process((void *)sensorEVT);
//         break;
//     }

//     case IDX_ALGO_INACTIVITY_OUTPUT:
//     {
// #if ALGO_USING_TEST
//         if (p[0] == INACTIVITY_MODE_OUTPUT_SEDENTARY)
//         {
//             rt_kprintf("%s:%.1f, %.1f, %.1f\n", "sedentary", p[0], p[1], p[2]);
//         }
//         else if ((p[0] == INACTIVITY_MODE_OUTPUT_SLEEPING) || (p[0] == INACTIVITY_MODE_OUTPUT_SLEEP_TIME))
//         {
//             rt_kprintf("%s:%.1f, %.1f, %.1f,%.1f, %.1f, %.1f\n", "sleep", p[1], p[1], p[2], p[3], p[4], p[5], p[6]);
//         }
// #endif
//         cwm_inactivity_output_process((void *)sensorEVT);
//         break;
//     }

    case IDX_ALGO_WATCH_HANDUP:
    {
#if ALGO_USING_TEST
        rt_kprintf("%s: %.4f, %.4f, %.4f\n", "watch handup",
                   p[1], p[1], p[2]);
#endif
        cwm_set_gesture_data((void *)sensorEVT);
        break;
    }
    }
}

//fifo data input algo lib
static void cwm_acce_fifo_data_input(void *buf, int16_t len, uint64_t ts_ns)
{
    int16_t i;

    gsensors_data_t *data_buf = (gsensors_data_t *)buf;
    fifo_send_start = 1;
    CWM_CustomSensorInput_Fifo_Start(CUSTOM_ACC, ts_ns, 20 * 1000);
    //rt_kprintf("cwm_acce_fifo len = %d \n", len);
    for (i = 0; i < len; i++)
    {
        CustomSensorData data;
        rt_memset(&data, 0, sizeof(data));
        data.sensorType = CUSTOM_ACC;
        rt_memcpy(data.fData, (const void *)&data_buf[i].acce_data[0], sizeof(float) * 3);
        //rt_kprintf("cwm_acce_fifo %f %f %f\r\n",data_buf[i].acce_data[0],data_buf[i].acce_data[1],data_buf[i].acce_data[2]);
        CWM_CustomSensorInput(&data);
        CWM_process();
    }
    CWM_CustomSensorInput_Fifo_End(CUSTOM_ACC);
    fifo_send_start = 0;
}


#ifdef USING_GYRO_SENSOR
static void cwm_gyro_fifo_data_input(void *buf, int16_t len, uint64_t ts_ns)
{
    int16_t i;
    gsensors_data_t *data_buf = (gsensors_data_t *)buf;
    fifo_send_start = 1;
    CWM_CustomSensorInput_Fifo_Start(CUSTOM_GYRO, ts_ns, 20 * 1000);

    for (i = 0; i < len; i++)
    {
        CustomSensorData data;
        rt_memset(&data, 0, sizeof(data));
        data.sensorType = CUSTOM_GYRO;
        rt_memcpy(data.fData, (const void *)&data_buf[i].gyro_data[0], sizeof(float) * 3);
        //rt_kprintf("cwm_gyro_fifo %f %f %f\n",data_buf[i].gyro_buf[0],data_buf[i].gyro_buf[1],data_buf[i].gyro_buf[2]);
        CWM_CustomSensorInput(&data);
        CWM_process();
    }
    CWM_CustomSensorInput_Fifo_End(CUSTOM_GYRO);
    fifo_send_start = 0;
}
#endif

//if both acc and gyro be input, acc must be the last one.
void cwm_all_fifo_data_input(void *buf, int16_t len, uint64_t ts_ns)
{
    uint64_t ts_ns_in = CWM_OS_GetTimeNs();

#ifdef USING_GYRO_SENSOR
    cwm_gyro_fifo_data_input(buf, len, ts_ns_in);
#endif
    cwm_acce_fifo_data_input(buf, len, ts_ns_in);
}

// int cwm_algo_deinit(void)
// {
//     cwm_delete_all_node_from_malloc_list();
//     CWM_LibDeinit();
//     g_cwm_inited = false;
//     return 0;
// }

int cyewee_algo_init(void)
{


    //initial cywee algo.
#if defined INPUT_DATA_WITH_FIFO_MODE
    CWM_CustomSensorInput_Fifo_Init(SIGNAL_FIFO_COUNT);//
#endif

// #ifdef CWM_LOG_SHOW
//     cwm_open_log();
// #endif

    // cwm_get_lib_info();//获取版本号
    CWM_LibPreInit(&device_func);//注册底层接口函数
    cwm_chip_vendor_config();//配置芯片厂商
    cwm_set_lib_debug();//设置调试模式
    CWM_LibPostInit(cwm_ap_sensorListen);//注册回调函数
    // cwm_inactivity_config();//配置静止检测参数
    // cwm_get_chip_info();//获取芯片信息
    // cwm_set_activity_mode(ACTIVITY_MODE_NORMAL);//设置初始运动模式
    // cwm_set_date_time();//同步时间
    // cwm_set_user_info();//设置用户信息
    cwm_enable_gesture();//使能抬手亮屏
    // time_data_t *cur_time = service_current_time_get(false);//获取当前时间
    // cwm_enable_seden(cur_time);//使能久坐提醒
    g_cwm_inited = true;

    return 0;
}

INIT_PRE_APP_EXPORT(cyewee_algo_init);

// static uint16_t convert_algo_to_sport_type(uint16_t type)
// {
//     uint16_t sport_type = 0;
//     //need to do...
//     return sport_type;
// }
/**
 * @brief gsensor send sleep history info to app,
         iclude :
                uint8_t         year;
                uint8_t         month;
                uint8_t         day;
                uint8_t         wday;
                uint16_t        num;
                sleep_info_t    data[HISTORY_MAX_SLEEP_NUM];
 */

// void gsensor_algo_save_sleep_to_history(void)
// {
//     if (CWM_WAKEUP_NORMAL == g_wakeup_type)
//         g_wakeup_type = CWM_WAKEUP_NATURALLY;

//     uint8_t sleep_id = 0;
//     sleep_history_info_t *history_sleep;

//     history_sleep = rt_calloc(1, sizeof(sleep_history_info_t));
//     RT_ASSERT(history_sleep);

//     // this fun only save cwm main info of the sleep data to history_sleep
//     cwm_sleep_data_t *p_sleep = g_sleep_buf;

//     for (uint8_t i = 0; i < g_sleep_buf_num; i ++)
//     {
//         if (p_sleep->main_info[i].sleep_status == 6 && p_sleep->main_info[i + 1].sleep_status == 0)
//         {
//             sleep_id = i + 1;
//             break;
//         }
//     }

//     time_data_t *end_time = service_current_time_get(false);
//     // in according to app rule, y/m/d use the time of the first data
//     if ((p_sleep->main_info[sleep_id].month == 12)
//             && (p_sleep->main_info[sleep_id].day == 31)
//             && (sensor_sleep_time.sleep_start_time_h > sensor_sleep_time.sleep_stop_time_h))
//     {
//         history_sleep->year = end_time->year - 2000 - 1;
//     }
//     else
//     {
//         history_sleep->year = end_time->year - 2000;
//     }

//     if (sensor_sleep_time.sleep_start_time_h > sensor_sleep_time.sleep_stop_time_h)
//     {
//         if (end_time->wday == 0)
//             end_time->wday = 6;
//         else
//             end_time->wday -= 1;
//     }
//     history_sleep->wday = end_time->wday;
//     history_sleep->month = end_time->month; //p_sleep->main_info[sleep_id].month;
//     history_sleep->day = end_time->day;     //p_sleep->main_info[sleep_id].day;

//     if (HISTORY_MAX_SLEEP_NUM > g_sleep_buf_num) g_sleep_buf_num = HISTORY_MAX_SLEEP_NUM ;

//     for (uint8_t i = 0; i < g_sleep_buf_num; i ++)
//     {
//         // change sleep status, because of app not support, if app support all status, not change them.
//         if ((p_sleep->main_info[i + 1].sleep_status == 0)
//                 || (p_sleep->main_info[i + 1].sleep_status == 12))
//             history_sleep->data[i].state = 1;
//         else if ((p_sleep->main_info[i + 1].sleep_status == 4)
//                  || (p_sleep->main_info[i + 1].sleep_status == 3))
//             history_sleep->data[i].state = 0;
//         else
//             history_sleep->data[i].state = p_sleep->main_info[i + 1].sleep_status;

//         history_sleep->data[i].start_hour = p_sleep->main_info[i + 1].hour;
//         history_sleep->data[i].start_min = p_sleep->main_info[i + 1].min;
//         history_sleep->data[i].start_second = 0;
//         history_sleep->data[i].index = i;
//     }

//     history_sleep->num = g_sleep_buf_num;
//     uint16_t size = sizeof(sleep_history_t) + g_sleep_buf_num * sizeof(sleep_info_t);
//     // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_HISTORY_SLEEP_IND, history_sleep, size);

//     rt_free(history_sleep);
//     g_sleep_buf_num = 0;
// }

/**
 * @brief gsensor send sport info to app,
         iclude :   uint8_t         sport_type;
                    uint8_t         par_type;
                    uint32_t        timestamp;
                    uint16_t        steps;
                    uint16_t        calories;
                    uint16_t        times;

//  */
// void gsensor_algo_send_sport_info_to_app(void)
// {
//     sport_result_t sport_result;
//     rt_memset(&sport_result, 0, sizeof(sport_result_t));
//     sport_result.sport_type = convert_algo_to_sport_type(activity_mode_status);
//     sport_result.par_type = convert_algo_to_sport_type(activity_mode_status);
//     sport_result.timestamp = service_timezone_get_timestamp(service_timezone_get()->tz_minuteswest);
//     sport_result.steps = g_pedo_info.main_info.step;
//     sport_result.calories = (uint16_t)g_pedo_info.main_info.cal;
//     sport_result.times = 1;
//     // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_HISTORY_SPORT_IND, &sport_result, sizeof(sport_result_t));

// }

/**
 * @brief gsensor_result_process ;
 * @param   step_info_t *step_info      step info
            include:
                 uint32_t        step;
                 uint32_t        distance;         //float, Three decima. actual = calory/1000
                 uint32_t        calories;         //float, Two decima. actual = calory/100
                 uint8_t         active_type;
                 float           step_frequency;
                 float           step_length;
                 float           pace;

 * @param       event_type_t sede_event
 * @param       event_type_t hand_event
 * @param       uint32_t peroid
//  */
// static void gsensor_meas_result_postprocess(step_info_t *step_info, remind_event_t sede_event, remind_event_t hand_event, uint32_t peroid)
// {
//     static uint32_t last_step;
//     static uint32_t interval_1m = 0;
//     static uint32_t interval_sport = 0;
//     static uint8_t  last_hour = 0;
//     static bool remind_target = true;
//     const step_info_t result_null = {0, 0, 0, 0, 0.0, 0.0, 0.0};
//     event_remind_t remind_ind;
//     static step_info_t pre_result;
//     bool diff = false;

//     //if wrist lift event is detected, send a reminder event to wake up HCPU
//     if (sensor_gesture.raise_hand && (REMIND_RAISE_HAND == hand_event || (REMIND_DOWN_HAND == hand_event)))// && !lcpu_app_is_sleep()
//     {
//         remind_ind.event = hand_event;
// #ifdef ALGO_USING_TEST
//         rt_kprintf(" cwm hand_event =%d;\n", hand_event);
// #endif
//         // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_EVENT_REMIND_IND, &remind_ind, sizeof(event_remind_t));
//     }

//     //if sedentary event is detected, send a reminder event to wake up HCPU
//     time_data_t *cur_time = service_current_time_get(false);
//     if (sensor_sedentary.sedentary_enable && REMIND_SEDENTARY == sede_event && g_cwm_inited && cwm_seden_time_judgement(cur_time, &sensor_sedentary))
//     {
//         remind_ind.event = sede_event;
// #ifdef ALGO_USING_TEST
//         rt_kprintf(" cwm sede_event =%d;\n", sede_event);
// #endif
//         // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_EVENT_REMIND_IND, &remind_ind, sizeof(event_remind_t));
//     }
//     //rt_kprintf("alog: step %d target %d remind %d", step_info->step, sensor_user_info.step_target, remind_target);

//     //if steps exceed the set target, send a reminder event to wake up HCPU
//     if (step_info->step < sensor_sport_target.step_target)
//     {
//         remind_target = true;
//     }
//     else if (remind_target)
//     {
//         remind_target = false;
//         remind_ind.event = REMIND_STEP_TARGET;
//         // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_EVENT_REMIND_IND, &remind_ind, sizeof(event_remind_t));
//     }

//     //compare with the previous results, and save the results if changes occur

//     if (rt_memcmp(step_info, &pre_result, sizeof(step_info_t)) && rt_memcmp(step_info, &result_null, sizeof(step_info_t)))
//     {
//         diff = true;
//         pre_result = *step_info;
//     }

// //!lcpu_app_is_sleep() &&
//     if ((last_step != step_info->step && (step_info->step > 1)))
//     {
//         //send current data to ui immediatly if hcpu on.
// #ifdef ALGO_USING_TEST
//         rt_kprintf("1cwm step =%d;\n", step_info->step);
// #endif
//         if (diff)
//         {
//             // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_RT_STEP_INFO_IND, step_info, sizeof(step_info_t));
//         }
//         last_step = step_info->step ;
//     }


//     //if sleep event happened, buffer it, and send it to app when condition satisfied
//     if (0 < g_sleep_buf_num &&
//             (CWM_WAKEUP_TIME_ARRIVED == g_wakeup_type ||
//              CWM_WAKEUP_NORMAL == g_wakeup_type))
//     {
//         gsensor_algo_save_sleep_to_history();
//     }

//     last_hour = cur_time->hour;

//     interval_1m += peroid;
//     interval_sport += peroid;
//     if ((interval_sport > sensor_sport_peroid) && flag_sporting)
//     {
//         interval_sport =  0;
// #ifdef ALGO_USING_TEST
//         rt_kprintf("sensor_sport_peroid sport mode = %d;\n", g_pedo_info.main_info.active_type);
// #endif
//         // ipc_send_lcpu_msg_to_hcpu(SENSOR_APP_RT_SPORT_INFO_ID, &g_pedo_info, sizeof(gsensor_pedo_info_t));
//     }

//     //every minutes send sport info (result) to app
//     if (interval_1m > 60 * 1000)
//     {
//         interval_1m = 0;
//         gsensor_algo_send_sport_info_to_app();
//     }
// }

// /**
//  * @brief gsensor_para_process, process all setting, ;
//  * @param   uint16_t msg_id
//             include:
//                 LCPU_WATCHDOG_ON = LCPU_SERVICE_CUSTOM_ID_BEGIN,
//                 LCPU_WATCHDOG_OFF,
//                 LCPU_PWR_ON,
//                 LCPU_PWR_OFF,
//                 LCPU_PWR_OFF_STANDBY_REQ,
//                 LCPU_MAC_ADDR_SET,
//                 LCPU_TP_WAKEUP_ENABLE_REQ,
//                 LCPU_TP_WAKEUP_DISABLE_REQ,
//                 LCPU_TIME_SET,
//  * @param   void *data  data is different according to msg_id
//  */
// static uint8_t cwm_enable_seden(time_data_t *cur_time);
// static void cwm_enable_gesture(void);
// void cwm_get_sleep_data_cmd(uint8_t cmd);

// static void gsensor_para_process(uint16_t msg_id, void *data)
// {
//     switch (msg_id)
//     {
//     // case LCPU_TIME_SET:
//     // {
//         if (g_cwm_inited)
//             cwm_set_date_time();
//         break;
//     // }

//     case APP_SENSOR_SETTING_USER_INFO:
//     {
//         sensor_user_info = *((user_info_t *) data);
//         if (g_cwm_inited)
//             cwm_set_user_info();
//         break;
//     }
//     case APP_SENSOR_SETTING_SPORT_TARGET:
//     {
//         sensor_sport_target = *((sport_target_t *) data);
//         break;
//     }
//     case APP_SENSOR_SETTING_SEDENTARY:
//     {
//         sensor_sedentary = *((sedentary_info_t *) data);
//         time_data_t *cur_time = service_current_time_get(false);

//         if (sensor_sedentary.sedentary_enable)
//         {
//             if (g_cwm_inited)
//             {
//                 cwm_enable_seden(cur_time);
//             }
//         }
//         else
//         {
//             cwm_disable_seden();
//         }
//         break;
//     }
//     case APP_SENSOR_SETTING_GESTURE_DISPLAY:
//     {
//         sensor_gesture = *((gesture_info_t *) data);
//         if (g_cwm_inited)
//             cwm_enable_gesture();
//         break;
//     }
//     case APP_SENSOR_SETTING_SLEEP_TIME:
//     {
//         sensor_sleep_time = *((sleep_time_t *)data);
//         break;
//     }
//     case APP_SENSOR_SETTING_SPORT_TIME:
//     {
//         sensor_sport_peroid = *((uint32_t *)data);
// #ifdef ALGO_USING_TEST
//         rt_kprintf("set sensor_sport_peroid is %d ! \n", sensor_sport_peroid);
// #endif
//         break;
//     }
//     case APP_SENSOR_ACTIVE_GET_SLEEP_DATA:
//     {
//         if (CWM_WAKEUP_NORMAL == g_wakeup_type) // active get sleep data only in normal sleep satuts
//             cwm_get_sleep_data_cmd(0);
//         break;
//     }
//     case APP_SENSOR_SPORT_MODE:
//     {
//         sport_mode_t *sport_mode = (sport_mode_t *)data;
//         flag_sporting = sport_mode->flag_sporting;
//         if (flag_sporting)
//         {
//             g_pedo_info.sport_type = sport_mode->sport_type;
//             cwm_set_activity_mode(sport_mode->mode);
//         }
//         else/*default*/
//         {
//             g_pedo_info.sport_type = 0;
//             cwm_set_activity_mode(ACTIVITY_MODE_NORMAL);
//         }

//         break;
//     }
//     default:
//         ;
//     }
// }
/**
 * @brief gsensor algo process, inclde: input data,process, ;
 * @param   uint32_t peroid   sensor_timer piroid
 * @param   gsensors_fifo_t *fifo   gsensor raw data fifo point
 */
// void gsensor_algo_process(uint32_t peroid, gsensors_fifo_t *fifo)
// {
//     static uint32_t interval_1s = 0, interval_1m = 0;
//     step_info_t  step_info;
//     remind_event_t sede_event = REMIND_NULL;
//     remind_event_t hand_event = REMIND_NULL;
//     rt_memset(&step_info, 0, sizeof(step_info_t));
//     //when EXT CYWEE lib was placed in HCPU, buffer it and send the data_sets to hcpu for every interval

//     fifo->time_ns = CWM_OS_GetTimeNs();

//     //for every gsensor sample peroid, call cywee algo
//     cwm_all_fifo_data_input((void *)fifo->buf, fifo->num, fifo->time_ns);
//     CWM_process();

//     //deal the result of cwm_algo_process
//     if (g_gesture_info.gesture > 0)
//     {
//         if (g_gesture_info.gesture == 1)
//         {
//             hand_event = REMIND_RAISE_HAND;
//         }
//         else if (g_gesture_info.gesture == 2)
//         {
//             hand_event = REMIND_DOWN_HAND;
//         }

//         g_gesture_info.gesture = 0;
//     }

//     interval_1s += peroid;
//     interval_1m += peroid;
//     if (interval_1s >= 1000) // 1s
//     {
//         interval_1s = 0;
//         CWM_systickUpdate();

//         if (interval_1m == 60000) //1min
//         {
//             interval_1m = 0;
//             CWM_process();
//             cwm_sleeping_seden_algo();

//             //sedentary remind
//             if (g_seden_info.trigger_type >= 1 || g_seden_info.trigger_type == 3)
//             {
//                 sede_event = REMIND_SEDENTARY;
//                 g_seden_info.trigger_type = 0;
//             }
//         }

//         //get user information to calculate calories and distance
//         gsensor_pedo_main_info_t *main_info = &g_pedo_info.main_info;
//         step_info.step = main_info->step;
//         step_info.distance = main_info->distance;
//         step_info.calories = main_info->cal;
//         step_info.active_type = main_info->active_type;
//         step_info.step_frequency = main_info->step_frequency;
//         step_info.step_length = main_info->step_length;
//         step_info.pace = main_info->pace;
//     }//1s

//     //carry out post-processing according to gsensor algorithm results, save relevant results,
//     //and notify HCPU if triggered remind event

//     gsensor_meas_result_postprocess(&step_info, sede_event, hand_event, peroid);
// }




// static rt_sensor_t cywee_algo = RT_NULL;

/**
 * @brief algo control for register; can update config info ;
        include :
        sensor_id       sensor algo  id
        gs_para_ops     algo para  process
        gs_algo_ops     algo   process
 */
// static rt_err_t cywee_algo_control(struct rt_sensor_device *sensor, int cmd, void *args)
// {
//     rt_err_t result = RT_EOK;

//     //VC32S_LOG_I("hr cmd 0x%x\n", cmd);

//     switch (cmd)
//     {
//     case RT_SENSOR_CTRL_GET_ID:
//     {
//         sensor_reg_info_t *info = args;
//         info->sensor_id = 0xff;
//         cyewee_algo_init();
//         info->gs_para_ops = gsensor_para_process;
//         info->gs_algo_ops = gsensor_algo_process;
//         rt_kprintf("algo_id  : %d ! \n", info->sensor_id);
//         break;
//     }
//     case RT_SENSOR_CTRL_SET_RANGE:
//         result = RT_ERROR;
//         break;
//     case RT_SENSOR_CTRL_SET_ODR:
//         result = RT_ERROR;
//         break;
//     case RT_SENSOR_CTRL_SET_MODE:
//         result = RT_ERROR;
//         break;
//     case RT_SENSOR_CTRL_SET_POWER:
//         result = RT_ERROR;
//         break;
//     case RT_SENSOR_CTRL_SELF_TEST:
//         result = RT_ERROR;
//         break;
//     default:
//         return -RT_ERROR;
//     }

//     return result;
// }
/**
 * @brief fetch_data for register;

 */

// static rt_size_t cywee_algo_fetch_data(struct rt_sensor_device *sensor, void *buf, rt_size_t len)
// {
//     return 0;
// }
/**
 * @brief gsensor ops for register;

 */
// static struct rt_sensor_ops sensor_ops =
// {
//     cywee_algo_fetch_data,
//     cywee_algo_control
// };
/**
 * @brief gsensor algo  register;
 * @param const char *name  algo name;
 * @param struct rt_sensor_config *cfg  sensor config include
     struct rt_sensor_intf        intf;      // sensor interface config
    struct rt_device_pin_mode    irq_pin;   // Interrupt pin, The purpose of this pin is to notification read data
    rt_uint8_t                   mode;      //sensor work mode
    rt_uint8_t                   power;     // sensor power mode
    rt_uint16_t                  odr;       /// sensor out data rate
    rt_int32_t                   range;     // sensor range of measurement
*/

// static int rt_hw_cywee_algo_register(const char *name, struct rt_sensor_config *cfg)
// {
//     /* magnetometer/compass sensor register */
//     cywee_algo = rt_calloc(1, sizeof(struct rt_sensor_device));
//     RT_ASSERT(cywee_algo)

//     cywee_algo->info.type       = RT_SENSOR_CLASS_ACCE;
//     cywee_algo->info.vendor     = RT_SENSOR_VENDOR_UNKNOWN;
//     cywee_algo->info.model      = "cywee_algo";
//     cywee_algo->info.unit       = RT_SENSOR_UNIT_NONE;
//     cywee_algo->info.intf_type  = 0;
//     cywee_algo->info.range_max  = 220;
//     cywee_algo->info.range_min  = 1;
//     cywee_algo->info.period_min = 1;
//     cywee_algo->info.fifo_max     = 1;
//     cywee_algo->data_buf        = RT_NULL;
//     cywee_algo->data_len            = 0;        //must be 0, because don't use data buf

//     rt_memcpy(&cywee_algo->config, cfg, sizeof(struct rt_sensor_config));
//     cywee_algo->ops = &sensor_ops;
//     rt_kprintf("cywee_algo_register name: %s\n", name);
//     rt_err_t result = rt_hw_sensor_register(cywee_algo, name, RT_DEVICE_FLAG_RDWR, RT_NULL);
//     if (result != RT_EOK)
//     {
//         rt_kprintf("device register err code: %d", result);
//         goto __exit;
//     }

//     rt_kprintf("GS algo[%s] init success!\n", name);

//     return RT_EOK;

// __exit:
//     if (cywee_algo)
//     {
//         rt_free(cywee_algo);
//         cywee_algo = RT_NULL;
//     }
//     return -RT_ERROR;
// }
// /**
//  * @brief gsensor algo  register, register name need unique, and longth is less than RT_NAME_MAX
// */

// static int cyewee_algo_register(void)
// {
//     //register cywee algo to gsensor algo.
//     char name[RT_NAME_MAX] = "a_cywee";


//     //all pin will be configed in drv.
//     struct rt_sensor_config cfg = {0};
//     cfg.irq_pin.pin = RT_PIN_NONE; //disable pin operation of sensor_close
//     rt_hw_cywee_algo_register(name, &cfg);
//     return 0;
// }
// INIT_COMPONENT_EXPORT(cyewee_algo_register);


