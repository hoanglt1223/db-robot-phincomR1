#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"

void touch_init(spi_host_device_t host);
/* Raw 12-bit readings; false when not pressed. */
bool touch_read_raw(int *rx, int *ry);
/* Screen coordinates through the calibration; false when not pressed. */
bool touch_read(int16_t *x, int16_t *y);
