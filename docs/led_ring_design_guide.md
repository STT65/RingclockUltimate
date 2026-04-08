# LED Ring Design Guide — Clock Face Layout

In the firmware, the count of LEDs per ring can be configured via `LEDS_PER_RING`
(defined in `include/config.h`).
This guide explains how to choose a value for a good dial design.

---

## Table of Contents

1. [The Clock Face Grid](#1-the-clock-face-grid)
2. [The Mapping Formula](#2-the-mapping-formula)
3. [Supported Values — Overview](#3-supported-values--overview)
   - [12 LEDs — Minimal, Hours Only](#12-leds--minimal-hours-only)
   - [24 LEDs — Half-Hour Grid](#24-leds--half-hour-grid)
   - [36 LEDs — 3 per Hour](#36-leds--3-per-hour)
   - [48 LEDs — 4 per Hour](#48-leds--4-per-hour)
   - [60 LEDs — Recommended: Perfect Alignment](#60-leds--recommended-perfect-alignment)
   - [72 LEDs — 6 per Hour](#72-leds--6-per-hour)
   - [96 LEDs — 8 per Hour](#96-leds--8-per-hour)
   - [120 LEDs — Recommended: Double Resolution](#120-leds--recommended-double-resolution)
   - [180 LEDs — 3 per Minute/Second](#180-leds--3-per-minutesecond)
   - [240 LEDs — Maximum (4 per Minute/Second)](#240-leds--maximum-4-per-minutesecond)
4. [Decision Matrix](#4-decision-matrix)
5. [The General Rule](#5-the-general-rule)
6. [Practical Checklist for Dial Designers](#6-practical-checklist-for-dial-designers)

---

## 1. The Clock Face Grid

A standard clock face has two different grids for the three time components:

| Component  | Grid      | Grid name   | Position[1/60] |
|------------|-----------|-------------|----------------|
| Hour       | 12 steps  | `H12-grid`  | 0,5,10..55     |
| Minute     | 60 steps  | `MS60-grid` | 0,1,2,..59     |
| Second     | 60 steps  | `MS60-grid` | 0,1,2,..59     |

A good `LEDS_PER_RING` value shall align cleanly with as many of those positions as possible.

If one LED ring shall represent only the hours, perfect alignment is reached when `LEDS_PER_RING` is a multiple of 12 — matching the `H12-grid`.

If one LED ring shall represent all three units simultaneously, perfect alignment is reached when `LEDS_PER_RING` is a multiple of 60 — matching the `MS60-grid`, which is the finer of the two grids.

The following chapters consider an LED ring that represents all three units simultaneously.

---

## 2. The Mapping Formula

The firmware maps each time component to an LED index using integer arithmetic:

```
minuteIndex = second_index = (value * LEDS_PER_RING) / 60
hourIndex   = ((hour % 12) * LEDS_PER_RING + minuteIndex) / 12
```

**Key consequence:** if `LEDS_PER_RING` is not a multiple of 60, the minute and
second hands will not align with the `MS60-grid` — they skip some LEDs and
double-land on others. The hands still move, but the steps are uneven —
e.g. "skip 1, advance 1, skip 1, advance 2".

---

## 3. Supported Values — Overview

Here are some examples for `LEDS_PER_RING` values.
All values must be multiples of 12. Range supported by the firmware: 12–240.

### 12 LEDs — Minimal, Hours Only

```
LED layout (one LED per hour):
  10  11   0
 9          1
8            2
 7          3
  6   5   4
```

- Each of the 12 LEDs sits exactly on an `H12-grid` position.
- `MS60-grid` is quantised to 5-minute groups (60 / 12 = 5):
  step pattern is completely even — every 5 minutes = 1 LED step.
- No sub-minute resolution, but the result is **visually clean and symmetric**.
- Good for: minimalist design, very small rings, low power budget.

### 24 LEDs — Half-Hour Grid

- 2 LEDs per `H12-grid` position; hours land perfectly on the grid.
- `MS60-grid`: step = 60/24 = 2.5 → uneven pattern
  (alternates: advance 2 LEDs, advance 3 LEDs due to integer division).
- Quarter-hour markers land cleanly (every 6 LEDs).
- **Moderate visual quality.** Better than 12 for minute resolution,
  but the uneven stepping is noticeable.

### 36 LEDs — 3 per Hour

- `MS60-grid`: step = 60/36 = 1.67 → uneven (skip pattern every 3rd step).
- 3 LEDs per `H12-grid` position.
- Quarter markers land cleanly (every 9 LEDs).
- Not recommended: neither the minute nor the second hand moves smoothly.

### 48 LEDs — 4 per Hour

- `MS60-grid`: step = 60/48 = 1.25 → uneven (1 or 2 LED jumps).
- 4 LEDs per `H12-grid` position.
- Quarter markers land cleanly (every 12 LEDs).
- **Acceptable** if minute/second resolution matters less than hour precision.

### 60 LEDs — **Recommended: Perfect Alignment**

```
LED layout (schematic, top = 12 o'clock = LED 0):
  55  0   5
45          10
40            15
 35         20
  30  25  (every 5th LED = H12-grid marker)
```

- **Perfect `MS60-grid` alignment**: 1 LED per step (1 LED = 1 minute/second).
- **Perfect `H12-grid` alignment**: hour markers land on indices 0, 5, 10, 15 … 55.
- Quarter-hour markers: indices 0, 15, 30, 45 — also perfect.
- **Best balance of resolution and simplicity.**

### 72 LEDs — 6 per Hour

- `MS60-grid`: step = 60/72 = 0.83 → uneven (alternates 1 and 0 LED jumps).
- The second hand will occasionally "stand still" for one second before jumping.
- Not recommended for smooth animation.

### 96 LEDs — 8 per Hour

- `MS60-grid`: step = 60/96 = 0.625 → very uneven.
- The second hand skips frequently. Avoid.

### 120 LEDs — **Recommended: Double Resolution**

- **Perfect `MS60-grid` alignment**: 2 LEDs per step (2 LEDs = 1 minute/second).
- **Perfect `H12-grid` alignment**: 10 LEDs per hour, indices 0, 10, 20 … 110.
- Quarter-hour markers: indices 0, 30, 60, 90 — perfect.
- **Excellent visual quality.** Higher density allows for richer tail effects
  and smoother gradients. Requires more power (≈ 7 W at full white).

### 180 LEDs — 3 per Minute/Second

- **Perfect `MS60-grid` alignment**: 3 LEDs per step.
- **Perfect `H12-grid` alignment**: 15 LEDs per hour.
- Visually very smooth; power budget becomes significant (≈ 10 W at full white).

### 240 LEDs — Maximum (4 per Minute/Second)

- **Perfect `MS60-grid` alignment**: 4 LEDs per step.
- **Perfect `H12-grid` alignment**: 20 LEDs per hour.
- At the upper limit of the firmware's supported range.
- Requires a powerful 5 V supply and adequate thermal management.

---

## 4. Decision Matrix

| LEDS_PER_RING | `MS60-grid` alignment   | `H12-grid` alignment | Power     | Recommended?      |
|:---:|---|---|---|:---:|
| 12            | ✅ even (5 min/LED)     | ✅ exact (1 LED/h)   | very low  | ✅ minimal design |
| 24            | ⚠️ uneven              | ✅ exact (2 LED/h)   | low       | ⚠️               |
| 36            | ❌ uneven              | ✅ exact (3 LED/h)   | low       | ❌               |
| 48            | ⚠️ slightly uneven     | ✅ exact (4 LED/h)   | medium    | ⚠️               |
| **60**        | ✅ **perfect (1/step)** | ✅ exact (5 LED/h)  | medium    | ✅ **standard**  |
| 72            | ❌ hand stalls         | ✅ exact (6 LED/h)   | medium    | ❌               |
| 96            | ❌ very uneven         | ✅ exact (8 LED/h)   | high      | ❌               |
| **120**       | ✅ **perfect (2/step)** | ✅ exact (10 LED/h) | high      | ✅ **premium**   |
| 180           | ✅ perfect (3/step)    | ✅ exact (15 LED/h)  | very high | ✅ if power allows |
| 240           | ✅ perfect (4/step)    | ✅ exact (20 LED/h)  | very high | ✅ **ultimate**  |

---

## 5. The General Rule

A value `N` gives **perfect `H12-grid` alignment** if and only if:

```
N mod 12 == 0    (i.e. N ∈ {12, 24, 36, 48, 60, 72, …})
```

This is the mandatory minimum — the firmware requires it.
Each `H12-grid` position occupies exactly `N/12` LEDs.

A value `N` gives **perfect `MS60-grid` alignment** if and only if:

```
N mod 60 == 0    (i.e. N ∈ {60, 120, 180, 240, …})
```

Each `MS60-grid` step advances exactly `N/60` LEDs.

For the **ambient hour-marker layer** (`layer_ambient.cpp`), the firmware always
places markers at every `N/12` index, so `H12-grid` markers are always
pixel-perfect regardless of `N`, as long as `N` is a multiple of 12.

---

## 6. Practical Checklist for Dial Designers

1. **Choose `LEDS_PER_RING`** from the recommended values (60, 120) unless you
   have a specific hardware constraint.
2. **Verify power budget:** 60 mA per LED at full white × LED count.
   At 5 V: 60 LEDs ≈ 3.6 W, 120 LEDs ≈ 7.2 W.
3. **Physical spacing:** standard 60-LED rings (diameter 68 mm) have 6 mm
   between LED centres — comfortable for individual readability. At 120 LEDs on
   the same ring, spacing drops to 3 mm; consider a larger ring diameter.
4. **Tail lengths** (`tailFwdLength`, `tailBackLength` in settings) look best
   when expressed as a fraction of `LEDS_PER_RING/60` — scale them up
   proportionally when doubling the LED count.
5. **Multi-ring setups** Use the ring-checkboxes in hand settings to assign hands to
   specific rings independently. Optimize count of LEDs for the time components individually.
