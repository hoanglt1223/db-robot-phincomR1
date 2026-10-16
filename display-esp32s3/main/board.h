#pragma once
/*
 * ESP32-S3-DevKitC-1 N16R8 + 2.8" SPI TFT 240x320 (ST7789 or ILI9341) with XPT2046 touch.
 * The LCD and the touch controller share one SPI bus. GPIO 35..37 are taken by the
 * octal PSRAM on the N16R8 and must not be used; GPIO 0/3/45/46 are strapping pins.
 */
#define PIN_SPI_MOSI   11   // LCD SDI(MOSI)  + T_DIN
#define PIN_SPI_SCLK   12   // LCD SCK        + T_CLK
#define PIN_SPI_MISO   13   // T_DO           (leave the LCD's SDO(MISO) unconnected)
#define PIN_LCD_CS     10
#define PIN_LCD_DC      9
#define PIN_LCD_RST    14
#define PIN_LCD_BL     21   // LED / BL (PWM)
#define PIN_TOUCH_CS    1   // T_CS
#define PIN_TOUCH_IRQ   2   // T_IRQ (optional)

#define LCD_SPI_HOST   SPI2_HOST
#define LCD_SPI_HZ     (40 * 1000 * 1000)
#define TOUCH_SPI_HZ   (2 * 1000 * 1000)

#define LCD_H_RES      320  // landscape
#define LCD_V_RES      240

#define SPEAKER_PORT   8089
