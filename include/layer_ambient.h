/**
 * @file layer_ambient.h
 * @brief Ambient layer renderer — background light, hour and quarter-hour markers.
 *
 * Fills an LED ring buffer with the static background lighting layer.
 * Does not perform gamma correction or physical LED output.
 */

#pragma once
#include <Arduino.h>
#include "color.h"

namespace AmbientLayer
{
    /**
     * @brief Renders the ambient layer into an LED ring buffer.
     * @details Writes three sub-layers in ascending priority order so that
     * higher-priority elements overwrite lower ones:
     *
     * 1. Background light — fills all 60 LEDs with Settings::ambientColor,
     *    or black if ambientEnabled is false.
     *
     * 2. Hour markers — overwrites every 5th LED (0, 5, 10, ..., 55)
     *    with Settings::hourMarkColor if hourMarksEnabled is true.
     *
     * 3. Quarter-hour markers — overwrites LEDs 0, 15, 30 and 45
     *    with Settings::quarterMarkColor if quarterMarksEnabled is true.
     *
     * @param ringIndex Index of the target ring (0–2). Currently unused
     *                  internally but reserved for per-ring configuration.
     * @param buffer    Pointer to an array of exactly LEDS_PER_RING RGB values.
     *                  The entire array is overwritten on every call.
     */
    void render(uint8_t ringIndex, Color::RGB *buffer);

} // namespace AmbientLayer
