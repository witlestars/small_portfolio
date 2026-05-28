/**
 * @file    i2c.c
 * @brief   Software I2C Master — Bit-bang Implementation
 * 
 * 时序参照 Philips I2C 标准 (100kHz Standard Mode)
 * SCL/SDA 通过 config.h 中的 sbit 定义
 */

#include "i2c.h"
#include "intrins.h"

/*===========================================================================
 * LOW-LEVEL HELPERS
 *===========================================================================*/

static void i2c_delay(void)
{
    uint8_t i = I2C_DELAY;
    while (i--) {
        _nop_();
        _nop_();
    }
}

static void i2c_scl_high(void) { I2C_SCL = 1; i2c_delay(); }
static void i2c_scl_low(void)  { I2C_SCL = 0; i2c_delay(); }

/*===========================================================================
 * INIT
 *===========================================================================*/

void i2c_init(void)
{
    I2C_SCL = 1;
    I2C_SDA = 1;
    i2c_delay();
}

/*===========================================================================
 * BUS CONTROL
 *===========================================================================*/

void i2c_start(void)
{
    I2C_SDA = 1;
    i2c_delay();
    I2C_SCL = 1;
    i2c_delay();
    I2C_SDA = 0;        /* SDA high→low while SCL high = START */
    i2c_delay();
    I2C_SCL = 0;
    i2c_delay();
}

void i2c_stop(void)
{
    I2C_SDA = 0;
    i2c_delay();
    I2C_SCL = 1;
    i2c_delay();
    I2C_SDA = 1;        /* SDA low→high while SCL high = STOP */
    i2c_delay();
}

/*===========================================================================
 * BYTE WRITE & READ
 *===========================================================================*/

/**
 * @brief  Write one byte, return ACK status
 * @return 0 = ACK received, 1 = NACK (device not responding)
 */
uint8_t i2c_write(uint8_t dat)
{
    uint8_t i, ack;
    
    for (i = 0; i < 8; i++) {
        if (dat & 0x80)
            I2C_SDA = 1;
        else
            I2C_SDA = 0;
        dat <<= 1;
        i2c_delay();
        i2c_scl_high();
        i2c_scl_low();
    }
    
    /* Release SDA for ACK */
    I2C_SDA = 1;
    i2c_delay();
    i2c_scl_high();
    ack = I2C_SDA;      /* 0 = ACK, 1 = NACK */
    i2c_scl_low();
    
    return ack;
}

/**
 * @brief  Read one byte, optionally send ACK
 * @param  ack  1 = Master sends ACK (more bytes), 0 = NACK (last byte)
 */
uint8_t i2c_read(uint8_t ack)
{
    uint8_t i, dat = 0;
    
    I2C_SDA = 1;        /* Release SDA */
    i2c_delay();
    
    for (i = 0; i < 8; i++) {
        dat <<= 1;
        i2c_scl_high();
        if (I2C_SDA) dat |= 1;
        i2c_scl_low();
    }
    
    /* Send ACK/NACK */
    if (ack)
        I2C_SDA = 0;    /* ACK */
    else
        I2C_SDA = 1;    /* NACK */
    i2c_delay();
    i2c_scl_high();
    i2c_scl_low();
    I2C_SDA = 1;        /* Release */
    i2c_delay();
    
    return dat;
}

/*===========================================================================
 * BURST READ (Address + Register → Sequential Read)
 *===========================================================================*/

/**
 * @brief  Convenience: write register addr, then read len bytes
 *         标准 I2C "combined format" 读操作
 */
void i2c_read_burst(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    
    i2c_start();
    i2c_write((addr << 1) | 0x00);      /* Write mode */
    i2c_write(reg);                      /* Register address */
    
    i2c_start();                         /* Repeated START */
    i2c_write((addr << 1) | 0x01);      /* Read mode */
    
    for (i = 0; i < len; i++) {
        buf[i] = i2c_read(i < (len - 1));   /* ACK all but last */
    }
    
    i2c_stop();
}
