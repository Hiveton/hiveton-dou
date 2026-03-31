#ifndef _CWM_MOTION_H_
#define _CWM_MOTION_H_

#include "simulate_signal.h"
#include "cwm_lib.h"
// #include "service_comm.h"
// #include "sensor_service.h"
//#define ALGO_USING_TEST   1

//every sleep buf
#define MAIN_INFO_NUM    15

#define CWM_WAKEUP_NORMAL          1
#define CWM_WAKEUP_TIME_ARRIVED    2
#define CWM_WAKEUP_NATURALLY       3
#define CWM_WAKEUP_EXIT            4

#define CWM_SEDEN_CLOSE    0
#define CWM_SEDEN_OPEN     1

#define SLEEP_START_TIME  22 //  0~23
#define SLEEP_STOP_TIME   23 //  0~23

/*
 ******************************************************************************
 * @file   sensor_reg.h
 * @author Sifli software development team
 ******************************************************************************
 */
/*
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

#ifndef _SENSOR_REG_H_
#define _SENSOR_REG_H_

#include "rtthread.h"
#include "stdint.h"
// #include "comm_info.h"

#define GSENSOR_FIFO_SIZE       (52 / 4)
// #define MAG_FIFO_SIZE   3

typedef struct
{
    float               acce_data[3];
#ifdef USING_GYRO_SENSOR
    float               gyro_data[3];
#endif
#ifdef USING_STEP_SENSOR
    uint32_t              step;
#endif
} gsensors_data_t;

typedef struct
{
    uint8_t hand;
    uint8_t raise_hand;
    uint8_t turn_wrist;
} gesture_info_t;

// typedef struct
// {
//     float                   acce_range;
// #ifdef USING_GYRO_SENSOR
//     int32_t                 gyro_range;
// #endif
// } gsensors_range_t;

typedef struct
{
    uint16_t            num;
    gsensors_data_t     buf[GSENSOR_FIFO_SIZE];
    uint64_t            time_ns;
    rt_list_t           list;
} gsensors_fifo_t;

/**
 * @brief  hr rawdata struct
*/
// typedef struct
// {
//     void *p_rawdata;    /**< read rawdata buff point                                     */
// } hrs_raw_data_t;

// typedef union
// {
//     hr_info_t           hr_info;
//     spo2_info_t         spo2_info;
//     bp_info_t           bp_info;
// } hrs_info_t;

// typedef struct
// {
//     float               mag_data[3];
// } mag_data_t;

// typedef struct
// {
//     uint16_t            num;
//     mag_data_t buf[MAG_FIFO_SIZE];
// } mag_fifo_t;

// typedef void (* gs_algo_ops_t)(uint32_t peroid, gsensors_fifo_t *fifo);
// typedef void (* gs_para_ops_t)(uint16_t msg_id, void *data);
// typedef void (* hrs_algo_ops_t)(uint8_t hr_type, void *data, void *g_in);
// typedef uint8_t (* hrs_algo_post_ops_t)(uint8_t hr_type, hrs_info_t *info);
// typedef void (* sesnor_rd_indicate_t)(void *param);
// typedef void (* mag_algo_ops_t)(uint32_t peroid, mag_fifo_t *data, void *g_in);
// typedef void (* mag_para_ops_t)(uint16_t msg_id, void *data);


// typedef struct
// {
//     uint8_t                     sensor_id;
//     uint8_t                     type;
//     uint8_t                     fetch_mode;
//     sesnor_rd_indicate_t        sesnor_rd_indicate;
//     const char                 *algo_name;
//     rt_uint32_t                 dev_period;
//     rt_uint32_t                 algo_period;
//     rt_uint8_t                  fifo_len;
//     void                       *gs_input;
//     float                       gs_acce_para;
//     gs_algo_ops_t               gs_algo_ops;
//     gs_para_ops_t               gs_para_ops;

//     hrs_algo_ops_t              hr_algo_ops;
//     hrs_algo_post_ops_t         hr_algo_post_ops;

//     mag_algo_ops_t               mag_algo_ops;
//     mag_para_ops_t               mag_para_ops;
// } sensor_reg_info_t;

// rt_err_t sensor_reg_open(const char *sensor_name, const char *algo_name, sensor_reg_info_t *sensor_info,  rt_device_t *sensor_device);
// rt_err_t sensor_reg_close(rt_device_t *sensor_device);

#endif



typedef struct
{
    uint32_t step;
    float distance;
    float cal;
    uint8_t active_type;
    float step_frequency;
    float step_length;
    float pace;
} cwm_pedo_main_info_t;

typedef struct
{
    float av_step_frequency;
    float av_step_length;
    float av_pace;
    float max_step_frequency;
    float max_pace;
    float max_vo2;
    float hrr_intensity;
    float hrr_zone;
    float uncalibration_distance;
} cwm_pedo_extra_info_t;

typedef struct
{
    uint32_t active_mode;
    cwm_pedo_main_info_t main_info;
    cwm_pedo_extra_info_t extra_info;
} cwm_pedo_info_t;

typedef struct
{
    float latitude;
    float longitude;
    float distance;
    uint16_t speed;
    uint16_t cur_elevation;
    uint8_t cur_slope;
    float cal;
    float max_vo2;
    float hrr_intensity;
    float hrr_zone;
} cwm_biking_main_info_t;

typedef struct
{
    uint16_t av_speed;
    uint16_t max_speed;
    uint16_t elevation_up;
    uint16_t elevation_down;
} cwm_biking_extra_info_t;

typedef struct
{
    cwm_biking_main_info_t main_info;
    cwm_biking_extra_info_t extra_info;
} cwm_biking_info_t;


typedef struct
{
    uint8_t gesture;
    uint32_t counter;

} cwm_gesture_info_t;

typedef struct
{
    uint16_t trigger_count;
    uint8_t trigger_type;
} cwm_seden_info_t;

typedef struct
{
    uint8_t trigger_count;
    uint8_t sleep_status;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
} cwm_sleep_main_info_t;

typedef struct
{
    uint16_t total_sleep_time;
    uint16_t total_lsleep_time;
    uint16_t total_dsleep_time;
    uint16_t total_awake_time;
    uint16_t total_eyemove_time;
} cwm_sleep_accumulate_time_t;

typedef struct
{
    cwm_sleep_main_info_t main_info[MAIN_INFO_NUM];
    cwm_sleep_accumulate_time_t accumulate_time;
} cwm_sleep_data_t;

typedef struct
{
    float cal;
    float max_vo2;
    float hrr_intensity;
    float hrr_zone;
    uint32_t duration_sec;
} cwm_freetraining_info_t;

#endif

