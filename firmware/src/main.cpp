#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "display_cfg.h"

static TFT_eSPI tft;

// Double-buffered partial render. Two 320×20 buffers in SRAM (~12.5 KB each).
// Sized down from the design's 320×40 — at 320×40 the linker overflows DRAM
// by ~15 KB once LVGL/NimBLE/Arduino runtime/font tables are accounted for.
#define BUF_LINES 20
static uint16_t buf1[SCR_W * BUF_LINES];
static uint16_t buf2[SCR_W * BUF_LINES];
static lv_display_t* lv_disp = nullptr;

static void disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixelsDMA((uint16_t*)px_map, w * h);
    tft.endWrite();
    lv_display_flush_ready(disp);
}

static uint32_t millis_cb(void) { return millis(); }

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Clawdmeter / CYD boot");
    Serial.printf("sizeof(lv_color_t)=%d (expect 2 for RGB565)\n",
                  (int)sizeof(lv_color_t));

    tft.begin();
    tft.setRotation(TFT_ROTATION);

    // CYD's ILI9341 needs MADCTL=0x68 for landscape with correct R/B order.
    // TFT_eSPI's TFT_RGB_ORDER build_flag is not getting honored in our setup,
    // so we override the register explicitly. 0x68 = MX | MV | BGR.
    tft.writecommand(0x36);
    tft.writedata(0x68);

    // --- Color test pattern (bypasses LVGL) ---
    // Three labeled stripes — left to right: RED, GREEN, BLUE.
    // If you see them in that order, panel + TFT_eSPI are correct and any
    // remaining color issue is in our LVGL pipeline. If colors are swapped
    // or wrong, the panel/driver layer is to blame.
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(  0, 60, 106, 120, TFT_RED);
    tft.fillRect(106, 60, 108, 120, TFT_GREEN);
    tft.fillRect(214, 60, 106, 120, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("RED  GREEN  BLUE", 80, 20, 2);
    delay(5000);   // hold the test pattern long enough to inspect
    // --- End test pattern ---

    tft.fillScreen(TFT_BLACK);
    tft.initDMA();

    lv_init();
    lv_tick_set_cb(millis_cb);
    lv_disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_flush_cb(lv_disp, disp_flush);
    lv_display_set_buffers(lv_disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Smoke test: orange rectangle on black
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t* box = lv_obj_create(scr);
    lv_obj_set_size(box, 200, 80);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xd97757), 0);
    lv_obj_t* lbl = lv_label_create(box);
    lv_label_set_text(lbl, "CYD bring-up");
    lv_obj_center(lbl);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
