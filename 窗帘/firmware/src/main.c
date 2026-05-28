/**
 * @file    main.c
 * @brief   SHADE — Smart Home Adaptive Daylight Environment
 *          STC89C52 Firmware Entry Point
 * 
 * System Overview:
 *   Timer0 ISR (1ms):
 *     - System tick counter
 *     - Sample timer
 *     - Key debounce
 *     - Buzzer pattern
 *     - LED flashing
 *     - Serial TX driver
 * 
 *   INT0 ISR:
 *     - HC-SR04 ECHO edge capture
 * 
 *   Main Loop:
 *     - Serial RX processing (protocol parser)
 *     - Frame dispatch
 *     - Sensor reading (1Hz)
 *     - PID computation
 *     - Motor control
 *     - LCD update
 *     - Alert detection
 */

#include "config.h"
#include "i2c.h"
#include "bme280.h"
#include "bh1750.h"
#include "hcsr04.h"
#include "pcf8591.h"
#include "stepper.h"
#include "pid.h"
#include "lcd1602.h"

/*===========================================================================
 * GLOBAL DATA
 *===========================================================================*/

/* Register map (in xdata for space) */
uint8_t xdata g_reg_map[REG_MAP_SIZE];

/* UART buffers (in idata for fast access) */
uint8_t idata g_uart_rx_buf[UART_RX_BUF_SIZE];
uint8_t idata g_uart_tx_buf[UART_TX_BUF_SIZE];
volatile uint8_t g_uart_rx_head = 0;
volatile uint8_t g_uart_rx_tail = 0;
volatile uint8_t g_uart_tx_len = 0;
volatile uint8_t g_uart_tx_done = 1;

/* System tick (1ms increment, wraps at 65535) */
volatile uint16_t g_sys_ticks = 0;

/*===========================================================================
 * APPLICATION STATE
 *===========================================================================*/

volatile uint8_t sample_tick = 0;          /* Count-up to sample period (ISR accessible) */
static uint8_t  lcd_page = LCD_PAGE_MAIN;
static uint16_t lcd_page_timer = 0;       /* Auto-scroll timer */
static uint8_t  report_seq = 0;           /* Rolling sequence for DATA_REPORT */

/* Sensor data cache */
static bme280_data_t bme_val;
static uint16_t      bh1750_lux;
static uint16_t      pcf8591_mv;
static uint16_t      hcsr04_dist;
static uint8_t       sensors_ok = 0;      /* Bitmask of working sensors */

/* PID instance (light → motor angle) */
static pid_t light_pid;

/* Alert state (prevents repeated triggers) */
static uint8_t alert_cooldown = 0;
static uint8_t last_alert_type = 0;

/*===========================================================================
 * FORWARD DECLARATIONS
 *===========================================================================*/

static void sensors_read_all(void);
static void sensors_to_registers(void);
static void pid_control_step(void);
static void alert_check(void);
static void lcd_update(void);
static void key_process(void);
static void buzzer_update(void);
static void motor_from_register(void);

/*===========================================================================
 * CALLBACKS (required by idacs_lite.h)
 *===========================================================================*/

void app_on_report_trigger(void)
{
    sensors_read_all();
    sensors_to_registers();
    report_seq = (report_seq + 1) & 0xFF;
    idacs_build_report(report_seq);
    uart_send_frame();
}

void app_on_alert(uint8_t type, uint16_t value)
{
    /* Handled in alert_check() */
}

void app_on_heartbeat(void)
{
    /* Link is alive */
    LED_RUN = 1;
}

/*===========================================================================
 * MAIN
 *===========================================================================*/

void main(void)
{
    uint8_t i;
    
    /* Disable all interrupts during init */
    EA = 0;
    
    /* Init hardware modules */
    uart_init();
    timer0_init();
    reg_init();
    
    /* Init I2C bus and sensors */
    i2c_init();
    
    if (bme280_init() == BME280_OK) {
        sensors_ok |= 0x01;
    }
    if (bh1750_init() == BH1750_OK) {
        sensors_ok |= 0x02;
    }
    if (hcsr04_init() == HCSR04_OK) {
        sensors_ok |= 0x04;
    }
    if (pcf8591_init() == PCF8591_OK) {
        sensors_ok |= 0x08;
    }
    
    /* Init actuators */
    stepper_init();
    stepper_set_speed(reg_read(REG_MOTOR_SPEED));
    
    /* Init PID */
    pid_init(&light_pid);
    pid_set_setpoint(&light_pid, (int16_t)reg_read16(REG_LIGHT_THRESH));
    pid_set_gains(&light_pid, (int16_t)reg_read16(REG_PID_KP),
                               (int16_t)reg_read16(REG_PID_KI),
                               (int16_t)reg_read16(REG_PID_KD));
    
    /* Init UI */
    lcd_init();
    lcd_set_cursor(0, 0);
    lcd_write_string(" SHADE v1.0");
    lcd_set_cursor(1, 0);
    lcd_write_string(" Init OK 4Ch");
    
    /* Start-up delay (show splash) */
    for (i = 0; i < 100; i++) {
        uint16_t w;
        for (w = 0; w < 10000; w++) { _nop_(); }
    }
    
    lcd_clear();
    LED_RUN = 1;
    
    /* Enable interrupts */
    EA = 1;
    
    /* Indicators */
    LED_MOTOR = 0;
    LED_ALARM = 0;
    
    /* ========================================
     * MAIN LOOP
     * ======================================== */
    while (1) {
        /* --- Protocol RX Processing --- */
        {
            uint8_t rx_byte;
            while (g_uart_rx_head != g_uart_rx_tail) {
                rx_byte = g_uart_rx_buf[g_uart_rx_tail];
                g_uart_rx_tail = (g_uart_rx_tail + 1) & (UART_RX_BUF_SIZE - 1);
                
                idacs_frame_t frame;
                if (idacs_parse_byte(rx_byte, &frame)) {
                    if (frame.error == IDACS_ERR_NONE) {
                        idacs_dispatch(&frame);
                    } else {
                        /* CRC error — send NACK */
                        idacs_build_nack(frame.seq, IDACS_ERR_CRC, 0);
                        uart_send_frame();
                    }
                }
            }
        }
        
        /* --- Periodic Tasks (driven by 1ms timer flags) --- */
        
        /* Sensor sampling */
        if (sample_tick >= reg_read(REG_SAMPLE_PERIOD)) {
            sample_tick = 0;
            
            sensors_read_all();
            sensors_to_registers();
            alert_check();
            pid_control_step();
            motor_from_register();
            
            /* Auto-report via serial */
            report_seq = (report_seq + 1) & 0xFF;
            if (idacs_build_report(report_seq)) {
                uart_send_frame();
            }
        }
        
        /* LCD update (every 500ms) */
        if ((g_sys_ticks % 500) == 0) {
            lcd_update();
        }
        
        /* Key scanning */
        key_process();
        
        /* Alert cooldown */
        if (alert_cooldown > 0) {
            alert_cooldown--;
        }
    }
}

/*===========================================================================
 * SENSOR READING
 *===========================================================================*/

static void sensors_read_all(void)
{
    /* BME280: Temperature + Humidity + Pressure */
    if (sensors_ok & 0x01) {
        bme280_read(&bme_val);
    }
    
    /* BH1750: Digital light */
    if (sensors_ok & 0x02) {
        bh1750_lux = bh1750_read_lux();
    }
    
    /* HC-SR04: Distance */
    if (sensors_ok & 0x04) {
        if (hcsr04_measure() == HCSR04_OK) {
            hcsr04_dist = hcsr04_get_distance();
        }
    }
    
    /* PCF8591: Analog voltage */
    if (sensors_ok & 0x08) {
        pcf8591_mv = pcf8591_read_mv(PCF8591_AIN0);
    }
}

/**
 * @brief  Copy sensor readings into register map
 *         Format: each channel = 2 bytes, big-endian, value x10
 */
static void sensors_to_registers(void)
{
    uint8_t  base = REG_SENSOR_BASE;
    uint16_t val;
    
    /* CH0: Temperature (x10) */
    val = (uint16_t)(bme_val.temperature / 10);  /* x100 → x10 */
    g_reg_map[base + 0] = (uint8_t)(val >> 8);
    g_reg_map[base + 1] = (uint8_t)(val & 0xFF);
    
    /* CH1: Humidity (x10) */
    val = (uint16_t)(bme_val.humidity / 10);     /* x100 → x10 */
    g_reg_map[base + 2] = (uint8_t)(val >> 8);
    g_reg_map[base + 3] = (uint8_t)(val & 0xFF);
    
    /* CH2: Pressure (hPa x10) */
    val = (uint16_t)(bme_val.pressure / 10);
    g_reg_map[base + 4] = (uint8_t)(val >> 8);
    g_reg_map[base + 5] = (uint8_t)(val & 0xFF);
    
    /* CH3: BH1750 light (lux) */
    g_reg_map[base + 6] = (uint8_t)(bh1750_lux >> 8);
    g_reg_map[base + 7] = (uint8_t)(bh1750_lux & 0xFF);
    
    /* CH4: Analog voltage (mV) */
    g_reg_map[base + 8]  = (uint8_t)(pcf8591_mv >> 8);
    g_reg_map[base + 9]  = (uint8_t)(pcf8591_mv & 0xFF);
    
    /* CH5: Distance (cm) */
    g_reg_map[base + 10] = (uint8_t)(hcsr04_dist >> 8);
    g_reg_map[base + 11] = (uint8_t)(hcsr04_dist & 0xFF);
    
    /* CH6: Motor position (x10) */
    val = stepper_get_position();
    g_reg_map[base + 12] = (uint8_t)(val >> 8);
    g_reg_map[base + 13] = (uint8_t)(val & 0xFF);
    
    /* CH7: Reserved */
    g_reg_map[base + 14] = 0;
    g_reg_map[base + 15] = 0;
    
    /* Update system status */
    {
        uint8_t status = SYS_STAT_BOOT_OK;
        if (reg_read(REG_SYS_STATUS) & SYS_STAT_ALARM) {
            status |= SYS_STAT_ALARM;
        }
        if (stepper_is_running()) {
            status |= SYS_STAT_MOTOR_RUN;
        }
        g_reg_map[REG_SYS_STATUS] = status;
    }
    
    /* Update uptime */
    g_reg_map[REG_UPTIME]     = (uint8_t)(g_sys_ticks >> 8);
    g_reg_map[REG_UPTIME + 1] = (uint8_t)(g_sys_ticks & 0xFF);
}

/*===========================================================================
 * PID CONTROL (Light → Motor)
 *===========================================================================*/

static void pid_control_step(void)
{
    int16_t output;
    uint8_t alert_mask = reg_read(REG_ALERT_MASK);
    
    if (!(alert_mask & ALERT_LIGHT)) {
        /* Light alert disabled, skip PID */
        return;
    }
    
    if (!(sensors_ok & 0x02)) {
        /* BH1750 not available */
        return;
    }
    
    /* Check if collision avoidance is active */
    if (hcsr04_dist > 0 && hcsr04_dist < reg_read16(MOTOR_TARGET) / 100 + DEFAULT_DIST_THRESH) {
        /* Too close, don't move motor toward obstacle */
        return;
    }
    
    /* Run PID */
    output = pid_compute(&light_pid, (int16_t)bh1750_lux);
    
    /* Update motor target register */
    reg_write16(REG_MOTOR_TARGET, (uint16_t)output);
}

/*===========================================================================
 * MOTOR CONTROL FROM REGISTER
 *===========================================================================*/

static void motor_from_register(void)
{
    int16_t target = (int16_t)reg_read16(REG_MOTOR_TARGET);
    
    if (target < 0) target = 0;
    if (target > MOTOR_MAX_ANGLE) target = MOTOR_MAX_ANGLE;
    
    if (target != (int16_t)stepper_get_position()) {
        stepper_set_speed(reg_read(REG_MOTOR_SPEED));
        stepper_move_to(target);
    }
}

/*===========================================================================
 * ALERT DETECTION
 *===========================================================================*/

static void alert_check(void)
{
    uint8_t  alert_mask = reg_read(REG_ALERT_MASK);
    uint16_t temp_thresh = reg_read16(REG_TEMP_THRESH);
    uint16_t humi_thresh = reg_read16(REG_HUMI_THRESH);
    uint16_t temp_val = (uint16_t)(bme_val.temperature / 10);
    uint16_t humi_val = (uint16_t)(bme_val.humidity / 10);
    
    /* Temperature alert */
    if ((alert_mask & ALERT_TEMP) && temp_val > temp_thresh) {
        if (alert_cooldown == 0 || last_alert_type != EVT_TEMP_OVER) {
            idacs_build_alert(EVT_TEMP_OVER, temp_val, report_seq);
            uart_send_frame();
            alert_cooldown = 50;   /* 50 * 1s = 50 second cooldown */
            last_alert_type = EVT_TEMP_OVER;
            reg_write(REG_SYS_STATUS, reg_read(REG_SYS_STATUS) | SYS_STAT_ALARM);
        }
    }
    
    /* Humidity alert */
    if ((alert_mask & ALERT_HUMI) && humi_val > humi_thresh) {
        if (alert_cooldown == 0 || last_alert_type != EVT_HUMI_OVER) {
            idacs_build_alert(EVT_HUMI_OVER, humi_val, report_seq);
            uart_send_frame();
            alert_cooldown = 50;
            last_alert_type = EVT_HUMI_OVER;
            reg_write(REG_SYS_STATUS, reg_read(REG_SYS_STATUS) | SYS_STAT_ALARM);
        }
    }
    
    /* Distance alert (collision avoidance) */
    if ((alert_mask & ALERT_DIST) && hcsr04_dist > 0 && hcsr04_dist < DEFAULT_DIST_THRESH) {
        if (alert_cooldown == 0 || last_alert_type != EVT_COLLISION) {
            idacs_build_alert(EVT_COLLISION, hcsr04_dist, report_seq);
            uart_send_frame();
            alert_cooldown = 20;   /* 20 second cooldown for collision */
            last_alert_type = EVT_COLLISION;
            stepper_emergency_stop();
            reg_write(REG_SYS_STATUS, reg_read(REG_SYS_STATUS) | SYS_STAT_ALARM);
        }
    }
    
    /* Clear alarm if all conditions resolved */
    if (temp_val <= temp_thresh && humi_val <= humi_thresh &&
        (hcsr04_dist == 0 || hcsr04_dist >= DEFAULT_DIST_THRESH)) {
        reg_write(REG_SYS_STATUS, reg_read(REG_SYS_STATUS) & ~SYS_STAT_ALARM);
        LED_ALARM = 0;
    }
}

/*===========================================================================
 * LCD UPDATE
 *===========================================================================*/

static void lcd_update(void)
{
    uint16_t temp_x10  = (uint16_t)(bme_val.temperature / 10);
    uint16_t humi_x10  = (uint16_t)(bme_val.humidity / 10);
    uint16_t motor_pos = stepper_get_position();
    uint8_t  alert = reg_read(REG_SYS_STATUS) & SYS_STAT_ALARM;
    
    /* Auto-scroll pages every 3 seconds */
    lcd_page_timer += 500;
    if (lcd_page_timer >= 3000) {
        lcd_page_timer = 0;
        lcd_page = (lcd_page + 1) % LCD_PAGE_COUNT;
    }
    
    switch (lcd_page) {
    case LCD_PAGE_MAIN:
        lcd_set_cursor(0, 0);
        lcd_write_fixed(0, 0, (int16_t)temp_x10, 1);
        lcd_write_data('C');
        lcd_write_data(' ');
        lcd_write_fixed(0, 7, (int16_t)humi_x10, 1);
        lcd_write_data('%');
        
        lcd_set_cursor(1, 0);
        lcd_write_int(1, 0, (int16_t)bh1750_lux, 5);
        lcd_write_string("lx ");
        if (hcsr04_dist > 0) {
            lcd_write_int(1, 9, (int16_t)hcsr04_dist, 3);
            lcd_write_data('c');
            lcd_write_data('m');
        } else {
            lcd_write_string("---cm");
        }
        break;
        
    case LCD_PAGE_DETAIL:
        lcd_set_cursor(0, 0);
        lcd_write_string("P:");
        lcd_write_int(0, 2, (int16_t)(bme_val.pressure / 100), 5);
        lcd_write_data('h');
        lcd_write_string("Pa   ");
        
        lcd_set_cursor(1, 0);
        lcd_write_string("A:");
        lcd_write_int(1, 2, (int16_t)pcf8591_mv, 4);
        lcd_write_string("mV");
        lcd_write_string(" M:");
        lcd_write_int(1, 10, (int16_t)(motor_pos / 10), 3);
        lcd_write_data(0xDF);   /* Degree symbol */
        break;
        
    case LCD_PAGE_THRESH:
        lcd_set_cursor(0, 0);
        lcd_write_string("TH:");
        lcd_write_fixed(0, 3, (int16_t)reg_read16(REG_TEMP_THRESH), 1);
        lcd_write_data('C');
        lcd_write_string(" L:");
        lcd_write_int(0, 9, (int16_t)reg_read16(REG_LIGHT_THRESH), 4);
        
        lcd_set_cursor(1, 0);
        lcd_write_string("KP:");
        lcd_write_fixed(1, 3, (int16_t)reg_read16(REG_PID_KP), 2);
        lcd_write_string(" ");
        lcd_write_string(alert ? "!ALARM!" : " OK    ");
        break;
    }
}

/*===========================================================================
 * KEY PROCESSING
 *===========================================================================*/

static void key_process(void)
{
    static uint16_t key_timer = 0;
    static uint8_t  prev_keys = 0xFF;
    uint8_t keys;
    
    /* Only scan every 20ms for debounce */
    if ((uint16_t)(g_sys_ticks - key_timer) < 20) return;
    key_timer = g_sys_ticks;
    
    keys = (uint8_t)(KEY_MODE ? 0x01 : 0) |
           (uint8_t)(KEY_UP   ? 0x02 : 0) |
           (uint8_t)(KEY_DOWN ? 0x04 : 0);
    
    /* Detect key release (falling edge) */
    if (prev_keys != 0xFF && keys == 0xFF) {
        /* No action on release currently */
    }
    
    /* Detect key press (rising edge → key was 0, now 1, meaning not pressed → pressed) */
    /* On A2 board, keys are active-low (pressed = 0). We invert for logic. */
    if ((prev_keys & 0x01) && !(keys & 0x01)) {
        /* MODE key pressed: cycle LCD page */
        lcd_page = (lcd_page + 1) % LCD_PAGE_COUNT;
        lcd_page_timer = 0;
        lcd_clear();
    }
    
    if ((prev_keys & 0x02) && !(keys & 0x02)) {
        /* UP key pressed */
        if (lcd_page == LCD_PAGE_THRESH) {
            /* Increase light threshold */
            uint16_t th = reg_read16(REG_LIGHT_THRESH);
            th += 50;
            if (th > 5000) th = 5000;
            reg_write16(REG_LIGHT_THRESH, th);
            pid_set_setpoint(&light_pid, (int16_t)th);
        }
    }
    
    if ((prev_keys & 0x04) && !(keys & 0x04)) {
        /* DOWN key pressed */
        if (lcd_page == LCD_PAGE_THRESH) {
            /* Decrease light threshold */
            uint16_t th = reg_read16(REG_LIGHT_THRESH);
            if (th >= 50) th -= 50;
            else th = 0;
            reg_write16(REG_LIGHT_THRESH, th);
            pid_set_setpoint(&light_pid, (int16_t)th);
        }
    }
    
    prev_keys = keys;
}

/*===========================================================================
 * BUZZER CONTROL
 *===========================================================================*/

static void buzzer_update(void)
{
    uint8_t ctrl = reg_read(REG_BUZZER_CTRL);
    uint8_t alert = reg_read(REG_SYS_STATUS) & SYS_STAT_ALARM;
    
    if (alert) {
        /* Override: alarm active → fast beep */
        BUZZER = (g_sys_ticks & 0x80) ? 1 : 0;   /* ~4Hz */
        return;
    }
    
    switch (ctrl) {
    case BUZZER_OFF:
        BUZZER = 0;
        break;
    case BUZZER_SLOW:
        BUZZER = (g_sys_ticks & 0x200) ? 1 : 0;  /* ~2Hz */
        break;
    case BUZZER_FAST:
        BUZZER = (g_sys_ticks & 0x40) ? 1 : 0;   /* ~8Hz */
        break;
    default:
        BUZZER = 0;
        break;
    }
}

/*===========================================================================
 * TIMER0 ISR (1ms System Tick)
 *===========================================================================*/

void timer0_isr(void) interrupt 1
{
    /* Reload for 1ms @ 11.0592MHz */
    TH0 = 0xFC;
    TL0 = 0x66;
    
    g_sys_ticks++;
    
    /* Sample timer (counts up to SAMPLE_PERIOD seconds) */
    if (g_sys_ticks % 1000 == 0) {
        sample_tick++;
    }
    
    /* Buzzer pattern */
    buzzer_update();
    
    /* LED flashing */
    {
        static uint16_t led_toggle = 0;
        led_toggle++;
        if (led_toggle >= 500) {
            led_toggle = 0;
            LED_RUN = !LED_RUN;
        }
    }
}
