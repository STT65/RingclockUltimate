#pragma once
#include <Arduino.h>
#include "color.h"

/**
 * @file power_limiter.h
 * @brief Estimation and limiting of the LED ring power consumption.
 *
 * This module estimates the theoretical current draw based on the RGB values
 * of a LED buffer and scales down brightness when the limit configured in
 * the web interface is exceeded.
 */

namespace PowerLimiter {

    /**
     * @brief Estimates the current draw of a LED buffer.
     *
     * @param buffer Pointer to an array of 60 RGB LEDs.
     * @return Estimated current draw in milliamps.
     *
     * @note WS2812B: max. 60 mA per LED at full brightness.
     */
    uint16_t estimateCurrent(const Color::RGB* buffer);

    /**
     * @brief Applies a current limit to a LED buffer.
     *
     * If the estimated current draw exceeds the limit, overall brightness
     * is scaled down proportionally.
     *
     * @param buffer    Pointer to an array of 60 RGB LEDs.
     * @param limit_mA  Maximum allowed current in milliamps.
     * @return Actual current draw after limiting.
     */
    uint16_t applyLimit(Color::RGB* buffer, uint16_t limit_mA);

} // namespace PowerLimiter
