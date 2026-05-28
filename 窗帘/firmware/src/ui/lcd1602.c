/**
 * @file    lcd1602.c
 * @brief   LCD1602 8-bit Driver
 */

#include "lcd1602.h"
#include "intrins.h"

/*===========================================================================
 * LOW-LEVEL COMMANDS
 *===========================================================================*/

static void lcd_busy_wait(void)
{
    uint8_t i;
    for (i = 0; i < 100; i++) { _nop_(); }
}

static void lcd_write_cmd(uint8_t cmd)
{
    LCD_RS = 0;         /* Command mode */
    LCD_RW = 0;         /* Write mode */
    LCD_DATA_PORT = cmd;
    lcd_busy_wait();
    LCD_EN = 1;
    lcd_busy_wait();
    LCD_EN = 0;
}

static void lcd_write_data(uint8_t dat)
{
    LCD_RS = 1;         /* Data mode */
    LCD_RW = 0;
    LCD_DATA_PORT = dat;
    lcd_busy_wait();
    LCD_EN = 1;
    lcd_busy_wait();
    LCD_EN = 0;
}

/*===========================================================================
 * INIT
 *===========================================================================*/

void lcd_init(void)
{
    uint16_t i;
    for (i = 0; i < 5000; i++) { _nop_(); }   /* Power-up delay > 15ms */
    
    /* 8-bit mode init sequence (3x) */
    lcd_write_cmd(0x38);    /* 8-bit, 2 lines, 5x8 font */
    lcd_write_cmd(0x0C);    /* Display ON, cursor OFF, blink OFF */
    lcd_write_cmd(0x06);    /* Increment cursor, no shift */
    lcd_write_cmd(0x01);    /* Clear display */
    
    for (i = 0; i < 2000; i++) { _nop_(); }   /* Clear needs ~2ms */
}

/*===========================================================================
 * PUBLIC API
 *===========================================================================*/

void lcd_clear(void)
{
    lcd_write_cmd(0x01);
    {
        uint16_t i;
        for (i = 0; i < 2000; i++) { _nop_(); }
    }
}

void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t addr;
    if (col > 15) col = 15;
    addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_write_cmd(addr);
}

void lcd_write_char(uint8_t ch)
{
    lcd_write_data(ch);
}

void lcd_write_string(const char *str)
{
    while (*str) {
        lcd_write_data((uint8_t)*str);
        str++;
    }
}

/**
 * @brief  Write integer value at cursor position
 * @param  val      Value (negative supported)
 * @param  digits   Minimum field width (right-aligned, padded with spaces)
 */
void lcd_write_int(uint8_t row, uint8_t col, int16_t val, uint8_t digits)
{
    char buf[8];
    uint8_t i = 0, len;
    uint8_t is_neg = 0;
    
    if (val < 0) {
        is_neg = 1;
        val = -val;
    }
    
    /* Extract digits (reverse order) */
    do {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val > 0 && i < 7);
    
    if (is_neg) buf[i++] = '-';
    len = i;
    
    lcd_set_cursor(row, col);
    
    /* Right-align: padding spaces */
    while (digits > len) {
        lcd_write_data(' ');
        digits--;
    }
    
    /* Output digits in correct order */
    while (i > 0) {
        lcd_write_data(buf[--i]);
    }
}

/**
 * @brief  Write fixed-point number: integer + decimal separator + decimal
 * @param  val       Value x10 (e.g., 263 = "26.3")
 * @param  decimals  Number of decimal places (usually 1)
 */
void lcd_write_fixed(uint8_t row, uint8_t col, int16_t val, uint8_t decimals)
{
    int16_t int_part = val / 10;
    int16_t dec_part = val % 10;
    uint8_t digits = 3;   /* "xx" = 2 digits + possible negative + decimal */
    
    if (val < 0) {
        dec_part = -dec_part;
        digits = 4;
    }
    
    lcd_write_int(row, col, int_part, 0);
    lcd_write_data('.');
    lcd_write_data((uint8_t)('0' + (dec_part < 0 ? -dec_part : dec_part)));
}

void lcd_create_char(uint8_t location, const uint8_t *pattern)
{
    uint8_t i;
    location &= 0x07;   /* CGRAM locations 0~7 */
    lcd_write_cmd(0x40 | (location << 3));
    for (i = 0; i < 8; i++) {
        lcd_write_data(pattern[i]);
    }
}
