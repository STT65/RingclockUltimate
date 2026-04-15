# Requirements — Ringclock Ultimate

Ringclock Ultimate is an ESP8266-based firmware for a hybrid LED ring clock. Time is displayed on
one to three WS2812B LED rings, where a coloured LED sweeps around the ring like a clock hand — and
optionally a physical stepper motor hand moves on the same face, driven by a hardware timer for
jitter-free precision.

All features are configured through a browser-based web interface over Wi-Fi. The interface is
organised in eight tabs: **Brightness, Ambient, Time, Effects, Motor, Night, MQTT,** and **System**.
No firmware rebuild is required for day-to-day configuration.

This document captures the firmware requirements. Requirements are listed first by feature in the
order they appear in the web interface, followed by general infrastructure requirements (platform,
connectivity, persistence, and build) that do not correspond to a specific UI tab.

---

## Table of Contents

1. [Brightness](#1-brightness)
2. [Ambient](#2-ambient)
3. [Time Hands](#3-time-hands)
4. [Special Effects (SFX)](#4-special-effects-sfx)
5. [Stepper Motor](#5-stepper-motor)
6. [Night Mode](#6-night-mode)
7. [MQTT](#7-mqtt)
8. [System & Live Monitoring](#8-system--live-monitoring)
9. [Web Interface](#9-web-interface)
10. [Wi-Fi & Network](#10-wi-fi--network)
11. [Time Synchronisation](#11-time-synchronisation)
12. [LED Rings & Rendering Pipeline](#12-led-rings--rendering-pipeline)
13. [Serial Logging](#13-serial-logging)
14. [Persistence & Config Files](#14-persistence--config-files)
15. [OTA Update](#15-ota-update)
16. [Release Build](#16-release-build)
17. [Platform & Toolchain](#17-platform--toolchain)
18. [Pin Assignment](#18-pin-assignment)

---

## 1. Brightness

The LED rings can draw significant current and need to adapt to different lighting conditions —
bright in a daylit room, dimmed at night. Two control modes are available: a fixed manual level
and an automatic mode driven by a light sensor. A hard power ceiling prevents the total LED
current from exceeding what the power supply can deliver.

- **Manual mode:** a fixed brightness level (0–255) shall be applied at all times.
- **Automatic mode:** brightness shall be derived from a **BH1750** ambient light reading with
  configurable min/max brightness and lux threshold.
- The firmware shall continuously estimate total LED current draw and throttle brightness
  automatically if the estimate exceeds the configured **Power Limit**.
- The current brightness value (0–255) shall be included in the live monitoring data.

---

## 2. Ambient

The ambient layer gives the clock face a decorative background that is always visible behind the
moving hands. Without it the rings are dark except for the hands themselves, which makes the ring
shape hard to read at a glance. Three independently configurable sub-layers — a general fill,
hour marks at all 12 positions, and quarter marks at 12/3/6/9 — let the user set up anything
from a minimal tick-mark ring to a fully illuminated dial.

- The ambient layer shall support three independently configurable sub-layers, applied in order:
  1. **Ambient Light** — all LEDs in a single background colour
  2. **Hour Markers** — accent LEDs at all 12 hour positions; overrides ambient colour
  3. **Quarter Markers** — accent LEDs at 12, 3, 6, 9 o'clock; highest priority within the ambient layer
- Each sub-layer shall have an independent configurable colour and enable flag.

---

## 3. Time Hands

The time layer renders the three clock hands — second, minute, hour — as coloured LEDs on the
rings. Each hand has a gradient trail in both directions so the current position remains visually
clear even when hands overlap. Because the clock may have multiple rings, each hand can be
assigned to any combination of rings via a bitmask — hours, minutes, and seconds can each live
on their own dedicated ring, or all three can share a single ring.

Each hand (second, minute, hour) shall have independently configurable:

- **Tip colour** — the LED at the current position
- **Tail start colour** — root colour shared by both trails (located behind the tip)
- **Backward tail end colour** — far end of the trail behind the tip
- **Forward tail end colour** — far end of the trail ahead of the tip
- **Forward trail length** (0 = tip only, 1–10 = N LEDs, 11 = fill to 12 o'clock with full gradient)
- **Backward trail length** (same range)
- **Ring assignment** (bitmask) — each hand shall be assignable to any combination of rings;
  a hand with no ring assigned shall be hidden

---

## 4. Special Effects (SFX)

Periodic visual animations add life to the clock beyond the steady sweep of the hands. Each
effect plays on a configurable interval aligned to midnight and can also be triggered immediately
from the web interface for preview. If multiple effects are due at the same minute, only one fires.

Each of the four effects shall have a configurable **interval** (0 = disabled; otherwise triggers
every N minutes, aligned to midnight) and a **manual trigger** command.

| Effect | Duration | Description |
|---|---|---|
| **Short Circuit** | ~1 s | Irregular cold-white flickering across all LEDs |
| **Radar** | ~6 s | Green phosphor-style sweep, 3 revolutions |
| **Shooting Star** | ~1 s | Bright streak moving around the ring |
| **Heartbeat** | ~5 s | Lubb-dupp brightness pulse; configurable intensity (modulation depth) |

If multiple effects are due at the same minute, only the one with the largest interval shall fire.

---

## 5. Stepper Motor

The stepper motor adds a physical, mechanical dimension to the clock — a real hand sweeping
around the face. Because the ISR runs independently of Wi-Fi, TCP, and rendering, the hand never
jitters or skips even under heavy network load.

- The motor shall be driven by a **hardware timer ISR** to prevent jitter under network or
  rendering load.
- ISR-shared variables shall be declared `volatile`; writes from the main loop shall be atomic
  or guarded.

### Motor Modes

| Mode | Value | Behaviour |
|---|---|---|
| Off | 0 | Motor seeks to 12 o'clock then de-energises |
| Hours | 1 | One revolution = 12 hours |
| Minutes | 2 | One revolution = 60 minutes |
| Seconds | 3 | One revolution = 60 seconds |
| Homing | 4 | Calibration mode |

### Time Grid

| Setting | Behaviour |
|---|---|
| Analog (smooth) | Motor runs continuously, locked to NTP time |
| Grid | Motor snaps to discrete time positions using a configurable speed and acceleration ramp (Austin ramp) |

- In Grid mode, a **ramp monitor** shall show the ramp duration and flag a warning if the ramp
  takes longer than the grid interval.

### Motor Homing

A stepper motor has no absolute position sensor. At power-on the firmware cannot know where the
rotor is, so a calibration procedure establishes the 12 o'clock reference. The calibrated
zero-point offset is stored permanently in `homing.json` and survives reboots. Two procedures
exist depending on whether a Hall sensor is installed.

- The motor shall support homing via **Hall sensor** (`MOTOR_AH_EN = 1`) or **manual
  calibration** (`MOTOR_AH_EN = 0`).
- Hall-sensor homing shall consist of two phases: a **travel phase** (motor rotates CCW at seek
  speed until it exits the sensor zone) followed by a **measure phase** (motor rotates CW through
  the zone for `MOTOR_AH_PASSES` passes at `MOTOR_AH_SPEED`). The circular average of all pass
  midpoints is the sensor centre `M`; the motor then seeks to `M + autoHomingFinePitch`. A
  configurable **watchdog timeout** (`MOTOR_AH_WDT_MS`) aborts homing if no Hall signal is
  detected.
- After homing, the user shall be able to **jog** (±1, ±10, ±100 steps) to fine-tune the zero
  position.
- The accepted zero-point offset shall be written to `homing.json`.
- **Auto-homing on boot** shall be selectable when a Hall sensor is installed.

---

## 6. Night Mode

Clocks in bedrooms or living rooms should be unobtrusive at night. Night mode defines a daily
time window during which selected features are automatically suppressed. Each suppression flag
is independent — the user can dim the LEDs and silence the effects without stopping the motor,
for example.

Night mode shall suppress selected features during a configurable daily time window.
User settings are never modified by night mode — the configured values remain intact and
are fully restored as soon as the night window ends. The web interface shall always display
the user's configured values and visually indicate which parameters are currently overridden
by night mode.

Settings:
- Enable/disable flag
- Start time and end time (HH:MM)
- A live **currently active** indicator shall be pushed to the UI every second

The following features shall each be independently selectable for suppression during the night
window:

| Feature | Effect |
|---|---|
| Dim LEDs | Reduce brightness to a configurable Night Brightness level |
| Disable SFX | Pause all SFX effect intervals |
| Hide second hand | Switch off the seconds hand |
| Park motor at 12 o'clock | Motor seeks to 12 o'clock and de-energises during the night window |
| Hide ambient markers | Hour and quarter markers are hidden |

---

## 7. MQTT

MQTT integration lets the clock participate in a home automation system (Home Assistant,
Node-RED, FHEM, …). Every setting that is accessible through the web interface can also be
written over MQTT, and live status values are published every second. The integration is
optional and disabled by default.

### Connection

- The client shall reconnect automatically on disconnection.
- MQTT credentials shall be stored in `mqtt.json`, separately from other settings.
- The **MQTT password** shall never be sent to the browser.

### Topic Structure

All topics follow `<base>/<type>/<key>` (default base: `ringclock`):

| Pattern | Direction | Purpose |
|---|---|---|
| `<base>/set/<key>` | → Device | Write a setting; device echoes the new value on `get/<key>` |
| `<base>/get/<key>` | ← Device | Current value, published on connect, after each setting change, and on a full publish |
| `<base>/cmd/<command>` | → Device | One-shot command |
| `<base>/status/<key>` | ← Device | Live readings, published every second |
| `<base>/status/online` | ← Device | `1` on connect, `0` as Last Will (retained) |

Note: the device subscribes to `set/#` and `cmd/#` only. Incoming messages on `get/<key>` are
not consumed — `get/<key>` is a state-output topic, not a query interface.

### Commands

| Command | Effect |
|---|---|
| `save` | Persist all settings to flash |
| `reboot` | Restart the device |
| `resync` | Resynchronise motor position to current time |
| `sfxShortCircuitTrigger` | Trigger Short Circuit effect |
| `sfxRadarTrigger` | Trigger Radar effect |
| `sfxShootingStarTrigger` | Trigger Shooting Star effect |
| `sfxHeartbeatTrigger` | Trigger Heartbeat effect |

### Live Status Topics (published every second)

| Topic | Value |
|---|---|
| `<base>/status/lux` | Ambient light [lux] |
| `<base>/status/current_mA` | Estimated LED current [mA] |
| `<base>/status/rssi` | Wi-Fi signal [dBm] |
| `<base>/status/uptime` | Uptime [s] |

---

## 8. System & Live Monitoring

The System tab consolidates global settings that cut across multiple features — the current limit
that caps LED brightness, the log verbosity for debugging, and the timezone that governs night
mode and NTP display. A live data stream updates every second so the current device state is
always visible without reloading the page.

### Live Monitoring (WebSocket, every second)

The firmware shall broadcast a JSON monitoring packet once per second to all connected WebSocket
clients:

| Field | Description |
|---|---|
| `lux` | Ambient light [lux] |
| `brightness` | Filtered system brightness (0–255) |
| `current_mA` | Estimated LED current [mA] |
| `rssi` | Wi-Fi RSSI [dBm] |
| `ip` | Current IP address |
| `uptime` | Uptime [s] |
| `localTime` | Current local time (HH:MM:SS) |
| `mqttConnected` | MQTT connection status |
| `nightActive` | Night mode currently active |
| `rampDurationMs` | Grid-mode ramp duration [ms] (only after first ramp) |
| `rampStepsTotal` | Total steps in last ramp |
| `rampMissedSteps` | Missed steps in last ramp |
| `homingState` | Homing state: idle/travel/measure/done/error (only when `MOTOR_AH_EN = 1`) |

### Serial Logging — Runtime Control

- The **runtime log level** shall be adjustable via the web UI (System tab) and MQTT without
  reflashing.

---

## 9. Web Interface

The web interface is the primary way users interact with the clock. The entire configuration
lives in a browser page served from the device itself — no external server or app is involved.
The interface must load reliably on all common browsers including iOS Safari, where WebSocket
connections can be torn down during page navigation before the initial data arrives.

- Static web assets (`index.html`, `script.js`, `style.css`) shall be served from **LittleFS**
  over HTTP on **port 80**.
- The UI shall use a **WebSocket** (`/ws`) for real-time bidirectional communication (live
  monitoring updates and settings changes).
- Settings changes shall take effect **immediately** without a page reload.
- Settings shall be written to flash only on explicit **Save Settings** command.
- The UI shall be organised in tabs: **Brightness, Ambient, Time, Effects, Motor, Night, MQTT,
  System**.
- A **reload button** (⟳) in the header shall re-trigger the currently active tab's click
  handler — removing and re-applying the active CSS state and re-fetching settings via HTTP GET
  `/settings`. This resets stuck UI state without a full page reload, which is needed on iOS
  home-screen web apps where pull-to-refresh is unavailable.
- An HTTP `GET /settings` endpoint shall return the full settings JSON (for initial page load
  and after reconnect). The response shall include an `Access-Control-Allow-Origin: *` header
  to support LiveServer development.

---

## 10. Wi-Fi & Network

- The device shall connect to a configured home WLAN (SSID/password stored in the ESP's secure NVS flash partition).
- If no credentials are stored or the connection fails, the device shall open a **Wi-Fi Access
  Point** with a captive portal for initial setup.
  - The AP SSID shall include the last 4 digits of the MAC address: `Ringclock-Ultimate-XXXX`.
  - The captive portal shall redirect Android, Windows, and Apple detection URLs to the setup
    page.
- After Wi-Fi setup the device shall reboot and connect to the configured network.
- The device shall advertise itself via **mDNS** as `Ringclock-Ultimate-XXXX.local`.
- The device shall provide an **Erase WiFi** function that clears stored credentials and reboots
  into AP mode.

---

## 11. Time Synchronisation

- The device shall synchronise time via **NTP** at a configurable interval.
- The NTP server URL shall be configurable.
- A configurable **timezone** string shall be applied with automatic DST handling.

---

## 12. LED Rings & Rendering Pipeline

The LED output is the most timing-sensitive part of the firmware. The NeoPixelBus async UART
method serialises pixel data in the background over UART1 (GPIO2/D4) without disabling
interrupts, keeping the stepper motor ISR unaffected. All compositing happens in linear colour
space; gamma correction is applied once at the very end so the visual result is perceptually
uniform.

- The firmware shall support **1 to 3 WS2812B LED rings**, all daisy-chained on a single data
  line (D4/UART1).
- Each ring shall have a configurable LED count (multiple of 12; range 12–240).
- The LED output shall use an **async UART driver** (NeoPixelBus) so that `Show()` never
  disables interrupts.
- A hardware-level note: the D4 output (3.3 V) shall be level-shifted to 5 V via a
  **74HCT125** before the first ring's DIN.
- The rendering pipeline shall apply **gamma correction** (configurable exponent) once at the
  final output stage; all layers shall work in linear colour space.
- The frame rate shall be **dynamic**: 1 fps at idle (hands only), up to 60 fps during active
  SFX.

### Rendering Pipeline (layer order, bottom to top)

1. Ambient layer
2. Time layer (second, minute, hour hands)
3. SFX layer

Higher layers override the pixels they occupy.

---

## 13. Serial Logging

- All log output shall be sent via UART at **115200 baud**.
- A **compile-time log-level floor** (`LOG_LEVEL_DEFAULT`) shall strip messages below the
  threshold from the binary entirely.
- Per-module enable/disable flags shall allow individual modules to be silenced with zero binary
  overhead.

---

## 14. Persistence & Config Files

- Runtime configuration shall be stored in three LittleFS JSON files:
  - `mqtt.json` — MQTT connection parameters (broker, credentials)
  - `homing.json` — motor zero-point offset
  - `settings.json` — all other configuration parameters: LED, brightness, time, night mode, and motor settings
- Since config files are erased when a new LittleFS image is flashed (OTA filesystem update replaces
  the entire partition) the device shall give read and write access to the files in order to backup and restore them:
  - `GET /settings.json`, `GET /mqtt.json`, `GET /homing.json` — download current config
  - `POST /upload` (multipart) — restore a config file after reflash; only `settings.json`,
    `mqtt.json`, and `homing.json` shall be accepted; all other filenames shall be rejected

---

## 15. OTA Update

- OTA updates shall be available via a password-protected web interface on **port 8080** at
  path `/update`.
- Credentials shall be configurable in `config.h` (`OTA_USERNAME`, `OTA_PASSWORD`).
- Two upload targets shall be supported: **firmware** (`.bin`) and **filesystem** (LittleFS.bin
  image).
- OTA upload shall also be configurable via PlatformIO (`espota`).

---

## 16. Release Build

- A `release` PlatformIO environment shall produce a deployment-ready LittleFS image.
- A pre-build script shall patch `script.js` before the filesystem build: replacing hardcoded
  dev IP addresses with `location.host`-relative URLs. The original `data/` directory shall
  never be modified.
- The source file shall retain the hardcoded dev IP as a comment for LiveServer development;
  it shall never be committed with the production line commented out.

---

## 17. Platform & Toolchain

- The firmware shall run on an **ESP8266 D1 Mini**.
- The project shall use **PlatformIO** as build system and **C++** as implementation language.
- Source code shall be organised in modules — one module per `.cpp`/`.h` pair.
- All public APIs and non-trivial logic shall be documented with **Doxygen** comments; the docs
  shall build without errors.
- Compile-time constants (pin numbers, timing values, feature flags) shall be defined
  exclusively in `config.h`.

---

## 18. Pin Assignment (fixed)

| Function | Pin | GPIO |
|---|---|---|
| I²C SDA (BH1750) | D1 | GPIO5 |
| I²C SCL (BH1750) | D2 | GPIO4 |
| LED rings 1–3 data (all rings daisy-chained) | D4 | GPIO2 (UART1 TX) |
| Hall sensor (homing) | D5 | GPIO14 |
| Stepper motor STEP | D3 | GPIO0 |
| Stepper motor DIR | D6 | GPIO12 |
| Stepper motor EN | D7 | GPIO13 |

All LED rings share a single data line (D4). GPIO2 doubles as UART1 TX, used by NeoPixelBus
for interrupt-safe async output. Only seven pins are used; D0, D8, and a microstepping (MS) pin
are not assigned.
