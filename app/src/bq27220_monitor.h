#ifndef BQ27220_MONITOR_H
#define BQ27220_MONITOR_H

#include "rtthread.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool valid;
    uint8_t battery_percent;
    bool charging;
    uint8_t aw_charge_state;
    uint8_t aw_fault_status;
    uint16_t battery_status;
    uint16_t operation_status;
    uint16_t voltage_mv;
    int16_t current_ma;
    int16_t average_power_mw;
    bool battery_present;
    bool init_complete;
    bool smoothing_active;
    bool discharge_mode;
} bq27220_power_snapshot_t;

void bq27220_monitor_start(void);
void bq27220_monitor_get_status_text(char *buffer, rt_size_t buffer_size);
void bq27220_monitor_get_power_snapshot(bq27220_power_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
