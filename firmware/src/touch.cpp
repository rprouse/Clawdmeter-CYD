#include "touch.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "display_cfg.h"

extern TFT_eSPI tft;   // owned by main.cpp

// CYD touch wiring (XPT2046 on its own SPI bus, NOT shared with the TFT).
// TFT uses HSPI per platformio.ini USE_HSPI_PORT=1, so give touch VSPI.
#define TOUCH_SCLK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS_PIN 33

static XPT2046_Touchscreen ts(TOUCH_CS_PIN);

volatile bool     touch_pressed = false;
volatile uint16_t touch_x = 0;
volatile uint16_t touch_y = 0;

// Two-point linear calibration: capture raw XPT2046 values at top-left and
// bottom-right of the screen, then linearly map raw -> screen pixels.
// Stored in NVS as 4 uint16 (raw_tl_x, raw_tl_y, raw_br_x, raw_br_y).
struct TouchCal {
    uint16_t raw_tl_x;
    uint16_t raw_tl_y;
    uint16_t raw_br_x;
    uint16_t raw_br_y;
};
static TouchCal cal = {0, 0, 0, 0};
static bool     cal_loaded = false;

static const char* NVS_NS  = "clawd";
static const char* NVS_KEY = "tcal_v2";   // bump from v1 — format changed

static bool load_cal_from_nvs(void) {
    Preferences p;
    if (!p.begin(NVS_NS, /*readOnly=*/true)) return false;
    size_t n = p.getBytesLength(NVS_KEY);
    if (n != sizeof(cal)) { p.end(); return false; }
    p.getBytes(NVS_KEY, &cal, sizeof(cal));
    p.end();
    return true;
}

static void save_cal_to_nvs(void) {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putBytes(NVS_KEY, &cal, sizeof(cal));
    p.end();
}

// Block until a fresh touch lands. Returns the raw XPT2046 reading averaged
// over a short window for noise reduction.
static void wait_for_tap(uint16_t* out_raw_x, uint16_t* out_raw_y) {
    // Wait for any prior touch to release.
    while (ts.touched()) delay(10);
    delay(150);
    // Wait for a new touch.
    while (!ts.touched()) delay(10);
    // Average a few samples for stability.
    uint32_t sx = 0, sy = 0;
    const int N = 16;
    for (int i = 0; i < N; i++) {
        TS_Point p = ts.getPoint();
        sx += p.x;
        sy += p.y;
        delay(5);
    }
    *out_raw_x = sx / N;
    *out_raw_y = sy / N;
    // Wait for release before returning, so subsequent waits aren't fooled.
    while (ts.touched()) delay(10);
    delay(150);
}

static void draw_target(int16_t cx, int16_t cy) {
    tft.fillCircle(cx, cy, 6, TFT_WHITE);
    tft.fillCircle(cx, cy, 3, TFT_RED);
}

static void run_calibration_screen(void) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Tap each target",      SCR_W / 2, SCR_H / 2 - 10, 2);
    tft.drawString("Top-left first",       SCR_W / 2, SCR_H / 2 + 10, 2);
    delay(1500);

    // Top-left target at (12, 12)
    tft.fillScreen(TFT_BLACK);
    draw_target(12, 12);
    wait_for_tap(&cal.raw_tl_x, &cal.raw_tl_y);

    // Bottom-right target at (SCR_W-12, SCR_H-12)
    tft.fillScreen(TFT_BLACK);
    draw_target(SCR_W - 12, SCR_H - 12);
    wait_for_tap(&cal.raw_br_x, &cal.raw_br_y);

    save_cal_to_nvs();
    cal_loaded = true;

    tft.fillScreen(TFT_BLACK);
    tft.drawString("Calibrated", SCR_W / 2, SCR_H / 2, 2);
    delay(700);
    tft.fillScreen(TFT_BLACK);
}

void touch_init(void) {
    // TFT is on HSPI (platformio.ini USE_HSPI_PORT=1), so the default SPI
    // global (VSPI on ESP32) is free for touch. The Paul Stoffregen library's
    // begin() uses the default SPI, which we configure here for touch pins.
    SPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS_PIN);
    ts.begin();
    ts.setRotation(0);   // we calibrate in raw space; rotation handled by mapping

    if (load_cal_from_nvs()) {
        cal_loaded = true;
        return;
    }
    run_calibration_screen();
}

void touch_read(void) {
    if (!cal_loaded) return;
    if (!ts.touched()) {
        touch_pressed = false;
        return;
    }
    TS_Point p = ts.getPoint();

    // Linear map raw -> screen pixels using the two calibration anchors.
    // Guard against zero-span (uncalibrated or pathological cal).
    int32_t dx = (int32_t)cal.raw_br_x - (int32_t)cal.raw_tl_x;
    int32_t dy = (int32_t)cal.raw_br_y - (int32_t)cal.raw_tl_y;
    if (dx == 0 || dy == 0) { touch_pressed = false; return; }

    int32_t sx = ((int32_t)(p.x - cal.raw_tl_x) * (SCR_W - 1)) / dx;
    int32_t sy = ((int32_t)(p.y - cal.raw_tl_y) * (SCR_H - 1)) / dy;
    if (sx < 0) sx = 0; else if (sx >= SCR_W) sx = SCR_W - 1;
    if (sy < 0) sy = 0; else if (sy >= SCR_H) sy = SCR_H - 1;

    touch_pressed = true;
    touch_x = (uint16_t)sx;
    touch_y = (uint16_t)sy;
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
