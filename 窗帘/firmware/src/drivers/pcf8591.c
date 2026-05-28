/**
 * @file    pcf8591.c
 * @brief   PCF8591 8-bit ADC Driver
 * 
 * I2C address: 0x48 (A0=A1=A2=GND)
 * Reference voltage: 5V (from A2 board)
 * Conversion: 8-bit = 0~255 → 0~5000mV
 * 
 * Note: PCF8591 requires a dummy read to get the current value,
 *       then a second read to get the next conversion result.
 *       We read twice and keep the second value.
 */

#include "pcf8591.h"
#include "i2c.h"

uint8_t pcf8591_init(void)
{
    uint8_t ack;
    
    /* Verify device presence */
    i2c_start();
    ack  = i2c_write((PCF8591_ADDR << 1) | 0x00);
    i2c_stop();
    
    return ack ? PCF8591_ERR_I2C : PCF8591_OK;
}

/**
 * @brief  Read ADC raw value (0~255)
 * @param  channel  AIN0~AIN3 (0x00~0x03)
 * 
 * PCF8591 protocol:
 *   1. Write control byte: channel + auto-increment off + 4-channel mode
 *   2. Read 2 bytes: first = previous conversion, second = current channel
 *   3. Return second byte
 */
uint8_t pcf8591_read_adc(uint8_t channel)
{
    uint8_t val;
    
    /* Control byte: [0]channel[0]autoinc[0]00
       bit7-6: analog output enable (00 = off)
       bit5-4: channel select (00=A0, 01=A1, 10=A2, 11=A3)
       bit3: auto-increment (0=off)
       bit2: 0
       bit1-0: analog input mode (00 = 4 single-ended) */
    uint8_t ctrl = 0x00 | (channel << 4);
    
    i2c_start();
    i2c_write((PCF8591_ADDR << 1) | 0x00);   /* Write address */
    i2c_write(ctrl);                           /* Control byte */
    
    i2c_start();                               /* Repeated START */
    i2c_write((PCF8591_ADDR << 1) | 0x01);    /* Read address */
    
    i2c_read(1);            /* Dummy read: previous conversion, send ACK */
    val = i2c_read(0);      /* Real read: current channel, send NACK */
    
    i2c_stop();
    
    return val;
}

/**
 * @brief  Convert ADC raw value to mV
 *         Vref = 5V = 5000mV, ADC = 8-bit = 0~255
 *         mV = raw * 5000 / 255 ≈ raw * 19.608
 *         Integer approx: raw * 196 / 10
 */
uint16_t pcf8591_read_mv(uint8_t channel)
{
    uint8_t raw;
    uint16_t mv;
    
    raw = pcf8591_read_adc(channel);
    mv = (uint16_t)(((uint32_t)raw * 5000 + 127) / 255);   /* +127 for rounding */
    
    return mv;
}
