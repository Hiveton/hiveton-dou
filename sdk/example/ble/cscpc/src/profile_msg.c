/**
  ******************************************************************************
  * @file   profile_msg.c
  * @author Sifli software development team
  ******************************************************************************
*/
/**
 * @attention
 * Copyright (c) 2025 - 2025,  Sifli Technology
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

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <stdlib.h>

#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"

#define LOG_TAG "profile_msg"
#include "log.h"

#ifdef BSP_BLE_CSCPC
    #include "bf0_ble_cscpc.h"
#endif

int profile_msg_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    switch (event_id)
    {
#ifdef BSP_BLE_CSCPC
    case BLE_CSCPC_CSC_MEASUREMENT_NOTIFY:
    {
        ble_csc_meas_value_ind const *rsp = (ble_csc_meas_value_ind const *) data;
        ble_csc_meas_value_ind ind;
        ind.att_code = rsp->att_code;
        ind.csc_meas.flags = rsp->csc_meas.flags;
        ind.csc_meas.cumul_wheel_rev = rsp->csc_meas.cumul_wheel_rev;
        ind.csc_meas.last_wheel_evt_time = rsp->csc_meas.last_wheel_evt_time;
        ind.csc_meas.cumul_crank_rev = rsp->csc_meas.cumul_crank_rev;
        ind.csc_meas.last_crank_evt_time = rsp->csc_meas.last_crank_evt_time;
        LOG_I("read csc meas flags is %d\r\n", ind.csc_meas.flags);
        LOG_I("read csc meas cumul_crank_rev is %d\r\n", ind.csc_meas.cumul_crank_rev);
        LOG_I("read csc meas cumul_wheel_rev is %d\r\n", ind.csc_meas.cumul_wheel_rev);
        LOG_I("read csc meas last_crank_evt_time is %d\r\n", ind.csc_meas.last_crank_evt_time);
        LOG_I("read csc meas last_wheel_evt_time is %d\r\n", ind.csc_meas.last_wheel_evt_time);
        break;
    }
    case BLE_CSCPC_CONTROL_POINT_INDICATE:
    {
        struct cscpc_ctnl_pt_rsp const *rsp = (struct cscpc_ctnl_pt_rsp const *)data;
        struct cscpc_ctnl_pt_rsp ind;
        ind.ctnl_pt_rsp.req_op_code = rsp->ctnl_pt_rsp.req_op_code;
        ind.ctnl_pt_rsp.resp_value = rsp->ctnl_pt_rsp.resp_value;
        ind.ctnl_pt_rsp.supp_loc = rsp->ctnl_pt_rsp.supp_loc;
        LOG_I("read ctnl pt opcode is %d\r\n", ind.ctnl_pt_rsp.req_op_code);
        LOG_I("read ctnl pt value is %d\r\n", ind.ctnl_pt_rsp.resp_value);
        LOG_I("read ctnl pt supp_loc is %d\r\n", ind.ctnl_pt_rsp.supp_loc);
        break;
    }
    case BLE_CSCPC_READ_CSC_FEATURE_RSP:
    {
        ble_csc_feat_value_ind const *rsp = (ble_csc_feat_value_ind const *)data;
        ble_csc_feat_value_ind ind;
        ind.att_code = rsp->att_code;
        ind.sensor_feat = rsp->sensor_feat;
        LOG_I("read csc feat code is %d\r\n", ind.att_code);
        LOG_I("read csc feat sensor feat is %d\r\n", ind.sensor_feat);
        break;
    }
    case BLE_CSCPC_READ_SENSOR_LOCATION_RSP:
    {
        ble_sensor_loc_value_ind const *rsp = (ble_sensor_loc_value_ind const *)data;
        ble_sensor_loc_value_ind ind;
        ind.att_code = rsp->att_code;
        ind.sensor_loc = rsp->sensor_loc;
        LOG_I("read sensor loc code is %d\r\n", ind.att_code);
        LOG_I("read sensor loc is %d\r\n", ind.sensor_loc);
        break;
    }
#endif
    }
    return 0;
}

BLE_EVENT_REGISTER(profile_msg_handler, (uint32_t)NULL);

