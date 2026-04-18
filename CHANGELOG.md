# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Motor hours grid option `1/5h` (12-minute intervals, 60 positions/revolution) — aligns
  the hours grid with the native 60-position grid used by Minutes and Seconds modes.
- Night mode shadow variables: `night_mode.cpp` exposes effective runtime variables
  (`NightMode::motorMode`, `NightMode::autoBrightness`, etc.) recomputed every loop.
  Settings are never mutated; all values restore automatically when the night window ends.
- Web GUI amber indicators — parameters currently overridden by night mode are highlighted
  with an amber left border, updating within one second of a state change.
- `POST /upload` endpoint for restoring config files (`settings.json`, `mqtt.json`,
  `homing.json`) after a LittleFS reflash.
- Reload button (⟳) in the web GUI header for iOS home-screen web app users.

### Changed

- `motorGridH` index values renumbered: `1/5h` inserted at position 1; ¼h…3h shifted
  from 1–4 to 2–5. **Existing configs with a non-zero Hours grid must be reconfigured.**
- All consuming modules (`motor.cpp`, `time_state.cpp`, `layer_*.cpp`, `brightness.cpp`)
  now read `NightMode::` shadow variables instead of `Settings::` directly.

### Fixed

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
