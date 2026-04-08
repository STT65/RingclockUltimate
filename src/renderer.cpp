/**
 * @file renderer.cpp
 * @brief Central rendering engine implementation.
 *
 * Composites the ambient, time and SFX layers into an RGB frame, applies
 * power limiting, brightness scaling and gamma correction, then outputs the
 * result to the LED strip via NeoPixelBus.
 *
 * The LED strip driver is selected via @ref LED_UART_METHOD in config.h and
 * must use the ESP8266 hardware UART channel. This is critical for motor
 * compatibility: unlike bit-banging methods, UART output does not disable
 * interrupts during NeoPixelBus::Show(), so the stepper motor timer ISR
 * continues to fire uninterrupted.
 *
 * @par NeoPixelBus UART transmission model (asynchronous method)
 * This project uses @c NeoEsp8266AsyncUart1800KbpsMethod, the asynchronous
 * UART variant. NeoPixelBus::Show() kicks off the transmission and returns
 * immediately; a dedicated UART TX ISR feeds the FIFO in the background until
 * all pixel data has been sent. The render buffer and the transmission buffer
 * are swapped on every Show() call so that rendering can proceed in parallel.
 *
 * This is essential at full scale (3 × 240 LEDs): synchronous transmission
 * would block the main loop for ~21.6 ms per frame, consuming ~65 % of the
 * 30 fps frame budget. With the asynchronous method the main loop is free
 * throughout the entire transmission.
 *
 * NeoPixelBus::CanShow() is time-based: it returns @c true once
 * @c ByteSendTimeUs*pixelCount + @c ResetTimeUs have elapsed since the last
 * Show() call.
 *
 * @par Buffer architecture
 * Two buffer layers are present at runtime:
 *
 * 1. @c buffer[MAX_RINGS][MAX_LEDS_PER_RING] — the compositing scratch pad.
 *    Holds @c Color::RGB values (RGB struct, host byte order). All three
 *    rendering layers write here in sequence; power limiting and brightness
 *    scaling are applied here before the result is handed to NeoPixelBus.
 *
 * 2. NeoPixelBus internal buffers @c _data / @c _dataSending — the async
 *    double buffer. @c _data receives the final pixel values via
 *    @c SetPixelColor(); @c _dataSending is fed to the UART TX ISR.
 *    Both are stored in the native wire format of the LED strip — the byte
 *    order is determined by the @c T_COLOR_FEATURE template parameter, which
 *    must match the physical strip type (e.g. @c NeoGrbFeature for WS2812B).
 *
 * **Why not render directly into the NeoPixelBus buffer?**
 * The NeoPixelBus edit buffer (@c _data) stores pixels in the native strip
 * wire format, whereas all layer math uses @c Color::RGB structs. Eliminating
 * buffer (1) would require byte-level wire-format access or per-pixel type
 * conversions throughout every layer. Additionally, after @c Show() the async swap leaves @c _data
 * with stale content from the previous @c _dataSending, making it unsuitable
 * as a compositing surface without an explicit clear. The RAM saving would be
 * 2 160 bytes (3 × 240 × 3) — roughly 2.7 % of the available heap — which
 * does not justify the added complexity.
 *
 * @par Wiring
 * UART1 TX (GPIO2/D4) -> Ring 0 data in
 * Ring 0 data out     -> Ring 1 data in
 * Ring 1 data out     -> Ring 2 data in
 */

#include "renderer.h"
#include "brightness.h"
#include "layer_ambient.h"
#include "time_state.h"
#include "layer_time.h"
#include "layer_sfx.h"
#include "gamma.h"
#include "settings.h"
#include "logging.h"
#include "config.h"

#include <NeoPixelBus.h>

namespace Renderer
{
    // -------------------------------------------------------------------------
    //  LED strip
    // -------------------------------------------------------------------------

    /// @brief NeoPixelBus strip — all rings chained on the hardware UART TX pin.
    /// The TX pin is fixed by the UART hardware (UART1 → GPIO2/D4) and must not
    /// be passed to the constructor; UART methods ignore the pin argument entirely.
    /// Uses hardware UART so Show() does not disable interrupts,
    /// keeping the stepper motor ISR unaffected.
    static NeoPixelBus<NeoGrbFeature, LED_UART_METHOD> strip(TOTAL_LEDS);

    // -------------------------------------------------------------------------
    //  Render buffer
    // -------------------------------------------------------------------------

    /// @brief Single render buffer — no double-buffering needed because
    /// renderFrame() is called from the Arduino main loop (single-threaded),
    /// guaranteeing the sequence: [all layers → buffer] → [SetPixelColor] → [Show()].
    /// Dimensioned to MAX_LEDS_PER_RING so rings with fewer LEDs simply use
    /// the first RING_LEDS[r] entries of each row.
    static Color::RGB buffer[MAX_RINGS][MAX_LEDS_PER_RING];

    static uint16_t lastCurrentmA  = 0; // Estimated current draw of the last frame [mA].
    static uint32_t nextRenderTime = 0; // Absolute millis() timestamp of the next required render.

#if LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG
    static uint32_t fpsFrameCount = 0; //  FPS counter (debug build only)
    static uint32_t fpsLastReport = 0;
#endif

    // -------------------------------------------------------------------------
    //  Internal helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the linear LED index for a given ring and pixel.
     * @param ring  Ring index (0..MAX_RINGS-1).
     * @param pixel Pixel index within the ring (0..RING_LEDS[ring]-1).
     * @return Absolute LED index in the strip.
     */
    static inline uint16_t ledIndex(uint8_t ring, uint8_t pixel)
    {
        uint16_t offset = 0;
        for (uint8_t r = 0; r < ring; r++) offset += RING_LEDS[r];
        return offset + pixel;
    }

    /**
     * @brief Clears a single ring buffer to black.
     * @param buf         Pointer to the ring's RGB array.
     * @param ledsPerRing Number of LEDs in this ring.
     */
    static void clearBuffer(Color::RGB *buf, uint8_t ledsPerRing)
    {
        for (uint8_t i = 0; i < ledsPerRing; i++)
            buf[i] = Color::RGB(0, 0, 0);
    }

    /**
     * @brief Estimates the current draw of a single ring buffer.
     * @details Sums all RGB channel values and scales to milliamperes.
     * Assumes WS2812B LEDs drawing up to 60 mA per LED at full brightness
     * (255, 255, 255 = 765 channel sum = 60 mA).
     * @param buf         Pointer to the ring's RGB array.
     * @param ledsPerRing Number of LEDs in this ring.
     * @return Estimated current in milliamperes.
     */
    static uint16_t estimateCurrent(const Color::RGB *buf, uint8_t ledsPerRing)
    {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < ledsPerRing; i++)
        {
            sum += buf[i].r;
            sum += buf[i].g;
            sum += buf[i].b;
        }
        return (sum * 60) / 765; // 255*3 = 765 → 60 mA
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void init()
    {
        LOG_INFO(LOG_REN, F("REN: Initialising renderer..."));
        Gamma::init(GAMMA_VALUE);
        SFXLayer::init();
        strip.Begin();
        strip.Show();
        LOG_INFO(LOG_REN, F("REN: Renderer initialised."));
    }

    void renderFrame()
    {
        // Gate 1: wait until the layer schedule requires a new frame.
        // Signed subtraction is used deliberately: unsigned wrap-around at ~49.7 days
        // is handled correctly as long as the scheduled interval is < 2^31 ms (~24 days).
        if ((int32_t)(millis() - nextRenderTime) < 0)
            return;

        // Gate 2: wait until the previous UART transmission has completed
        if (!strip.CanShow())
            return;

        nextRenderTime = UINT32_MAX;

        // ----------------------------------------------------------------
        // Step 1: Clear buffer
        // ----------------------------------------------------------------
        for (uint8_t r = 0; r < MAX_RINGS; r++)
            clearBuffer(buffer[r], RING_LEDS[r]);

        // ----------------------------------------------------------------
        // Step 2: Ambient layer — background, hour and quarter markers
        // ----------------------------------------------------------------
        for (uint8_t r = 0; r < MAX_RINGS; r++)
            AmbientLayer::render(r, buffer[r]);

        // ----------------------------------------------------------------
        // Step 3: Time layer — hour, minute and second hands with tails
        // ----------------------------------------------------------------
        for (uint8_t r = 0; r < MAX_RINGS; r++)
            nextRenderTime = min(nextRenderTime, TimeLayer::render(r, buffer[r]));

        // ----------------------------------------------------------------
        // Step 4: SFX layer — interval check + effect rendering
        // ----------------------------------------------------------------
        const auto &t = TimeState::get();
        SFXLayer::checkIntervals(t.h, t.m);
        for (uint8_t r = 0; r < MAX_RINGS; r++)
            nextRenderTime = min(nextRenderTime, SFXLayer::render(r, buffer[r]));

        // ----------------------------------------------------------------
        // Step 5: Power limiting — scale down if estimated draw exceeds limit
        // ----------------------------------------------------------------
        lastCurrentmA = 0;
        for (uint8_t r = 0; r < MAX_RINGS; r++)
            lastCurrentmA += estimateCurrent(buffer[r], RING_LEDS[r]);

        if (lastCurrentmA > Settings::powerLimit)
        {
            float scale = (float)Settings::powerLimit / (float)lastCurrentmA;
            for (uint8_t r = 0; r < MAX_RINGS; r++)
            {
                for (uint8_t i = 0; i < RING_LEDS[r]; i++)
                {
                    buffer[r][i].r = buffer[r][i].r * scale;
                    buffer[r][i].g = buffer[r][i].g * scale;
                    buffer[r][i].b = buffer[r][i].b * scale;
                }
            }
            lastCurrentmA = Settings::powerLimit;
        }

        // ----------------------------------------------------------------
        // Step 6: Brightness scaling + gamma correction + strip output
        // ----------------------------------------------------------------
        uint8_t sysBright = Brightness::getSystemBrightness();

        for (uint8_t r = 0; r < MAX_RINGS; r++)
        {
            for (uint8_t i = 0; i < RING_LEDS[r]; i++)
            {
                Color::RGB pixel = buffer[r][i];

                // Apply system brightness (bit-shift avoids division)
                if (sysBright < 255)
                {
                    pixel.r = (pixel.r * sysBright) >> 8;
                    pixel.g = (pixel.g * sysBright) >> 8;
                    pixel.b = (pixel.b * sysBright) >> 8;
                }

                // Apply gamma correction to the already-dimmed value
                Color::RGB g = Gamma::apply(pixel);
                strip.SetPixelColor(ledIndex(r, i), RgbColor(g.r, g.g, g.b));
            }
        }

        // Adjust current estimate to reflect actual brightness applied
        lastCurrentmA = (uint32_t)lastCurrentmA * sysBright / 255;

        // Single Show() call covers all rings
        strip.Show();

#if LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG
        // FPS logging — once per second
        fpsFrameCount++;
        uint32_t now = millis();
        if (now - fpsLastReport >= 1000)
        {
            LOG_DEBUG(LOG_REN, String(F("REN: StripFrameRate=")) + fpsFrameCount + F("fps"));
            fpsFrameCount = 0;
            fpsLastReport = now;
        }
#endif

    }

    uint16_t getLastCurrentmA()
    {
        return lastCurrentmA;
    }

    uint32_t getNextRenderTime()
    {
        return nextRenderTime;
    }

    void scheduleFrame()
    {
        nextRenderTime = millis();
    }

} // namespace Renderer
