/**
 * @file    idacs_lite.c
 * @brief   IDACS Lite Protocol Stack - Implementation
 * 
 * 负责帧组包、解包（状态机）、命令分发、CRC校验、
 * 寄存器读写。本模块无硬件依赖，纯逻辑。
 */

#include "idacs_lite.h"
#include <string.h>

/*===========================================================================
 * CRC-16/CCITT 查表法
 * Polynomial: x^16 + x^12 + x^5 + 1 (0x1021)
 *===========================================================================*/

static code const uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

uint16_t crc16_ccitt(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFF;
    uint8_t i;
    for (i = 0; i < len; i++) {
        crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF]);
    }
    return crc;
}

uint16_t crc16_byte(uint16_t crc, uint8_t byte)
{
    crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ byte) & 0xFF]);
    return crc;
}

/*===========================================================================
 * FRAME ASSEMBLY
 *===========================================================================*/

static volatile uint8_t *tx_ptr;

static uint8_t idacs_commit_frame(void)
{
    uint8_t payload_len = g_uart_tx_buf[5]; /* build_raw暂存 */
    uint16_t crc;
    uint8_t total_len;
    
    total_len = IDACS_HEADER_SIZE + payload_len + IDACS_CRC_SIZE;
    if (total_len > UART_TX_BUF_SIZE) return 0;
    
    crc = crc16_ccitt(&g_uart_tx_buf[3], 2 + payload_len);
    
    g_uart_tx_buf[total_len - 2] = (uint8_t)(crc >> 8);
    g_uart_tx_buf[total_len - 1] = (uint8_t)(crc & 0xFF);
    g_uart_tx_buf[2] = (uint8_t)(total_len & 0xFF);
    
    g_uart_tx_len = total_len;
    g_uart_tx_done = 0;
    tx_ptr = g_uart_tx_buf;
    return total_len;
}

uint8_t idacs_build_report(uint8_t seq)
{
    uint8_t ch;
    uint8_t payload[16];
    for (ch = 0; ch < 8; ch++) {
        uint16_t val = reg_read16(REG_SENSOR_BASE + ch * 2);
        payload[ch * 2]     = (uint8_t)(val >> 8);
        payload[ch * 2 + 1] = (uint8_t)(val & 0xFF);
    }
    idacs_build_raw(g_uart_tx_buf, IDACS_TYPE_DATA_REPORT, seq, payload, 16);
    return idacs_commit_frame();
}

uint8_t idacs_build_ack(uint8_t ack_seq, uint8_t seq)
{
    uint8_t payload[1];
    payload[0] = ack_seq;
    idacs_build_raw(g_uart_tx_buf, IDACS_TYPE_ACK, seq, payload, 1);
    return idacs_commit_frame();
}

uint8_t idacs_build_nack(uint8_t nack_seq, uint8_t err_code, uint8_t seq)
{
    uint8_t payload[2];
    payload[0] = nack_seq;
    payload[1] = err_code;
    idacs_build_raw(g_uart_tx_buf, IDACS_TYPE_NACK, seq, payload, 2);
    return idacs_commit_frame();
}

uint8_t idacs_build_alert(uint8_t alert_type, uint16_t value, uint8_t seq)
{
    uint8_t payload[3];
    payload[0] = alert_type;
    payload[1] = (uint8_t)(value >> 8);
    payload[2] = (uint8_t)(value & 0xFF);
    idacs_build_raw(g_uart_tx_buf, IDACS_TYPE_EVENT_ALERT, seq, payload, 3);
    return idacs_commit_frame();
}

void idacs_build_raw(uint8_t *buf, uint8_t type, uint8_t seq,
                     const uint8_t *payload, uint8_t len)
{
    uint8_t i;
    buf[0] = IDACS_STX_H;
    buf[1] = IDACS_STX_L;
    buf[3] = type;
    buf[4] = seq;
    buf[5] = len;
    for (i = 0; i < len; i++) {
        buf[IDACS_HEADER_SIZE + i] = payload[i];
    }
}

/*===========================================================================
 * FRAME PARSER (Stateless Byte-at-a-time State Machine)
 *===========================================================================*/

uint8_t idacs_parse_byte(uint8_t byte, idacs_frame_t *frame)
{
    static idacs_state_t state = IDACS_STATE_IDLE;
    static uint8_t payload_idx;
    static uint8_t frame_len;
    static uint16_t calc_crc;
    
    switch (state) {
    case IDACS_STATE_IDLE:
        if (byte == IDACS_STX_H) {
            state = IDACS_STATE_RECV_STX1;
        }
        break;
        
    case IDACS_STATE_RECV_STX1:
        if (byte == IDACS_STX_L) {
            state = IDACS_STATE_RECV_LEN;
        } else if (byte != IDACS_STX_H) {
            state = IDACS_STATE_IDLE;
        }
        break;
        
    case IDACS_STATE_RECV_LEN:
        if (byte < IDACS_MIN_FRAME || byte > IDACS_MAX_FRAME) {
            state = IDACS_STATE_IDLE;
            break;
        }
        frame_len = byte;
        state = IDACS_STATE_RECV_TYPE;
        break;
        
    case IDACS_STATE_RECV_TYPE:
        frame->type = byte;
        calc_crc = crc16_byte(0xFFFF, byte);
        state = IDACS_STATE_RECV_SEQ;
        break;
        
    case IDACS_STATE_RECV_SEQ:
        frame->seq = byte;
        calc_crc = crc16_byte(calc_crc, byte);
        frame->payload_len = frame_len - IDACS_HEADER_SIZE - IDACS_CRC_SIZE;
        payload_idx = 0;
        if (frame->payload_len == 0) {
            state = IDACS_STATE_RECV_CRC1;
        } else {
            state = IDACS_STATE_RECV_PAYLOAD;
        }
        break;
        
    case IDACS_STATE_RECV_PAYLOAD:
        frame->payload[payload_idx] = byte;
        calc_crc = crc16_byte(calc_crc, byte);
        payload_idx++;
        if (payload_idx >= frame->payload_len) {
            state = IDACS_STATE_RECV_CRC1;
        }
        break;
        
    case IDACS_STATE_RECV_CRC1:
        frame->crc = (uint16_t)byte << 8;
        state = IDACS_STATE_RECV_CRC2;
        break;
        
    case IDACS_STATE_RECV_CRC2:
        frame->crc |= byte;
        state = IDACS_STATE_IDLE;
        if (calc_crc == frame->crc) {
            frame->error = IDACS_ERR_NONE;
            return 1;
        } else {
            frame->error = IDACS_ERR_CRC;
            return 0;
        }
        
    case IDACS_STATE_COMPLETE:
    default:
        state = IDACS_STATE_IDLE;
        break;
    }
    return 0;
}

/*===========================================================================
 * FRAME DISPATCH
 *===========================================================================*/

void idacs_dispatch(idacs_frame_t *frame)
{
    uint8_t err_code = IDACS_ERR_NONE;
    
    switch (frame->type) {
    case IDACS_TYPE_WRITE_REG:
    {
        uint8_t addr  = frame->payload[0];
        uint8_t wlen  = frame->payload[1];
        uint8_t i;
        if (addr + wlen > REG_MAP_SIZE) {
            err_code = IDACS_ERR_REG_RANGE;
            break;
        }
        if (addr < 0x06) {
            err_code = IDACS_ERR_REG_RANGE;
            break;
        }
        for (i = 0; i < wlen; i++) {
            reg_write(addr + i, frame->payload[2 + i]);
        }
        idacs_build_ack(frame->seq, 0);
        uart_send_frame();
        return;
    }
    case IDACS_TYPE_READ_REG:
    {
        uint8_t addr  = frame->payload[0];
        uint8_t rlen  = frame->payload[1];
        uint8_t resp_payload[18];
        uint8_t i;
        if (rlen == 0 || addr + rlen > REG_MAP_SIZE) {
            err_code = IDACS_ERR_REG_RANGE;
            break;
        }
        resp_payload[0] = addr;
        resp_payload[1] = rlen;
        for (i = 0; i < rlen; i++) {
            resp_payload[2 + i] = reg_read(addr + i);
        }
        idacs_build_raw(g_uart_tx_buf, IDACS_TYPE_ACK, frame->seq, resp_payload, 2 + rlen);
        idacs_commit_frame();
        uart_send_frame();
        return;
    }
    case IDACS_TYPE_HEARTBEAT:
        idacs_build_ack(frame->seq, 0);
        uart_send_frame();
        app_on_heartbeat();
        return;
    default:
        err_code = IDACS_ERR_UNKNOWN_CMD;
        break;
    }
    if (err_code != IDACS_ERR_NONE) {
        reg_write(REG_ERROR_CODE, err_code);
        idacs_build_nack(frame->seq, err_code, 0);
        uart_send_frame();
    }
}

/*===========================================================================
 * REGISTER ACCESS
 *===========================================================================*/

void reg_init(void)
{
    uint8_t i;
    for (i = 0; i < REG_MAP_SIZE; i++) { g_reg_map[i] = 0; }
    reg_write16(REG_DEVICE_ID, DEVICE_ID_VALUE);
    reg_write16(REG_FW_VERSION, 0x0100);
    reg_write16(REG_TEMP_THRESH, 300);
    reg_write16(REG_HUMI_THRESH, 700);
    reg_write16(REG_LIGHT_THRESH, 800);
    reg_write16(REG_MOTOR_TARGET, 900);
    reg_write(REG_MOTOR_SPEED, 5);
    reg_write(REG_BUZZER_CTRL, BUZZER_OFF);
    reg_write16(REG_PID_KP, 200);
    reg_write16(REG_PID_KI, 30);
    reg_write16(REG_PID_KD, 10);
    reg_write(REG_SAMPLE_PERIOD, 1);
    reg_write(REG_ALERT_MASK, ALERT_TEMP | ALERT_LIGHT | ALERT_DIST);
    reg_write(REG_SYS_STATUS, SYS_STAT_BOOT_OK);
}

uint8_t reg_read(uint8_t addr)
{
    if (addr >= REG_MAP_SIZE) return 0;
    return g_reg_map[addr];
}

void reg_write(uint8_t addr, uint8_t value)
{
    if (addr >= REG_MAP_SIZE) return;
    g_reg_map[addr] = value;
}

uint16_t reg_read16(uint8_t addr)
{
    if (addr + 1 >= REG_MAP_SIZE) return 0;
    return ((uint16_t)g_reg_map[addr] << 8) | g_reg_map[addr + 1];
}

void reg_write16(uint8_t addr, uint16_t value)
{
    if (addr + 1 >= REG_MAP_SIZE) return;
    g_reg_map[addr]     = (uint8_t)(value >> 8);
    g_reg_map[addr + 1] = (uint8_t)(value & 0xFF);
}

/*===========================================================================
 * UART DRIVER
 *===========================================================================*/

void uart_init(void)
{
    TMOD &= 0x0F;
    TMOD |= 0x20;
    TH1 = IDACS_TIMER1_RELOAD;
    TL1 = IDACS_TIMER1_RELOAD;
    TR1 = 1;
    SCON = 0x50;
    ES = 1;
    g_uart_rx_head = g_uart_rx_tail = 0;
    g_uart_tx_len = 0;
    g_uart_tx_done = 1;
}

void uart_send_byte(uint8_t byte)
{
    SBUF = byte;
    while (!TI);
    TI = 0;
}

void uart_send_frame(void)
{
    if (g_uart_tx_len == 0) return;
    g_uart_tx_done = 0;
    TI = 1;
}

void uart_send_buffer(const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++) {
        SBUF = buf[i];
        while (!TI);
        TI = 0;
    }
}

void uart_isr(void) interrupt 4
{
    uint8_t next;
    if (RI) {
        RI = 0;
        next = (g_uart_rx_head + 1) & (UART_RX_BUF_SIZE - 1);
        if (next != g_uart_rx_tail) {
            g_uart_rx_buf[g_uart_rx_head] = SBUF;
            g_uart_rx_head = next;
        }
    }
    if (TI) {
        TI = 0;
        if (g_uart_tx_done) return;
        if (g_uart_tx_len > 0) {
            SBUF = *tx_ptr;
            tx_ptr++;
            g_uart_tx_len--;
        } else {
            g_uart_tx_done = 1;
        }
    }
}

/*===========================================================================
 * TIMER DRIVER
 *===========================================================================*/

void timer0_init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x01;
    TH0 = 0xFC;
    TL0 = 0x66;
    ET0 = 1;
    TR0 = 1;
}

void timer1_init(void)
{
    /* Already configured in uart_init() */
}
