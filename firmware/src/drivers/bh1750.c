/**
 * @file    bh1750.c
 * @brief   BH1750 Driver — One-Shot High Resolution Mode
 */

#include "bh1750.h"
#include "i2c.h"

uint8_t bh1750_init(void)
{
    uint8_t ack;
    
    /* Power on + set to continuous high-res to verify */
    i2c_start();
    ack = i2c_write((BH1750_ADDR << 1) | 0x00);
    if (ack) { i2c_stop(); return BH1750_ERR_I2C; }
    i2c_write(0x01);    /* Power ON */
    i2c_stop();
    
    return BH1750_OK;
}

uint16_t bh1750_read_lux(void)
{
    uint16_t raw;
    uint8_t hi, lo;
    
    /* Trigger one-shot high resolution measurement */
    i2c_start();
    i2c_write((BH1750_ADDR << 1) | 0x00);
    i2c_write(BH1750_ONCE_H_RES);
    i2c_stop();
    
    /* Wait 180ms for measurement (datasheet: max 180ms for H-Res mode) */
    {
        uint8_t i;
        for (i = 0; i < 180; i++) {
            uint16_t w;
            for (w = 0; w < 1000; w++) { _nop_(); }
        }
    }
    
    /* Read 2 bytes */
    i2c_start();
    i2c_write((BH1750_ADDR << 1) | 0x01);
    hi = i2c_read(1);   /* ACK */
    lo = i2c_read(0);   /* NACK */
    i2c_stop();
    
    raw = ((uint16_t)hi << 8) | lo;
    
    /* Convert: raw / 1.2 = lux (per datasheet for H-Res mode) */
    /* raw * 10 / 12 ≈ raw * 5 / 6 */
    return (uint16_t)(((uint32_t)raw * 5 + 3) / 6);
}
