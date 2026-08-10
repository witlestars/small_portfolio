/**
 * @file    pid.h
 * @brief   Fixed-Point PID Controller
 * 
 * All computations use int32 integer arithmetic.
 * Coefficients stored as x100 (e.g., Kp=2.0 → 200).
 * Anti-windup: integrator clamping.
 */

#ifndef __PID_H__
#define __PID_H__

#include "config.h"

typedef struct {
    int16_t  kp;           /* Proportional gain x100 */
    int16_t  ki;           /* Integral gain x100 */
    int16_t  kd;           /* Derivative gain x100 */
    
    int16_t  setpoint;     /* Target value */
    int16_t  input;        /* Current measured value */
    int16_t  output;       /* PID output (clamped 0~1800) */
    
    int32_t  integral;     /* Accumulated error (x100) */
    int16_t  prev_error;   /* Previous error for derivative */
    
    int16_t  out_min;      /* Output lower limit */
    int16_t  out_max;      /* Output upper limit */
    int32_t  i_max;        /* Integral windup limit */
    
    uint8_t  enabled;
} pid_t;

void pid_init(pid_t *pid);
void pid_set_gains(pid_t *pid, int16_t kp, int16_t ki, int16_t kd);
void pid_set_setpoint(pid_t *pid, int16_t sp);
void pid_set_limits(pid_t *pid, int16_t min, int16_t max);
int16_t pid_compute(pid_t *pid, int16_t input);

#endif /* __PID_H__ */
