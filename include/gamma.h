/**
 * @file gamma.h
 * @brief Fast gamma correction for RGB colors using a lookup table.
 *
 * Provides perceptually linear LED brightness by applying a power-law
 * correction to raw 8-bit values. The lookup table is computed once at
 * startup and thereafter accessed read-only, making apply() negligible
 * in cost during rendering.
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call apply() on every RGB value before writing to the LED strip.
 */

#pragma once
#include <Arduino.h>
#include "color.h"

namespace Gamma
{
    /**
     * @brief Builds the gamma correction lookup table.
     * @details Computes 256 corrected values using the formula:
     * output = (input / 255) ^ gamma * 255.
     * Floating-point arithmetic is used here but only runs once at startup.
     * Must be called before any call to apply().
     * @param gamma Gamma exponent (e.g. 2.2 for typical LED strips).
     */
    void init(float gamma);

    /**
     * @brief Applies gamma correction to a single 8-bit value.
     * @details Single lookup table access — negligible runtime cost.
     * @param value Raw input value (0–255).
     * @return Gamma-corrected value (0–255).
     */
    uint8_t apply(uint8_t value);

    /**
     * @brief Applies gamma correction to all three channels of an RGB color.
     * @param rgb Input RGB color.
     * @return Gamma-corrected RGB color.
     */
    Color::RGB apply(const Color::RGB &rgb);

} // namespace Gamma
