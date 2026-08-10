/**
 * @file    bh1750.h
 * @brief   BH1750 Digital Light Sensor Driver (I2C)
 */

#ifndef __BH1750_H__
#define __BH1750_H__

#include "config.h"

/* Measurement modes */
#define BH1750_CONT_H_RES    0x10    /* 1lx resolution, 120ms */
#define BH1750_CONT_H_RES2   0x11    /* 0.5lx, 120ms */
#define BH1750_CONT_L_RES    0x13    /* 4lx, 16ms */
#define BH1750_ONCE_H_RES    0x20    /* One-shot high res */
#define BH1750_ONCE_H_RES2   0x21    /* One-shot high res mode 2 */
#define BH1750_ONCE_L_RES    0x23    /* One-shot low res */

#define BH1750_OK       0
#define BH1750_ERR_I2C  1

uint8_t bh1750_init(void);
uint16_t bh1750_read_lux(void);      /* returns lux */

#endif /* __BH1750_H__ */
