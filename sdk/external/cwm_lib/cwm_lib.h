#ifndef __CWM_LIB_H__
#define __CWM_LIB_H__

#include <stdint.h>
//#include "os_api.h"
#include "CWM_EventStructure.h"
#include "OsApi.h"
#include "CWM_CustomDataStruct.h"

/*CWM_SettingControl*/
#define SCL_LOG                 0
#define SCL_USER_INFO           1
#define SCL_DATE_TIME           2
#define SCL_SEDENTARY           3
#define SCL_REQ_SLEEPING_DATA   5
#define SCL_SET_ACTIVITY_MODE   7
#define SCL_GET_CHIP_INFO       8
#define SCL_LIB_DEBUG           10
#define SCL_PEDO_CONFIG         11
#define SCL_GET_LIB_INFO        12
#define SCL_CHIP_VENDOR_CONFIG  17
#define SCL_ABS_STATIC_CONFIG   19
#define SCL_INACTIVITY_CONFIG   26
#define SCL_SET_INACTIVITY_MODE 27
#define SCL_ALGO_PROC_CONFIG    100
#define SCL_SENS_CALI_CONFIG    110
#define SCL_SENS_CALI_CTRL_MAG  111
#define SCL_SENS_CALI_CTRL_A    112
#define SCL_PEDO_RESET          4       /*reset pedo data*/
#define SCL_LOW_POWER_MODE      6       /*use with acc_any_motion*/
#define SCL_ACTIVITY_CONFIG     9
#define SCL_SWIM_CONFIG         13
#define SCL_BIKING_CONFIG       14
#define SCL_ACTIVITY_PAUSE      15
#define SCL_HAND_UPDOWN_CONFIG  16
#define SCL_SLEEP_CONFIG        18
#define SCL_HEART_RATE_CONFIG   20
#define SCL_REQ_SWIM_EXIT       21
#define SCL_WATCH_FALL_CONFIG   22
#define SCL_AR_ALERT_CONFIG     23
#define SCL_ACT_PAUSE_DETECT    24
#define SCL_WM_CONFIG           25
#define SCL_STAND_CONFIG        28
#define SCL_SS_CONFIG           29
#define SCL_ACT_INFO_CONFIG     30
#define SCL_REQ_ACTIVITY_EXIT   31
#define SCL_TAP_CONFIG          32

//#define SCL_SZ_BREACH_CONFIG    50
//#define SCL_WASH_HAND_CONFIG    52
//#define SCL_INPUT_SENSOR_CONFIG 101
//#define SCL_INPUT_DT_CONFIG     102
//#define SCL_SPV_CONFIG          113
//#define SCL_SPV_MODE            114
//#define SCL_SENS_CALI_SET_MAG   115
//#define SCL_GET_HEAP_INFO       120
//#define SCL_SEC_VERIFY_CLIENT   200
//#define SCL_SEC_VERIFY_SERVER   201
//#define SCL_HS_ORIEN_CONFIG     300
//#define SCL_HS_ORIEN_CTRL_EXC   301
//#define SCL_HS_ORIEN_RESET      302
//#define SCL_HS_PHONE_CONFIG     303
//#define SCL_EAR_FALL_CONFIG     304
//#define SCL_FUSION_MOUSE_CONFIG 501

/*CWM_CustomSensorInput*/

#define CUSTOM_ACC              0
#define CUSTOM_GYRO             1
#define CUSTOM_MAG              2
#define CUSTOM_BARO             3
#define CUSTOM_TEMP             4
#define CUSTOM_HEARTRATE        5
#define CUSTOM_GNSS             6
#define CUSTOM_OFFBODY_DETECT   7
#define CUSTOM_ON_CHARGING      8
#define CUSTOM_ACC_ANY_MOTION   9
//#endif
//#define CUSTOM_SENS1            10

/* CWM_OS_setGpio*/
/*#define PIN_INT1                    0
#define PIN_INT2                    1
#define PIN_VDD                     2
#define PIN_RESET                   3
#define PIN_CS                      4
#define INT_TRIGGER_TYPE_RISING     0
#define INT_TRIGGER_TYPE_FALLING    1

#define INT_RESISTOR_PULLUP         0
#define INT_RESISTOR_PULLDOWN       1
#define INT_RESISTOR_NOPULL         2*/

/*output sensor*/

#define IDX_ACCEL                    0
#define IDX_GYRO                     1
#define IDX_MAG                      2
#define IDX_BARO                     3
#define IDX_TEMP                     4
#define IDX_HEARTRATE                5
#define IDX_GNSS                     6
#define IDX_OFFBODY_DETECT           7
#define IDX_REQUEST_SENSOR           11
#define IDX_ONCHARGING_DETECT        18
#define IDX_ALGO_ABSOLUTE_STATIC     19
#define IDX_ALGO_SENS_CALIBRATION    21
#define IDX_ALGO_INACTIVITY_OUTPUT   22
#define IDX_ALGO_WATCHSEDENTARY      8
#define IDX_ALGO_WATCH_HANDUP        9
#define IDX_ALGO_WATCH_SLEEP         10
#define IDX_ALGO_SHAKE               13
#define IDX_ALGO_ACTIVITY_OUTPUT     14
#define IDX_ALGO_ANY_MOTION          15
#define IDX_ALGO_NO_MOTION           16
/*#define IDX_ALGO_SLEEP               10
#define IDX_ALGO_ACTTYPEDETECT       12
#define IDX_ALGO_SHAKE               13
#define IDX_ALGO_FALLING             17*/



/*set inactivity mode*/
#define SET_INACTIVITY_MODE_NOTHING         0
#define SET_INACTIVITY_MODE_SEDENTARY       1
#define SET_INACTIVITY_MODE_SLEEP           2
#define SET_INACTIVITY_MODE_SEDENTARY_NAP   3
#define SET_INACTIVITY_MODE_NAP             4
#define SET_INACTIVITY_MODE_SLEEP_SEDENTARY 5

/*inactivity mode output*/
#define INACTIVITY_MODE_OUTPUT_SEDENTARY    20
#define INACTIVITY_MODE_OUTPUT_SLEEPING     21
#define INACTIVITY_MODE_OUTPUT_SLEEP_TIME   31
#define INACTIVITY_MODE_OUTPUT_NAP          22
#define INACTIVITY_MODE_OUTPUT_NAP_TIME     32

/*wm_list*/
#define WM_MODE_NUMBER                      6001
#define WM_SUB_MODE_BOXING                  353588804
#define WM_SUB_MODE_DUMBBELL                1409751446
#define WM_SUB_MODE_ROPE_SKIPPING           236262734
#define WM_SUB_MODE_DOUBLEUNDERSKIPPING     1985241117
#define WM_SUB_MODE_ELIPTICAL_TRAINER       1248291560
#define WM_SUB_MODE_ROWING_MACHINE          718685710

typedef struct
{
    union
    {
        int iData[16];
        float fData[16];
        double dData[8];
    };
} SettingControl_t;

typedef void (*FN_SENSOR_LISTEN)(pSensorEVT_t);

/**
  * @brief  CyweeMotion internal process Initial
  */
void    CWM_LibPreInit(OsAPI *api);
void    CWM_LibDeinit(void);

/**
* @brief  Sensor Event register callback, when sensor event is triggered,
*         this function will be called to notify
*/
void    CWM_LibPostInit(FN_SENSOR_LISTEN pfn);

/**
* @brief  Sensor enable based on sensor index provided
* @param  sensorId: sensor register handle, if not setup this handle don't care
* @retval 0 is success, otherwise is failed
*/
int     CWM_Sensor_Enable(uint32_t sensorId);
int cyewee_algo_init(void);
/**
* @brief  Sensor disable based on sensor index provided
* @param  sensorId: sensor register handle, if not setup this handle don't care
* @retval 0 is success, otherwise is failed
*/
int     CWM_Sensor_Disable(uint32_t sensorId);
void    CWM_process(void);
int     CWM_IsReadyForProcess(void);
int64_t CWM_GetNextActionDuration_ns(void);
void    CWM_CustomSensorInput_Fifo_Init(int maxRecords_Acc);
int     CWM_CustomSensorInput_Fifo_Start(uint32_t extSensorId, uint64_t triggerFifoRead_timeStamp_ns, uint64_t dt_ns);
void    CWM_CustomSensorInput_Fifo_End(uint32_t extSensorId);

/**
* @brief  Read sensor data and output through sensor call-back function
* @retval 0 is no data to process, mcu can enter low power mode,
otherwise has some data needs process this function again.
*/
int     CWM_process2(int dt_us);

/**
* @brief  Get how many event in CyweeMotion Library to be processed
* @retval 0 is no data to process, mcu can enter low power mode,
otherwise has some data
*/
//int CWM_IsReadyForProcess(void);

/**
* @brief  Put custom sensor data.
* @param  SensorDataInput: sensor data input handle, input format as below:
*                          sensorType= CustomSensorDataIndex
*                          fData     = sensor raw data
* @retval 0 is success, otherwise is failed
*/
int     CWM_CustomSensorInput(CustomSensorData *SensorDataInput);

/**
* @brief Get the duration time of the next event
* @retval the duration time (unit nanosecond)
*/
//int64_t CWM_GetNextActionDuration_ns(void);

/**
* @brief  Switch log output by input parameters
* @param  Control ID
* @param  Setting control array
* @retval 0 is success, otherwise is failed
*/
int     CWM_SettingControl(int CtrId, SettingControl_t *pData);

/**
  * @brief Fifo initial
  * @param max recored number of acc fifo
  */
//void CWM_CustomSensorInput_Fifo_Init(int maxRecords_Acc);

/**
  * @brief  setting lib can start input fifo
  * @param  sensor ID
  * @param  timestamp that interrupt trigger to read hw fifo data
  * @param  fifo delta_time
  * @retval 0 is success, otherwise is failed
  */
//int CWM_CustomSensorInput_Fifo_Start(uint32_t extSensorId, uint64_t triggerFifoRead_timeStamp_ns, uint64_t dt_ns);

/**
  * @brief  setting lib to stop input fifo
  * @param  sensor ID
  */
//void CWM_CustomSensorInput_Fifo_End(uint32_t extSensorId);

#endif
