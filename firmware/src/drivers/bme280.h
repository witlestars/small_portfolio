/**
 * @file    bme280.h
 * @brief   BME280 Temperature/Humidity/Pressure Sensor Driver
 * 
 * I2C Address: 0x76 (SDO pin = GND)
 * Measures: Temperature (0.01C), Humidity (0.008%), Pressure (0.18Pa)
 * Mode: Forced mode — one-shot measurement on demand
 */

#ifndef __BME280_H__
#define __BME280_H__

#include "config.h"

/* Status codes */
#define BME280_OK       0
#define BME280_ERR_I2C  1
#define BME280_ERR_ID   2   /* Chip ID mismatch */

typedef struct {
    int32_t  temperature;    /* x100, e.g. 2635 = 26.35 C */
    uint32_t humidity;       /* x100, e.g. 6825 = 68.25 % */
    uint32_t pressure;       /* x100, e.g. 101325 = 1013.25 hPa */
    uint8_t  status;
} bme280_data_t;

uint8_t bme280_init(void);
uint8_t bme280_read(bme280_data_t *data);

#endif /* __BME280_H__ */
