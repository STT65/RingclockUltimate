/**
 * @file motor_homing.h
 * @brief Hall sensor homing for the Ringclock stepper motor.
 *
 * @details Determines the mechanical zero position of the stepper motor by
 * driving it across the Hall sensor zone multiple times and recording the
 * midpoint of each pass.
 *
 * The module is compiled in only when MOTOR_AH_EN is set to 1
 * in config.h.
 *
 * **Call order:**
 * - Call init() once during Motor::init() to configure the sensor pin.
 * - Call start() to begin a new homing sequence. It sets isrDir, isrIntervalUs,
 *   and isrPositionTarget directly; the caller must then call timerStart().
 * - Call update() on every loop iteration while homing is active. It reacts
 *   to two independent events:
 *   - Sensor event: isrPositionSensor < MOTOR_STEPS_PER_REV (set by ISR).
 *   - Motor-stopped event: isrEnabled == false (ISR reached isrPositionTarget).
 *   When update() sets outRestartTimer = true, the caller must call timerStart()
 *   to re-arm the ISR (isrDir and isrIntervalUs have already been updated).
 * - Once update() returns Done or Error, call getZeroPosition() to retrieve
 *   the measured zero offset in ISR position coordinates.
 */

#pragma once
#include <Arduino.h>
#include "config.h"

#if MOTOR_AH_EN

namespace MotorHoming
{
    /**
     * @brief Moves the motor by the given number of steps for manual calibration.
     *
     * @details Available regardless of sensor presence. Only active while
     * motorMode == 4 (Homing state). Uses SEEK_SPEED as step rate.
     *
     * @param steps Signed step count: positive = forward, negative = backward.
     */
    void jog(int16_t steps);

    void acceptPosition();

    /**
     * @brief Current state of the homing sequence.
     */
    enum class State
    {
        Idle,    // Homing has not been started after power-on.
        Travel,  // Initial traveling to the sensor zone.
        Measure, // Making the measurements at sensor zone.
        Done,    // Homing completed successfully; zero position is valid.
        Error    // Homing timed out; zero position falls back to 0.
    };

    /**
     * @brief Returns the current state of the homing sequence.
     * @return HomingState value.
     */
    State getHomingState();

    /**
     * @brief Just go manHomingFinePitch
     */
    void manualHoming();

    /**
     * @brief Manually triggers a new homing sequence.
     *
     * @details Safe to call at any time while motorMode == 4.
     */
    void startHoming();

    /**
     * @brief Configures the Hall sensor GPIO pin.
     *
     * @details Sets PIN_HALL as INPUT_PULLUP. Must be called once during
     * Motor::init(), before start() or update() are used.
     */
    void init();

    /**
     * @brief Begins a new homing sequence.
     *
     * @details Resets all internal state and sets the ISR variables for the
     * initial motor movement. The caller must call timerStart() immediately
     * after this function returns.
     *
     * Start strategy (checked in order):
     * -# Sensor active (LOW): magnet is above sensor — drive clockwise at homing
     *    speed to exit the zone before the first measurement pass.
     * -# Position calibrated (autoHomingFinePitch != 0): seek to the known sensor
     *    position via the shortest path at seek speed.
     * -# Position unknown: drive clockwise at seek speed until the sensor fires.
     */
    void start();

    /**
     * @brief Advances the homing state machine by one loop iteration.
     *
     * @details Contains three independent reactors:
     *
     * **Reactor 1** — sensor event (`isrPositionSensor < MOTOR_STEPS_PER_REV`):
     *   Reads the current Hall level to distinguish zone entry from zone exit.
     *   On entry: records posEnter, switches to homing speed.
     *   On exit: records midpoint, sets overshoot target so the ISR stops there.
     *   Acknowledges by writing isrPositionSensor = MOTOR_STEPS_PER_REV.
     *
     * **Reactor 2** — motor-stopped event (`isrEnabled == false` after overshoot):
     *   Reverses direction and sets outRestartTimer = true for the caller to
     *   restart the ISR.
     *
     * **Reactor 3** — done condition (passCount >= MOTOR_AH_PASSES):
     *   Triggered inside Reactor 2 when all passes are complete. Computes the
     *   circular average of recorded midpoints and transitions to Done.
     * @return Is Busy = motorHoming running
     */
    bool update();

    /**
     * @brief Returns the raw sensor-midpoint position in motor steps.
     *
     * @details Returns the circular average of all per-pass midpoints on
     * success (State::Done). Returns 0 on timeout (State::Error) so the motor
     * continues from an assumed position 0 without blocking the clock.
     *
     * Only meaningful after update() has returned Done or Error.
     *
     * @note Expressed in the ISR position coordinate system at the time of the
     * homing run. Motor::update() applies Settings::autoHomingFinePitch on top to
     * produce the final 12 o'clock coordinate.
     *
     * @return Sensor midpoint position (0 .. MOTOR_STEPS_PER_REV-1), or 0 on error.
     */
    uint16_t getZeroPosition();

} // namespace MotorHoming

#endif // MOTOR_AH_EN
