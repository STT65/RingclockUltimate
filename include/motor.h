/**
 * @file motor.h
 * @brief Public interface for the stepper motor controller.
 *
 * @details This module controls a stepper motor that mechanically displays the current
 * time. It must be used together with TimeState and Settings.
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call update() on every Arduino loop iteration.
 * - Call resync() whenever the NTP time source or any motor setting changes.
 */

#pragma once
#include <Arduino.h>
#include "config.h"

namespace Motor
{

#if MOTOR_AH_EN
    /**
     * @brief State of the motor homing / calibration sequence.
     *
     * @details Reported by getHomingState(). Transitions:
     *   Idle → Running (when homing starts) → Done or Error (when complete).
     * The state persists until the next homing run.
     */
    enum class HomingState
    {
        Idle,    // No homing has run since boot, or motor is off.
        Running, // Homing sequence is actively executing.
        Done,    // Last homing completed successfully; zero position is valid.
        Error    // Last homing timed out; zero position falls back to assumed 0.
    };

    /**
     * @brief Starts the hardware timer with the given direction, interval and optional target.
     * @param dir           Step direction: +1 = forward, -1 = backward.
     * @param intervalUs    Step interval [µs].
     * @param positionTarget Stop position (default MOTOR_STEPS_PER_REV = run indefinitely).
     */
    void timerStart(int8_t dir, uint32_t intervalUs,
                    uint16_t positionTarget = MOTOR_STEPS_PER_REV);

    /**
     * @brief Starts the hardware timer, choosing the shortest-path direction automatically.
     *
     * @details Calculates the signed circular error from the current position to
     * @p positionTarget and selects forward (+1) or backward (−1) accordingly,
     * then delegates to timerStart(int8_t, uint32_t, uint16_t).
     *
     * @param intervalUs     Step interval [µs].
     * @param positionTarget Stop position (0 .. MOTOR_STEPS_PER_REV-1).
     */
    void timerStartAutoDir(uint32_t intervalUs, uint16_t positionTarget);

    /**
     * @brief Updates the hardware timer with the given direction, interval and optional target.
     * @param dir           Step direction: +1 = forward, -1 = backward.
     * @param intervalUs    Step interval [µs].
     * @param positionTarget Stop position (default MOTOR_STEPS_PER_REV = run indefinitely).
     */
    void timerUpdate(int8_t dir, uint32_t intervalUs,
                     uint16_t positionTarget = MOTOR_STEPS_PER_REV);

    /**
     * @brief Updates the hardware timer, choosing the shortest-path direction automatically.
     * @param intervalUs     Step interval [µs].
     * @param positionTarget Target position (0 .. MOTOR_STEPS_PER_REV-1).
     */
    void timerUpdateAutoDir(uint32_t intervalUs, uint16_t positionTarget);

    /**
     * @brief Returns the position recorded at the last Hall sensor edge and acknowledges it.
     * @return isrPositionSensor value, or MOTOR_STEPS_PER_REV if no new event.
     */
    uint16_t timerPositionSensor();

    /**
     * @brief Returns the current position.
     * @return position
     */
    uint16_t timerPosition();

    /**
     * @brief Returns true if the ISR completed a step since the last call, then clears the flag.
     * @return true on the first call after a step, false otherwise.
     */
    bool timerStepDone();

    /**
     * @brief Returns true when the ISR has stopped because it reached isrPositionTarget.
     * @return true = motor stopped at target, false = still running.
     */
    bool timerDone();

    /** @brief Stops the hardware timer immediately. */
    void timerStop();

    /**
     * @brief Set isrCurrentPos to zero
     */
    void timerZero();

#endif // MOTOR_AH_EN

    /**
     * @brief Statistics of the last completed grid-mode ramp.
     *
     * @details Populated by the motor controller after each ramp and accessible via
     * getRampStats(). Intended for the web interface to warn the user when
     * the configured speed or acceleration is too low for the selected grid
     * interval (i.e. when durationMs approaches the grid period).
     */
    struct RampStats
    {
        uint32_t durationMs; // Total duration of the last ramp [ms].
        bool valid;          // true once at least one ramp has completed since boot.
    };

    /**
     * @brief Initialises the motor controller.
     *
     * @details Configures the GPIO pins (EN, STEP, DIR), attaches the hardware timer
     * ISR and performs the initial synchronisation to the current time.
     * Must be called once during application setup, after Settings::load()
     * and TimeState::init() have completed.
     */
    void init();

    /**
     * @brief Resynchronises the motor position to the current time.
     *
     * @details Stops any ongoing movement, recalculates the target position from the
     * current NTP time and restarts the motor. If the positional error exceeds
     * a small threshold, a seek is performed at SEEK_SPEED before resuming
     * normal operation.
     *
     * Must be called after:
     * - An NTP time update.
     * - Any change to motorMode, motorGrid, motorSpeed or motorAccel.
     */
    void resync();

    /**
     * @brief Main motor update — must be called on every Arduino loop iteration.
     *
     * @details Drives the motor state machine. Depending on the current state it will:
     * - Disable the motor when motorMode == 0.
     * - Start a new ramp when a grid position change is detected (grid mode).
     * - Handle seek completion and switch to the configured operating mode.
     * - Run the PLL correction ~60 times per revolution (analog mode).
     * - Advance the acceleration ramp after each ISR step (grid mode).
     */
    void update();

    /**
     * @brief Returns the statistics of the last completed grid-mode ramp.
     *
     * @details The @c valid flag is false until the first ramp has completed after boot.
     * Use @c durationMs to detect whether the motor can keep up with the
     * configured grid interval — if durationMs is close to or exceeds the
     * grid period, Speed or Acceleration should be increased.
     *
     * @return Const reference to the last RampStats.
     */
    const RampStats &getRampStats();

} // namespace Motor
