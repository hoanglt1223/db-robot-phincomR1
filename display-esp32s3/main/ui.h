#pragma once
void ui_init(void);          // builds the screens; call with the display lock held
void ui_calibrate(void);     // touch calibration, blocking; call without the lock
