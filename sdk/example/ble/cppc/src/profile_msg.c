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


#ifdef BSP_BLE_CPPC
    #include "bf0_ble_cppc.h"
#endif



int profile_msg_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    switch (event_id)
    {
#ifdef BSP_BLE_CPPC
    case BLE_CPPC_CPM_NOTIFY:
    {
        ble_cppc_cpm *cpm = (ble_cppc_cpm *)data;
        LOG_I("cpm flags %x\r\n", cpm->flags);
        LOG_I("cpm inst_power %x\r\n", cpm->inst_power);
        if (cpm->flags & CPP_MEAS_PEDAL_POWER_BALANCE_PRESENT)
        {
            LOG_I("cpm pedal_power_balance %x\r\n", cpm->pedal_power_balance);
        }
        if (cpm->flags & CPP_MEAS_ACCUM_TORQUE_PRESENT)
        {
            LOG_I("cpm accum_torque %x\r\n", cpm->accum_torque);
        }
        if (cpm->flags & CPP_MEAS_WHEEL_REV_DATA_PRESENT)
        {
            LOG_I("cpm cumul_wheel_rev %x\r\n", cpm->cumul_wheel_rev);
            LOG_I("cpm last_wheel_evt_time %x\r\n", cpm->last_wheel_evt_time);
        }
        if (cpm->flags & CPP_MEAS_CRANK_REV_DATA_PRESENT)
        {
            LOG_I("cpm cumul_crank_rev %x\r\n", cpm->cumul_crank_rev);
            LOG_I("cpm last_crank_evt_time %x\r\n", cpm->last_crank_evt_time);
        }
        if (cpm->flags & CPP_MEAS_EXTREME_FORCE_MAGNITUDES_PRESENT)
        {
            LOG_I("cpm max_force_magnitude %x\r\n", cpm->max_force_magnitude);
            LOG_I("cpm min_force_magnitude %x\r\n", cpm->min_force_magnitude);
        }
        else if (cpm->flags & CPP_MEAS_EXTREME_TORQUE_MAGNITUDES_PRESENT)
        {
            LOG_I("cpm max_torque_magnitude %x\r\n", cpm->max_torque_magnitude);
            LOG_I("cpm min_torque_magnitude %x\r\n", cpm->min_torque_magnitude);
        }
        if (cpm->flags & CPP_MEAS_EXTREME_ANGLES_PRESENT)
        {
            LOG_I("cpm max_angle %x\r\n", cpm->max_angle);
            LOG_I("cpm min_angle %x\r\n", cpm->min_angle);
        }
        if (cpm->flags & CPP_MEAS_TOP_DEAD_SPOT_ANGLE_PRESENT)
        {
            LOG_I("cpm top_dead_spot_angle %x\r\n", cpm->top_dead_spot_angle);
        }
        if (cpm->flags & CPP_MEAS_BOTTOM_DEAD_SPOT_ANGLE_PRESENT)
        {
            LOG_I("cpm bot_dead_spot_angle %x\r\n", cpm->bot_dead_spot_angle);
        }
        if (cpm->flags & CPP_MEAS_ACCUM_ENERGY_PRESENT)
        {
            LOG_I("cpm accum_energy %x\r\n", cpm->accum_energy);
        }
        break;
    }
    case BLE_CPPC_CPV_NOTIFY:
    {

        ble_cppc_cpv *cpv = (ble_cppc_cpv *)data;

        LOG_I("cpv flags %x\r\n", cpv->flags);
        if (cpv->flags & CPP_VECTOR_CRANK_REV_DATA_PRESENT)
        {
            LOG_I("cpv cumul_crank_rev %x\r\n", cpv->cumul_crank_rev);

            LOG_I("cpv last_crank_evt_time %x\r\n", cpv->last_crank_evt_time);

        }
        if (cpv->flags & CPP_VECTOR_FIRST_CRANK_MEAS_ANGLE_PRESENT)
        {

            LOG_I("cpv first_crank_meas_angle %x\r\n", cpv->first_crank_meas_angle);

        }
        if (!(cpv->flags & CPP_VECTOR_INST_FORCE_MAGNITUDE_ARRAY_PRESENT) !=
                !(cpv->flags & CPP_VECTOR_INST_TORQUE_MAGNITUDE_ARRAY_PRESENT))
        {

            if (cpv->nb)
            {
                for (int i = 0; i < cpv->nb; i++)
                {
                    LOG_I("cpv force_torque_magnitude(%u) %x\r\n", i, cpv->force_torque_magnitude[i]);
                }
            }
        }
        break;
    }
    case BLE_CPPC_CPPC_NOTIFICATION_IND:
    {
        ble_cpcp_notyf_rsp *cppc = (ble_cpcp_notyf_rsp *)data;
        LOG_I("cppc req_op_code %x\r\n", cppc->req_op_code);
        LOG_I("cppc resp_value %x\r\n", cppc->resp_value);

        if ((cppc->resp_value == CPP_CTNL_PT_RESP_SUCCESS) && (len >= 3))
        {
            switch (cppc->req_op_code)
            {
            case (CPP_CTNL_PT_REQ_SUPP_SENSOR_LOC):
            {
                // Get the number of supported locations that have been received
                LOG_I("cppc supp_loc %x\r\n", cppc->value.supp_loc);
            }
            break;
            case (CPP_CTNL_PT_REQ_CRANK_LENGTH):
            {
                LOG_I("cppc crank_length %x\r\n", cppc->value.crank_length);
            }
            break;
            case (CPP_CTNL_PT_REQ_CHAIN_LENGTH):
            {
                LOG_I("cppc chain_length %x\r\n", cppc->value.chain_length);
            }
            break;
            case (CPP_CTNL_PT_REQ_CHAIN_WEIGHT):
            {
                LOG_I("cppc chain_weight %x\r\n", cppc->value.chain_weight);
            }
            break;
            case (CPP_CTNL_PT_REQ_SPAN_LENGTH):
            {
                LOG_I("cppc span_length %x\r\n", cppc->value.span_length);
            }
            break;
            case (CPP_CTNL_PT_START_OFFSET_COMP):
            {
                LOG_I("cppc Start Offset Compensation %x\r\n", cppc->value.offset_comp);
            }
            break;
            case (CPP_CTNL_REQ_SAMPLING_RATE):
            {

                LOG_I("cppc sampling_rate %x\r\n", cppc->value.sampling_rate);
            }
            break;
            case (CPP_CTNL_REQ_FACTORY_CALIBRATION_DATE):
            {
                LOG_I("cppc Calibration date year %u,month %u,day %u,hour %u,min %u,sec %u \r\n", cppc->value.factory_calibration.year
                      , cppc->value.factory_calibration.month, cppc->value.factory_calibration.day, cppc->value.factory_calibration.hour
                      , cppc->value.factory_calibration.min, cppc->value.factory_calibration.sec);
            }
            break;
            case (CPP_CTNL_PT_SET_CUMUL_VAL):
            case (CPP_CTNL_PT_UPD_SENSOR_LOC):
            case (CPP_CTNL_PT_SET_CRANK_LENGTH):
            case (CPP_CTNL_PT_SET_CHAIN_LENGTH):
            case (CPP_CTNL_PT_SET_CHAIN_WEIGHT):
            case (CPP_CTNL_PT_SET_SPAN_LENGTH):
            case (CPP_CTNL_MASK_CP_MEAS_CH_CONTENT):
            {
                // No parameters
            } break;

            default:
            {

            } break;
            }
        }
        break;
    }
    case BLE_CPPC_READ_CPF_RSP:
    {
        ble_cppc_cpf *cpf = (ble_cppc_cpf *)data;
        LOG_I("cpf CP Feature %x\r\n", cpf->sensor_feat);
        break;
    }
    case BLE_CPPC_READ_SL_RSP:
    {
        ble_cppc_sl *sl = (ble_cppc_sl *)data;
        LOG_I("sl Sensor Location %x\r\n", sl->sensor_loc);
        break;
    }
    case BLE_CPPC_READ_CPCP_CEP_RSP:
    {
        ble_cppc_cep *cep = (ble_cppc_cep *)data;
        LOG_I("cep cep_val %x\r\n", cep->cep_val);
        break;
    }
#endif
    }
    return 0;
}

BLE_EVENT_REGISTER(profile_msg_handler, (uint32_t)NULL);

