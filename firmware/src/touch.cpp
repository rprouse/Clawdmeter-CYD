#include "touch.h"
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;   // owned by main.cpp

volatile bool     touch_pressed = false;
volatile uint16_t touch_x = 0;
volatile uint16_t touch_y = 0;

static uint16_t cal_data[5] = {0, 0, 0, 0, 0};   // TFT_eSPI calibration blob
static bool     cal_loaded  = false;

static const char* NVS_NS  = "clawd";
static const char* NVS_KEY = "tcal_v1";

static bool load_cal_from_nvs(void) {
    Preferences p;
    if (!p.begin(NVS_NS, /*readOnly=*/true)) return false;
    size_t n = p.getBytesLength(NVS_KEY);
    if (n != sizeof(cal_data)) { p.end(); return false; }
    p.getBytes(NVS_KEY, cal_data, sizeof(cal_data));
    p.end();
    return true;
}

static void save_cal_to_nvs(void) {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putBytes(NVS_KEY, cal_data, sizeof(cal_data));
    p.end();
}

static void run_calibration_screen(void) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Touch each corner", 160, 100, 2);
    tft.drawString("(top-left first)", 160, 120, 2);
    // calibrateTouch draws the corner targets and reads each tap. The size
    // (last arg) is the crosshair half-size in pixels.
    tft.calibrateTouch(cal_data, TFT_WHITE, TFT_BLACK, 15);
    save_cal_to_nvs();
    cal_loaded = true;
    tft.fillScreen(TFT_BLACK);
}

void touch_init(void) {
    if (load_cal_from_nvs()) {
        tft.setTouch(cal_data);
        cal_loaded = true;
        return;
    }
    run_calibration_screen();
    tft.setTouch(cal_data);
}

void touch_read(void) {
    if (!cal_loaded) return;
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        touch_pressed = true;
        touch_x = x;
        touch_y = y;
    } else {
        touch_pressed = false;
    }
}

void touch_force_recalibrate(void) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.remove(NVS_KEY);
    p.end();
    Serial.println("Calibration cleared, rebooting");
    delay(100);
    ESP.restart();
}
