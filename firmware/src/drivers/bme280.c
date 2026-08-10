/**
 * @file    bme280.c
 * @brief   BME280 Driver Implementation (Forced Mode)
 * 
 * 寄存器参考: BME280 Datasheet Rev 1.6
 * 简化实现: 固定 oversampling x1, IIR filter off
 * 校准数据读取 + 补偿算法 (整数运算, 无浮点)
 */

#include "bme280.h"
#include "i2c.h"

/*===========================================================================
 * REGISTER ADDRESSES
 *===========================================================================*/

#define BME280_REG_ID           0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_PRESS_MSB    0xF7

#define BME280_CHIP_ID          0x58

/* Oversampling settings */
#define BME280_OSRS_T_X1        0x20
#define BME280_OSRS_P_X1        0x04
#define BME280_OSRS_H_X1        0x01
#define BME280_MODE_FORCED      0x01

/*===========================================================================
 * CALIBRATION DATA (stored in RAM after init)
 *===========================================================================*/

static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

/*===========================================================================
 * I2C HELPERS
 *===========================================================================*/

static uint8_t bme280_read_reg(uint8_t reg)
{
    uint8_t val;
    i2c_start();
    if (i2c_write((BME280_ADDR << 1) | 0x00)) { i2c_stop(); return 0; }
    i2c_write(reg);
    i2c_start();
    i2c_write((BME280_ADDR << 1) | 0x01);
    val = i2c_read(0);
    i2c_stop();
    return val;
}

static void bme280_write_reg(uint8_t reg, uint8_t val)
{
    i2c_start();
    i2c_write((BME280_ADDR << 1) | 0x00);
    i2c_write(reg);
    i2c_write(val);
    i2c_stop();
}

/*===========================================================================
 * INIT
 *===========================================================================*/

uint8_t bme280_init(void)
{
    uint8_t chip_id;
    uint8_t calib[26];
    uint8_t i;
    
    i2c_init();
    
    /* Verify chip ID */
    chip_id = bme280_read_reg(BME280_REG_ID);
    if (chip_id != BME280_CHIP_ID) {
        return BME280_ERR_ID;
    }
    
    /* Soft reset */
    bme280_write_reg(BME280_REG_RESET, 0xB6);
    
    /* Wait for reset (2ms) */
    for (i = 0; i < 20; i++) { uint8_t d; for (d = 0; d < 200; d++) { _nop_(); } }
    
    /* Read calibration data (26 bytes from 0x88 + 7 bytes from 0xE1) */
    i2c_read_burst(BME280_ADDR, 0x88, calib, 26);
    
    dig_T1 = ((uint16_t)calib[1]  << 8) | calib[0];
    dig_T2 = ((int16_t)calib[3]   << 8) | calib[2];
    dig_T3 = ((int16_t)calib[5]   << 8) | calib[4];
    
    dig_P1 = ((uint16_t)calib[7]  << 8) | calib[6];
    dig_P2 = ((int16_t)calib[9]   << 8) | calib[8];
    dig_P3 = ((int16_t)calib[11]  << 8) | calib[10];
    dig_P4 = ((int16_t)calib[13]  << 8) | calib[12];
    dig_P5 = ((int16_t)calib[15]  << 8) | calib[14];
    dig_P6 = ((int16_t)calib[17]  << 8) | calib[16];
    dig_P7 = ((int16_t)calib[19]  << 8) | calib[18];
    dig_P8 = ((int16_t)calib[21]  << 8) | calib[20];
    dig_P9 = ((int16_t)calib[23]  << 8) | calib[22];
    
    /* H1 (0xA1) */
    dig_H1 = bme280_read_reg(0xA1);
    
    /* H2~H6 (0xE1~0xE7) */
    i2c_read_burst(BME280_ADDR, 0xE1, calib, 7);
    dig_H2 = ((int16_t)calib[1] << 8) | calib[0];
    dig_H3 = calib[2];
    dig_H4 = ((int16_t)calib[3] << 4) | (calib[4] & 0x0F);
    dig_H5 = ((int16_t)calib[5] << 4) | ((calib[4] >> 4) & 0x0F);
    dig_H6 = (int8_t)calib[6];
    
    /* Configure: oversampling x1 for T/P/H, forced mode */
    bme280_write_reg(BME280_REG_CTRL_HUM, BME280_OSRS_H_X1);
    bme280_write_reg(BME280_REG_CONFIG, 0x00);   /* IIR off, SPI 3-wire off */
    
    return BME280_OK;
}

/*===========================================================================
 * COMPENSATION (Integer Math)
 *===========================================================================*/

/**
 * @brief  BME280 compensation formula (datasheet Appendix A)
 *         全部使用 int32 整数运算，避免浮点。
 *         返回值: temperature = actual * 100
 */
static int32_t bme280_compensate_T(int32_t adc_T)
{
    int32_t var1, var2, T;
    
    var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) *
            ((int32_t)dig_T2)) >> 11;
    
    var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
              ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
            ((int32_t)dig_T3)) >> 14;
    
    T = (var1 + var2) * 5 + 3200;  /* x100 */
    return T;
}

static uint32_t bme280_compensate_P(int32_t adc_P, int32_t t_fine)
{
    int32_t var1, var2, var3, var4;
    uint32_t p;
    
    var1 = ((int32_t)t_fine) - 128000;
    var2 = var1 * var1 * (int32_t)dig_P6;
    var2 = var2 + ((var1 * (int32_t)dig_P5) << 17);
    var2 = var2 + (((int32_t)dig_P4) << 35);
    var1 = ((var1 * var1 * (int32_t)dig_P3) >> 8) +
           ((var1 * (int32_t)dig_P2) << 12);
    var1 = (((((int32_t)1) << 47) + var1)) * ((int32_t)dig_P1) >> 33;
    
    if (var1 == 0) return 0;
    
    var3 = 1048576 - adc_P;
    var3 = (int32_t)(((int32_t)var3 * 3125 - var2) * 2 / var1);
    var4 = ((int32_t)dig_P9 * (int32_t)(((int32_t)var3 * (int32_t)var3) >> 13)) >> 25;
    var2 = ((int32_t)dig_P8 * var3) >> 19;
    var3 = ((var3 + var2 + var4) >> 8) + (((int32_t)dig_P7) << 4);
    
    p = (uint32_t)var3;
    return p;   /* Pa, divide by 256 for hPa */
}

static uint32_t bme280_compensate_H(int32_t adc_H, int32_t t_fine)
{
    int32_t v_x1_u32r;
    
    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) -
                    (((int32_t)dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)dig_H3)) >> 11) +
                       ((int32_t)32768))) >> 10) + ((int32_t)2097152)) *
                   ((int32_t)dig_H2) + 8192) >> 14));
    
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                                ((int32_t)dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    
    return (uint32_t)(v_x1_u32r >> 12);   /* x100 */
}

/*===========================================================================
 * READ SENSOR
 *===========================================================================*/

uint8_t bme280_read(bme280_data_t *data)
{
    uint8_t raw[8];
    int32_t adc_T, adc_P, adc_H;
    int32_t t_fine;
    
    if (!data) return BME280_ERR_I2C;
    
    /* Trigger forced measurement */
    bme280_write_reg(BME280_REG_CTRL_MEAS,
                     BME280_OSRS_T_X1 | BME280_OSRS_P_X1 | BME280_MODE_FORCED);
    
    /* Wait for measurement (max ~9ms for x1 oversampling) */
    {
        uint8_t i;
        for (i = 0; i < 15; i++) {
            uint16_t w;
            for (w = 0; w < 1000; w++) { _nop_(); }
        }
    }
    
    /* Read 8 bytes: P[3] + T[3] + H[2] from 0xF7 */
    i2c_read_burst(BME280_ADDR, BME280_REG_PRESS_MSB, raw, 8);
    
    adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    adc_H = ((int32_t)raw[6] << 8)  | raw[7];
    
    /* Compensate (order matters: T first → t_fine → P → H) */
    data->temperature = bme280_compensate_T(adc_T);
    
    /* Calculate t_fine for P and H compensation */
    {
        int32_t var1, var2;
        var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) *
                ((int32_t)dig_T2)) >> 11;
        var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                  ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                ((int32_t)dig_T3)) >> 14;
        t_fine = var1 + var2;
    }
    
    data->pressure    = bme280_compensate_P(adc_P, t_fine) * 100 / 256;  /* → Pa*100 → hPa*100 */
    data->humidity    = bme280_compensate_H(adc_H, t_fine);
    
    data->status = BME280_OK;
    return BME280_OK;
}
