#pragma once

#include <stdint.h>

/**
 * @file night_mode.h
 * @brief Night mode state machine.
 *
 * Evaluates the configured time window every loop iteration and tracks
 * the active/inactive state. On state transitions it coordinates the
 * per-feature side effects (motor parking, etc.).
 *
 * Effective runtime values are exposed as public variables and updated
 * on every call to update(). Consuming modules use NightMode::xyz instead
 * of Settings::xyz — values equal Settings when inactive, clamped when active.
 */

namespace NightMode
{
    /**
     * @brief Must be called on every Arduino loop iteration (STA mode only).
     * Evaluates the night window, updates the active flag, and refreshes all
     * effective runtime values. Triggers Motor::resync() if motorMode changes.
     */
    void update();

    /**
     * @brief Returns true while the clock is inside the night window.
     */
    bool isActive();

    /**
     * @brief Effective runtime values — equal to Settings when night mode is
     * inactive, clamped to override values when active. Updated every loop
     * by update(). Consuming modules replace Settings::xyz with NightMode::xyz.
     */
    extern bool     autoBrightness;          ///< false when NIGHT_DIM_LEDS active
    extern int      manualBrightness;        ///< nightBrightness when NIGHT_DIM_LEDS active
    extern int      motorMode;               ///< 0 (Off) when NIGHT_MOTOR_HOME active
    extern uint16_t sfxShortCircuitInterval; ///< 0 (disabled) when NIGHT_SFX_OFF active
    extern uint16_t sfxRadarInterval;        ///< 0 (disabled) when NIGHT_SFX_OFF active
    extern uint16_t sfxShootingStarInterval; ///< 0 (disabled) when NIGHT_SFX_OFF active
    extern uint16_t sfxHeartbeatInterval;    ///< 0 (disabled) when NIGHT_SFX_OFF active
    extern uint8_t  secondHandRingMask;      ///< 0 (hidden) when NIGHT_SECOND_HAND_OFF active
    extern bool     hourMarksEnabled;        ///< false when NIGHT_MARKERS_OFF active
    extern bool     quarterMarksEnabled;     ///< false when NIGHT_MARKERS_OFF active

} // namespace NightMode
