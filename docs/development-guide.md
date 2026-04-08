# Development Guide — Ringclock Ultimate

Practical reference for building, flashing, debugging, and working on the web interface.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Build and Flash](#2-build-and-flash)
3. [Serial Logging / Debug Output](#3-serial-logging--debug-output)
4. [Web Interface Development with LiveServer](#4-web-interface-development-with-liveserver)
5. [Release Build — Filesystem Image](#5-release-build--filesystem-image)
6. [OTA Update](#6-ota-update)
7. [Code Style Pointers](#7-code-style-pointers)

---

## 1. Prerequisites

| Tool | Notes |
|------|-------|
| **VS Code** | Any recent version |
| **PlatformIO extension** | Handles toolchain, libraries, upload |
| **Python 3** | Required by PlatformIO scripts |
| **Live Server extension** | VS Code marketplace (`ritwickdey.liveserver`) — for web UI development only |

Clone the repository and open the root folder in VS Code. PlatformIO will automatically download all library dependencies on first build.

---

## 2. Build and Flash

### Firmware

```bash
# Build
pio run

# Build + upload via USB
pio run --target upload

# Build + upload via OTA (set IP first in platformio.ini)
# Uncomment upload_protocol and upload_port in [env:d1_mini], then:
pio run --target upload
```

### Filesystem (web interface files)

The web interface files live in `data/`. They must be uploaded to LittleFS **separately** from the firmware:

```bash
# Build and upload filesystem image (development — keeps hardcoded IPs)
pio run --target buildfs
pio run --target uploadfs
```

> **Important:** Stop the serial monitor before uploading. The USB port is shared.

### Two environments

| Environment | Command | Purpose |
|---|---|---|
| `d1_mini` (default) | `pio run` | Development — `data/script.js` uploaded as-is |
| `release` | `pio run -e release` | Production — script patched before filesystem build (see §5) |

---

## 3. Serial Logging / Debug Output

Connect via USB and open the serial monitor at **115200 baud**:

```bash
pio device monitor
# or in VS Code: PlatformIO sidebar → Monitor
```

### Controlling verbosity

`LOG_LEVEL_DEFAULT` in `config.h` sets the **compile-time floor** — levels below it are stripped from the binary entirely:

```cpp
#define LOG_LEVEL_DEFAULT LOG_LEVEL_DEBUG  // compile in all levels
#define LOG_LEVEL_DEFAULT LOG_LEVEL_INFO   // strip debug messages
```

The **runtime level** can be lowered further via the web interface (System tab → Log Level) or MQTT (`set/logLevel`). Raising it above the compiled floor has no effect.

Per-module flags suppress individual modules regardless of level:

```cpp
#define LOG_MOT  1   // motor.cpp        — on
#define LOG_HOM  1   // motor_homing.cpp — on
#define LOG_WEB  1   // webserver.cpp    — on
#define LOG_MQTT 0   // mqtt.cpp         — off (zero code generated)
// … see config.h for the full list
```

Set a module flag to `0` to remove its log output entirely from the binary.

### ESP8266 core debug output

Additional low-level WiFi/TCP diagnostics can be enabled by uncommenting these flags in `platformio.ini`:

```ini
build_flags =
    ;-DDEBUG_ESP_PORT=Serial
    ;-DDEBUG_ESP_CORE
    ;-DDEBUG_ESP_WIFI
    ;-DDEBUG_ESP_HTTP_SERVER
```

---

## 4. Web Interface Development with LiveServer

The LiveServer VS Code extension serves the entire `data/` folder locally and refreshes the browser on every save — so changes to `index.html`, `script.js`, or `style.css` are visible instantly without reflashing. The page connects to the real ESP over the network, giving full interaction with the running firmware.

Because the browser and the ESP are on different origins, the WebSocket and the `/settings` endpoint must point directly at the ESP's IP address rather than `location.host`.

### Setup (one-time)

1. Open `data/script.js`.
2. Find the `connectWS()` function (~line 162) and switch the URL:

```js
// ws = new WebSocket(`ws://${location.host}/ws`);      // production — comment out
ws = new WebSocket(`ws://192.168.178.130/ws`);           // LiveServer — set your ESP IP
```

3. Find the `loadSettings()` function and switch the fetch URL:

```js
// fetch('/settings')                                    // production — comment out
fetch('http://192.168.178.130/settings')                 // LiveServer — set your ESP IP
   .then(r => r.json())
```

> Replace `192.168.178.130` with your ESP's actual IP (visible in the serial log on boot or in your router's DHCP list).

### Start LiveServer

Right-click `data/index.html` in VS Code → **Open with Live Server**.  
The browser opens at `http://127.0.0.1:5500/data/index.html` and connects to the running ESP.

Changes to `index.html`, `script.js`, or `style.css` are reflected instantly without reflashing.

### Before committing / releasing

**Never commit the hardcoded IP.** The two patched lines must remain in their original commented-out form in `data/script.js`. The `release` environment's pre-build script (`scripts/release_data.py`) strips the dev lines and uncomments the production lines automatically — it never modifies the source file.

> The memory file [`feedback_liveserver_ip.md`](../memory/feedback_liveserver_ip.md) documents this convention.

---

## 5. Release Build — Filesystem Image

The `release` environment produces a deployment-ready LittleFS image with the hardcoded dev URLs removed:

```bash
# Build patched filesystem image
pio run -e release --target buildfs

# Upload patched filesystem image
pio run -e release --target uploadfs

# Build firmware (same as d1_mini)
pio run -e release
```

The pre-build script `scripts/release_data.py`:
- copies `data/` to `.pio/release_data/`
- removes the `ws://x.x.x.x/ws` dev line
- uncomments the `ws://${location.host}/ws` production line
- removes the `http://x.x.x.x/settings` dev line
- uncomments the `/settings` production line

The original `data/` directory is never modified.

---

## 6. OTA Update

Navigate to `http://<device-ip>:8080/update` (port and path defined by `OTA_PORT` / `OTA_UPDATE_PATH` in `config.h`). Login with `OTA_USERNAME` / `OTA_PASSWORD`.

| Upload target | File |
|---|---|
| Firmware | `.pio/build/d1_mini/firmware.bin` |
| Filesystem | `.pio/build/release/littlefs.bin` (use the `release` build!) |

> Settings (`settings.json`, `mqtt.json`, `homing.json`) are preserved during a filesystem OTA flash because the released image intentionally does not include them.

Alternatively, configure OTA upload directly in `platformio.ini`:

```ini
upload_protocol = espota
upload_port     = 192.168.178.130   ; ESP IP
```

Then `pio run --target upload` flashes over Wi-Fi.

---

## 7. Code Style Pointers

**config.h is the single source of truth.** Never hardcode pin numbers, timing values, or feature flags outside of `config.h`.

**No blocking I/O in WebSocket callbacks.** `WS_EVT_CONNECT` / `WS_EVT_DATA` run inside the ESPAsyncWebServer async callback. Only set a flag there; do the actual work in the main loop. See [`feedback_async_context.md`](../memory/feedback_async_context.md).

**`MQTT::requestPublishAll()`** is the safe way to trigger a settings broadcast from a WebSocket callback — it sets a flag that `MQTT::update()` consumes in the main-loop context.

**Motor ISR shared variables** must be `volatile`. All writes to `isrIntervalUs`, `isrCurrentPos`, etc. from the main loop must be atomic or guarded — the ISR fires at any time.

**Gamma correction** is applied in the renderer, not in individual layers. Layers work in linear colour space.

**Doxygen** — all public functions and non-trivial logic shall be documented. Run `doxygen` from the project root (requires a `Doxyfile`) to verify the docs build without errors.
