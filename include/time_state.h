#pragma once
#include <Arduino.h>

/**
 * @file time_state.h
 * @brief Current time management and NTP synchronisation for the Ringclock Ultimate.
 *
 * This module provides the central time base used by all rendering layers
 * and the motor controller. LED index mapping is ring-size dependent and is
 * therefore performed in each layer using RING_LEDS[ringIndex].
 */

namespace TimeState
{

    /**
     * @brief Snapshot of the current system time.
     *
     * Updated on every call to update(). Contains the raw time components only.
     * LED indices are computed per ring by each layer using RING_LEDS[ringIndex].
     */
    struct TimeInfo
    {
        uint8_t  h;  // Hour   (0–23)
        uint8_t  m;  // Minute (0–59)
        uint8_t  s;  // Second (0–59)
        uint16_t ms; // Milliseconds within the current second (0–999)
    };

    /**
     * @brief Initialises the time module and starts NTP synchronisation.
     */
    void init();

    /**
     * @brief Updates the local time from NTP.
     *
     * Must be called regularly from the main loop.
     */
    void update();

    /**
     * @brief Returns the current time snapshot.
     *
     * @return Const reference to the internal TimeInfo struct.
     */
    const TimeInfo &get();

    /**
     * @brief Forces an immediate NTP synchronisation.
     */
    void forceSync();

    /**
     * @brief Checks whether the NTP time source was updated since the last call.
     *
     * Used by the motor controller to validate its position after a sync.
     *
     * @return true  if the time source was updated since the last call.
     * @return false otherwise.
     */
    bool timeSourceUpdated();

    // Calculates the motor position between 0 .. MOTOR_STEPS_PER_REV-1
    // In analog mode (motorGrid=0) every motor position is used, without gaps.
    // In grid mode  (motorGrid>0) the time-grid will be considered
    uint16_t calcMotorPosition();

    /**
     * @brief Returns the millis() timestamp of the next grid position change.
     *
     * @details Calculates the duration until the next grid boundary based on
     * motorMode and motorGrid, then adds it to millis(). The motor controller
     * uses this to avoid polling calcMotorPosition() on every loop iteration.
     *
     * @return millis() value at which the next grid change occurs.
     *         Returns 0 if motorGrid == 0 (analog mode — no grid).
     */
    uint32_t calcNextGridChangeMs();

} // namespace TimeState
