#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { LCD_ST7789 = 0, LCD_ILI9341 = 1 };

typedef struct {
    char ssid[33];
    char pass[65];
    char name[33];        // this display's name, shown in the speaker's panel
    char spk_id[48];      // the speaker to follow (its id from "here"); empty = choose
    char spk_ip[16];      // optional fixed speaker address (if broadcasts are blocked)
    uint8_t lcd_type;     // LCD_ST7789 / LCD_ILI9341
    uint8_t invert;       // colour inversion (most ST7789 panels need it on)
    uint8_t bgr;          // red and blue swapped
    uint8_t rot180;       // turn the picture upside down
    uint8_t mirror;       // mirror left-right (for odd panels)
    uint8_t bright;       // 5..100 %
    uint8_t night_dim;    // dim between 22:00 and 06:00
    uint8_t cal_ok;       // touch calibrated
    uint8_t cal_swap;     // raw X drives screen Y
    uint8_t want_cal;     // run the calibration on the next start
    int16_t cal_x0, cal_x1, cal_y0, cal_y1;   // raw readings at screen x=CAL_A/CAL_B, y=CAL_A/CAL_C
    uint8_t light;        // light theme (0 = dark); added after v1.5.15, keep it last
} cfg_t;

#define CAL_A 30
#define CAL_B 290
#define CAL_C 210

extern cfg_t g_cfg;

void cfg_load(void);
void cfg_save(void);
