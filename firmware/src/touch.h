#pragma once
#include <stdint.h>
#include <stdbool.h>

// Public state — written by touch_read() once per main-loop iteration.
extern volatile bool touch_pressed;
extern volatile uint16_t touch_x;  // screen pixels, post-calibration
extern volatile uint16_t touch_y;

// Initialize touch. Loads calibration from NVS if present; otherwise runs
// a 4-corner tap calibration screen, saves the result, and continues.
// Must be called AFTER tft.init() / tft.setRotation() in main.cpp.
void touch_init(void);

// Poll the XPT2046 controller via TFT_eSPI's getTouch(). Updates the public
// touch_pressed / touch_x / touch_y state. Call once per main loop iteration.
void touch_read(void);

// Wipe calibration from NVS and reboot. Triggered by the "touch-cal" serial
// command. Next boot will run the calibration screen.
void touch_force_recalibrate(void);
