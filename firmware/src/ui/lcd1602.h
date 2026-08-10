/**
 * @file    lcd1602.h
 * @brief   LCD1602 8-bit Parallel Driver (HD44780 Compatible)
 * 
 * Pin mapping (普中A2板):
 *   D0~D7 → P0
 *   RS → P2.5
 *   RW → P2.6
 *   EN → P2.7
 */

#ifndef __LCD1602_H__
#define __LCD1602_H__

#include "config.h"

void lcd_init(void);
void lcd_clear(void);
void lcd_set_cursor(uint8_t row, uint8_t col);  /* row: 0 or 1, col: 0~15 */
void lcd_write_char(uint8_t ch);
void lcd_write_string(const char *str);
void lcd_write_int(uint8_t row, uint8_t col, int16_t val, uint8_t digits);
void lcd_write_fixed(uint8_t row, uint8_t col, int16_t val, uint8_t decimals);

/* Custom characters (0~7) */
void lcd_create_char(uint8_t location, const uint8_t *pattern);

#endif /* __LCD1602_H__ */
