# Ringclock Ultimate — User Guide

Ringclock Ultimate is an ESP8266-based firmware for a hybrid LED ring clock. Time is displayed on
one to three WS2812B LED rings, where a coloured LED sweeps around the ring like a clock hand — and
optionally a real physical stepper motor hand moves on the same face.

What makes this clock different from a simple LED strip project is the combination of several
independently configurable display layers: colour-coded hands with gradient trails, a decorative
ambient background, automatic brightness adaptation to the room light, scheduled night dimming,
visual special effects, and a mechanical motor pointer that tracks time with hardware-timer precision.
All of this is controlled entirely through a browser-based web interface over Wi-Fi — no app, no
cloud service, and no need to reflash the firmware for day-to-day configuration.

This guide walks you through the initial setup and explains every configurable feature.

---

## Table of Contents

1. [First-time Setup](#1-first-time-setup)
2. [Brightness](#2-brightness)
3. [Ambient](#3-ambient)
4. [Time Hands](#4-time-hands)
5. [Special Effects](#5-special-effects)
6. [Stepper Motor](#6-stepper-motor)
7. [Night Mode](#7-night-mode)
8. [MQTT](#8-mqtt)
9. [System](#9-system)
10. [Compile-time Configuration](#10-compile-time-configuration)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. First-time Setup

Wi-Fi is the backbone of almost everything this clock does. The device fetches accurate time from
an NTP server over Wi-Fi, hosts its configuration interface as a local web page, optionally connects
to an MQTT broker for home automation, and receives firmware updates wirelessly. Without a working
Wi-Fi connection, the clock cannot display correct time.

### Connecting to Wi-Fi

On first power-on the device has no Wi-Fi credentials and automatically opens an access point:

| Parameter | Value |
|-----------|-------|
| SSID | `Ringclock-Ultimate-XXXX` (last 4 digits of MAC) |
| Password | configured in `config.h` (`AP_PASSWORD`, default `yourpassword`) |

1. Connect your phone or computer to this network.
2. A captive portal opens automatically (or navigate to `http://192.168.4.1`).
3. Select your home Wi-Fi network from the dropdown, enter the password, and tap **Save**.
4. The device reboots and connects to your network.

The AP also opens as a fallback if the saved Wi-Fi network is unreachable.

### Accessing the Web Interface

Once connected to your network, open a browser and navigate to:

```
http://Ringclock-Ultimate-XXXX.local
```

Replace `XXXX` with the 4-digit MAC suffix of your device (e.g. `A3F2`). The full hostname is
printed to the serial console on boot and appears in your router's DHCP client list.

If mDNS is not supported on your network, use the IP address directly:

```
http://<device-ip>
```

### Using the Web Interface

The interface is organised into eight tabs. All changes take effect **immediately** — no page reload
needed. Settings are held in RAM until you explicitly save them.

> Click **Save Settings** (in the **System** tab) to write all current settings to flash. Settings
> survive reboots and power cuts only after saving. If you lose power before saving, the device
> reverts to the previously saved state.

---

## 2. Brightness

**Tab: Brightness**

Controls how bright the LED rings appear. Two modes are available; switch between them with the
**Automatic Brightness** checkbox.

### Manual Mode

A single slider sets a fixed brightness level (0–255) that applies at all times regardless of
ambient light.

- **255** — full brightness (limited only by the Power Limit)
- **1–254** — proportionally dimmed
- **0** — display off

### Automatic Mode

When a BH1750 light sensor is connected, the clock continuously measures the ambient light and
adjusts its brightness automatically — dimming in the evening, brightening when room lights come on.

| Setting | Description |
|---------|-------------|
| **Max Lux Threshold** | The room light level at which the display reaches maximum brightness |
| **Max Brightness** | Upper brightness limit in automatic mode |
| **Min Brightness** | Lower brightness limit (dark room) |

### Power Limit

WS2812B LEDs can draw significant current at high brightness. The firmware continuously estimates
the total LED draw and throttles brightness automatically if the estimate exceeds the configured
**Power Limit**. Set the limit in the **System** tab to match your power supply's rated output.
The live current estimate is shown there as well.

---

## 3. Ambient

**Tab: Ambient**

The Ambient layer is a static background that is always visible behind the moving clock hands.
Three sub-layers are stacked in order; higher layers override the pixels they occupy.

| Setting | Description |
|---------|-------------|
| **Ambient Light** | Illuminates all LEDs in a single background colour |
| **Hour Markers** | Accent lights at every hour position (1–12); overrides ambient colour |
| **Quarter Markers** | Accent lights at 12, 3, 6, and 9 o'clock; highest priority |

Each sub-layer has an independent colour picker and enable checkbox.

---

## 4. Time Hands

**Tab: Time**

Configures the visual appearance of the three clock hands. Each hand (Second, Minute, Hour) has
its own sub-section with identical settings.

### Colors

Each hand has four independently configurable colours, blending linearly along the tail:

| Setting | What it colours |
|---------|----------------|
| **Hand Color** | The tip LED at the current position |
| **Tail Start** | The color at the tail's root, shared by both tails |
| **Back End** | The far end of the trail behind the tip |
| **Fwd End** | The far end of the trail ahead of the tip |

### Trail Lengths

Forward and backward trail lengths are set independently.

| Value | Effect |
|-------|--------|
| 0 | No trail — tip LED only |
| 1–10 | Trail of that many LEDs |
| 11 | Fill mode — trail extends to 12 o'clock; the full color gradient is always visible |

### Ring Assignment

Use the **Ring 1 / Ring 2 / Ring 3** checkboxes to assign each hand to one or more rings.
A hand with no ring checked is hidden. 

---

## 5. Special Effects

**Tab: Effects**

Periodic visual animations that overlay the clock display. Each effect has an **interval** slider
(0 = off, otherwise trigger every N minutes) and a **Trigger** button for immediate playback.

| Effect | Duration | Description |
|--------|----------|-------------|
| **Short Circuit** | 1 s | Irregular cold-white flickering across all LEDs |
| **Radar** | ~6 s | Green phosphor-style sweep around the ring (3 revolutions) |
| **Shooting Star** | ~1 s | Brief bright streak moving around the ring |
| **Heartbeat** | 5 s | Lubb-dupp brightness pulse; **Intensity** controls the modulation depth |

Intervals are aligned to midnight (0:00). Example: an interval of `60` triggers exactly at the
top of every hour. If multiple effects are due at the same minute, only the one with the largest
interval fires.

---

## 6. Stepper Motor

**Tab: Motor**

The stepper motor moves a physical hand around the clock face. It is driven by a hardware timer
interrupt and never skips or jitters under network or rendering load.

### Motor Mode

Select what the motor tracks:

| Mode | Behaviour |
|------|-----------|
| **Off** | Driver disabled, motor de-energised |
| **Hours** | One revolution = 12 hours |
| **Minutes** | One revolution = 60 minutes |
| **Seconds** | One revolution = 60 seconds |
| **Homing** | Calibration mode — see below |

### Time Grid

Determines how the motor advances:

| Setting | Behaviour |
|---------|-----------|
| **Analog (smooth)** | Motor runs continuously, locked to NTP time. Speed and acceleration are ignored. |
| **Grid** | Motor snaps to discrete time positions using a configurable speed and acceleration ramp. |

In grid mode, **MaxSpeed** and **Acceleration** define how the motor moves between positions.
The **Grid Mode Monitor** (visible in the tab) shows the ramp duration and flags a warning if the
ramp takes longer than the grid interval — in that case increase MaxSpeed / Acceleration or choose
a coarser grid step.

### Motor Calibration (Homing)

A stepper motor has no built-in position sensor, so the firmware needs to know which physical
position is 12 o'clock. This one-time calibration is called homing and is stored permanently.
It only needs to be repeated if the motor shaft is moved manually or the clock face is reassembled.

Set **Motor Mode → Homing** to enter calibration. Two procedures exist depending on hardware:

---

#### With Hall Sensor (`MOTOR_AH_EN = 1`)

A magnet on the motor shaft and a Hall effect sensor on the fixed frame provide an automatic
reference. The web interface shows the homing status and, once homing is complete, the
**Homing & Zero Calibration** panel appears.

1. Wait for automatic homing to complete (status: *Ready*).
2. The hand should now be roughly at 12 o'clock.
3. Use the **jog buttons** (±1, ±10, ±100 steps) to fine-tune.
4. Press **Accept & Save** — the offset is stored permanently.
5. Switch to the desired Motor Mode.

Enable **Auto Homing** in the **System** tab to run this sequence automatically on every boot.

---

#### Without Hall Sensor (`MOTOR_AH_EN = 0`)

Without a sensor, the motor must start from 12 o'clock at every power-on.

1. Disconnect the clock from power.
2. Turn the motor shaft to the 12 o'clock position by hand.
3. Reconnect power. On boot the motor energises and snaps to a nearby detent.
4. Set **Motor Mode → Homing** and use the **jog buttons** to fine-tune to exact 12 o'clock.
5. Press **Accept & Save**.
6. Switch to the desired Motor Mode.

> If you switch Motor Mode to **Off** via the web interface, the motor first seeks back to
> 12 o'clock before de-energising — so a controlled shutdown preserves the calibration. After an
> unexpected power cut, turn the shaft back to 12 o'clock by hand before the next power-on.

---

## 7. Night Mode

**Tab: Night**

Automatically suppresses selected features during a configurable time window — useful for clocks
in bedrooms or living spaces.

### Time Window

| Setting | Description |
|---------|-------------|
| **Enable Night Mode** | Main on/off switch |
| **Start / End** | Window start and end time e.g. 22:00–06:00 |
| **Currently active** | Live indicator — updates every second |

The correct local time depends on the **Timezone** setting in the **System** tab.

### Features During Night Window

Each feature is an independent checkbox:

| Feature | Effect when active |
|---------|-------------------|
| **Dim LEDs** | Reduce brightness to the **Night Brightness** slider value |
| **Disable SFX effects** | Pauses intervalls for all special effects |
| **Hide second hand** | Switch off seconds hand |
| **Park motor at 12 o'clock** | Motor seeks to 12 o'clock and stops; mode is restored on exit |
| **Hide ambient markers** | Hour and quarter markers are hidden |

---

## 8. MQTT

**Tab: MQTT**

Integrates the clock into home automation systems (Home Assistant, Node-RED, FHEM, …). When enabled,
every web interface setting can also be read and written over MQTT.

### Connection

Enable **Enable MQTT** and configure broker address, port, and credentials. The client reconnects
automatically on disconnection. Connection status is shown as a badge next to the checkbox.

| Setting | Default | Description |
|---------|---------|-------------|
| Broker | — | IP address or hostname of the MQTT broker |
| Port | 1883 | Broker port |
| Client ID | `ringclock` | Must be unique on the broker |
| Topic Base | `ringclock` | Prefix for all topics |

 Press **Apply & Save MQTT** to apply the MQTT settings and make them persistant. MQTT credentials are saved separately to `mqtt.json` and survive a LittleFS OTA flash.

Note: **Save Settings** button at **System** tab does not store these settings!

### Topic Structure

All topics follow the pattern `<base>/<type>/<key>` (default base: `ringclock`):

| Pattern | Direction | Purpose |
|---------|-----------|---------|
| `<base>/set/<key>` | → Device | Write a setting |
| `<base>/get/<key>` | → Device / ← Device | Query current value (publish to request; device replies on same topic) |
| `<base>/cmd/<command>` | → Device | One-shot command |
| `<base>/status/<key>` | ← Device | Live readings, published every second |
| `<base>/status/online` | ← Device | `1` on connect, `0` as Last Will (retained) |

### Commands

| Command | Effect |
|---------|--------|
| `save` | Persist all settings to flash |
| `reboot` | Restart the device |
| `resync` | Resynchronise motor position to current time |
| `sfxShortCircuitTrigger` | Trigger the short-circuit effect |
| `sfxRadarTrigger` | Trigger the radar scan |
| `sfxShootingStarTrigger` | Trigger the shooting star |
| `sfxHeartbeatTrigger` | Trigger the heartbeat |

### Live Status Topics

Published every second:

| Topic | Value |
|-------|-------|
| `<base>/status/lux` | Ambient light [lux] |
| `<base>/status/current_mA` | Estimated LED current [mA] |
| `<base>/status/rssi` | Wi-Fi signal [dBm] |
| `<base>/status/uptime` | Uptime [s] |

For the full list of settable keys see [requirements.md §13](requirements.md).

---

## 9. System

**Tab: System**

Live diagnostics, global settings, and device controls.

### Status

The top of the System tab shows live readings:

| Field | Description |
|-------|-------------|
| Local Time | Current time on the device (including DST) |
| System Uptime | Time since last boot |
| IP Address | Current IP address |
| Signal (RSSI) | Wi-Fi signal strength |
| MQTT | Connection status |
| Estimated Power | Estimated LED current draw |

### Settings

| Setting | Description |
|---------|-------------|
| **Auto Homing** | Run Hall sensor homing automatically on every boot (only shown if sensor is installed) |
| **Power Limit (mA)** | Maximum LED current; brightness is throttled if exceeded. Set to match your power supply. |
| **Log Level** | Serial log verbosity (0 = off, 3 = debug). Useful for troubleshooting via USB serial. |
| **Timezone** | Local timezone with automatic DST support |

### Controls

| Button | Effect |
|--------|--------|
| **Save Settings** | Write all current settings to flash (`settings.json`). Does **not** save MQTT credentials (use **Apply & Save MQTT** in the MQTT tab) or the motor zero-point offset (use **Accept & Save** in the Motor Homing panel). |
| **Reboot Device** | Restart the ESP8266 |
| **Erase WiFi** | Clear stored Wi-Fi credentials and restart in setup mode |
| **OTA Update** | Open the OTA firmware/filesystem update page |

### OTA Firmware Update

Navigate to **OTA Update** to flash new firmware or web interface files directly from your
browser — no USB cable needed.

| Upload target | File |
|---------------|------|
| Firmware | `.bin` built by PlatformIO (`firmware.bin`) |
| Filesystem | LittleFS image (built with `pio run -e release -t buildfs`) |

Authentication is required (username/password configured in `config.h`).

> **Your settings are safe during a filesystem update.** The released LittleFS image does not
> include `settings.json`, `mqtt.json`, or `homing.json`. The ESP8266 preserves every file absent
> from the uploaded image, so flashing a new filesystem leaves your configuration exactly as it was.

---

## 10. Compile-time Configuration

Some settings describe the physical hardware and require a firmware rebuild when changed. They are
defined in `include/config.h`.

### Hardware

| Parameter | Default | Description |
|-----------|---------|-------------|
| `RING_LEDS[]` | `{60, 12, 12}` | LED count per ring — one value per ring, each a multiple of 12 |
| `MAX_RINGS` | 3 | Number of daisy-chained rings (1–3) |
| `MOTOR_STEPS_PER_REV` | 3200 | Full steps per motor revolution (3200 = 1/16 microstepping) |

> **Level shifter:** A 74HCT125 is required between pin D4 (3.3 V) and the first ring's DIN (5 V).
> See [wiring.md](wiring.md) for the circuit.

### Motor Homing

| Parameter | Default | Description |
|-----------|---------|-------------|
| `MOTOR_AH_EN` | 1 | `1` = Hall sensor homing active; `0` = manual calibration only |
| `MOTOR_AH_SPEED` | 100 | Homing measurement speed [steps/s] |
| `MOTOR_AH_PASSES` | 3 | Number of CW measurement passes |
| `MOTOR_AH_WDT_MS` | 60000 | Abort homing after this many milliseconds |

### Defaults

These values apply on the very first boot, before any settings have been saved to flash.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `DEFAULT_BRIGHTNESS` | 128 | Brightness on first boot |
| `POWER_LIMIT_DEFAULT` | 1500 | LED current limit [mA] |
| `GAMMA_VALUE` | 2.20 | Gamma correction exponent for LED output |
| `NTP_QUERY` | 2 | NTP resync interval [hours] |

### Access Credentials

| Parameter | Default | Description |
|-----------|---------|-------------|
| `AP_PASSWORD` | `yourpassword` | Password for the fallback Wi-Fi access point |
| `OTA_USERNAME` | `admin` | OTA update authentication username |
| `OTA_PASSWORD` | `yourpassword` | OTA update authentication password |

> Change the default passwords before deploying the device on a shared network.

---

## 11. Troubleshooting

For most issues, the serial console (115200 baud) provides additional detail — connect a USB cable
and open a serial monitor to see boot messages, Wi-Fi status, NTP results, and motor homing progress.

### Device does not connect to Wi-Fi

- Check that the saved SSID and password are correct.
- The device falls back to AP mode (`Ringclock-Ultimate-XXXX`) if the connection fails — reconnect
  and re-enter credentials via the captive portal.
- Erase stored Wi-Fi credentials from the **System** tab (**Erase WiFi**) or hold the flash button
  during boot (if wired).

### Web interface is unreachable

- Confirm the device is connected (check router DHCP list or serial console output).
- Try `http://Ringclock-Ultimate-XXXX.local` (XXXX = your device's MAC suffix, visible in serial
  output or DHCP list).
- If `.local` does not resolve (some managed networks block mDNS multicast), use the IP address
  directly.
- If the interface loads but the WebSocket does not connect, try a hard browser refresh
  (Ctrl+Shift+R).

### LEDs do not light up

- Check the 5 V supply current capacity — LEDs at full brightness draw up to 60 mA each.
- Verify the **74HCT125 level shifter** is correctly wired between D4 (3.3 V output) and Ring 1
  DIN (5 V input). Without it the signal voltage may be too low for reliable reception by the
  WS2812B. See [wiring.md](wiring.md) for the complete circuit.
- Verify the 300–500 Ω series resistor on the data line is in place.
- Confirm `RING_LEDS[]` and `MAX_RINGS` in `config.h` match your hardware.
- Check that at least one hand has a ring checked and a non-black colour.

### Motor does not move

- Check that **Motor Mode** is not **Off**.
- Verify stepper driver wiring (STEP/DIR/EN) and power supply. See [wiring.md](wiring.md).
- Confirm `MOTOR_STEPS_PER_REV` matches your driver's microstepping configuration.
- In analog mode, movement may be imperceptible in Hours mode. Switch to Seconds to verify.

### Motor position drifts over time

- In analog mode, the PLL corrects small deviations automatically. Use **Resync** (`cmd/resync`
  via MQTT) to correct larger jumps after missed steps.
- Check the stepper driver current limit — too low a current causes missed steps.

### Homing never completes (timeout)

- Verify Hall sensor wiring: sensor OUT → D5 (GPIO14), active LOW, internal pull-up active.
  See [wiring.md](wiring.md).
- Check that the magnet passes close enough to the sensor face during rotation.
- Increase `MOTOR_AH_WDT_MS` in `config.h` if the motor is slow.

### Brightness does not change in automatic mode

- Check if **Night Mode** is active and **Dim LEDs** is enabled — Night Mode overrides the
  auto-brightness target with the fixed **Night Brightness** value. The **Currently active**
  indicator in the Night tab shows the current state.

### Settings lost after reboot

- Remember to click **Save Settings** after making changes. Settings only persist when explicitly
  saved.

### NTP time is wrong

- Check that the device has internet access.
- Set the correct **Timezone** in the **System** tab.
- Force a resync via MQTT `cmd/resync`.
