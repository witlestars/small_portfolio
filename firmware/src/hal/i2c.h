/**
 * @file    i2c.h
 * @brief   Software I2C Master Driver for STC89C52
 * 
 * STC89C52 无硬件 I2C，全部用 GPIO 模拟。
 * 支持多从设备挂在同一总线上（地址区分）。
 */

#ifndef __I2C_H__
#define __I2C_H__

#include "config.h"

void i2c_init(void);
void i2c_start(void);
void i2c_stop(void);
uint8_t i2c_write(uint8_t dat);
uint8_t i2c_read(uint8_t ack);     /* ack=1: master ACK, ack=0: master NACK */
void i2c_read_burst(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len);

#endif /* __I2C_H__ */
