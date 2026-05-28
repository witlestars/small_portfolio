/**
 * @file    idacs_lite.h
 * @brief   IDACS Lite Protocol Stack for STC89C52
 * @author  SHADE Project
 * @version 1.0
 * 
 * IDACS (Intelligent Data Acquisition & Control System) Lite
 * 面向 8-bit MCU 的轻量二进制采集控制协议
 * 
 * Frame Format:
 * ┌──────┬──────┬──────┬──────┬──────┬─────────────┬─────────┐
 * │ 0xAA │ 0x55 │ Len  │ Type │ Seq  │  Payload    │ CRC16   │
 * │ STX_H│ STX_L│ 1B   │ 1B   │ 1B   │  0~28 Bytes │ 2 Bytes │
 * └──────┴──────┴──────┴──────┴──────┴─────────────┴─────────┘
 */

#ifndef __IDACS_LITE_H__
#define __IDACS_LITE_H__

#include <STC89C5xRC.H>

/*===========================================================================
 * CONSTANTS
 *===========================================================================*/

/* Frame Structure Limits */
#define IDACS_STX_H         0xAA
#define IDACS_STX_L         0x55
#define IDACS_HEADER_SIZE   5
#define IDACS_CRC_SIZE      2
#define IDACS_MIN_FRAME     7
#define IDACS_MAX_FRAME     35
#define IDACS_MAX_PAYLOAD   28

/* UART Settings */
#define IDACS_BAUD          9600
#define IDACS_TIMER1_RELOAD 0xFD

/* Timing */
#define IDACS_ACK_TIMEOUT   200
#define IDACS_MAX_RETRY     3

/*===========================================================================
 * FRAME TYPE DEFINITIONS
 *===========================================================================*/

/* C51 -> Host */
#define IDACS_TYPE_DATA_REPORT   0x01
#define IDACS_TYPE_ACK           0x02
#define IDACS_TYPE_NACK          0x03
#define IDACS_TYPE_EVENT_ALERT   0x04

/* Host -> C51 */
#define IDACS_TYPE_WRITE_REG     0x10
#define IDACS_TYPE_READ_REG      0x11
#define IDACS_TYPE_HEARTBEAT     0x14

/*===========================================================================
 * ERROR CODES
 *===========================================================================*/

#define IDACS_ERR_NONE           0x00
#define IDACS_ERR_CRC            0x01
#define IDACS_ERR_REG_ADDR       0x02
#define IDACS_ERR_REG_RANGE      0x03
#define IDACS_ERR_PARAM          0x04
#define IDACS_ERR_BUSY           0x05
#define IDACS_ERR_UNKNOWN_CMD    0x06
#define IDACS_ERR_FRAME_LEN      0x07

/*===========================================================================
 * REGISTER MAP
 *===========================================================================*/

/* Register Addresses (0x00 ~ 0x29 = 42 bytes total) */
#define REG_DEVICE_ID       0x00  /* RO 2B */
#define REG_FW_VERSION      0x02  /* RO 2B */
#define REG_SYS_STATUS      0x04  /* RO 1B */
#define REG_ERROR_CODE      0x05  /* RO 1B */
#define REG_TEMP_THRESH     0x06  /* RW 2B  temperature x10 */
#define REG_HUMI_THRESH     0x08  /* RW 2B  humidity x10 */
#define REG_LIGHT_THRESH    0x0A  /* RW 2B  light (lux) */
#define REG_MOTOR_TARGET    0x0C  /* RW 2B  motor angle x10 */
#define REG_MOTOR_SPEED     0x0E  /* RW 1B  speed 1~10 */
#define REG_BUZZER_CTRL     0x0F  /* RW 1B  0=off 1=slow 2=fast */
#define REG_PID_KP          0x10  /* RW 2B  Kp x100 */
#define REG_PID_KI          0x12  /* RW 2B  Ki x100 */
#define REG_PID_KD          0x14  /* RW 2B  Kd x100 */
#define REG_SAMPLE_PERIOD   0x16  /* RW 1B  seconds */
#define REG_ALERT_MASK      0x17  /* RW 1B  bit mask */
#define REG_UPTIME          0x18  /* RO 2B  seconds */
#define REG_SENSOR_BASE     0x1A  /* RO 16B 8ch x2B */

#define REG_MAP_SIZE        0x2A

/* Sensor Channel Indices */
#define SENSOR_CH_TEMP      0
#define SENSOR_CH_HUMI      1
#define SENSOR_CH_PRESS     2
#define SENSOR_CH_LIGHT     3
#define SENSOR_CH_ANALOG    4
#define SENSOR_CH_DIST      5
#define SENSOR_CH_MOTOR     6
#define SENSOR_CH_RESERVED  7

/* SYS_STATUS Bit Flags */
#define SYS_STAT_ALARM      0x01
#define SYS_STAT_MOTOR_RUN  0x02
#define SYS_STAT_BOOT_OK    0x80

/* ALERT_MASK Bit Flags */
#define ALERT_TEMP          0x01
#define ALERT_HUMI          0x02
#define ALERT_LIGHT         0x04
#define ALERT_DIST          0x08
#define ALERT_PRESSURE      0x10

/* Event Alert Types */
#define EVT_TEMP_OVER       0x01
#define EVT_HUMI_OVER       0x02
#define EVT_LIGHT_OVER      0x03
#define EVT_DIST_NEAR       0x04
#define EVT_PRESSURE_DROP   0x05
#define EVT_COLLISION       0x06

/* BUZZER_CTRL */
#define BUZZER_OFF          0
#define BUZZER_SLOW         1
#define BUZZER_FAST         2

/* Device Identity */
#define DEVICE_ID_VALUE     0xDA01

/*===========================================================================
 * DATA TYPES
 *===========================================================================*/

typedef unsigned char  uint8_t;
typedef unsigned int   uint16_t;
typedef unsigned long  uint32_t;
typedef signed char    int8_t;
typedef signed int     int16_t;

/* Parsed Frame */
typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t payload_len;
    uint8_t payload[IDACS_MAX_PAYLOAD];
    uint16_t crc;
    uint8_t error;
} idacs_frame_t;

/* RX State Machine */
typedef enum {
    IDACS_STATE_IDLE = 0,
    IDACS_STATE_RECV_STX1,
    IDACS_STATE_RECV_LEN,
    IDACS_STATE_RECV_TYPE,
    IDACS_STATE_RECV_SEQ,
    IDACS_STATE_RECV_PAYLOAD,
    IDACS_STATE_RECV_CRC1,
    IDACS_STATE_RECV_CRC2,
    IDACS_STATE_COMPLETE
} idacs_state_t;

/*===========================================================================
 * GLOBAL DATA
 *===========================================================================*/

extern uint8_t xdata g_reg_map[REG_MAP_SIZE];

#define UART_RX_BUF_SIZE    64
#define UART_TX_BUF_SIZE    48

extern uint8_t idata        g_uart_rx_buf[UART_RX_BUF_SIZE];
extern volatile uint8_t     g_uart_rx_head;
extern volatile uint8_t     g_uart_rx_tail;

extern uint8_t idata        g_uart_tx_buf[UART_TX_BUF_SIZE];
extern volatile uint8_t     g_uart_tx_len;
extern volatile uint8_t     g_uart_tx_done;

/*===========================================================================
 * API
 *===========================================================================*/

/* CRC */
uint16_t crc16_ccitt(const uint8_t *data, uint8_t len);
uint16_t crc16_byte(uint16_t crc, uint8_t byte);

/* Frame Builders */
uint8_t idacs_build_report(uint8_t seq);
uint8_t idacs_build_ack(uint8_t ack_seq, uint8_t seq);
uint8_t idacs_build_nack(uint8_t nack_seq, uint8_t err_code, uint8_t seq);
uint8_t idacs_build_alert(uint8_t alert_type, uint16_t value, uint8_t seq);
void    idacs_build_raw(uint8_t *buf, uint8_t type, uint8_t seq,
                        const uint8_t *payload, uint8_t len);

/* Frame Parser */
uint8_t idacs_parse_byte(uint8_t byte, idacs_frame_t *frame);

/* Dispatch */
void idacs_dispatch(idacs_frame_t *frame);

/* Register Access */
uint8_t  reg_read(uint8_t addr);
void     reg_write(uint8_t addr, uint8_t value);
uint16_t reg_read16(uint8_t addr);
void     reg_write16(uint8_t addr, uint16_t value);
void     reg_init(void);

/* UART */
void uart_init(void);
void uart_send_byte(uint8_t byte);
void uart_send_buffer(const uint8_t *buf, uint8_t len);
void uart_send_frame(void);

/* Timer */
void timer0_init(void);
void timer1_init(void);

/* App Callbacks (defined in main.c) */
extern void app_on_report_trigger(void);
extern void app_on_alert(uint8_t type, uint16_t value);
extern void app_on_heartbeat(void);

#endif /* __IDACS_LITE_H__ */
