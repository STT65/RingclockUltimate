/**
 * @file color.cpp
 * @brief Color conversion, interpolation and blending operations.
 *
 * All functions operate on the RGB and HSV structures defined in color.h.
 * The HSV→RGB conversion is fully integer-based. The hex↔HSV conversions
 * use floating-point arithmetic since they are only called when settings
 * are loaded or saved, not during rendering.
 */

#include "color.h"
#include <algorithm> // std::max, std::min
#include <stdio.h>   // snprintf

namespace Color
{
    // -------------------------------------------------------------------------
    //  HSV → RGB conversion (integer-based)
    // -------------------------------------------------------------------------

    RGB hsvToRgb(const HSV &hsv)
    {
        uint16_t h = hsv.h % 360;
        uint8_t  s = hsv.s;
        uint8_t  v = hsv.v;

        // Achromatic case: return grey
        if (s == 0)
            return RGB(v, v, v);

        uint16_t region    = h / 60;
        uint16_t remainder = (h - (region * 60)) * 255 / 60;

        uint8_t p = (v * (255 - s)) / 255;
        uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
        uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

        switch (region)
        {
        case 0:  return RGB(v, t, p);
        case 1:  return RGB(q, v, p);
        case 2:  return RGB(p, v, t);
        case 3:  return RGB(p, q, v);
        case 4:  return RGB(t, p, v);
        default: return RGB(v, p, q);
        }
    }

    // -------------------------------------------------------------------------
    //  Linear interpolation (0–255)
    // -------------------------------------------------------------------------

    RGB lerp(const RGB &a, const RGB &b, uint8_t factor)
    {
        uint16_t inv = 255 - factor;
        return RGB(
            (a.r * inv + b.r * factor) / 255,
            (a.g * inv + b.g * factor) / 255,
            (a.b * inv + b.b * factor) / 255);
    }

    // -------------------------------------------------------------------------
    //  Additive blending with per-channel clamping
    // -------------------------------------------------------------------------

    RGB add(const RGB &base, const RGB &add)
    {
        return RGB(
            (uint16_t)base.r + add.r > 255 ? 255 : base.r + add.r,
            (uint16_t)base.g + add.g > 255 ? 255 : base.g + add.g,
            (uint16_t)base.b + add.b > 255 ? 255 : base.b + add.b);
    }

    // -------------------------------------------------------------------------
    //  Hex ↔ HSV conversion (used for web interface serialization only)
    // -------------------------------------------------------------------------

    HSV hexToHsv(const String &hex)
    {
        if (hex.length() < 7)
            return HSV(0, 0, 0);

        long  rgb   = strtol(hex.c_str() + 1, NULL, 16);
        float r     = ((rgb >> 16) & 0xFF) / 255.0f;
        float g     = ((rgb >>  8) & 0xFF) / 255.0f;
        float b     = ( rgb        & 0xFF) / 255.0f;

        float cmax  = std::max({r, g, b});
        float cmin  = std::min({r, g, b});
        float delta = cmax - cmin;

        int     h = 0;
        uint8_t s = 0;
        uint8_t v = cmax * 255.0f;

        if (delta > 0.001f)
        {
            if      (cmax == r) h = 60.0f * fmod(((g - b) / delta), 6.0f);
            else if (cmax == g) h = 60.0f * (((b - r) / delta) + 2.0f);
            else                h = 60.0f * (((r - g) / delta) + 4.0f);

            s = (delta / cmax) * 255.0f;
        }

        if (h < 0) h += 360;
        return HSV((uint16_t)h, s, v);
    }

    String hsvToHex(const HSV &hsv)
    {
        RGB  rgb = hsvToRgb(hsv);
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02x%02x%02x", rgb.r, rgb.g, rgb.b);
        return String(buf);
    }

} // namespace Color
