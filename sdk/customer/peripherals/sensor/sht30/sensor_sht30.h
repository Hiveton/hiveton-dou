#ifndef SENSOR_SHT30_H
#define SENSOR_SHT30_H

#include "sht30.h"
#include "sensor.h"
#include <rtthread.h>
#include <rtdbg.h>

#define RT_SENSOR_CTRL_GET_TEMP     (0x100)  /* get temperature */
#define RT_SENSOR_CTRL_GET_HUMI     (0x101)  /* get humidity */

int rt_hw_sht30_init(const char *name, struct rt_sensor_config *cfg);

#endif
