# Project context

ESP32 firmware for a desk-side Claude Code usage monitor on an **AOKIN CYD board (ESP32-2432S028R, 320×240 landscape resistive-touch TFT)**. Connects to a host daemon over BLE; daemon polls Anthropic API for usage data.

This file is for future Claude Code sessions to bootstrap quickly. Read this first.

> Forked from the upstream Clawdmeter project, which targets a Waveshare ESP32-S3 AMOLED board with auto-rotation, battery, and physical buttons. The CYD port drops all of that. Spec: `docs/superpowers/specs/2026-05-19-cyd-port-design.md`. Plan: `docs/superpowers/plans/2026-05-19-cyd-port.md`.

## Hardware (critical pins)

- MCU: classic ESP32 (Tensilica LX6 dual-core, no PSRAM). PlatformIO `board = esp32dev`, `platform = espressif32@6.7.0`, `partitions = huge_app.csv`.
- Display: **ST7789 (not ILI9341)** 320×240 IPS, SPI. Wired on **HSPI** (`USE_HSPI_PORT=1`). Pins MISO=12, MOSI=13, SCLK=14, CS=15, DC=2, RST=-1, BL=21.
- Touch: **XPT2046** resistive on the same HSPI bus, CS=33. Calibration is persisted in NVS under namespace `clawd` key `tcal_v1`; first boot runs the cal screen, subsequent boots load from NVS. Serial command `touch-cal` wipes NVS and reboots into recalibration.
- No PMU, no IMU, no physical buttons (BOOT button is flashing-only). Reserved-but-unused pins documented in `firmware/src/display_cfg.h`: GPIO 26 (speaker amp), 34 (LDR), 5 (SD CS), 4/16/17 (RGB LED), 0 (BOOT).

**The CYD form factor is sold with multiple display controllers** (ILI9341 in older batches, ST7789 in newer including the AOKIN board we use), and product listings rarely disclose which. If you see weird color shifts or rotation/MADCTL issues, suspect chip identity before suspecting your code.

A working **non-LVGL reference** for this exact physical board lives at `D:\src\Electronics\CarDashboard` — diff against its `platformio.ini` first when in doubt about any panel-driver setting.

Known-good TFT_eSPI `build_flags` (all in `platformio.ini` since `USER_SETUP_LOADED=1`):

```ini
-DUSER_SETUP_LOADED=1
-DST7789_DRIVER=1
-DTFT_WIDTH=240
-DTFT_HEIGHT=320
-DTFT_RGB_ORDER=TFT_BGR
-DTFT_INVERSION_OFF=1        ; ST7789 defaults to inverted; turn off explicitly
-DTFT_MISO=12 -DTFT_MOSI=13 -DTFT_SCLK=14 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=-1
-DUSE_HSPI_PORT=1            ; CYD wires TFT on HSPI not VSPI
-DTFT_BL=21 -DTFT_BACKLIGHT_ON=HIGH
-DSPI_FREQUENCY=55000000
-DTOUCH_CS=33                ; XPT2046
```

Runtime setup is just `tft.init()` + `tft.setRotation(1)` — **no manual MADCTL writes, no `TFT_RGB_ORDER` overrides via macro tricks**. Because LVGL's flush callback uses `pushPixelsDMA`, also call `tft.setSwapBytes(true)` once at init (affects `pushPixelsDMA` only — not `fillRect`/`drawString`).

## Architecture

```text
main.cpp        — setup(), loop(), BLE wire-up, screenshot serial command
display_cfg.h   — pin macros (all panel pins come from platformio.ini build_flags)
ui.{h,cpp}      — 2 functional screens (Usage, Bluetooth) + Splash; tap cycles Usage↔Bluetooth, long-press on BT reset zone clears bonds
splash.{h,cpp}  — 20×20 pixel-art animation engine, 12× upscale to 240×240 centered, palette[0] fills the 40px side bars
touch.{h,cpp}   — XPT2046 polling via tft.getTouch(); NVS-persisted calibration
ble.{h,cpp}     — NimBLE peripheral: custom data service (UUIDs unchanged from upstream) + HID keyboard (no buttons today but kept for future use)
usage_rate.{h,cpp} — running-window classifier of session_pct → animation group
data.h          — UsageData struct (wire format unchanged)
icons.h         — icon arrays sized for 320×240 (BT 24×24, trash 16×16)
logo.h          — 48×48 RGB565A8 logo
font_*.c        — LVGL 9 bitmap fonts (Tiempos 22, Styrene 12/16/28, Mono 18)
splash_animations.h — generated, do not hand-edit
```

## Build / flash

Two envs exist for the two CYD display variants — use `ESP32_2432S028R_ST7789` for the AOKIN board this project targets; `ESP32_2432S028R_ILI9341` is for older CYD batches with the ILI9341 controller.

```bash
pio run -d firmware -e ESP32_2432S028R_ST7789                # build
pio run -d firmware -e ESP32_2432S028R_ST7789 -t upload      # flash; CH340 USB-UART, picks the COM/ttyUSB automatically
pio device monitor -e ESP32_2432S028R_ST7789                 # serial @ 115200
```

If `pio` isn't on PATH on Windows: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

The CYD uses a **CH340 USB-UART**, not Espressif USB JTAG. On Windows it enumerates as `COM*`; on Linux as `/dev/ttyUSB*`. The CH340 driver ships with current Windows but may need installing on older systems. No boot-mode gymnastics — direct `pio … -t upload` Just Works.

## QA your own UI changes — don't ask the user

The firmware ships a `screenshot` serial command that streams the LVGL screen as raw RGB565 over USB serial, framed by `SCREENSHOT_START w h size` / `SCREENSHOT_END`. `./screenshot.sh out.png COM3` (or `/dev/ttyUSB0`) captures a 320×240 PNG. **Use this on every UI iteration** — Read the PNG with the Read tool, verify visually, iterate.

Boot screen is `SCREEN_SPLASH` and only advances on a screen tap, so a fresh flash sits on the splash. To screenshot the screen you're editing without asking the user to tap, **temporarily change the default boot screen** in `main.cpp` (search for `ui_show_screen(SCREEN_SPLASH);`) to `SCREEN_USAGE` / `SCREEN_BLUETOOTH`, iterate, then revert before committing.

## Critical gotchas

1. **No PSRAM — DRAM is the binding constraint.** LVGL render buffers, NimBLE bond tables, ArduinoJson heap docs, and font glyph tables all share the ~327 KB of internal SRAM. We use **two 320×10 RGB565 partial-render buffers** (~6.4 KB each); each previous halving was forced by a `dram0_0_seg` overflow as more subsystems linked in. If you add a feature that touches DRAM (e.g. a bigger font), expect to either shrink buffers further or drop to single-buffer.
2. **LVGL 9 font patching.** `lv_font_conv` outputs LVGL 8 format. Must remove `#if LVGL_VERSION_MAJOR >= 8` guards, drop `.cache` field, add `.release_glyph`, `.kerning`, `.static_bitmap`, `.fallback`, `.user_data`. Without patching, fonts render invisible.
3. **Touch reading must be centralized.** `tft.getTouch()` does an SPI transaction that costs ~ms. `touch_read()` is called once per main-loop iteration in `main.cpp`; LVGL's `lvgl_touch_read_cb` and any other readers consume the shared `touch_pressed/touch_x/touch_y` state.
4. **First-boot calibration is mandatory.** XPT2046 raw values vary enough across panels that taps land tens-of-pixels off without per-device calibration. The cal screen runs once when NVS has no entry; thereafter `tft.setTouch(cal_data)` is applied at init. `touch-cal` serial command clears NVS and reboots if calibration drifts.
5. **`tft.setSwapBytes(true)`** is required at init because LVGL hands the flush callback little-endian RGB565 but `pushPixelsDMA` writes raw bytes. It affects ONLY `pushPixelsDMA` — direct `tft.fillRect`/`drawString` calls don't swap.
6. **LVGL RGB565A8 is planar.** `w*h` RGB565 pixels followed by `w*h` alpha bytes; `data_size = w*h*3`, `stride = w*2`. Use `init_icon_dsc_rgb565a8()` for icons over non-uniform backgrounds. Lucide source PNGs are black-on-transparent — converter must tint or icons render invisible. See `tools/png_to_lvgl.js`.
7. **`huge_app.csv` partition is mandatory.** LVGL + NimBLE + fonts together exceed the 1.5 MB default app slot. If the linker complains about image size, confirm `board_build.partitions = huge_app.csv` in `platformio.ini`.

## Icons

`tools/png_to_lvgl.js <input.png> <symbol> [W_MACRO] [H_MACRO] [--tint=RRGGBB | --no-tint]` converts an alpha PNG to RGB565A8. Default tint is white (`0xFFFFFF`) — necessary for Lucide PNGs. Splice output into `firmware/src/icons.h` and use `init_icon_dsc_rgb565a8()` in `ui.cpp`.

## Splash animations

13 × 20×20 pixel-art creature animations sourced from
[claudepix.vercel.app](https://claudepix.vercel.app). Pipeline:

```bash
node tools/scrape_claudepix.js  # → tools/claudepix_data/*.json
node tools/convert_to_c.js      # → firmware/src/splash_animations.h
```

Each animation has a per-animation 10-color RGB565 palette. Cell values 0..9 index it. The 20×20 native grid is upscaled 12× to 240×240 and centered on the 320×240 panel; the leftover 40px columns on either side are filled with `palette[0]` so the bars blend with the animation rather than letterboxing in black. `usage_rate_sample()` classifies the current session burn-rate into a group, and `splash_pick_for_current_rate()` swaps to a creature appropriate to that group on the next state change.

## User profile / preferences

See `~/.claude/projects/D--src-Electronics-Clawdmeter/memory/` files for persistent context (user is an embedded-beginner senior dev, brand-conscious, prefers iterative UI refinement, dislikes me authoring my own art when third-party assets are intended). Always read those memory files at session start.

## Daemon / host side

The CYD port **preserved the existing BLE wire protocol** (name, UUIDs, JSON shape) so the upstream daemon works against the CYD firmware unchanged. The daemon section below still describes the in-tree `daemon/claude-usage-daemon.sh`.

Bash daemon (`daemon/claude-usage-daemon.sh`) reads OAuth token, polls Anthropic API, sends JSON over BLE GATT. Run with `systemctl --user start claude-usage-daemon`. The unit file's `ExecStart` is the absolute path to the script — repoint it when switching between the worktree and the main checkout.

**Discovery & resilience:**

- Connects by name (`"Claude Controller"`) on first run, caches resolved MAC at `~/.config/claude-usage-monitor/ble-address`. ESP32 BLE addresses are factory-burned per-chip, so swapping any board invalidates the cache.
- On connect failure: cache is dropped AND device is removed from bluez (`bluetoothctl remove`) so the next scan won't re-pick a dead MAC. Multi-candidate scans pick `head -1` and let the failure cycle converge.
- `POLL_INTERVAL=60`, `TICK=5`. Inner loop wakes every 5s to detect disconnects fast; polls Anthropic when 60s elapsed OR when ESP fires a refresh request.

**GATT characteristics on service `4c41555a-...0001`:**

- `...0002` RX — daemon writes JSON usage payload here.
- `...0003` TX — firmware notifies ack/nack (daemon doesn't subscribe).
- `...0004` REQ — firmware fires `0x01` notify in `onSubscribe` if `has_received_data` is false. Daemon subscribes via `setsid bash -c "stdbuf -oL dbus-monitor … | awk …"`; awk drops a flag file the inner loop picks up. See the `feedback_dbus_monitor_pipe` memory for the three subtle gotchas (pipe buffering, busctl-exits race, `wait` blocking on pipeline jobs).
