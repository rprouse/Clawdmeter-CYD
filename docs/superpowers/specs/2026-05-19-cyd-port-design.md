# Clawdmeter — CYD Port Design

**Date:** 2026-05-19
**Status:** Approved for implementation planning
**Target hardware:** AOKIN ESP32-2432S028R "Cheap Yellow Display" (ESP32 LX6, 240×320 ILI9341 TFT, XPT2046 resistive touch, CH340 USB-UART)
**Replaces:** Waveshare ESP32-S3-Touch-AMOLED-2.16 (480×480 CO5300 AMOLED) — Waveshare support is dropped entirely.

## 1. Goals & non-goals

**Goals:**
- Port the Clawdmeter firmware to the CYD board with no hardware add-ons.
- Preserve the daemon ↔ device data path (BLE GATT JSON) and the user-facing splash + Usage + Bluetooth screens.
- Keep the screenshot-over-serial QA loop working at the new resolution.

**Non-goals:**
- No BLE HID keyboard. The Space / Shift+Tab keystroke feature is removed.
- No battery indicator, no auto-rotation. CYD has no PMU and no IMU.
- No Wi-Fi / OTA / SD-card / RGB-LED / speaker / LDR features in this port. Those pins are reserved but unused.
- No multi-board support. This is a replacement, not a variant.

## 2. Feature scope

| Feature | Status | Reason |
|---|---|---|
| BLE data service (daemon → device JSON) | Keep | Core feature |
| Splash + 13 pixel-art animations | Keep | Re-scale to 240×240 |
| Three screens (Splash / Usage / Bluetooth) | Keep | Same state machine |
| Screenshot serial command | Keep | UI QA loop |
| BLE HID keyboard | **Drop** | User decision |
| Battery indicator | **Drop** | No PMU on CYD |
| Auto-rotation via IMU | **Drop** | No IMU on CYD |
| 3-physical-button input | **Drop** | CYD has only BOOT + RST |

Input is touch-only: short-tap cycles screens, long-press on splash cycles animations, long-press on the Bluetooth reset zone clears bonds.

## 3. Architecture & file map

```text
firmware/src/
  main.cpp              — rewritten flush_cb (TFT_eSPI + DMA), rewritten touch_read
                          (XPT2046 over HSPI), removed rotation/brightness flash,
                          removed 3-button polling, simplified loop
  display_cfg.h         — CYD pin map (ILI9341 on VSPI, XPT2046 on HSPI)
  ui.{h,cpp}            — same 3-screen state machine; layouts redrawn for 320×240
  splash.{h,cpp}        — same engine; UPSCALE constant 24 → 12; palette[0] side bars
  touch.{h,cpp}         — rewritten: XPT2046 reads + 4-point affine cal, NVS-stored coefficients
  ble.{h,cpp}           — keep NimBLE; delete HID service entirely; new UUIDs + name
  data.h                — unchanged
  icons.h               — re-exported at smaller sizes
  logo.h                — re-exported at 48×48
  font_*.c              — regenerated at smaller pt sizes
  splash_animations.h   — unchanged (data is resolution-independent)

  -- DELETED --
  imu.{h,cpp}           — no IMU
  power.{h,cpp}         — no PMU

daemon/
  claude-usage-daemon.sh — name + UUIDs + cache path updated in place
  *.service              — unchanged (just systemctl daemon-reload after update)

tools/
  png_to_lvgl.js        — unchanged
  scrape_claudepix.js   — unchanged
  convert_to_c.js       — unchanged
  (touch calibration runs in firmware on first boot — no separate tool)

platformio.ini          — platform=espressif32 (stock); board=esp32dev;
                          TFT_eSPI + LVGL 9 + NimBLE-Arduino; User_Setup via build_flags
screenshot.sh           — unchanged (still works; framebuffer is now 320×240)
```

**Retained unchanged:** `data.h`, `splash_animations.h`, splash engine logic (only the upscale constant changes), screenshot serial command, all tools.

**Rewritten:** `main.cpp` flush + touch + loop, `display_cfg.h`, `touch.cpp`, `ble.cpp` (HID stripped), `ui.cpp` (layouts).

**Deleted:** `imu.{h,cpp}`, `power.{h,cpp}`.

## 4. BLE service shape

**Firmware (`ble.cpp`):**
- NimBLE peripheral.
- Advertised name: **`Clawdmeter`**.
- HID GATT service removed entirely (descriptor, report map, `ble_send_key()` API).
- `ble_clear_bonds()` retained, exposed via long-press on the Bluetooth reset zone and via a `bonds-clear` serial command.
- Data service UUIDs (replacing the `4c41555a-...` set):

  | Role | UUID | Direction |
  |---|---|---|
  | Service | `c1aw0001-0000-1000-8000-00805f9b34fb` | — |
  | RX | `c1aw0002-0000-1000-8000-00805f9b34fb` | daemon → device, write JSON payload |
  | TX | `c1aw0003-0000-1000-8000-00805f9b34fb` | device → daemon, notify ack/nack |
  | REQ | `c1aw0004-0000-1000-8000-00805f9b34fb` | device → daemon, fires `0x01` on subscribe if `has_received_data` is false |

**Daemon (`daemon/claude-usage-daemon.sh`):**
- `DEVICE_NAME`: `"Claude Controller"` → `"Clawdmeter"`.
- Four UUID constants updated to the `c1aw...` set.
- Cache path: `~/.config/claude-usage-monitor/ble-address` → `~/.config/clawdmeter/ble-address` (forces clean re-resolve; no risk of using stale cache from old firmware).
- All other logic (POLL_INTERVAL=60, TICK=5, dbus-monitor pipe, MAC re-resolve on failure) unchanged.

## 5. UI / layout (all 320×240 landscape, fixed orientation)

**Common shell:**
- Title: Tiempos ~22pt, top-left, 12px left padding.
- Content margins: 12px horizontal (no rounded-corner clearance needed).
- Color palette unchanged: COL_BG, COL_PANEL, COL_TEXT, COL_DIM, COL_ACCENT.

**Splash:**
- 20×20 art upscaled **12×** to 240×240, centered horizontally.
- 40px-wide side bars on left and right painted in the current animation's `palette[0]` (the conventional background color), so the art appears to bleed off-edge rather than being letterboxed.
- Tap anywhere → dismiss to last non-splash screen.
- Long-press ≥500ms → cycle to next animation.

**Usage screen:**
- Title "Usage" top-left.
- Two stacked panels, each ~80px tall:
  - "Current" — 5-hour window: sub-label, big % number (right-aligned), progress bar, reset-time.
  - "Weekly" — 7-day window: same layout.
- Animation label (mono ~14pt) at bottom-center, retained.

**Bluetooth screen:**
- Title "Bluetooth" top-left.
- Info panel: BT icon + status string ("Connected" / "Disconnected" / "Advertising") on top row, then device name and MAC address on monospace lines.
- Reset zone: trash icon + "Tap to forget paired hosts" label. Long-press ≥1500ms clears NimBLE bonds.
- Single-line credits at the bottom edge.

## 6. Build system & pin map

`platformio.ini` switches to the stock `espressif32@6.x` platform (no more pioarduino). Display, touch, and LVGL pins are passed as `build_flags` so TFT_eSPI's `User_Setup.h` is generated at compile time.

```ini
[env:cyd]
platform = espressif32@6.x
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.partitions = huge_app.csv
lib_deps =
    lvgl/lvgl@^9.1.0
    bodmer/TFT_eSPI@^2.5.43
    h2zero/NimBLE-Arduino@^1.4.1
build_flags =
    -DLV_CONF_INCLUDE_SIMPLE
    -DUSER_SETUP_LOADED=1
    -DILI9341_2_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_MISO=12
    -DTFT_MOSI=13
    -DTFT_SCLK=14
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=-1
    -DTFT_BL=21
    -DTFT_BACKLIGHT_ON=HIGH
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000
    -DTOUCH_CS=33
    -DSPI_TOUCH_FREQUENCY=2500000
    -DLV_COLOR_DEPTH=16
```

**LVGL buffers:** two 320×40 RGB565 buffers (~25 KB each, double-buffered = 50 KB) allocated in regular SRAM with `MALLOC_CAP_8BIT | MALLOC_CAP_DMA`. No PSRAM check.

**CYD pin map (AOKIN ESP32-2432S028R):**

| Function | GPIO | Notes |
|---|---|---|
| TFT MISO | 12 | VSPI |
| TFT MOSI | 13 | VSPI |
| TFT SCLK | 14 | VSPI |
| TFT CS | 15 | display select |
| TFT DC | 2 | data/command |
| TFT BL | 21 | PWM, on=HIGH |
| Touch IRQ | 36 | input-only, polling sufficient |
| Touch CS | 33 | XPT2046, HSPI |
| Touch MOSI | 32 | HSPI |
| Touch MISO | 39 | input-only, HSPI |
| Touch SCLK | 25 | HSPI |
| Speaker | 26 | unused (future) |
| LDR | 34 | unused (future auto-dim) |
| SD CS | 5 | unused |
| RGB LED R/G/B | 4 / 16 / 17 | unused |
| BOOT | 0 | flashing only |

**Flashing:**
```bash
pio run -d firmware -e cyd -t upload
```
CH340 + EN-reset is automatic — no JTAG dance, no boot-button hold.

## 7. Touch, calibration, gestures

**Driver:** XPT2046 over HSPI, polled once per main loop iteration in `touch_read()`. Single shared `touch_pressed/touch_x/touch_y` state, same centralization pattern as the current firmware.

**Calibration:**
- 4-point affine transform: `screen_x = ax * raw_x + bx`, `screen_y = ay * raw_y + by`.
- Coefficients stored in NVS namespace `clawd`, keys `tcal_ax`, `tcal_bx`, `tcal_ay`, `tcal_by`.
- On boot: if any key is missing, run a 4-tap calibration screen with crosshair targets in each corner. Otherwise apply transform and continue normally.
- Recalibrate: serial command `touch-cal` clears NVS keys and reboots.

**Gestures:**

| Context | Gesture | Action |
|---|---|---|
| Splash | Short tap (anywhere) | Dismiss splash → last non-splash screen |
| Splash | Long-press ≥500ms | Cycle to next animation |
| Usage / Bluetooth | Short tap (anywhere) | Cycle to other screen |
| Bluetooth reset zone | Long-press ≥1500ms | Clear NimBLE bonds |

No swipe / drag / multi-touch — XPT2046 is single-point resistive and LVGL's indev model would make those error-prone.

## 8. Asset regeneration

**Fonts** (`font_*.c`) — regenerated via existing `lv_font_conv` + LVGL-9 patching pipeline:

| Font | Old size (pt) | New size (pt) | Used for |
|---|---|---|---|
| `font_tiempos` | 56 | 22 | Screen titles |
| `font_styrene_48` | 48 | 18 | Big % numbers |
| `font_styrene_28` | 28 | 12 | Labels, info rows |
| `font_styrene_24` | 24 | 10 | Credit line |
| `font_styrene_20` | 20 | — | Dropped, not needed |
| `font_mono_32` | 32 | 14 | Animation label, MAC |

**Icons** (`icons.h`) — re-exported smaller via `tools/png_to_lvgl.js`:
- Bluetooth icon: ~24×24 (was ~56×56)
- Trash icon: ~16×16 (was ~32×32)
- No alpha needed (icons sit on opaque panels) — raw RGB565.

**Logo** (`logo.h`) — re-exported at 48×48 (was 80×80), retained as RGB565A8 since it composites over the splash.

**Splash animations** (`splash_animations.h`) — unchanged. Data is resolution-independent; only the runtime `UPSCALE` constant in `splash.cpp` changes from 24 to 12.

## 9. Migration & testing

**Host-side migration (one-time, when swapping boards):**
1. `systemctl --user stop claude-usage-daemon`
2. `rm -rf ~/.config/claude-usage-monitor/`
3. `bluetoothctl remove <old-MAC>` (clears bluez's record of the old device)
4. Pull updated `daemon/claude-usage-daemon.sh` (already part of this port)
5. `systemctl --user daemon-reload && systemctl --user start claude-usage-daemon`

These steps will be documented in the project README as part of the implementation.

**Test plan:**
- Splash renders; palette-color bars visible; long-press cycles animations; short-tap dismisses to last non-splash screen.
- Usage screen renders both panels with values from a manually-injected BLE payload (test payload script to be added to `tools/` if not already present).
- Bluetooth screen: status text updates on connect/disconnect; MAC visible; long-press on reset zone clears bonds and reflects in the status.
- Touch calibration runs on first boot, persists across reboots, recovers cleanly from `touch-cal`.
- `screenshot.sh` produces a valid 320×240 PNG.
- Daemon connects, caches MAC, survives a disconnect/reconnect cycle (>1).

## 10. Risks & open questions

- **Resistive touch feel.** XPT2046 needs deliberate finger pressure compared to the CST9220 capacitive panel. The calibration step mitigates accuracy issues but not the "press harder" feel. Acceptable for a glanceable monitor; flagged here so it isn't a surprise.
- **No PSRAM.** Two 25 KB DMA buffers + LVGL internal state + NimBLE + Arduino runtime should fit comfortably in ~320 KB SRAM, but worth measuring early. If we run tight, the second draw buffer can be dropped (single-buffer partial rendering, slightly more tearing).
- **CH340 driver on Windows.** Not all CH340 clones work with the default Windows driver. If `pio upload` can't see the port, the user installs the WCH driver — not a code issue.
