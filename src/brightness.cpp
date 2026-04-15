/**
 * @file brightness.cpp
 * @brief Ambient light measurement and system brightness control.
 *
 * Reads the BH1750 light sensor and calculates the effective system brightness
 * that is applied to all LED output. Two operating modes are supported:
 *
 * - **Manual mode**: brightness is fixed at NightMode::manualBrightness.
 * - **Auto mode**: brightness is derived from the measured lux value, scaled
 *   between Settings::autoMin and Settings::autoMax.
 *
 * Night mode dimming is handled transparently via the NightMode shadow variables:
 * when active, NightMode::autoBrightness is forced false and NightMode::manualBrightness
 * is set to Settings::nightBrightness. This module requires no special night mode logic.
 *
 * All mode transitions are smoothed by a first-order low-pass filter to avoid
 * abrupt brightness changes.
 */

#include "Brightness.h"
#include "night_mode.h"
#include "settings.h"
#include "config.h"
#include "logging.h"
#include "time_state.h"
#include <Wire.h>
#include <BH1750.h>

namespace Brightness
{
    // -------------------------------------------------------------------------
    //  Module state
    // -------------------------------------------------------------------------

    static float lastLux = 0.0f;                                 // Last raw lux reading from the sensor.
    static uint32_t lastMeasureMs = 0;                           // Timestamp of the last update() execution [ms].
    static float smoothedBrightness = (float)DEFAULT_BRIGHTNESS; // Low-pass filtered brightness (float for precision).
    static uint8_t systemBrightness = DEFAULT_BRIGHTNESS;        // Final rounded brightness value (0–255).

    static bool lastAutoMode = false;  // Tracks the previous auto-mode state to detect transitions.

    static BH1750 lightMeter; // BH1750 sensor instance.

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void init()
    {
        LOG_INFO(LOG_BRI, F("BRI: Initialising BH1750 sensor..."));
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
        if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE))
            LOG_INFO(LOG_BRI, F("BRI:  BH1750 found."));
        else
            LOG_ERROR(LOG_BRI, F("BRI:  BH1750 not found! Please check wiring."));

        // NightMode shadows are not yet initialised at this point — read Settings directly.
        lastAutoMode = Settings::autoBrightness;

        if (Settings::autoBrightness)
        {
            smoothedBrightness = (float)Settings::autoMin;
            LOG_INFO(LOG_BRI, F("BRI:  Auto mode active."));
        }
        else
        {
            smoothedBrightness = (float)Settings::manualBrightness;
            LOG_INFO(LOG_BRI, F("BRI:  Manual mode active."));
        }
        systemBrightness = (uint8_t)smoothedBrightness;
    }

    void update()
    {
        uint32_t now = millis();
        if (now - lastMeasureMs < Brightness_INTERVAL_MS)
            return;
        lastMeasureMs = now;

        // Log mode transitions triggered e.g. by the web interface or night mode
        if (NightMode::autoBrightness != lastAutoMode)
        {
            lastAutoMode = NightMode::autoBrightness;
            LOG_INFO(LOG_BRI, lastAutoMode ? F("BRI:  Switched to AUTO mode.") : F("BRI:  Switched to MANUAL mode."));
        }

        // -------------------------------------------------------------------------
        // Determine target brightness from effective (night-aware) mode variables
        // -------------------------------------------------------------------------
        float targetBrightness;
        if (NightMode::autoBrightness)
        {
            lastLux = lightMeter.readLightLevel();
            if (lastLux < 0)
                lastLux = 0;

            // Clamp lux to the configured maximum and scale to the brightness range
            float clampedLux = min(lastLux, (float)Settings::autoLuxMax);
            targetBrightness = (clampedLux / (float)Settings::autoLuxMax) * (Settings::autoMax - Settings::autoMin) + Settings::autoMin;
        }
        else
        {
            targetBrightness = (float)NightMode::manualBrightness;
        }

        // -------------------------------------------------------------------------
        // Apply low-pass filter for smooth brightness transitions
        // -------------------------------------------------------------------------
        const float filterFactor = 0.1f;
        smoothedBrightness = (targetBrightness * filterFactor) + (smoothedBrightness * (1.0f - filterFactor));
        systemBrightness = (uint8_t)(smoothedBrightness + 0.5f); // round to nearest integer
    }

    float getLux()
    {
        return lastLux;
    }

    uint8_t getSystemBrightness()
    {
        return systemBrightness;
    }

} // namespace Brightness
