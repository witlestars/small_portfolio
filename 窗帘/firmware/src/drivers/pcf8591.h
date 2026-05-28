/**
 * @file    pcf8591.h
 * @brief   PCF8591 8-bit ADC/DAC Driver (I2C)
 * 
 * Used as ADC to read analog photoresistor voltage.
 * 4 analog input channels: AIN0~AIN3
 * Photoresistor connects to AIN0
 */

#ifndef __PCF8591_H__
#define __PCF8591_H__

#include "config.h"

#define PCF8591_OK       0
#define PCF8591_ERR_I2C  1

/* ADC channels */
#define PCF8591_AIN0      0x00
#define PCF8591_AIN1      0x01
#define PCF8591_AIN2      0x02
#define PCF8591_AIN3      0x03

uint8_t  pcf8591_init(void);
uint8_t  pcf8591_read_adc(uint8_t channel);    /* 0~255 */
uint16_t pcf8591_read_mv(uint8_t channel);     /* Convert to mV (with 5V ref) */

#endif /* __PCF8591_H__ */
