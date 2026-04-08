/**
 * @file layer_sfx.h
 * @brief Special effects (SFX) layer for the LED ring clock.
 *
 * Provides four visual effects that are composited on top of the ambient
 * and time layers. Effects can be triggered automatically at configurable
 * intervals or manually from the web interface.
 *
 * Available effects:
 *
 * - **Short circuit**: 1 s of irregular cold-white flashing across all LEDs.
 *
 * - **Radar scan**: a green phosphor-style sweep that orbits the ring
 *   a configurable number of times with a quadratically fading tail.
 *
 * - **Heartbeat**: 5-second sinusoidal modulation of the ambient brightness,
 *   simulating a double-beat (lubb-dupp) rhythm. Triggered manually or on an
 *   interval; modulation range at full intensity is 50–100 % brightness.
 *
 * - **Shooting star**: a brief bright streak that travels around ring 1.
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call checkIntervals() once per minute with the current time.
 * - Call render() on every frame for each ring.
 */

#pragma once
#include <stdint.h>
#include "color.h"

namespace SFXLayer
{
    /**
     * @brief Initialises the SFX layer and resets all effect states.
     * Must be called once during application setup before render().
     */
    void init();

    /**
     * @brief Renders all active effects into an LED ring buffer.
     * @details Effects are composited in the following priority order
     * (lowest to highest, each may overwrite the previous):
     *
     * 1. Heartbeat — modulates existing buffer brightness in-place.
     *
     * 2. Shooting star — draws a brief streak on ring 1.
     *
     * 3. Radar scan — draws a green sweep with fading tail.
     *
     * 4. Short circuit — overwrites everything with cold-white flashes.
     *
     * @param ringIndex Index of the ring currently being rendered (0–2).
     * @param buffer    Pointer to an array of LEDS_PER_RING RGB values.
     * @return Absolute millis() timestamp at which this layer next needs to be
     *         rendered. Determined by the fastest currently active effect:
     *         heartbeat and short-circuit → millis()+16, radar → next step
     *         timestamp, shooting star → next LED position change. Returns
     *         UINT32_MAX when no effect is active.
     */
    uint32_t render(uint8_t ringIndex, Color::RGB *buffer);

    /**
     * @brief Checks whether any effect interval has elapsed and triggers accordingly.
     * @details Compares the current time (in total minutes since midnight) against
     * the configured intervals from Settings. Each effect fires at most once per
     * minute. Must be called regularly from the main loop with the current time.
     * @param currentHour   Current hour (0–23).
     * @param currentMinute Current minute (0–59).
     */
    void checkIntervals(uint8_t currentHour, uint8_t currentMinute);

    /**
     * @brief Manually triggers the short-circuit effect.
     * Starts 1 s of irregular cold-white flashing. Safe to call at any time.
     * Calls Renderer::scheduleFrame() so the animation begins on the very next
     * loop iteration rather than waiting for the next scheduled render.
     */
    void sfxShortCircuitTrigger();

    /**
     * @brief Manually triggers the radar scan effect.
     * Starts a green phosphor sweep for SFX_RADAR_ROUNDS revolutions.
     * Calls Renderer::scheduleFrame() so the animation begins on the very next
     * loop iteration rather than waiting for the next scheduled render.
     */
    void sfxRadarTrigger();

    /**
     * @brief Manually triggers the shooting star effect.
     * Starts a brief bright streak travelling around ring 1.
     * Calls Renderer::scheduleFrame() so the animation begins on the very next
     * loop iteration rather than waiting for the next scheduled render.
     */
    void sfxShootingStarTrigger();

    /**
     * @brief Manually triggers the heartbeat effect.
     * Starts a 5-second lubb-dupp brightness modulation.
     * Calls Renderer::scheduleFrame() so the animation begins on the very next
     * loop iteration rather than waiting for the next scheduled render.
     */
    void sfxHeartbeatTrigger();

} // namespace SFXLayer
