#pragma once
#include <stdbool.h>
#include "lvgl.h"

void display_init(void);           // LCD, backlight, touch, LVGL and its task
bool display_lock(int timeout_ms);  // around any LVGL call made outside the LVGL task
void display_unlock(void);
void display_backlight(int percent);
lv_display_t *display_get(void);
void display_touch_enable(bool on);   // off while the calibration reads the panel itself
