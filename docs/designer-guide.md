# Ringclock Ultimate — Designer Guide

Design decisions, invariants, and non-obvious implementation details that are
not self-evident from reading the code alone. Intended audience: developers
maintaining or extending this codebase.

---

## Table of Contents

1. [Motor Homing](#1-motor-homing)
2. [Web Interface — iOS Safari Compatibility](#2-web-interface--ios-safari-compatibility)
3. [Night Mode — Shadow Variable Architecture](#3-night-mode--shadow-variable-architecture)

---

## 1. Motor Homing

### 1.1 The Problem

The motor is a stepper with no absolute position feedback. At power-on the
firmware has no way to know where the rotor is. The pointer must end up at
12 o'clock so the time display is correct.

There are two independent sources of positional uncertainty:

| Source | Description |
|--------|-------------|
| **Mechanical** | When the motor is de-energised the rotor rests in a weak mechanical detent. The user may reposition the clock hands freely. |
| **Electromagnetic snap** | When the motor is first energised it snaps to the nearest electromagnetic detent, which may not coincide with the mechanical resting position. This snap is small but repeatable for a given mechanical starting angle. |

### 1.2 Coordinate System

`isrCurrentPos` is the motor's internal step counter.

- `isrCurrentPos == 0` means **12 o'clock**
- Steps increase in the **forward (right / clockwise)** direction
- Range: `0 .. MOTOR_STEPS_PER_REV - 1` (circular arithmetic throughout)

All seek targets, offsets, and jog values use this same unit.

### 1.3 Fine-pitch calibration offsets

Two signed 16-bit calibration values are stored in `homing.json`:

| Value | Applied when | Meaning |
|-------|-------------|---------|
| `autoHomingFinePitch` | `motorAutoHoming = true` | Steps from Hall sensor midpoint to 12 o'clock |
| `manHomingFinePitch` | `motorAutoHoming = false` | Steps from cold-start snap-detent to 12 o'clock |

**`autoHomingFinePitch`** is added to the measured Hall sensor midpoint `M` to compute
the seek target after each homing run:

```cpp
seekTarget = (M + autoHomingFinePitch) % MOTOR_STEPS_PER_REV;
```

After the seek completes, `isrCurrentPos` is zeroed so the coordinate system
aligns: position 0 = 12 o'clock.

**`manHomingFinePitch`** is used when Hall sensor homing is disabled. At boot
the motor is assumed to start at the snap-detent. The firmware seeks by
`manHomingFinePitch` steps to reach 12 o'clock:

```cpp
seekTarget = circularAdd(0, manHomingFinePitch);
```

Both offsets start at 0 (factory default). With 0, the sensor midpoint (auto) or
the snap-detent (manual) is treated as 12 o'clock.

### 1.4 Cold Start vs. Warm Re-enable

There are two distinct situations when the motor is activated:

#### Cold start (power cycle)

The power supply was switched off; the motor was de-energised and the driver DACs reset to their initial state (phase current polarity = default snap-detent).

**With `MOTOR_AH_EN = 1` and `motorAutoHoming = true`:**
`init()` calls `MotorHoming::start()`. The homing sequence locates the sensor midpoint `M` and seeks to `M + autoHomingFinePitch`. No assumption about where the rotor physically rests is required.

**With `MOTOR_AH_EN = 0` or `motorAutoHoming = false` (manual):**
The user is expected to have positioned the pointer at 12 o'clock before power-on.
`init()` calls `MotorHoming::manualHoming()`, which seeks by `manHomingFinePitch` steps from the snap-detent to 12 o'clock:

```cpp
seekTarget = circularAdd(0, manHomingFinePitch);
```

After the seek `isrCurrentPos` is zeroed so position 0 = 12 o'clock.


#### Warm re-enable (mode change, no power cycle)

`isrCurrentPos` lives in RAM and is **never reset** between mode changes.
After a parking cycle the motor is de-energised with `isrCurrentPos == 0`.
When the mode is subsequently changed back to an active mode, `resync()` uses
`isrCurrentPos == 0` directly — no electromagnetic-snap correction is needed
because the motor is already sitting on the correct electromagnetic detent.

**Key invariant:** the cold-start formula for `isrCurrentPos` is applied only
in `init()`, exactly once per power cycle.

### 1.5 Homing Modes

#### Without Hall sensor (`MOTOR_AH_EN = 0`) — Manual Homing

The system has no sensor to detect a reference position autonomously. The
correct offset is determined once by the user and stored permanently.

**Calibration flow:**

     Disconnect the clock from the power supply.
     Manually bring the motor shaft to the 12 o'clock position.
     Reconnect the clock to the power supply.
     While booting, the motor will be powered and rotates to certain position.
     Set <em>Motor Mode=Homing</em> to bring it back to the 12 o'clock position by using the <em>Jog</em> buttons.
     Press <strong>Accept & Save</strong> to complete the homing procedure.
     Now you can switch <em>Motor Mode</em> to the desired time display mode.
 

#### With Hall sensor (`MOTOR_AH_EN = 1`) — Automatic Homing

The sensor (D5 / GPIO14) provides a reproducible reference position.
`autoHomingFinePitch` stores the fixed offset (steps) from the sensor midpoint
to 12 o'clock. Homing runs at every boot (if `motorAutoHoming = true`) or when
triggered manually from mode 4.

The homing state machine has two phases:
- **Travel** — motor rotates CCW at seek speed until it exits the sensor zone (initial approach only).
- **Measure** — motor rotates CW at `MOTOR_AH_SPEED` through the zone `MOTOR_AH_PASSES` times; the circular average of all pass midpoints is the sensor centre `M`. Motor then seeks to `M + autoHomingFinePitch`.

`autoHomingFinePitch` is calibrated once via Mode 4 + Jog + Accept and survives power cycles in `homing.json`.

### 1.6 Mode 4 (Homing Mode) Behaviour

Mode 4 is the calibration UI mode. Its behaviour is the same regardless of
`MOTOR_AUTO_HOMING_ENABLED`:

| Trigger | Action |
|---------|--------|
| Enter Mode 4 from any mode | `resync()` initiates seek to 0; `update()` starts seek on next call |
| Startup in Mode 4 | `init()` → same seek path |
| Seek in progress | `update()` manages steps until target reached |
| Jog button pressed | Blocked while `seekRunning` or homing active; once clear: motor moves N steps AND `finePitchAdjust += N` |
| Accept pressed | `autoHomingFinePitch += finePitchAdjust` (or `manHomingFinePitch`); saved to `homing.json`; `isrCurrentPos = 0`; `finePitchAdjust = 0` |

### 1.7 Parking

When `motorMode` changes to 0 (Off), the motor does **not** de-energise
immediately. Instead:

1. A seek to position 0 (12 o'clock) is initiated inside `update()`
2. Once position 0 is reached the motor holds position for **1 second**
   (rotor settle time — ensures the rotor is firmly locked in the detent)
3. The motor driver is de-energised

**Why parking is critical for manual homing:** parking ensures that at the
next cold start the motor is sitting on the 12 o'clock electromagnetic detent.
The cold-start formula `isrCurrentPos = (STEPS - motorHomingOffset) % STEPS`
is only valid when the motor was last de-energised at position 0 (12 o'clock).
Without parking (e.g. after a sudden power cut) the pointer is at an
unpredictable detent and the offset correction is meaningless.

### 1.8 Robustness Considerations

| Scenario | Result |
|----------|--------|
| Normal power-off (parking completed, `MOTOR_AH_EN=1`) | Motor at 12 o'clock detent; next boot auto-homing finds sensor and corrects position |
| Normal power-off (parking completed, manual homing) | Motor at 12 o'clock detent; next boot seeks by `manHomingFinePitch` from the snap-detent |
| Sudden power cut (no parking) | Motor at arbitrary detent; **auto-homing** recovers automatically; manual homing has positional error until hand is re-positioned |
| `homing.json` absent or corrupt | Both offsets default to 0; sensor midpoint or snap-detent is treated as 12 o'clock; minor positional error at most |
| Accept pressed twice without jogging | Idempotent — `finePitchAdjust = 0`, offsets unchanged, `isrCurrentPos` already 0 |
| Jog pressed while seek or homing is in progress | Blocked by state guards in `jog()`; user must wait for movement to complete |

---

## 2. Web Interface — iOS Safari Compatibility

### 2.1 The Problem

The web interface uses a WebSocket (`/ws`) for two purposes:

1. **Initial settings load** — browser requests `getSettings`, ESP responds with the full settings JSON.
2. **Live updates** — ESP pushes monitoring data (time, RSSI, uptime, …) once per second.
3. **Settings changes** — browser sends individual key/value pairs on user input.

On desktop browsers (Chrome, Edge, Firefox) this works reliably. On **iOS Safari** the initial settings load failed intermittently. The root cause:

iOS Safari aggressively manages WebSocket connections during page load and reload. The sequence on a reload is:

```
Browser opens new WebSocket connection
  → WS_EVT_CONNECT fires on ESP (18 ms after page load)
  → ESP queues sendAllSettings() response
  → Safari disconnects the old navigation context
  → WS_EVT_DISCONNECT fires (15–20 ms later)
  → The queued settings data is transmitted into a dead connection
  → Browser never receives the settings
```

The `sendAllSettings()` call itself is fast, but the TCP transmission completes asynchronously after `client->text()` returns. Safari's navigation lifecycle tears down the connection before that transmission completes.

### 2.2 Why WebSocket Is Unreliable for Initial Load

WebSocket connections are tied to the browser's navigation context. On a page reload Safari:

- Creates a new connection for the new page
- Destroys the old connection from the previous page

The timing of this destruction is not deterministic and can race with the ESP's async TCP transmission. This is not a bug in ESPAsyncWebServer — it is inherent to how iOS Safari manages its networking stack during navigation.

### 2.3 The Fix

Settings are loaded via a plain **HTTP GET `/settings`** request instead of over the WebSocket.

```
Browser loads index.html
  → window.onload fires
  → fetch('/settings') issued immediately (HTTP GET)
  → ESP responds synchronously with full settings JSON
  → updateUI() called with the response
```

HTTP requests are not affected by the WebSocket navigation lifecycle. The response arrives reliably regardless of when Safari tears down any WebSocket connection.

The WebSocket remains in use for purposes 2 and 3 above (live updates and settings changes), where its bidirectional, low-overhead nature is appropriate.

### 2.4 Implementation

**ESP (`webserver.cpp`):**

```cpp
server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json;
    buildSettingsJson(json);
    req->send(200, "application/json", json);
});
```

`buildSettingsJson()` is a shared helper used by both the HTTP endpoint and `sendAllSettings()` (which remains available for WebSocket-triggered refreshes).

**Browser (`script.js`):**

```js
function loadSettings() {
    fetch('/settings')
        .then(r => r.json())
        .then(data => { updateUI(data); })
        .catch(e => { setTimeout(loadSettings, 2000); }); // retry on failure
}
loadSettings(); // called from window.onload
```

### 2.5 Design Rules

- **Never rely on the WebSocket for initial page data.** Any data that must be present when the page first renders should be loaded via HTTP GET.
- **WebSocket is for incremental, real-time updates only.** It is fire-and-forget with no delivery guarantee when the connection is unstable.
- **Never call `sendAllSettings()` or any other substantial I/O directly from `WS_EVT_CONNECT`.** The connection is not yet fully established at that point. See also the async-callback rule in the project feedback notes.

---

## 3. Night Mode — Shadow Variable Architecture

### 3.1 The Problem with Direct Settings Mutation

An earlier implementation applied night mode by saving affected `Settings::*` members,
overwriting them with night values on entry, and restoring them on exit. This caused two
problems:

1. **GUI confusion** — While night mode was active, the web interface showed the night-mode
   values (e.g. `motorMode = 0`) rather than the user's actual configuration. The user
   could not tell whether they had configured the clock this way or whether night mode was
   responsible.

2. **Fragile save/restore** — The save and restore logic had to track each overridden variable
   individually and was error-prone if a new variable was added or if a power cycle occurred
   mid-window.

### 3.2 The Shadow Variable Pattern

`night_mode.cpp` owns a set of **effective runtime variables** in the `NightMode` namespace:

```cpp
// Declared extern in night_mode.h — defined in night_mode.cpp
NightMode::motorMode               // 0 when NIGHT_MOTOR_HOME active
NightMode::sfxShortCircuitInterval // 0 when NIGHT_SFX_OFF active
NightMode::sfxRadarInterval
NightMode::sfxShootingStarInterval
NightMode::sfxHeartbeatInterval
NightMode::secondHandRingMask      // 0 when NIGHT_SECOND_HAND_OFF active
NightMode::hourMarksEnabled        // false when NIGHT_MARKERS_OFF active
NightMode::quarterMarksEnabled
```

On every call to `NightMode::update()` (once per Arduino loop), `refreshShadows()` recomputes
all of these from the current `Settings::*` values and the active flag:

```cpp
motorMode = (active && NIGHT_MOTOR_HOME) ? 0 : Settings::motorMode;
// … same pattern for all other variables
```

`Settings::*` is **never modified** by night mode. It always reflects the user's intent.

### 3.3 Consuming Modules

Rendering and behavior modules replace their `Settings::xyz` reads with `NightMode::xyz`:

| Module | Variable |
|---|---|
| `motor.cpp`, `time_state.cpp` | `NightMode::motorMode` |
| `layer_sfx.cpp` | `NightMode::sfx*Interval` (×4) |
| `layer_time.cpp` | `NightMode::secondHandRingMask` |
| `layer_ambient.cpp` | `NightMode::hourMarksEnabled`, `NightMode::quarterMarksEnabled` |

`motor_homing.cpp` is the exception: its `Settings::motorMode` reads are pure **validation
guards** (`!= 4` checks) that test user intent, not behavioral lookups, so they correctly
remain on `Settings`.

Configuration interfaces (`webserver.cpp`, `mqtt.cpp`) also keep reading and writing
`Settings::*` directly — they represent the user's persistent configuration, not the
runtime state.

### 3.4 Motor Resync

`Motor::resync()` must be called whenever the effective motor mode changes (so the motor
seeks to the new target or parks). `NightMode::update()` detects this automatically:

```cpp
int prevMotorMode = motorMode;
refreshShadows();
if (motorMode != prevMotorMode)
    Motor::resync();
```

This covers both night mode transitions **and** user changes to `Settings::motorMode` while
night mode is active (the shadow stays 0 in that case, so no spurious resync is triggered).

### 3.5 Web GUI Indicators

Because `Settings::*` is never mutated, the web interface always shows the user's
configured values — even while night mode is active. Parameters that are currently being
overridden by night mode are highlighted with an amber left border (CSS class
`.night-override`, applied via `script.js` based on `nightActive` and `nightFeatures`).

This allows the user to see their configuration **and** understand that it is temporarily
suppressed, without any risk of losing their settings.
