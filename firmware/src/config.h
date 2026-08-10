/**
 * @file    config.h
 * @brief   SHADE - Global Configuration & Pin Definitions
 * 
 * 普中A2开发板 + 外接传感器管脚分配:
 * 
 * I2C总线 (软I2C, 共用):
 *   P2.0 = SCL
 *   P2.1 = SDA
 *   挂载: BME280(0x76), BH1750(0x23), PCF8591(0x48)
 * 
 * HC-SR04:
 *   P3.6 = TRIG (输出触发脉冲)
 *   P3.2 = ECHO (INT0 捕获输入)
 *   ⚠ P3.2 与板载按键K1冲突 — 本项目不使用K1
 * 
 * 步进电机 (ULN2003):
 *   P1.4~P1.7 = IN1~IN4  (高4位 → 高4LED实时显示电机步进,炫!)
 * 
 * 板载资源:
 *   LCD1602: P0数据口, P2.5=RS, P2.6=RW, P2.7=E
 *   蜂鸣器: P2.3
 *   LED: P1.0~P1.3 状态指示, P1.4~P1.7 电机步进可视化
 *      [P1.0]=绿RUN [P1.1]=红ALARM [P1.2]=黄MOTOR [P1.3]=备用
 *   LED: P1.0=绿RUN P1.1=红ALARM P1.2=黄MOTOR (板载,不额外飞线)
 *   按键: P3.3=K2(MODE), P3.4=K3(UP), P3.5=K4(DOWN)
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <STC89C5xRC.H>
#include "idacs_lite.h"

/*===========================================================================
 * SYSTEM
 *===========================================================================*/

#define FOSC        11059200UL     /* 晶振频率 */
#define FOSC_KHZ    (FOSC / 1000)  /* 方便延时计算 */

/*===========================================================================
 * I2C BUS (Software Bit-Bang)
 *===========================================================================*/

sbit I2C_SCL = P2^0;
sbit I2C_SDA = P2^1;

#define I2C_DELAY   5   /* I2C时钟延时常数(~5us @ 11.0592MHz) */

/* I2C Device Addresses (7-bit, left-aligned as per protocol) */
#define BME280_ADDR     0x76
#define BH1750_ADDR     0x23
#define PCF8591_ADDR    0x48

/*===========================================================================
 * HC-SR04 ULTRASONIC
 *===========================================================================*/

sbit HCSR04_TRIG = P3^6;    /* 触发脉冲输出 */
sbit HCSR04_ECHO = P3^2;    /* 回波捕获 (INT0) */

#define HCSR04_TIMEOUT_MS   38   /* 超时 ≈ 6.5m (声速340m/s) */

/*===========================================================================
 * STEPPER MOTOR (28BYJ-48 + ULN2003)
 *===========================================================================*/

sbit MOTOR_IN1 = P1^4;   /* 步进电机 — P1高4位，不受J24影响 */
sbit MOTOR_IN2 = P1^5;
sbit MOTOR_IN3 = P1^6;
sbit MOTOR_IN4 = P1^7;

#define MOTOR_STEPS_PER_REV    2048   /* 28BYJ-48 半步模式 4096步/圈 × 1/64 */
#define MOTOR_TOTAL_STEPS       1024   /* 对应0~180°的总步数 (2048 × 180/360) */
#define MOTOR_MAX_ANGLE         1800   /* 最大角度 x10 */

/*===========================================================================
 * LCD1602 (Board-Mounted)
 *===========================================================================*/

#define LCD_DATA_PORT   P0      /* 8-bit data bus */

sbit LCD_RS = P2^5;     /* Register Select: 0=Command, 1=Data */
sbit LCD_RW = P2^6;     /* Read/Write:      0=Write, 1=Read  */
sbit LCD_EN = P2^7;     /* Enable:          高脉冲锁存        */

/*===========================================================================
 * BUZZER (Board-Mounted, ULN2003 IN3)
 *===========================================================================*/

sbit BUZZER = P2^3;

/*===========================================================================
 * BUTTONS (Board-Mounted)
 * K1(P3.2) sacrificed for HC-SR04 ECHO
 * K2(P3.3), K3(P3.4), K4(P3.5) used as MODE/UP/DOWN
 *===========================================================================*/

sbit KEY_MODE = P3^3;
sbit KEY_UP   = P3^4;
sbit KEY_DOWN = P3^5;

#define KEY_DEBOUNCE_MS     20    /* 去抖延时 (ms) */
#define KEY_LONG_PRESS_MS    800   /* 长按判定 (ms) */

/*===========================================================================
 * LED INDICATORS (板载! P1.0~P1.3, J24保持插着即可)
 * P1.4~P1.7给步进电机 → 高4位LED会随电机闪烁 = 当电机状态灯看    
 *===========================================================================*/

sbit LED_RUN   = P1^0;     /* 绿色 运行指示 (板载LED, P1低4位D5) */
sbit LED_ALARM = P1^1;     /* 红色 报警指示 (板载LED, P1低4位D6) */
sbit LED_MOTOR = P1^2;     /* 黄色 电机运行 (板载LED, P1低4位D7) */

/*===========================================================================
 * APPLICATION DEFAULTS
 *===========================================================================*/

/* Default thresholds */
#define DEFAULT_TEMP_THRESH     300     /* 30.0 C * 10 */
#define DEFAULT_HUMI_THRESH     700     /* 70.0 % * 10 */
#define DEFAULT_LIGHT_THRESH    800     /* 800 lux */
#define DEFAULT_DIST_THRESH     10      /* 10 cm (碰撞) */

/* PID defaults */
#define DEFAULT_PID_KP          200     /* 2.00 * 100 */
#define DEFAULT_PID_KI          30      /* 0.30 * 100 */
#define DEFAULT_PID_KD          10      /* 0.10 * 100 */

/* Motor speed */
#define DEFAULT_MOTOR_SPEED     5       /* 1~10 */

/* LCD pages */
#define LCD_PAGE_MAIN           0
#define LCD_PAGE_DETAIL         1
#define LCD_PAGE_THRESH         2
#define LCD_PAGE_COUNT          3

#endif /* __CONFIG_H__ */
