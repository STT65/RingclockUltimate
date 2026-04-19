# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

---

## [1.1.0] - 2026-04-19

### Added

- Motor hours grid option `1/5h` (12-minute steps) — the finest available resolution,
  matching the native 60-position grid of Minutes and Seconds modes.
- Config files (`settings.json`, `mqtt.json`, `homing.json`) are now bundled in the
  LittleFS image, so the device starts with defaults after a fresh flash.
- `POST /upload` endpoint — config files can now be backed up and restored around a
  LittleFS reflash. See *Development Guide → OTA Update* for the procedure.
- Web GUI: amber highlight on any setting currently suppressed by Night Mode, so you
  know at a glance what Night Mode is overriding.
- Web GUI: Reload button (⟳) in the header, useful for iOS home-screen web app users.

### Changed

- Night Mode now applies its overrides at runtime without touching saved settings —
  everything reverts automatically when the night window ends.
- SFX trigger intervals replaced with a time picker (`HH:MM`) instead of a slider.
  Intervals are aligned to midnight; e.g. `01:00` fires every hour on the hour,
  `14:00` fires once a day at 14:00.
- `motorGridH` index values renumbered: `1/5h` inserted at position 1; previous values
  1–4 shift to 2–5. **Existing configs with a non-zero Hours grid must be reconfigured.**

### Fixed

- Shooting Star: the last LED in the animation stayed lit until the next regular tick.
  It now turns off correctly at animation end.
- Web GUI: reconnect after a disconnect was unreliable.
- Documentation incorrectly stated that config files survive a LittleFS OTA flash.

---

## [1.0.0] - 2026-03-24

### Added

- ESP8266 (D1 Mini) firmware with WS2812B LED ring support (up to 3 × 60 LEDs)
- NTP time synchronisation with configurable timezone offset
- LED rendering pipeline: Ambient layer, Time layer (hour/minute/second hands with
  tails), SFX layer (Short Circuit, Radar, Heartbeat, Shooting Star)
- Auto-brightness via BH1750 lux sensor; manual fallback
- Night mode with configurable on/off times
- Stepper motor support with PLL analog mode, Austin ramp grid mode, and auto-homing
- AsyncWebServer (port 80) with WebSocket real-time configuration UI
- OTA update server (port 8080) for firmware and filesystem images
- LittleFS persistence: `settings.json`, `mqtt.json`, `homing.json`
- MQTT client (optional, disabled by default)
- AP mode with captive portal for initial Wi-Fi setup
