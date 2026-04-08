/**
 * @file brightness.h
 * @brief Public interface for ambient light measurement and system brightness control.
 *
 * Manages the BH1750 light sensor and calculates the effective system brightness
 * that is applied to all LED output. Three operating modes are supported:
 * manual brightness, automatic brightness (sensor-based), and night mode
 * (scheduled dimming).
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call update() on every Arduino loop iteration.
 * - Read the result via getSystemBrightness() before rendering each LED frame.
 */

#pragma once
#include <Arduino.h>

namespace Brightness
{
    /**
     * @brief Initialises the BH1750 light sensor over I2C and sets the initial brightness.
     * @details Starts the sensor in CONTINUOUS_HIGH_RES_MODE. If the sensor is not found
     * an error is logged but execution continues. The initial smoothed brightness is set
     * from Settings::manualBrightness or Settings::autoMin depending on the active mode.
     */
    void init();

    /**
     * @brief Updates the system brightness — must be called on every Arduino loop iteration.
     * @details Runs at most once per Brightness_INTERVAL_MS (defined in config.h).
     * Performs three steps in order:
     *
     * 1. Determines the base target brightness from the active mode (auto or manual).
     * 2. Overrides the target with Settings::nightBrightness if night mode is active.
     * 3. Applies a low-pass filter (factor 0.1) for smooth brightness transitions.
     *
     * Mode and day/night transitions are logged at info level.
     */
    void update();

    /**
     * @brief Returns the last raw lux reading from the BH1750 sensor.
     * @details Only updated when autoBrightness is active. Returns 0.0 in manual mode
     * or before the first sensor reading.
     * @return Ambient light level in lux.
     */
    float getLux();

    /**
     * @brief Returns the current effective system brightness.
     * @details The value is the low-pass filtered result of the active brightness mode
     * (manual, automatic, or night). Use this value as the global LED brightness scalar.
     * @return Brightness in the range 0–255.
     */
    uint8_t getSystemBrightness();

} // namespace Brightness
