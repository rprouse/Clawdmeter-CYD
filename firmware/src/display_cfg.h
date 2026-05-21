#pragma once

// AOKIN ESP32-2432S028R "CYD" pin map (landscape orientation).
//
// Display rotation 1 (TFT_eSPI rotation index) makes the panel 320 wide × 240 tall.
#define SCR_W 320
#define SCR_H 240
#define TFT_ROTATION 3

// All TFT_* and TOUCH_* pin macros are passed in via platformio.ini build_flags
// (USER_SETUP_LOADED=1 means TFT_eSPI does not load its own User_Setup.h).
//
// Reserved-but-unused CYD pins (left here for future reference):
//   GPIO 26 — speaker amp
//   GPIO 34 — LDR (light sensor, input-only)
//   GPIO 5  — SD card CS
//   GPIO 4 / 16 / 17 — RGB LED R/G/B
//   GPIO 0  — BOOT button (flashing only)
