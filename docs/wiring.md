# Ringclock Ultimate — Wiring Diagram

## Table of Contents

- [D1 Mini Pinout](#d1-mini-pinout)
- [BH1750 Light Sensor (I²C)](#bh1750-light-sensor-i²c)
- [WS2812B LED Rings (up to 3, daisy-chained)](#ws2812b-led-rings-up-to-3-daisy-chained)
- [Stepper Motor Driver (e.g. A4988 / DRV8825)](#stepper-motor-driver-eg-a4988--drv8825)
- [Hall Effect Sensor (Homing)](#hall-effect-sensor-homing)
- [Power Supply Summary](#power-supply-summary)

---

## D1 Mini Pinout

```
                    ┌─────────────┐
                  ──┤ RST     TX  ├── UART0  — Serial log)
                  ──┤ A0      RX  ├── UART0  — Serial log)
                  ──┤ D0      D1  ├── GPIO5  → BH1750 SDA
           GPIO14 ──┤ D5      D2  ├── GPIO4  → BH1750 SCL
           GPIO12 ──┤ D6      D3  ├── GPIO0  → Stepper STEP
           GPIO13 ──┤ D7      D4  ├── GPIO2  → 74HCT125 → WS2812B Data In
                  ──┤ D8     GND  ├── GND
              3V3 ──┤ 3V3    5V   ├── 5V
                    └─────────────┘
                        D1 Mini

          D5 = GPIO14 → Hall sensor Signal (INPUT_PULLUP, active LOW)
          D6 = GPIO12 → Stepper DIR
          D7 = GPIO13 → Stepper EN
```

---

## BH1750 Light Sensor (I²C)

```
  D1 Mini        BH1750
  ───────        ──────
  3V3  ───────── VCC
  GND  ───────── GND
  D1 (GPIO5) ─── SDA   
  D2 (GPIO4) ─── SCL   
                 ADDR ── GND   (I²C address 0x23)
```

---

## WS2812B LED Rings (up to 3, daisy-chained)

A **74HCT125** quad buffer is used as a level shifter between the D1 Mini's
3.3 V data output and the 5 V data input of the WS2812B rings.

```
  D1 Mini        74HCT125             Ring 1        Ring 2        Ring 3
  ───────        ────────             ──────        ──────        ──────

  GND ────────┼── GND  (7)
              ┼── /OE  (1) 
  D4  ─────────── A    (2)
                  Y    (3)  ──[330Ω]── DIN    ┼───── DIN    ┼───── DIN 
              ┼── VCC (14)             DOUT ──┘      DOUT ──┘      DOUT ── (nc)
  5V  ────────┼─────────────────────── VCC  ──────── VCC  ──────── VCC
  GND ──────────────────────────────── GND  ──────── GND  ──────── GND
```

  Note: Pin numbers above refer to one channel of the 74HCT125 (14-pin DIP / SOIC).
        Only one of the four channels is required; tie unused /OE inputs HIGH to disable
        the unused channels.
        Add a 300–500 Ω series resistor on the data line between the 74HCT125 output
        and Ring 1 DIN (shown as 330 Ω above).
        Add a 100 µF capacitor across 5V/GND near each ring.
        Do NOT power the rings from the D1 Mini's 5V pin for more than
        a few LEDs — use a dedicated supply: 5 V / 3.6 A/60LEDs.

---

## Stepper Motor Driver (e.g. A4988 / DRV8825)

```
  D1 Mini    Supply     Driver       Motor
  ───────    ──────     ──────       ─────
  3V3 ───────────────── VDD
  D3 ────────────────── STEP
  D6 ────────────────── DIR
  D7 ────────────────── EN
  GND ──────────────┼── GND
             GND ───┼ 
             12V ────── VMOT
                        1A,1B ────── Coil A
                        2A,2B ────── Coil B

  Note: Place a 1000 µF capacitor across VMOT/GND on the driver board.
        Set current limit via the driver's trim potentiometer.
        MOTOR_STEPS_PER_REV = 3200 → 1/16 microstepping assumed.
```

---

## Hall Effect Sensor (Homing)

```
  D1 Mini          Hall Sensor (e.g. AH3144 / SS49E open-collector)
  ───────          ──────────────────────────────────────────────────
  3V3  ─────────── VCC
  GND  ─────────── GND
  D5 (GPIO14) ──── OUT    (internal INPUT_PULLUP active; output pulled LOW when magnet detected)

  Note: D5 / GPIO14 has an internal pull-up enabled in firmware (INPUT_PULLUP).
        No external pull-up resistor is required for open-collector sensors.
        Place the magnet on the rotating part of the motor; position the
        sensor on the fixed frame so it faces the magnet at the desired
        zero position (12 o'clock).
        Homing can be disabled at compile time: set MOTOR_AH_EN = 0
        in config.h if no Hall sensor is installed.
```

---

## Power Supply Summary

| Rail  | Current         | Consumer                        |
|-------|-----------------|---------------------------------|
| 5 V   | 60mA/LED        | WS2812B rings                   |
| 12 V  | per motor spec  | Stepper driver VMOT             |
| 3.3 V | < 10 mA         | BH1750 (supplied by D1 Mini)    |

> Use the `POWER_LIMIT_DEFAULT` setting in `config.h` to cap LED current
> in firmware (default 1500 mA).
