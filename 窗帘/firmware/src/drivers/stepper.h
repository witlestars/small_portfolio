/**
 * @file    stepper.h
 * @brief   28BYJ-48 Stepper Motor Driver (4-phase, ULN2003)
 * 
 * 驱动方式: 8-step half-stepping (higher resolution & smoother)
 * 步距角: 5.625° (motor) / 64 (gear ratio) → 4096 steps/rev half-step
 * 实际用 2048 steps 对应 0~180° (half revolution)
 */

#ifndef __STEPPER_H__
#define __STEPPER_H__

#include "config.h"

#define STEPPER_OK          0
#define STEPPER_RUNNING     1

/* Motor directions */
#define STEPPER_FORWARD     1
#define STEPPER_REVERSE     0

void     stepper_init(void);
void     stepper_stop(void);
uint8_t  stepper_step(uint8_t dir);                  /* Single step, returns 1 if limite
d */
void     stepper_move_to(int16_t target_angle);      /* Move to absolute angle (x10) */
uint16_t stepper_get_position(void);                 /* Current angle x10 */
uint8_t  stepper_is_running(void);
void     stepper_set_speed(uint8_t speed);           /* 1~10 */
void     stepper_emergency_stop(void);               /* Collision avoidance */

#endif /* __STEPPER_H__ */
