/**
 * @file    hcsr04.h
 * @brief   HC-SR04 Ultrasonic Distance Sensor Driver
 * 
 * 原理: TRIG发10us高脉冲 → ECHO返回等宽高电平
 *       距离 = (高电平时间 × 340m/s) / 2
 *       INT0捕获ECHO上升沿/下降沿, Timer0计脉宽
 */

#ifndef __HCSR04_H__
#define __HCSR04_H__

#include "config.h"

#define HCSR04_OK          0
#define HCSR04_TIMEOUT     1   /* No echo received */
#define HCSR04_OUT_RANGE   2   /* Distance > 4m */

uint8_t  hcsr04_init(void);
void     hcsr04_trigger(void);
uint16_t hcsr04_get_distance(void);  /* Returns cm, 0 = timeout */
uint8_t  hcsr04_measure(void);       /* Blocking measure, returns status */

#endif /* __HCSR04_H__ */
