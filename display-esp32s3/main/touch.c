/* A small XPT2046 (ADS7843-compatible) driver on the LCD's SPI bus. */
#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "touch.h"
#include "board.h"
#include "cfg.h"

static spi_device_handle_t s_dev;

void touch_init(spi_host_device_t host)
{
    spi_device_interface_config_t dc = {
        .clock_speed_hz = TOUCH_SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_TOUCH_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(host, &dc, &s_dev));
    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN_TOUCH_IRQ, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&io);
}

static int xfer(uint8_t cmd)
{
    spi_transaction_t t = { .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA, .length = 24 };
    t.tx_data[0] = cmd;
    if (spi_device_polling_transmit(s_dev, &t) != ESP_OK) return -1;
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

bool touch_read_raw(int *rx, int *ry)
{
    if (!s_dev) return false;
    int z1 = xfer(0xB1), z2 = xfer(0xC1);
    if (z1 < 0 || z2 < 0) return false;
    int z = z1 + 4095 - z2;
    if (z < 400) { xfer(0xD0); return false; }      // re-arm PENIRQ, power down
    int xs[7], ys[7];
    for (int i = 0; i < 7; i++) { xs[i] = xfer(0xD1); ys[i] = xfer(0x91); }
    xfer(0xD0);
    qsort(xs, 7, sizeof(int), cmp);
    qsort(ys, 7, sizeof(int), cmp);
    if (xs[5] - xs[1] > 120 || ys[5] - ys[1] > 120) return false;   // finger moving / noise
    *rx = xs[3];
    *ry = ys[3];
    return *rx > 50 && *rx < 4050 && *ry > 50 && *ry < 4050;
}

bool touch_read(int16_t *x, int16_t *y)
{
    int rx, ry;
    if (!touch_read_raw(&rx, &ry)) return false;
    int a = g_cfg.cal_swap ? ry : rx;   // drives screen x
    int b = g_cfg.cal_swap ? rx : ry;   // drives screen y
    int x0 = g_cfg.cal_x0, x1 = g_cfg.cal_x1, y0 = g_cfg.cal_y0, y1 = g_cfg.cal_y1;
    if (!g_cfg.cal_ok || x1 == x0 || y1 == y0) {   // a usable guess for the usual modules
        x0 = 3700; x1 = 400; y0 = 3600; y1 = 500;
    }
    int sx = CAL_A + (a - x0) * (CAL_B - CAL_A) / (x1 - x0);
    int sy = CAL_A + (b - y0) * (CAL_C - CAL_A) / (y1 - y0);
    sx = sx < 0 ? 0 : sx >= LCD_H_RES ? LCD_H_RES - 1 : sx;
    sy = sy < 0 ? 0 : sy >= LCD_V_RES ? LCD_V_RES - 1 : sy;
    *x = sx;
    *y = sy;
    return true;
}
