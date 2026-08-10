/**
 * @file    stepper.c
 * @brief   28BYJ-48 Stepper Motor — Half-Step Driving
 * 
 * 8-step sequence (half-stepping), higher torque & resolution:
 *   Step  IN4(P1.7) IN3(P1.6) IN2(P1.5) IN1(P1.4)
 *    0      1         0         0         1
 *    1      1         0         0         0
 *    2      1         1         0         0
 *    3      0         1         0         0
 *    4      0         1         1         0
 *    5      0         0         1         0
 *    6      0         0         1         1
 *    7      0         0         0         1
 * 
 * Speed control via step_delay (inter-step pause).
 */

#include "stepper.h"

/*===========================================================================
 * STATIC DATA
 *===========================================================================*/

/* Half-step sequence (8 steps) */
static code const uint8_t step_seq[8] = {
    0x09,   /* IN4(P1.7)=1 IN3(P1.6)=0 IN2(P1.5)=0 IN1(P1.4)=1 */
    0x08,   /* IN4(P1.7)=1 IN3(P1.6)=0 IN2(P1.5)=0 IN1(P1.4)=0 */
    0x0C,   /* IN4(P1.7)=1 IN3(P1.6)=1 IN2(P1.5)=0 IN1(P1.4)=0 */
    0x04,   /* IN4(P1.7)=0 IN3(P1.6)=1 IN2(P1.5)=0 IN1(P1.4)=0 */
    0x06,   /* IN4(P1.7)=0 IN3(P1.6)=1 IN2(P1.5)=1 IN1(P1.4)=0 */
    0x02,   /* IN4(P1.7)=0 IN3(P1.6)=0 IN2(P1.5)=1 IN1(P1.4)=0 */
    0x03,   /* IN4(P1.7)=0 IN3(P1.6)=0 IN2(P1.5)=1 IN1(P1.4)=1 */
    0x01    /* IN4(P1.7)=0 IN3(P1.6)=0 IN2(P1.5)=0 IN1(P1.4)=1 */
};

static uint8_t  step_idx = 0;          /* Current step index (0~7) */
static int16_t  current_angle = 0;     /* x10 (0~1800) */
static int16_t  target_angle = 0;      /* x10 */
static uint8_t  stepper_speed = 5;     /* 1~10 */
static uint8_t  is_running = 0;
static uint8_t  emergency = 0;

/*===========================================================================
 * HELPERS
 *===========================================================================*/

static void stepper_output(uint8_t pattern)
{
    MOTOR_IN1 = (pattern >> 0) & 0x01;
    MOTOR_IN2 = (pattern >> 1) & 0x01;
    MOTOR_IN3 = (pattern >> 2) & 0x01;
    MOTOR_IN4 = (pattern >> 3) & 0x01;
}

/**
 * @brief  Inter-step delay.
 *         Speed 1→10 maps to delay 5ms→0.5ms (roughly)
 */
static void stepper_delay(void)
{
    uint16_t delay_us;
    uint16_t i;
    
    /* delay (us) = 4000 / speed + 500 */
    delay_us = (uint16_t)(4000 / stepper_speed) + 500;
    
    for (i = 0; i < delay_us; i++) {
        uint8_t j;
        for (j = 0; j < 3; j++) { _nop_(); }
    }
}

/*===========================================================================
 * API
 *===========================================================================*/

void stepper_init(void)
{
    stepper_output(0x00);       /* All coils off */
    step_idx = 0;
    current_angle = 0;
    target_angle = 0;
    is_running = 0;
    emergency = 0;
}

void stepper_stop(void)
{
    stepper_output(0x00);
    is_running = 0;
}

void stepper_emergency_stop(void)
{
    emergency = 1;
    stepper_output(0x00);
    is_running = 0;
}

uint8_t stepper_step(uint8_t dir)
{
    int16_t ratio = (dir == STEPPER_FORWARD) ? 1 : -1;
    
    if (dir == STEPPER_FORWARD) {
        if (current_angle >= MOTOR_MAX_ANGLE) return 1;   /* Hard limit */
        step_idx = (step_idx + 1) & 0x07;
        current_angle += 1;   /* Each half-step = 180°/2048 ≈ 0.088°; 
                                 for simplicity, 2048 steps = 180° → ratio ≈ 0.088°
                                 We use 1 step = 1 unit (x10) for coarser tracking */
    } else {
        if (current_angle <= 0) return 1;   /* Hard limit */
        step_idx = (step_idx == 0) ? 7 : (step_idx - 1);
        current_angle -= 1;
    }
    
    stepper_output(step_seq[step_idx]);
    stepper_delay();
    return 0;
}

void stepper_move_to(int16_t target)
{
    /* Clamp target */
    if (target < 0) target = 0;
    if (target > MOTOR_MAX_ANGLE) target = MOTOR_MAX_ANGLE;
    
    target_angle = target;
    emergency = 0;
    
    if (current_angle == target_angle) return;
    
    is_running = 1;
    
    while (is_running && !emergency) {
        if (current_angle < target_angle) {
            stepper_step(STEPPER_FORWARD);
        } else if (current_angle > target_angle) {
            stepper_step(STEPPER_REVERSE);
        } else {
            break;
        }
    }
    
    stepper_output(0x00);   /* Release coils to save power */
    is_running = 0;
}

uint16_t stepper_get_position(void)
{
    return (uint16_t)current_angle;
}

uint8_t stepper_is_running(void)
{
    return is_running;
}

void stepper_set_speed(uint8_t speed)
{
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;
    stepper_speed = speed;
}
