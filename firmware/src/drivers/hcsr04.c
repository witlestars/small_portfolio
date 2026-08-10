/**
 * @file    hcsr04.c
 * @brief   HC-SR04 Driver — INT0 Edge Capture
 * 
 * Timer0 configured for 1ms tick (in idacs_lite.c).
 * ECHO pin (P3.2) = INT0 — captures edges for pulse width.
 * 
 * Strategy: EX0 triggers on both edges.
 *   Rising edge  → record start time
 *   Falling edge → record end time, calculate pulse width
 */

#include "hcsr04.h"

/*===========================================================================
 * GLOBALS
 *===========================================================================*/

/* These are updated by the 1ms timer ISR in main.c */
extern volatile uint16_t g_sys_ticks;      /* 1ms system tick counter */

static volatile uint16_t echo_start;        /* Rising edge tick */
static volatile uint16_t echo_width;        /* Pulse width in 1ms ticks */
static volatile uint8_t  echo_done;         /* Measurement complete flag */
static volatile uint8_t  echo_timeout;      /* Timeout flag */

/*===========================================================================
 * INT0 ISR (External Interrupt 0, triggered by ECHO pin edges)
 *===========================================================================*/

void ext0_isr(void) interrupt 0
{
    if (IT0) {
        /* Falling edge: measurement complete */
        uint16_t now = g_sys_ticks;
        if (now >= echo_start) {
            echo_width = now - echo_start;
        } else {
            echo_width = (65535UL - echo_start) + now + 1;
        }
        echo_done = 1;
        IT0 = 0;   /* Reset to rising edge */
    } else {
        /* Rising edge: capture start */
        echo_start = g_sys_ticks;
        IT0 = 1;   /* Switch to falling edge */
    }
}

/*===========================================================================
 * INIT
 *===========================================================================*/

uint8_t hcsr04_init(void)
{
    HCSR04_TRIG = 0;
    echo_done = 0;
    echo_timeout = 0;
    echo_width = 0;
    
    /* INT0: rising edge first (IT0=0), toggle to falling in ISR */
    IT0 = 0;
    EX0 = 0;        /* Disabled by default, enabled during measure */
    
    return HCSR04_OK;
}

/*===========================================================================
 * TRIGGER
 *===========================================================================*/

void hcsr04_trigger(void)
{
    /* Generate 10us+ pulse on TRIG */
    HCSR04_TRIG = 1;
    {
        uint8_t i;
        for (i = 0; i < 20; i++) { _nop_(); }   /* ~20us */
    }
    HCSR04_TRIG = 0;
}

/*===========================================================================
 * MEASURE
 *===========================================================================*/

/**
 * @brief  Blocking measurement
 * @return HCSR04_OK (0) on success, error code otherwise
 * 
 * Max blocking time: ~38ms (corresponds to ~6.5m max range)
 */
uint8_t hcsr04_measure(void)
{
    uint8_t timeout_cnt = 0;
    
    echo_done = 0;
    echo_timeout = 0;
    
    /* Start with rising edge detection */
    IT0 = 0;    /* Rising edge first */
    EX0 = 1;    /* Enable INT0 */
    
    /* Send trigger pulse */
    hcsr04_trigger();
    
    /* Wait for falling edge or timeout */
    while (!echo_done) {
        uint16_t w;
        for (w = 0; w < 2000; w++) { _nop_(); }   /* ~1ms delay */
        timeout_cnt++;
        if (timeout_cnt > HCSR04_TIMEOUT_MS) {
            EX0 = 0;
            echo_timeout = 1;
            return HCSR04_TIMEOUT;
        }
    }
    
    EX0 = 0;
    return HCSR04_OK;
}

/**
 * @brief  Get distance in cm from last successful measurement
 *         Distance = (pulse_width_ms * 34cm/ms) / 2 = pulse_width * 17
 */
uint16_t hcsr04_get_distance(void)
{
    uint16_t dist;
    
    if (echo_timeout || echo_width == 0) {
        return 0;
    }
    
    /* echo_width is in 1ms ticks.
       Speed of sound ≈ 343 m/s = 34.3 cm/ms.
       Round trip, so distance = (width_ms * 34.3) / 2 ≈ width * 17.15
       For better accuracy: dist = (width * 343 + 10) / 20   (extra +10 for rounding) */
    dist = (uint16_t)(((uint32_t)echo_width * 343 + 10) / 20);
    
    return dist;   /* cm */
}
