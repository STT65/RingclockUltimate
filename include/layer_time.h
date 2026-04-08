/**
 * @file layer_time.h
 * @brief Time layer renderer — hour, minute and second hands with tails.
 *
 * Renders the clock hands and their gradient tails into an LED ring buffer.
 * The result is additively composited on top of the ambient layer.
 * Does not perform gamma correction or physical LED output.
 */

#pragma once
#include <Arduino.h>
#include "color.h"

namespace TimeLayer
{
    /**
     * @brief Renders the time layer into an LED ring buffer.
     * @details Draws the hour, minute and second hands — each with a forward
     * and backward gradient tail — into the provided buffer. Each hand is only
     * rendered on rings whose index bit is set in its HandSettings::ringMask.
     * Hand colors, tail lengths and tail colors are read from Settings.
     * The hand LED overwrites the tail origin so it always appears at full brightness.
     * @param ringIndex Index of the ring currently being rendered (0–2).
     * @param buffer    Pointer to an array of LEDS_PER_RING RGB values.
     *                  Hand and tail pixels are written additively on top of existing values.
     * @return Absolute millis() timestamp at which this layer next needs to be
     *         rendered — the start of the next full second (next secondIndex change).
     */
    uint32_t render(uint8_t ringIndex, Color::RGB *buffer);

} // namespace TimeLayer
