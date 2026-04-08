/**
 * @file layer_time.cpp
 * @brief Time layer renderer implementation.
 *
 * Renders clock hands with gradient tails into an LED ring buffer.
 * All hand positions and LED indices are read from TimeState; all visual
 * parameters (colors, tail lengths, ring masks) come from Settings.
 */

#include "layer_time.h"
#include "time_state.h"
#include "settings.h"
#include "config.h"
#include "color.h"

namespace TimeLayer
{
    // -------------------------------------------------------------------------
    //  Internal helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Scales a time component linearly to a LED index for a given ring size.
     * @param value       Time component value (e.g. minutes 0–59).
     * @param maxValue    Full range of the component (e.g. 60).
     * @param ledsPerRing Number of LEDs in the target ring.
     * @return LED index in the range 0..(ledsPerRing-1).
     */
    static uint8_t mapToLED(uint8_t value, uint8_t maxValue, uint8_t ledsPerRing)
    {
        return (uint8_t)((uint16_t)value * ledsPerRing / maxValue);
    }

    /**
     * @brief Draws a gradient tail extending from a hand position.
     * @details Two modes depending on @p length:
     *
     * - **Normal tail** (length 1–10): draws @p length LEDs in the given
     *   direction, interpolating from @p startColor (closest to the hand)
     *   to @p endColor (tip of the tail).
     *
     * - **Fill mode** (length == 11): fills in the given direction from @p pos
     *   to index 0 (12 o'clock). The gradient spans the full startColor–endColor
     *   range across the actual fill distance so the complete gradient is always
     *   visible regardless of hand position.
     *
     * @param buffer      LED ring buffer to write into.
     * @param pos         LED index of the hand (tail origin).
     * @param length      Tail length in LEDs, or 11 for fill mode.
     * @param startColor  Color at the tail origin (closest to the hand).
     * @param endColor    Color at the tail tip (at index 0).
     * @param forward     true = tail extends clockwise, false = counter-clockwise.
     * @param ledsPerRing Number of LEDs in the ring.
     */
    static void drawTail(Color::RGB *buffer, uint8_t pos, uint8_t length,
                         Color::RGB startColor, Color::RGB endColor, bool forward,
                         uint8_t ledsPerRing)
    {
        if (length == 0) return;

        if (length == 11)
        {
            // Fill mode: fill from pos to index 0 in the given direction.
            // fillLength is the actual number of LEDs to paint; use it as the
            // gradient denominator so startColor→endColor always spans the full fill.
            uint8_t fillLength = forward ? (ledsPerRing - pos) : pos;
            if (fillLength == 0) return;

            for (uint8_t i = 1; i <= fillLength; i++)
            {
                uint8_t idx = forward
                    ? (pos + i) % ledsPerRing
                    : (pos + ledsPerRing - i) % ledsPerRing;

                uint8_t factor = (i * 255) / fillLength;
                buffer[idx] = Color::lerp(startColor, endColor, factor);
            }
        }
        else
        {
            // Normal tail: fixed length with linear gradient
            for (uint8_t i = 1; i <= length; i++)
            {
                uint8_t idx = forward
                    ? (pos + i) % ledsPerRing
                    : (pos + ledsPerRing - i) % ledsPerRing;

                uint8_t factor = (i * 255) / length;
                buffer[idx] = Color::lerp(startColor, endColor, factor);
            }
        }
    }

    /**
     * @brief Draws a single clock hand with its forward and backward tails.
     * @details Renders normal tails (length 1–10) first, then the hand LED on
     * top. Fill-mode tails (length == 11) are intentionally skipped here; they
     * are rendered in a dedicated pre-pass in render() so they appear below all
     * hand pointers and normal tails.
     * @param buffer      LED ring buffer to write into.
     * @param pos         LED index of the hand (0..ledsPerRing-1).
     * @param cfg         Hand configuration (colors, tail lengths) from Settings.
     * @param ledsPerRing Number of LEDs in the ring.
     */
    static void renderHand(Color::RGB *buffer, uint8_t pos, const Settings::HandSettings &cfg,
                           uint8_t ledsPerRing)
    {
        Color::RGB handRGB     = Color::hsvToRgb(cfg.handColor);
        Color::RGB tStartRGB   = Color::hsvToRgb(cfg.tailStartColor);
        Color::RGB tEndFwdRGB  = Color::hsvToRgb(cfg.tailFwdEndColor);
        Color::RGB tEndBackRGB = Color::hsvToRgb(cfg.tailBackEndColor);

        // Normal tails only; fill tails (length == 11) handled in a separate pre-pass
        if (cfg.tailFwdLength  != 11)
            drawTail(buffer, pos, cfg.tailFwdLength,  tStartRGB, tEndFwdRGB,  true,  ledsPerRing);
        if (cfg.tailBackLength != 11)
            drawTail(buffer, pos, cfg.tailBackLength, tStartRGB, tEndBackRGB, false, ledsPerRing);

        buffer[pos] = handRGB;
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    uint32_t render(uint8_t ringIndex, Color::RGB *buffer)
    {
        const auto &t = TimeState::get();
        const uint8_t n = RING_LEDS[ringIndex];

        // Compute LED indices for this ring's size
        uint8_t minuteLED = mapToLED(t.m, 60, n);
        uint8_t hourLED   = (uint8_t)(((uint16_t)(t.h % 12) * n + minuteLED) / 12);
        uint8_t secondLED = mapToLED(t.s, 60, n);

        const bool secondEnabled = (Settings::secondHand.ringMask != 0);

        // --- Pass 1: fill-mode tails (length == 11) rendered first so they
        //             appear below all hand pointers and normal tails.
        struct HandEntry { uint8_t pos; const Settings::HandSettings *cfg; bool enabled; };
        const HandEntry hands[] = {
            { hourLED,   &Settings::hourHand,   true          },
            { minuteLED, &Settings::minuteHand, true          },
            { secondLED, &Settings::secondHand, secondEnabled },
        };
        for (const auto &h : hands)
        {
            if (!h.enabled || !(h.cfg->ringMask & (1 << ringIndex))) continue;
            Color::RGB tStart   = Color::hsvToRgb(h.cfg->tailStartColor);
            Color::RGB tEndFwd  = Color::hsvToRgb(h.cfg->tailFwdEndColor);
            Color::RGB tEndBack = Color::hsvToRgb(h.cfg->tailBackEndColor);
            if (h.cfg->tailFwdLength  == 11)
                drawTail(buffer, h.pos, 11, tStart, tEndFwd,  true,  n);
            if (h.cfg->tailBackLength == 11)
                drawTail(buffer, h.pos, 11, tStart, tEndBack, false, n);
        }

        // --- Pass 2: normal tails and hand pointer LEDs on top
        if (Settings::hourHand.ringMask   & (1 << ringIndex))
            renderHand(buffer, hourLED,   Settings::hourHand,   n);

        if (Settings::minuteHand.ringMask & (1 << ringIndex))
            renderHand(buffer, minuteLED, Settings::minuteHand, n);

        if ((Settings::secondHand.ringMask & (1 << ringIndex)) && secondEnabled)
            renderHand(buffer, secondLED, Settings::secondHand, n);

        // Next render at the start of the next second (ms-accurate via getTimeMs base)
        return millis() + (1000 - t.ms);
    }

} // namespace TimeLayer
