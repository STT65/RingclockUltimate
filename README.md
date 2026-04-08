# Ringclock Ultimate — Firmware

ESP8266 firmware for a WS2812B LED ring clock with stepper motor hand, web interface, and MQTT support.

---

## What is a Ring Clock?

A ring clock displays time on one or more circular LED rings, where each ring acts like a clock face.
Instead of traditional hands, coloured LEDs light up at the position corresponding to the current time —
much like the tip of an analogue hand sweeping around a dial.

**Ringclock Ultimate** combines up to three WS2812B LED rings with a physical stepper motor hand,
giving you both a LED light display and a real mechanical pointer on the same clock face.

### How time is displayed

Each time unit — hours, minutes, and seconds — can be freely assigned to any combination of rings
or to the stepper motor hand:

| Display element | What it shows | Example assignment |
|---|---|---|
| LED ring 1 | Any time unit | Minutes |
| LED ring 2 | Any time unit | Hours |
| LED ring 3 | Any time unit | nothing |
| Stepper motor | One time unit | Seconds |

A typical single-ring setup shows all three hands on one ring with colour-coded trails.
With multiple rings each unit gets its own dedicated ring, just like separate clock faces stacked in depth.
The motor hand adds a tactile, mechanical dimension — silently tracking hours (or minutes or seconds)
with full microstepping precision, driven by a hardware timer ISR independent of Wi-Fi jitter.

---

## Highlights

- **Hybrid display** — LED rings + physical stepper motor hand on the same face
- **1–3 rings**, 12–240 LEDs each, all daisy-chained on a single GPIO
- **Freely assignable** — each time unit (H/M/S) to any ring or to the motor
- **Dynamic frame rate** — 1 fps at idle, up to 60 fps during effects
- **UART driven LED output** — async UART driver offloads serialisation from software
- **Precise motor timing via ISR** — hardware timer drives the stepper jitter-free
- **Live web interface** — all parameters adjustable in real-time, no reboot needed
- **Special effects** — radar scan, heartbeat, shooting star, short circuit
- **Smart brightness** — automatic via BH1750 sensor or manual; night mode
- **OTA firmware update** — directly from the browser
- **MQTT** — remote control and status publishing 
- **NTP** — accurate time synchronisation over Wi-Fi

---

## Features

### LED Rendering
- 1–3 WS2812B LED rings, 12–240 LEDs per ring (configurable in `config.h`)
- All rings daisy-chained on a single data line (UART1, GPIO2/D4)
- Asynchronous UART driver — FIFO feeds in background
- Dynamic frame rate — 1 fps for clock-only, up to 60 fps during effects
- Gamma correction (LUT-based, configurable exponent)
- Automatic current limiting — scales brightness if estimated draw exceeds limit (based on current estimation, current not measured)


### Time Layer
- Hour, minute and second hands with configurable forward and backward trails
- Trail length 0–10 LEDs per direction; length 11 = fill-to-12-o'clock effect
- All colours independently configurable (HSV), for the hands and tails
- Linear colour gradient along each trail 
- Each time-unit can be shown on any subset of rings
- Stripes with variable count of LEDs supported (multiple of 12 LEDs)

### Ambient Layer
- Background fill, hour markers (12 evenly spaced) and quarter markers
- All colours independently configurable (HSV)
- Static layer — no CPU cost between config changes

### Special Effects (SFX)
- **Short circuit** — 1 s of irregular cold-white flicker
- **Radar scan** — phosphor-style green sweep, 3 revolutions (~6 s)
- **Heartbeat** — 5 s lubb-dupp brightness pulse toward dark; configurable depth and interval
- **Shooting star** — brief white streak across all rings (~1 s)
- Each effect: configurable trigger interval (minutes) + manual trigger via web interface

### Stepper Motor
- Drives an analogue clock hand via EN / STEP / DIR
- Driven exclusively from a hardware timer ISR — immune to Wi-Fi and TCP stack jitter
- Display unit: Hours / Minutes / Seconds (or Off)
- Analog mode (continuous, PLL-tracked) or Grid mode (1–5 quantisation levels)
- Configurable speed and acceleration for Grid mode only
- **Hall sensor homing** (`MOTOR_AH_EN = 1`) — multi-pass zero-point calibration on every boot; measures the sensor zone midpoint over `MOTOR_AH_PASSES` passes and averages the results
- **Manual homing** Manual homing is an alternative procedure when no position sensor is installed. The motor must be in 12 o'clock position before powering the ring clock
- **Manual fine pitch** The 12‑o’clock position can be fine‑adjusted using the jog buttons. The adjustment value can be stored permanently and is applied on every future start‑up

### Connectivity
- Connects to configured Wi-Fi on boot; falls back to AP + Captive Portal if unavailable
- Captive Portal: SSID scan drop-down, iOS/Android compatible, dark responsive UI
- WebSocket-based web interface — parameter updates are applied without a reboot
- MQTT client for remote control and status publishing
- NTP time synchronisation 
- OTA firmware update via web interface

### System
- Ambient light measurement via BH1750 sensor
- Manual or configurable automatic system brightness
- Night mode — configurable time window with independent feature flags: dim LEDs, suppress SFX, hide second hand, park motor, hide ambient markers
- 4-level UART logging (compile-time floor + runtime control)
- All settings persisted to LittleFS on explicit Save

---

## Hardware

| Function | Pin | GPIO |
|---|---|---|
| I²C SDA (BH1750) | D1 | GPIO5 |
| I²C SCL (BH1750) | D2 | GPIO4 |
| LED data (all rings, chained) | D4 | GPIO2 |
| Stepper STEP | D3 | GPIO0 |
| Stepper DIR | D6 | GPIO12 |
| Stepper EN | D7 | GPIO13 |
| Hall sensor (homing) | D5 | GPIO14 |

All LED rings are daisy-chained: Ring 1 data-out → Ring 2 data-in → Ring 3 data-in.
GPIO2 doubles as UART1 TX, used by NeoPixelBus for interrupt-safe LED output.

Full wiring diagram (BH1750, WS2812B rings, stepper driver, power): [`docs/wiring.md`](docs/wiring.md).

---


## Project Structure

```
include/        — public headers (config.h, all module interfaces)
src/
  main.cpp          — setup() / loop()
  renderer.cpp      — compositing pipeline, dynamic frame rate engine
  layer_ambient.cpp — background, hour and quarter markers
  layer_time.cpp    — clock hands with gradient trails
  layer_sfx.cpp     — special effects engine
  night_mode.cpp    — night mode state machine (window evaluation, transitions)
  time_state.cpp    — NTP client, LED index calculation, motor position
  motor.cpp         — stepper driver, hardware timer ISR
  motor_homing.cpp  — Hall sensor homing state machine
  brightness.cpp    — manual / auto brightness (BH1750)
  settings.cpp      — runtime parameters, LittleFS persistence
  webserver.cpp     — AsyncWebServer + WebSocket handler
  mqtt.cpp          — MQTT client
  wifi_setup.cpp    — STA connection + AP fallback
  captive_portal.cpp— DNS + captive portal
  ota_update.cpp    — OTA firmware upload
  gamma.cpp         — gamma LUT
  color.cpp         — HSV → RGB conversion, lerp
  logging.cpp       — levelled UART logging
docs/
  wiring.md                — wiring diagram for all peripherals
  led_ring_design_guide.md — LED ring layout conventions and ring-assignment decision matrix
  designer-guide.md        — non-obvious design decisions
  development-guide.md     — build, flash, serial logging, LiveServer setup, release process
  user-guide.md            — end-user manual
```

---

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Flash firmware
pio run --target upload

# Build filesystem image (web UI assets)
pio run --target buildfs

# Upload filesystem (web UI assets)
pio run --target uploadfs

# Serial monitor
pio device monitor
```

> **OTA filesystem update:** `settings.json`, `mqtt.json` and `homing.json` are intentionally excluded from the
> released LittleFS image. The ESP8266 preserves any file that is not present in the uploaded image,
> so flashing a new filesystem leaves existing settings untouched.

OTA upload (after first flash):

```ini
# Uncomment in platformio.ini:
upload_protocol = espota
upload_port     = <device-ip>
```

---

## Dependencies

| Library | Version | License |
|---|---|---|
| ESPAsyncWebServer | latest (GitHub) | LGPL v3 |
| ESPAsyncTCP | latest (GitHub) | LGPL v3 |
| ArduinoJson | ^6.21 | MIT |
| NeoPixelBus | ^2.7 | LGPL v3 |
| BH1750 | ^1.3 | MIT |
| PubSubClient | ^2.8 | MIT |

The NTP client (`src/NTPClient.cpp` / `include/NTPClient.h`) is a modified copy of
[arduino-NTPClient](https://github.com/arduino-libraries/NTPClient) by Fabrice Weinberg,
licensed under the MIT License. The original copyright notice is retained in the source file.

---

## Documentation

| Document | Contents |
|---|---|
| [`docs/led_ring_design_guide.md`](docs/led_ring_design_guide.md) | LED ring layout conventions, ring-assignment decision matrix |
| [`docs/user-guide.md`](docs/user-guide.md) | End-user manual |
| [`docs/wiring.md`](docs/wiring.md) | Wiring diagram for all peripherals |
| [`docs/development-guide.md`](docs/development-guide.md) | Build, flash, logging, LiveServer setup, release process |
| [`docs/designer-guide.md`](docs/designer-guide.md) | Non-obvious design decisions |

All public APIs and non-trivial logic are documented in Doxygen format.
Generate HTML docs with:

```bash
doxygen Doxyfile
```

---

## Wiring diagram

For the wiring diagram see [`docs/wiring.md`](docs/wiring.md).

## License

MIT License — see [LICENSE](LICENSE) for details.
