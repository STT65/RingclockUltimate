#pragma once
#include <stdint.h>

//
// ============================================================
//  Ringclock Ultimate – central system configuration
//  Hardware pins, LED parameters, motor parameters, feature flags
// ============================================================
//

// Hardware pin assignments
// While booting these three pins require a defined level!
// D3 GPIO0	 HIGH (LOW → Bootloader/Flash-Modus)
// D4 GPIO2	 HIGH (LOW → Boot fails)
// D8 GPIO15 LOW  (HIGH → Boot fails)
// Special pin with separate  IO register GP16I and no pull-up support
// D0 GP16I

// I2C – BH1750 ambient light sensor
#define PIN_I2C_SDA D1 // GPIO5
#define PIN_I2C_SCL D2 // GPIO4

// LED rings (WS2812B) – all rings chained: D4 → Ring 1 → Ring 2 → Ring 3
// In this project, the serialisation of pixel data is done by UART1, which is hard-wired to D4

// Hall sensor with open collector output, active-low when magnet detected
#define PIN_HALL D5   // GPIO14 with internal Pullup

// Stepper motor (EN, DIR, STEP)
#define PIN_MOTOR_STEP D3 // GPIO0 - high pulse
#define PIN_MOTOR_DIR D6  // GPIO12 - level 
#define PIN_MOTOR_EN D7   // GPIO13 - Low aktiv



#define MOTOR_STEP_PULSE_STRETCH 1// Lengthen the high-pulse at PIN_MOTOR_STEP from 125ns to 1µs.
#define MOTOR_SEEK_SPEED 600 // 100..600 steps/s fixed seek-speed

// AH = Automatic Homing, based on position detection via a magnet in the rotor and a hall sensor at case
// Set MOTOR_AH_EN to 0 if you don't like to install the magnet/hall sensor 
#define MOTOR_AH_EN 1           // Compile the AutoHoming code
#define MOTOR_AH_SPEED 100      // steps/s during homing passes (slower = more precise edge detection)
#define MOTOR_AH_PASSES 3       // number of sensor overruns; must be >= 1
#define MOTOR_AH_OVERSHOOT 150  // steps past magnet zone exit before reversing
#define MOTOR_AH_WDT_MS 60000UL // Watchdog time [ms] — abort homing if no Hall signal is detected within this time

//  LED configuration

/// @brief NeoPixelBus output method for the LED rings.
///
/// Must be a hardware-UART-based method so that NeoPixelBus::Show() does not
/// disable interrupts, keeping the stepper motor timer ISR unaffected.
/// NeoPixelBus serialisation methods with UART:
/// | Value                              | UART | TX pin       |
/// |------------------------------------|------|--------------|
/// | NeoEsp8266AsyncUart0800KbpsMethod  |  0   | GPIO1 / D10  | Debugging UART
/// | NeoEsp8266AsyncUart1800KbpsMethod  |  1   | GPIO2 / D4   |
#define LED_UART_METHOD NeoEsp8266AsyncUart1800KbpsMethod

/// @brief Number of LED rings chained on the data line (1–3).
#define MAX_RINGS 3

/// @brief Number of LEDs per ring, one entry per ring (index 0..MAX_RINGS-1).
/// Each value must be divisible by 12 (required for hour and quarter markers)
/// and in the range 12–240.
/// Common values: 60 (standard WS2812B ring), 120, 240.
/// Example for two rings: { 60, 12 }
constexpr uint8_t RING_LEDS[MAX_RINGS] = {60, 12, 12};

// ---- Derived compile-time constants (do not edit) ----

namespace RingConfig
{
    constexpr uint8_t maxLedsPerRing()
    {
        uint8_t m = 0;
        for (uint8_t i = 0; i < MAX_RINGS; i++)
            if (RING_LEDS[i] > m)
                m = RING_LEDS[i];
        return m;
    }
    constexpr uint16_t totalLeds()
    {
        uint16_t t = 0;
        for (uint8_t i = 0; i < MAX_RINGS; i++)
            t += RING_LEDS[i];
        return t;
    }
    constexpr bool ringLedsValid()
    {
        for (uint8_t i = 0; i < MAX_RINGS; i++)
        {
            if (RING_LEDS[i] < 12 || RING_LEDS[i] > 240)
                return false;
            if (RING_LEDS[i] % 12 != 0)
                return false;
        }
        return true;
    }
}

/// @brief Largest RING_LEDS value — used for static buffer sizing.
constexpr uint8_t MAX_LEDS_PER_RING = RingConfig::maxLedsPerRing();
/// @brief Total number of LEDs across all rings.
constexpr uint16_t TOTAL_LEDS = RingConfig::totalLeds();

static_assert(MAX_RINGS >= 1 && MAX_RINGS <= 3,
              "MAX_RINGS must be between 1 and 3.");
static_assert(RingConfig::ringLedsValid(),
              "Each RING_LEDS entry must be between 12 and 240 and divisible by 12.");

#define GAMMA_VALUE 2.20f
#define DEFAULT_BRIGHTNESS 128

//  Time layer – hands & tails

#define MAX_TAIL_LENGTH 11
#define ENABLE_SMOOTH_GRADIENTS 1

//  Ambient layer

#define AMBIENT_DEFAULT_ON 1
#define AMBIENT_HOURMARKS_DEFAULT 1
#define AMBIENT_QUARTERS_DEFAULT 1

//  Special Effects Layer (SFX)

#define SFX_SHORT_CIRCUIT_ENABLED 1
#define SFX_RADAR_START_ENABLED 1
#define SFX_HEARTBEAT_ENABLED 1
#define SFX_SHOOTING_STAR_ENABLED 1

//  Light sensor

#define Brightness_INTERVAL_MS 100
#define AUTO_BRIGHTNESS_MIN 10
#define AUTO_BRIGHTNESS_MAX 255
#define AUTO_BRIGHTNESS_LUX_MAX 500

//  Stepper motor

// The HW-Timer1 of ESP8266 has just a 23bit counter, with a max pre-divider value of 256 clocked with 80MHz
// → 1 Tick = 3.2µs → timerCounterMax = 8.388.607 * 3.2µs = 26.843541s
// At 12h per revolution divided by 3200 steps we need already a motor-step interval of 13.5s
#define MOTOR_STEPS_PER_REV 3200 // Must be >= 3200
#define MOTOR_SPEED_DEFAULT 800
#define MOTOR_ACCELERATION_DEFAULT 400

//  Wi-Fi & web interface
#define WIFI_SSID "Ringclock-Ultimate" // will be extended with a 4 digit MAC suffix
#define AP_SSID "Ringclock-Ultimate"   // will be extended with a 4 digit MAC suffix
#define AP_PASSWORD "yourpassword"     // Change before use
#define WS_ENDPOINT "/ws"
// OTA
#define OTA_UPDATE_PATH "/update"
#define OTA_PORT 8080
#define OTA_USERNAME "admin"        // Change before use; keep empty ("") to disable authentication
#define OTA_PASSWORD "yourpassword" // Change before use

// NTP
#define NTP_QUERY 2 // Sync local time every 2 hours with internet

//  Logging
//
// LOG_LEVEL_DEFAULT is the compile-time floor: any log level below this value
// is stripped from the binary entirely (no code, no flash strings).
// Runtime calls to Log::setLevel() can still raise or lower output within the
// levels that were compiled in.
//
//   LOG_LEVEL_NONE  0  – all output suppressed
//   LOG_LEVEL_ERROR 1  – errors only
//   LOG_LEVEL_INFO  2  – errors + informational messages
//   LOG_LEVEL_DEBUG 3  – all messages including debug output

#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_DEBUG 3

#define LOG_LEVEL_DEFAULT LOG_LEVEL_DEBUG
#define LOG_TIMESTAMP 1  // 1 = prepend millis() timestamp to every log line, e.g. [DEBUG]@123456

// Per-module log enable (1 = on, 0 = off).
// A disabled module produces zero code — the compiler eliminates it entirely.
// Subject to the LOG_LEVEL_DEFAULT floor: debug messages are only compiled in
// when LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG, regardless of the module flag.
#define LOG_MOT   1   // motor.cpp
#define LOG_HOM   1   // motor_homing.cpp
#define LOG_WEB   1   // webserver.cpp
#define LOG_MQTT  0   // mqtt.cpp
#define LOG_TIME  0   // time_state.cpp
#define LOG_SET   0   // settings.cpp
#define LOG_REN   0   // renderer.cpp
#define LOG_SFX   0   // layer_sfx.cpp
#define LOG_BRI   0   // brightness.cpp
#define LOG_NIGHT 0   // night_mode.cpp
#define LOG_PWR   0   // power_limiter.cpp
#define LOG_OTA   0   // ota_update.cpp
#define LOG_CAP   0   // captive_portal.cpp

//  Power limiting

#define POWER_LIMIT_DEFAULT 1500

//  Night mode

#define NIGHTMODE_DEFAULT_ENABLED 0
#define NIGHTMODE_MIN_BRIGHTNESS 5

//  Persistence

#define SETTINGS_FILE "/settings.json"
#define MQTT_SETTINGS_FILE "/mqtt.json"
#define MOTOR_CALIBRATION_FILE "/homing.json"

//  Layer order

#define LAYER_AMBIENT 0
#define LAYER_TIME 1
#define LAYER_SFX 2

//  SFX-Radar - Renders the radar scan effect

// Total rounds the radar beam should go
#define SFX_RADAR_ROUNDS 3
// SFX_RADAR_STEP_TIME and SFX_RADAR_TAIL are computed at runtime in layer_sfx.cpp
// based on RING_LEDS[0], so that the animation duration is independent of ring size.
