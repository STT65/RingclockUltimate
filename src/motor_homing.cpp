/**
 * @file motor_homing.cpp
 * @brief Hall sensor homing for the Ringclock stepper motor.
 *
 * @details Implements an event-driven multi-pass homing sequence.
 *
 * The state machine is driven by two ISR-written flags that are polled in
 * update() on every loop iteration:
 *
 * - **mhPositionSensor** (< MOTOR_STEPS_PER_REV): the ISR recorded a Hall
 *   sensor edge. Reactor 1 reads the current sensor level to distinguish zone
 *   entry from zone exit and processes accordingly.
 *
 * - **isrEnabled** (== false): the ISR stopped because it reached
 *   mhPositionTarget. This signals that the overshoot distance has been
 *   travelled. Reactor 2 either reverses the motor for the next pass or
 *   delegates to Reactor 3 (done condition).
 *
 * **Pass sequence:**
 *   1. Motor approaches sensor zone.
 *   2. Zone entered  → record posEnter, switch to homing speed.
 *   3. Zone exited   → record midpoint, set mhPositionTarget to overshoot
 *                      position (MOTOR_AH_OVERSHOOT steps past exit).
 *   4. ISR stops     → Reactor 2: if more passes needed, reverse + restart ISR;
 *                      otherwise Reactor 3: compute final average → Done.
 *
 * After MOTOR_AH_PASSES complete enter/exit cycles the circular
 * average of the midpoints is stored in zeroPosition.
 * getZeroPosition() returns this value; Motor::update() applies
 * Settings::autoHomingFinePitch on top to produce the final 12 o'clock position.
 *
 * On timeout getZeroPosition() returns 0 (graceful degradation).
 */

#include "motor_homing.h"
#include "motor.h"
#include "circular_math.h"
#include "config.h"
#include "logging.h"
#include "settings.h"

#include <Arduino.h>

#if MOTOR_AH_EN

namespace MotorHoming
{

    static const __FlashStringHelper *stateStr(State s)
    {
        switch (s)
        {
        case State::Idle:
            return F("Idle");
        case State::Travel:
            return F("Travel");
        case State::Measure:
            return F("Measure");
        case State::Done:
            return F("Done");
        case State::Error:
            return F("Error");
        default:
            return F("?");
        }
    }

    // Motor variables
    static int8_t mhDir = 1;                                           //  Step direction: +1 = forward, -1 = backward.
    static const uint32_t msIntervalUs = 1000000UL / MOTOR_SEEK_SPEED; //  Seek step interval [µs]
    static const uint32_t mhIntervalUs = 1000000UL / MOTOR_AH_SPEED;   //  Homing step interval [µs]
    static uint16_t mhPositionTarget = 0;                              // Target position to step to (if < MOTOR_STEPS_PER_REV, else ignored).
    static bool mhHall = false;                                        //  Hall sensor activated by magnet
    // -------------------------------------------------------------------------
    //  State of MotorHoming
    // -------------------------------------------------------------------------
    static State currentState = State::Idle;
    static uint16_t posEnter = MOTOR_STEPS_PER_REV; // Position where the sensor zone was entered.
    static uint16_t posExit = MOTOR_STEPS_PER_REV;  // Position where the sensor zone was exited.
    static uint8_t passCount = 0;                   // Completed enter+exit cycles so far.
    static uint16_t centers[MOTOR_AH_PASSES];       // Midpoint per completed pass.
    static uint32_t wdtStart = 0;                   // millis() at sequence start.
    static uint16_t zeroPosition = 0;               // Final computed zero position.
    static int16_t finePitchAdjust = 0;             // Correction value for current fine pitch

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------
    /**
     * @brief Just go manHomingFinePitch
     */
    void manualHoming()
    { // We are at position 0
        if (Settings::manHomingFinePitch != 0)
            Motor::timerStartAutoDir(msIntervalUs, circularAdd(0,Settings::manHomingFinePitch));
        Motor::timerZero();
    }

    /**
     * @brief Moves the motor by @p steps steps for manual calibration.
     *
     * @details Available regardless of sensor presence. Stops any ongoing seek or
     * jog first, then starts the ISR for exactly @p steps steps at SEEK_SPEED.
     * Safe to call from the WebSocket callback.
     *
     * @param steps Signed step count: positive = forward, negative = backward.
     */
    void jog(int16_t steps)
    {
        LOG_DEBUG(LOG_HOM, String(F("HOM: jog(): motorMode=")) + Settings::motorMode + F(", steps=") + steps + F(", currentState=") + stateStr(currentState));
        if (Settings::motorMode != 4 || steps == 0 || currentState == State::Travel || currentState == State::Measure)
            return;
        LOG_DEBUG(LOG_HOM, String(F("HOM: Jog ")) + steps + F(" steps"));
        finePitchAdjust += steps; // Track manual pitch
        mhDir = (steps >= 0) ? 1 : -1;
        mhPositionTarget = circularAdd(0, finePitchAdjust); // we are at 12 o'clock postion
        Motor::timerStart(mhDir, msIntervalUs, mhPositionTarget);
    }

    void acceptPosition()
    {
        if (Settings::motorMode != 4 || currentState == State::Travel || currentState == State::Measure)
            return;
        // Precisely at 12 o'clock position, reset all indexes: isrCurrentPos & zeroPosition
        if (Settings::motorAutoHoming)
            Settings::autoHomingFinePitch += finePitchAdjust;
        else
            Settings::manHomingFinePitch += finePitchAdjust;
        Settings::saveMotor();
        Motor::timerZero();
        zeroPosition = 0;
        LOG_INFO(LOG_HOM, String(F("HOM: Fine pitch accepted and saved, autoOffset=")) + Settings::autoHomingFinePitch + F(", manOffset=") + Settings::manHomingFinePitch);
    }

    /**
     * @brief Returns the current state of the homing / calibration sequence.
     *
     * @return HomingState::Running while a sequence is active, otherwise the result
     *         of the last completed sequence (Done, Error, or Idle if none ran yet).
     */

    State getHomingState()
    {
        return currentState;
    }

    /**
     * @brief Manually triggers a new homing sequence while in Homing state.
     *
     * Various checks to safely start the autoHoming via start()
     */
    void startHoming()
    {
        LOG_DEBUG(LOG_HOM, String(F("HOM: startHoming(): motorMode=")) + Settings::motorMode + F(", currentState=") + stateStr(currentState) + F(", timerDone=") + Motor::timerDone());
        if (Settings::motorMode != 4 || currentState == State::Travel || currentState == State::Measure || !Motor::timerDone())
            return;
        LOG_INFO(LOG_HOM, F("HOM: Manually start of MotorHoming"));
        start();
    }

    void init()
    {
        pinMode(PIN_HALL, INPUT_PULLUP);
        LOG_INFO(LOG_HOM, F("HOM: MotorHoming initialized"));
    }

    void start()
    {
        mhDir = -1;
        // Initiate travel to sensor zone (the rest will be triggered by sensor toggling in update())
        // Case1: Magnet is already activating the sensor
        // Case2: Succesful homing already available -> we are at 0 position (not guarantied, that sensor is activated)
        // Case3: Unknown start position
        // In all cases we rotate CCW until we exit sensor zone
        LOG_INFO(LOG_HOM, String(F("HOM: Start autoHoming with ")) + MOTOR_AH_PASSES + F(" passes"));
        if (digitalRead(PIN_HALL) == LOW || currentState == State::Done)
        { // Magnet is already above the sensor OR succesful homing already available -> we are at 0 position
            // Rotate CCW at seek speed to exit sensor zone
            LOG_DEBUG(LOG_HOM, F("HOM: In zero position — rotating CCW directly to the overshoot target"));
            mhPositionTarget = circularAdd(0, -2 * MOTOR_AH_OVERSHOOT);
            Motor::timerStart(mhDir, msIntervalUs, mhPositionTarget);
            currentState = State::Measure;
        }
        else
        { // Unknown position — rotate CCW at seek speed
            LOG_DEBUG(LOG_HOM, F("HOM: Unknown sensor position — seeking CCW"));
            Motor::timerStart(mhDir, msIntervalUs);
            currentState = State::Travel;
        }
        // Update internal state

        posEnter = MOTOR_STEPS_PER_REV; // Mark invalid
        posExit = MOTOR_STEPS_PER_REV;  // Mark invalid
        passCount = 0;
        wdtStart = millis(); // Watchdog timer start time
    }

    bool update()
    {
        if (currentState != State::Measure && currentState != State::Travel)
            return false;

        uint16_t mhPositionSensor = Motor::timerPositionSensor();
        static bool mhHallOld = false;
        mhHall = digitalRead(PIN_HALL) == LOW;
        if (mhHall != mhHallOld)
        {
            mhHallOld = mhHall;
            LOG_DEBUG(LOG_HOM, String(F("HOM: Hall sensor ")) + (mhHall ? F("activated") : F("deactivated")));
        }

        // Initial travelling to sensor zone (only required for Unknown position)
        if (currentState == State::Travel)
        {
            if (mhPositionSensor < MOTOR_STEPS_PER_REV)
            { // Hall sensor toggeled
                LOG_INFO(LOG_HOM, String(F("HOM@")) + stateStr(currentState) + F(", Sensor toggled at ") + mhPositionSensor + F(", Hall sensor=") + (mhHall ? F("activated") : F("deactivated")));
                if ((mhDir == -1) && !mhHall)
                { // Turning CCW and exiting sensor zone
                    mhPositionTarget = circularAdd(mhPositionSensor, -MOTOR_AH_OVERSHOOT);
                    Motor::timerUpdate(mhDir, msIntervalUs, mhPositionTarget);
                    LOG_DEBUG(LOG_HOM, String(F("HOM@")) + stateStr(currentState) + (F(", Seeking to overshoot target due to: mhDir=")) + (mhDir > 0 ? F("CW") : F("CCW")) + F(", sensor=") + mhHall);
                    currentState = State::Measure;
                }
            }
            return true;
        }

        if (currentState != State::Measure)
            return true;

        // Watchdog timer
        if (millis() - wdtStart > MOTOR_AH_WDT_MS)
        { // Watchdog timer elapsed
            zeroPosition = 0;
            currentState = State::Error;
            Motor::timerStop();
            LOG_ERROR(LOG_HOM, F("HOM: Timeout — no Hall signal detected, falling back to zeroPosition=0"));
            return false;
        }

        // Reactor 1: hall sensor edge detected by ISR
        if (mhDir == 1 && (mhPositionSensor < MOTOR_STEPS_PER_REV)) // While rotating CW a position found where sensor toggeled
        {
            wdtStart = millis(); // sensor event restarts watchdog
            if (mhHall)          // Entered sensor zone?
            {
                posEnter = mhPositionSensor;
                LOG_DEBUG(LOG_HOM, String(F("HOM: Entering sensor zone @")) + posEnter +
                                       F(" pass ") + (passCount + 1) + F("/") + MOTOR_AH_PASSES);
            }
            else // Exited sensor zone
            {
                posExit = mhPositionSensor;
                // Calculate and store center positions
                if (posEnter < MOTOR_STEPS_PER_REV) // posEnter found?
                {
                    uint16_t center = circularMidpoint(posEnter, posExit, mhDir);
                    centers[passCount] = center;
                    passCount++;
                    LOG_DEBUG(LOG_HOM, String(F("HOM: Exiting sensor zone @")) + posExit +
                                           F(" center=") + center +
                                           F(" (") + passCount + F("/") + MOTOR_AH_PASSES + F(")"));

                    if (passCount >= MOTOR_AH_PASSES)
                    { // Measurements completed, calclulate zeroPosition and drive motor to it using autoHomingFinePitch
                        zeroPosition = circularAverage(centers, passCount);
                        mhPositionTarget = circularAdd(zeroPosition, Settings::autoHomingFinePitch);
                        mhDir = (circularError(mhPositionTarget, mhPositionSensor) >= 0) ? (int8_t)1 : (int8_t)-1;
                        LOG_INFO(LOG_HOM, String(F("HOM: Measurement completed, start motor to position=")) + mhPositionTarget);
                        Motor::timerUpdate(mhDir, msIntervalUs, mhPositionTarget);
                        return true;
                    }
                }
                else
                {
                    LOG_ERROR(LOG_HOM, F("HOM: posEnter not found"));
                }
                // Seek to the overshoot target before the sonsor zone
                mhPositionTarget = circularAdd(posEnter, -MOTOR_AH_OVERSHOOT);
                mhDir = -1;
                Motor::timerUpdate(mhDir, msIntervalUs, mhPositionTarget);
                posEnter = MOTOR_STEPS_PER_REV; // mark invalid
            }
        }

        // -----------------------------------------------------------------
        // Reactor 2: ISR stopped at the overshoot target before sonsor zone
        // -----------------------------------------------------------------
        if (Motor::timerDone()) // Motor stopped at mhPositionTarget
        {
            if (passCount >= MOTOR_AH_PASSES)
            { // Final move to 12 o'clock position done, reset all indexes: isrCurrentPos & zeroPosition
                currentState = State::Done;
                Motor::timerZero();
                zeroPosition = 0;
                LOG_INFO(LOG_HOM, String(F("HOM: autoHoming completed, motor is at 12 o'clock position, positions zeroed")));
            }
            else
            { // This is the stop at the overshoot target before sonsor zone
                mhDir = 1;
                Motor::timerStart(mhDir, mhIntervalUs);
                LOG_DEBUG(LOG_HOM, String(F("HOM: Overshoot position reached, rotating CW for measurement pass ")) + (passCount + 1));
            }
        }
        return true;
    }

    uint16_t getZeroPosition()
    {
        return zeroPosition;
    }

} // namespace MotorHoming

#endif // MOTOR_AH_EN
