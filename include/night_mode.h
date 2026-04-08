#pragma once

/**
 * @file night_mode.h
 * @brief Night mode state machine.
 *
 * Evaluates the configured time window every loop iteration and tracks
 * the active/inactive state. On state transitions it coordinates the
 * per-feature side effects (motor parking, etc.).
 * All rendering layers query isActive() to adapt their output.
 */

namespace NightMode
{
    /**
     * @brief Must be called on every Arduino loop iteration (STA mode only).
     * Evaluates the night window and triggers side effects on transitions.
     */
    void update();

    /**
     * @brief Returns true while the clock is inside the night window.
     */
    bool isActive();

} // namespace NightMode
