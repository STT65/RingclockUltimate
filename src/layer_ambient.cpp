/**
 * @file layer_ambient.cpp
 * @brief Ambient layer renderer implementation.
 *
 * Renders background light, hour markers and quarter-hour markers into
 * an LED ring buffer. All colors are read from Settings and converted
 * from HSV to RGB on every render call.
 */

#include "layer_ambient.h"
#include "settings.h"
#include "config.h"
#include "color.h"

namespace AmbientLayer
{
    void render(uint8_t ringIndex, Color::RGB *buffer)
    {
        const uint8_t n = RING_LEDS[ringIndex];

        // ----------------------------------------------------------------
        // 1. Background light — fill all LEDs or clear to black
        // ----------------------------------------------------------------
        Color::RGB base = Settings::ambientEnabled
            ? Color::hsvToRgb(Settings::ambientColor)
            : Color::RGB(0, 0, 0);

        for (uint8_t i = 0; i < n; i++)
            buffer[i] = base;

        // ----------------------------------------------------------------
        // 2. Hour markers — every n/12 LEDs (H12-grid)
        // ----------------------------------------------------------------
        if (Settings::hourMarksEnabled)
        {
            Color::RGB hourColor = Color::hsvToRgb(Settings::hourMarkColor);
            for (uint8_t i = 0; i < n; i += n / 12)
                buffer[i] = hourColor;
        }

        // ----------------------------------------------------------------
        // 3. Quarter-hour markers — LEDs at 0%, 25%, 50%, 75% (highest priority)
        // ----------------------------------------------------------------
        if (Settings::quarterMarksEnabled)
        {
            Color::RGB qColor = Color::hsvToRgb(Settings::quarterMarkColor);
            buffer[0]          = qColor;
            buffer[n / 4]      = qColor;
            buffer[n / 2]      = qColor;
            buffer[3 * n / 4]  = qColor;
        }
    }

} // namespace AmbientLayer
