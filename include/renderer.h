/**
 * @file renderer.h
 * @brief Central rendering engine for the Ringclock Ultimate.
 *
 * Composites all LED layers, applies power limiting, brightness scaling,
 * gamma correction and writes the final pixel data to the LED strip via
 * NeoPixelBus.
 *
 * @par Dynamic frame rate
 * The renderer does not run at a fixed frame rate. Instead, each layer's
 * render() function returns the absolute millis() timestamp at which it
 * next requires a new frame. renderFrame() collects the minimum across all
 * layers into nextRenderTime; the main loop only calls renderFrame() when
 * that timestamp has been reached.
 *
 * This means the actual frame rate adapts automatically to the content:
 *
 * | Situation                        | Effective frame rate |
 * |----------------------------------|----------------------|
 * | Clock only, no SFX               | ~1 fps (1 per second)|
 * | Heartbeat or short-circuit active| ~60 fps              |
 * | Radar scan active                | ~30 fps              |
 * | Shooting star active             | ~60 fps              |
 *
 * Configuration changes are applied immediately via scheduleFrame(), which
 * sets nextRenderTime to millis() regardless of which layer is affected.
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call renderFrame() on every Arduino loop iteration — timing is self-contained.
 * - Call scheduleFrame() after any configuration change.
 */

#pragma once
#include <Arduino.h>
#include "color.h"

namespace Renderer
{
    /**
     * @brief Initialises the renderer, the gamma LUT and the LED strip.
     * @details Calls Gamma::init(), SFXLayer::init(), and NeoPixelBus::Begin().
     * Sends a blank frame to clear any residual LED state.
     * Must be called once during application setup before renderFrame().
     */
    void init();

    /**
     * @brief Renders and outputs one complete LED frame.
     * @details Safe to call on every loop iteration — returns immediately
     * if the layer schedule has not yet elapsed (millis() < nextRenderTime)
     * or if the previous NeoPixelBus transmission is still in progress
     * (strip.CanShow() == false). No external timing gate required.
     *
     * Rendering pipeline:
     *
     * 1. Reset nextRenderTime to UINT32_MAX.
     *
     * 2. Clear render buffer.
     *
     * 3. Render ambient layer; update nextRenderTime with its return value.
     *
     * 4. Render time layer; update nextRenderTime with its return value.
     *
     * 5. Check and trigger SFX intervals, render SFX layer;
     *    update nextRenderTime with its return value.
     *
     * 6. Estimate total current draw; scale all pixels down if it exceeds
     *    Settings::powerLimit.
     *
     * 7. Apply system brightness scaling and gamma correction per pixel.
     *
     * 8. Write all pixels to the strip and call NeoPixelBus::Show().
     *
     * After this call, getNextRenderTime() returns the earliest absolute
     * millis() timestamp at which any layer requires the next frame.
     */
    void renderFrame();

    /**
     * @brief Returns the absolute millis() timestamp of the next required render.
     * @details Updated on every completed renderFrame() call as the minimum
     * nextRenderTime reported across all layers. The caller should invoke
     * renderFrame() only when millis() >= getNextRenderTime().
     * @return Absolute millis() timestamp. UINT32_MAX if no layer requires
     *         a periodic re-render (e.g. only static ambient, no SFX active).
     */
    uint32_t getNextRenderTime();

    /**
     * @brief Schedules an immediate re-render on the next loop iteration.
     * @details Sets nextRenderTime to millis(), causing the main loop's
     * millis() >= getNextRenderTime() check to pass on the very next call.
     * Call this whenever any configuration change should be reflected on the
     * LEDs immediately, regardless of which layer is affected.
     */
    void scheduleFrame();

    /**
     * @brief Returns the estimated current draw of the last rendered frame.
     * @details Based on per-channel RGB sums, assuming 60 mA per LED at full
     * brightness (WS2812B). Capped at Settings::powerLimit when limiting is active.
     * @return Estimated current in milliamperes.
     */
    uint16_t getLastCurrentmA();

} // namespace Renderer
