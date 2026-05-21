#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "display_cfg.h"
#include "touch.h"

TFT_eSPI tft;

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

// ---- Screenshot serial command ----
//
// Streams the LVGL screen as raw RGB565 over USB serial, framed by
// "SCREENSHOT_START w h size" / "SCREENSHOT_END". The host-side
// screenshot.sh script captures and converts to PNG via ffmpeg.

#define CMD_BUF_SIZE 64
static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_pos = 0;

static void send_screenshot() {
    const uint32_t w = SCR_W, h = SCR_H;
    const uint32_t row_bytes = w * 2;
    const uint32_t buf_size  = row_bytes * h;
    uint8_t* sbuf = (uint8_t*)malloc(buf_size);   // no PSRAM on CYD
    if (!sbuf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565, row_bytes,
                     sbuf, buf_size);

    lv_result_t res = lv_snapshot_take_to_draw_buf(
        lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf);
    if (res != LV_RESULT_OK) {
        free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %lu %lu %lu\n",
                  (unsigned long)w, (unsigned long)h, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");

    free(sbuf);
}

static void check_serial_cmd() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (strcmp(cmd_buf, "screenshot") == 0) {
                send_screenshot();
            } else if (strcmp(cmd_buf, "touch-cal") == 0) {
                touch_force_recalibrate();
            }
            cmd_pos = 0;
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

static void lvgl_touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touch_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch_x;
        data->point.y = touch_y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Clawdmeter / CYD boot");

    tft.init();
    tft.setRotation(TFT_ROTATION);
    // For pushPixelsDMA (used by LVGL's flush callback). Does NOT affect
    // fillRect / drawString / other direct TFT_eSPI draw calls.
    tft.setSwapBytes(true);
    touch_init();   // may run a calibration screen on first boot
    tft.fillScreen(TFT_BLACK);
    tft.initDMA();

    lv_init();
    lv_tick_set_cb(millis_cb);
    lv_disp = lv_display_create(SCR_W, SCR_H);
    lv_display_set_flush_cb(lv_disp, disp_flush);
    lv_display_set_buffers(lv_disp, buf1, buf2, sizeof(buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    lv_indev_set_long_press_time(indev, 1500);   // for destructive actions later

    // Smoke test: orange rectangle on black
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_t* box = lv_obj_create(scr);
    lv_obj_set_size(box, 200, 80);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xd97757), 0);
    lv_obj_t* lbl = lv_label_create(box);
    lv_label_set_text(lbl, "Tap me");
    lv_obj_add_event_cb(box, [](lv_event_t* e) {
        static bool on = false;
        on = !on;
        lv_obj_set_style_bg_color(lv_event_get_target_obj(e),
            lv_color_hex(on ? 0x788c5d : 0xd97757), 0);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_center(lbl);
}

void loop() {
    touch_read();
    lv_timer_handler();
    check_serial_cmd();
    delay(5);
}
