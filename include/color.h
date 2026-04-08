/**
 * @file color.h
 * @brief Color structures and operations for the Ringclock Ultimate.
 *
 * Provides RGB and HSV color models with conversion, interpolation and
 * blending functions. All operations are integer-based and therefore
 * well-suited for the ESP8266.
 */

#pragma once
#include <Arduino.h>

namespace Color
{
    // -------------------------------------------------------------------------
    //  Color structures
    // -------------------------------------------------------------------------

    /**
     * @brief 24-bit RGB color (8 bits per channel).
     */
    struct RGB
    {
        uint8_t r; // Red channel (0–255).
        uint8_t g; // Green channel (0–255).
        uint8_t b; // Blue channel (0–255).

        /**
         * @brief Constructs an RGB color.
         * @param r Red channel value (0–255).
         * @param g Green channel value (0–255).
         * @param b Blue channel value (0–255).
         */
        RGB(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : r(r), g(g), b(b) {}
    };

    /**
     * @brief HSV color (hue, saturation, value).
     * Hue is in degrees (0–359), saturation and value are in the range 0–255.
     */
    struct HSV
    {
        uint16_t h; // Hue in degrees (0–359).
        uint8_t  s; // Saturation (0–255).
        uint8_t  v; // Value / brightness (0–255).

        /**
         * @brief Constructs an HSV color.
         * @param h Hue in degrees (0–359).
         * @param s Saturation (0–255).
         * @param v Value / brightness (0–255).
         */
        HSV(uint16_t h = 0, uint8_t s = 0, uint8_t v = 0) : h(h), s(s), v(v) {}
    };

    // -------------------------------------------------------------------------
    //  Color operations
    // -------------------------------------------------------------------------

    /**
     * @brief Converts an HSV color to RGB.
     * @details Integer-only implementation — no floating-point arithmetic.
     * @param hsv Input color in the HSV color space.
     * @return Equivalent RGB color.
     */
    RGB hsvToRgb(const HSV &hsv);

    /**
     * @brief Linearly interpolates between two RGB colors.
     * @details factor=0 returns @p a, factor=255 returns @p b.
     * @param a      Start color.
     * @param b      End color.
     * @param factor Blend factor (0–255).
     * @return Interpolated RGB color.
     */
    RGB lerp(const RGB &a, const RGB &b, uint8_t factor);

    /**
     * @brief Additively blends two RGB colors, clamping each channel at 255.
     * @param base Base color.
     * @param add  Color to add.
     * @return Blended RGB color.
     */
    RGB add(const RGB &base, const RGB &add);

    /**
     * @brief Converts a CSS hex color string to HSV.
     * @details Accepts the format @c "#RRGGBB" as produced by HTML color pickers.
     * Used to deserialize colors received from the web interface.
     * @param hex Hex color string (e.g. @c "#ff8000").
     * @return Equivalent HSV color, or HSV(0,0,0) if the string is invalid.
     */
    HSV hexToHsv(const String &hex);

    /**
     * @brief Converts an HSV color to a CSS hex color string.
     * @details Produces the format @c "#rrggbb" suitable for JSON and HTML color pickers.
     * Used to serialize colors for transmission to the web interface.
     * @param hsv Input color in the HSV color space.
     * @return Hex color string (e.g. @c "#ff8000").
     */
    String hsvToHex(const HSV &hsv);

} // namespace Color
