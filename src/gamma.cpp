/**
 * @file gamma.cpp
 * @brief Gamma correction lookup table — implementation.
 *
 * The 256-entry lookup table is populated once by init() using powf().
 * All subsequent apply() calls are pure table lookups with no arithmetic.
 */

#include "gamma.h"
#include "config.h"

namespace Gamma
{
    static uint8_t gammaLUT[256]; // Precomputed gamma correction table (index = raw value, result = corrected value).

    void init(float gamma)
    {
        for (int i = 0; i < 256; i++)
        {
            float normalized = (float)i / 255.0f;
            float corrected  = powf(normalized, gamma);
            gammaLUT[i]      = (uint8_t)(corrected * 255.0f + 0.5f); // round to nearest integer
        }
    }

    uint8_t apply(uint8_t value)
    {
        return gammaLUT[value];
    }

    Color::RGB apply(const Color::RGB &rgb)
    {
        return Color::RGB(
            gammaLUT[rgb.r],
            gammaLUT[rgb.g],
            gammaLUT[rgb.b]);
    }

} // namespace Gamma
