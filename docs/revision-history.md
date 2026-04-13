# Revision History

## Unreleased — LittleFS config backup & restore

**Branch:** `LittleFS`

### Problem
Flashing a new `littlefs.bin` replaces the entire LittleFS partition. Runtime config files
(`settings.json`, `mqtt.json`, `homing.json`) are erased in the process. The previous
documentation incorrectly stated that these files were preserved.

### Changes

**`src/webserver.cpp`**
- Added `POST /upload` endpoint for restoring config files after a LittleFS reflash.
  Only `settings.json`, `mqtt.json`, and `homing.json` are accepted; all other filenames
  are rejected. The device must be rebooted after upload to apply the restored settings.

**`docs/development-guide.md`**
- Replaced the incorrect "settings are preserved" note in the OTA section with a warning
  and a step-by-step backup/restore procedure using `curl` (run on the PC).

**`docs/user-guide.md`**
- Removed the false claim that `mqtt.json` survives a LittleFS OTA flash; added a
  reference to the development guide instead.

**`data/index.html`, `data/style.css`**
- Added a reload button (⟳) in the top-right corner of the header. Calls
  `location.reload()` on click. Needed because iOS home-screen web apps have no
  address bar and do not support pull-to-refresh; the button provides a reliable
  way to reconnect after the device was off.

---

## v1.0.0 — Initial release (2026-03-24)

**Tag:** `v1.0.0`

First public release. Features:

- ESP8266 (D1 Mini) firmware with WS2812B LED ring support (up to 3 × 60 LEDs)
- NTP time synchronisation with configurable timezone offset
- LED rendering pipeline: Ambient layer, Time layer (hour/minute/second hands with tails),
  SFX layer (Short Circuit, Radar, Heartbeat, Shooting Star)
- Auto-brightness via BH1750 lux sensor; manual fallback
- Night mode with configurable on/off times
- Stepper motor support with PLL grid-mode, Austin ramp, and auto-homing
- AsyncWebServer (port 80) with WebSocket real-time configuration UI
- OTA update server (port 8080) for firmware and filesystem images
- LittleFS persistence: `settings.json`, `mqtt.json`, `homing.json`
- MQTT client (optional, disabled by default)
- AP mode with captive portal for initial Wi-Fi setup
- Release binaries: `firmware.bin`, `littlefs.bin`
