/**
 * @file    pid.c
 * @brief   Fixed-Point PID Controller Implementation
 * 
 * Formula: output = Kp*e + Ki*Σe*dt + Kd*de/dt
 * All scaled x100 for integer math.
 * Integral anti-windup with clamping.
 */

#include "pid.h"

void pid_init(pid_t *pid)
{
    pid->kp = 200;       /* 2.00 */
    pid->ki = 30;        /* 0.30 */
    pid->kd = 10;        /* 0.10 */
    pid->setpoint = 800; /* Default 800 lux */
    pid->input = 0;
    pid->output = 900;   /* Default 90.0 deg (mid position) */
    pid->integral = 0;
    pid->prev_error = 0;
    pid->out_min = 0;
    pid->out_max = 1800;
    pid->i_max = 500000L; /* Integral windup limit */
    pid->enabled = 1;
}

void pid_set_gains(pid_t *pid, int16_t kp, int16_t ki, int16_t kd)
{
    if (kp >= 0) pid->kp = kp;
    if (ki >= 0) pid->ki = ki;
    if (kd >= 0) pid->kd = kd;
}

void pid_set_setpoint(pid_t *pid, int16_t sp)
{
    pid->setpoint = sp;
}

void pid_set_limits(pid_t *pid, int16_t min, int16_t max)
{
    pid->out_min = min;
    pid->out_max = max;
    if (pid->i_max > (int32_t)max * 1000L) {
        pid->i_max = (int32_t)max * 500L;
    }
}

/**
 * @brief  Compute PID output
 * @param  input  Current process value (e.g., lux from BH1750)
 * @return Output value (motor angle x10)
 */
int16_t pid_compute(pid_t *pid, int16_t input)
{
    int32_t error, p_term, i_term, d_term, output;
    
    if (!pid->enabled) return pid->output;
    
    pid->input = input;
    
    /* Proportional term */
    error = (int32_t)pid->setpoint - (int32_t)input;
    p_term = (error * (int32_t)pid->kp) / 100;
    
    /* Integral term with anti-windup */
    pid->integral += error;
    if (pid->integral > pid->i_max)  pid->integral = pid->i_max;
    if (pid->integral < -pid->i_max) pid->integral = -pid->i_max;
    i_term = (pid->integral * (int32_t)pid->ki) / 100;
    
    /* Derivative term (on measurement, not error — avoids derivative kick) */
    d_term = ((int32_t)(input - pid->prev_error) * (int32_t)(-pid->kd)) / 100;
    pid->prev_error = input;
    
    /* Sum */
    output = p_term + i_term + d_term + (int32_t)pid->output;
    
    /* Clamp output */
    if (output > (int32_t)pid->out_max) output = pid->out_max;
    if (output < (int32_t)pid->out_min) output = pid->out_min;
    
    pid->output = (int16_t)output;
    return pid->output;
}
