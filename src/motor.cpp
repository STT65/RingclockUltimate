/**
 * @file motor.cpp
 * @brief Stepper motor control for the Ringclock Ultimate.
 *
 * Controls a stepper motor that mechanically displays the current time.
 * The motor position is kept in sync with the NTP time source at all times.
 *
 * **Operating modes** (selected by `NightMode::motorMode`):
 * - `0` — Off: driver disabled, motor de-energised.
 * - `1/2/3` — Hours / Minutes / Seconds: motor tracks the selected time unit.
 *   Two sub-modes:
 *   - **Analog** (motorGrid=0): continuous rotation at a constant step interval,
 *     PLL-corrected ~60× per revolution.
 *   - **Grid** (motorGrid>0): discrete jumps to time-grid positions using a
 *     trapezoidal acceleration/deceleration ramp (Austin algorithm).
 * - `4` — Homing / Calibration: motor is energised but does not track time.
 *   Used for one-time zero-point calibration via the web interface.
 *   Exposes jog(), acceptPosition(), and startHoming().
 *
 * **Zero-point calibration (`MOTOR_AH_EN = 1`):**
 *
 * On every 0 → enabled transition the MotorHoming module performs a multi-pass
 * Hall sensor sequence to locate the sensor midpoint. `motor.cpp` then applies
 * `Settings::autoHomingFinePitch` to translate from sensor midpoint coordinates
 * into 12 o'clock coordinates:
 *
 *   `isrCurrentPos = (STEPS_PER_REV − autoHomingFinePitch) % STEPS_PER_REV`
 *
 * In **Homing state** (motorTimeUnit = 4) the motor subsequently seeks to
 * position 0 (12 o'clock) so the user can fine-tune the position with jog()
 * and call acceptPosition() to store the corrected offset permanently.
 *
 * The step pin is driven exclusively from a hardware timer ISR (timer1,
 * TIM_SINGLE) so that Wi-Fi stack activity and loop jitter have no influence
 * on step timing.
 *
 * @note The ESP8266 timer1 uses a 23-bit counter. With TIM_DIV256 @ 80 MHz the
 *       tick period is 3.2 µs, giving a maximum interval of ~26.8 s. This covers
 *       all supported configurations (MOTOR_STEPS_PER_REV 3200, all modes).
 */

#include "motor.h"
#include "config.h"
#include "circular_math.h"
#include "settings.h"
#include "time_state.h"
#include "logging.h"
#include "night_mode.h"
#include "wifi_setup.h"

#if MOTOR_AH_EN
#include "motor_homing.h"
#endif

#include <Arduino.h>

namespace Motor
{

/** @brief Timer tick conversion factor.
 *  timer1 TIM_DIV256 @ 80 MHz → 1 tick = 3.2 µs → ticks = µs * 10 / 32. */
#define TIMER_TICKS_PER_US 10 / 32

    // -------------------------------------------------------------------------
    //  Constants
    // -------------------------------------------------------------------------

    static const uint32_t isrSeekIntervalUs = 1000000UL / MOTOR_SEEK_SPEED;

    // -------------------------------------------------------------------------
    //  ISR state
    //  All variables shared between the ISR and the main loop must be volatile.
    // -------------------------------------------------------------------------

    static volatile int8_t isrDir = 1;                                //  Step direction: +1 = forward, -1 = backward.
    static volatile uint32_t isrIntervalUs = 0;                       //  Current step interval [µs], reloaded after every step.
    static volatile uint16_t isrCurrentPos = 0;                       //  Current motor position (0 .. MOTOR_STEPS_PER_REV-1).
    static volatile uint16_t isrPositionTarget = 0;                   // Target position to step to (if < MOTOR_STEPS_PER_REV, else ignored).
    static volatile uint16_t isrPositionSensor = MOTOR_STEPS_PER_REV; //  Position where hall-sensor changed its value 0->1 or 1->0. MOTOR_STEPS_PER_REV = no event.
    static volatile bool isrStepDone = false;                         //  Set by ISR after each step; cleared by update().
    static volatile bool isrDone = true;                              //  Set by ISR after timer stopped because isrPositionTarget reached; cleared by timerStart().
    static volatile bool isrHall = false;                             //  Hall sensor activated by magnet

    // -------------------------------------------------------------------------
    //  Loop state — accessed only from update() / resync()
    // -------------------------------------------------------------------------

    static bool seekRunning = false; //  true while the initial seek to the target position is running.
    static bool rampRunning = false; //  true while a grid-mode acceleration ramp is running.

    // -------------------------------------------------------------------------
    //  Analog mode state
    // -------------------------------------------------------------------------

    static uint32_t analogBaseUs = 0; //  Nominal step interval [µs] for one full revolution.

    // -------------------------------------------------------------------------
    //  Grid mode — acceleration ramp state
    // -------------------------------------------------------------------------

    static uint32_t rampMinUs = 0;      //  Minimum step interval [µs] = 1 / motorSpeed.
    static uint32_t rampStartMs = 0;    //  System time at ramp start [ms], used for duration measurement.
    static uint16_t rampTargetPos = 0;  //  Target position at ramp start, used to detect missed steps.
    static int32_t rampStepsTotal = 0;  //  Total steps planned for the current ramp.
    static int32_t rampStepsDone = 0;   //  Steps completed so far in the current ramp.
    static uint32_t rampCurrentUs = 0;  //  Current step interval within the ramp [µs].
    static int32_t rampAccelSteps = 0;  //  Steps needed to reach full speed (simulation result).
    static int32_t rampBrakeAt = 0;     //  rampStepsDone value at which braking starts.
    static int32_t rampBrakingLeft = 0; //  Remaining braking steps (counts down from rampAccelSteps).

    static uint16_t pllLastPos = 0;          //  Motor position at the last PLL correction.
    static uint32_t nextGridChangeMs = 0;    //  millis() timestamp of the next grid position change.
    static uint32_t lastSeekCorrectMs = 0;   //  millis() timestamp of the last seek target correction.

    // Jog (manual calibration in Homing state) — available regardless of sensor presence.
    static volatile bool jogRunning = false;       //  true while a manual jog movement is executing.
    static volatile int32_t jogStepsRemaining = 0; //  steps left to execute in the current jog.

    static int oldMotorMode = 0;

#if MOTOR_AH_EN
    static bool homingRunning = false; //  true while automatic homing sequence is active.
#endif

    // -------------------------------------------------------------------------
    //  Ramp monitor — statistics of the last completed ramp.
    //  Available via getRampStats(). If durationMs exceeds the grid interval,
    //  the configured speed/acceleration is too low for the selected grid.
    // -------------------------------------------------------------------------

    static RampStats lastRampStats = {0, false};

    // -------------------------------------------------------------------------
    //  Public helper
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the statistics of the last completed grid-mode ramp.
     *
     * Useful for the web interface to warn the user when speed or acceleration
     * are too low for the selected grid interval.
     *
     * @return Const reference to the last RampStats. The @c valid flag indicates
     *         whether at least one ramp has been completed since boot.
     */
    const RampStats &getRampStats()
    {
        return lastRampStats;
    }

    // -------------------------------------------------------------------------
    //  Internal helpers
    // -------------------------------------------------------------------------

    /**
     * @brief For analog mode it calculates the nominal step interval [µs] for a certain motorTimeUnit
     * to do one full revolution inbetween time unit (12h, 60m or 60s).
     *
     * @details The interval is derived from the configured motorTimeUnit and the MOTOR_STEPS_PER_REV:
     * • Hours:   12h per revolution
     * • Minutes: 60m per revolution
     * • Seconds: 60s per revolution
     *
     * @return Step interval in microseconds, or 0 if motorTimeUnit is invalid.
     */
    static uint32_t calcAnalogBaseUs()
    {
        switch (NightMode::motorMode)
        {
        case 1:
            return (uint32_t)(43200ULL * 1000000ULL / MOTOR_STEPS_PER_REV); // hours
        case 2:
            return 3600UL * 1000000UL / MOTOR_STEPS_PER_REV; // minutes
        case 3:
            return 60UL * 1000000UL / MOTOR_STEPS_PER_REV; // seconds
        default:
            return 0;
        }
    }

    // -------------------------------------------------------------------------
    //  Hardware timer control
    // -------------------------------------------------------------------------
    /**
     * @brief Starts the hardware timer with the given step interval and direction.
     *
     * @details Uses TIM_DIV256 (1 tick = 3.2 µs) and TIM_LOOP mode. Supports step intervals
     * up to ~26.8 s, which is required for the hours analog mode. The ISR calls
     * timer1_write() each step to update the reload value (ramp speed control) and
     * timer1_disable() when the target position is reached.
     *
     * @param intervalUs      Step interval in microseconds.
     * @param dir             Direction: +1 = forward, -1 = backward.
     * @param positionTarget  Target
     */
    void timerStart(int8_t dir, uint32_t intervalUs, uint16_t positionTarget)
    {
        if (positionTarget == isrCurrentPos)
        {
            isrDone = true; // already at target — nothing to do
            LOG_DEBUG(LOG_MOT, String(F("MOT: timerStart(), already at target — nothing to do")));
            return;
        }
        LOG_DEBUG(LOG_MOT, String(F("MOT: timerStart(), dir=")) + (dir > 0 ? F("CW") : F("CCW")) + F(", intervalUs=") + intervalUs +
                               (positionTarget < MOTOR_STEPS_PER_REV ? String(F(", target=")) + positionTarget : String()));
        isrDir = dir;
        isrIntervalUs = intervalUs;
        isrPositionTarget = positionTarget;
        isrDone = false;
        isrPositionSensor = MOTOR_STEPS_PER_REV;       // clear any stale sensor event
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_LOOP); // must be called before timer1_write()
        timer1_write(intervalUs * TIMER_TICKS_PER_US); // writing a new value starts the timer
    }

    /**
     * @brief Starts the hardware timer, choosing the shortest-path direction automatically.
     *
     * @details Calculates the signed circular error from the current position to
     * @p positionTarget and selects forward (+1) or backward (−1) accordingly,
     * then delegates to timerStart(int8_t, uint32_t, uint16_t).
     *
     * @param intervalUs     Step interval in microseconds.
     * @param positionTarget Target position (0 .. MOTOR_STEPS_PER_REV-1).
     */
    void timerStartAutoDir(uint32_t intervalUs, uint16_t positionTarget)
    {
        int16_t delta = circularError(positionTarget, isrCurrentPos);
        timerStart((delta >= 0) ? (int8_t)1 : (int8_t)-1, intervalUs, positionTarget);
    }

    /**
     * @brief Updates the hardware timer, choosing the shortest-path direction automatically.
     *
     * @details Calculates the signed circular error from the current position to
     * @p positionTarget and selects forward (+1) or backward (−1) accordingly,
     * then delegates to timerUpdate(int8_t, uint32_t, uint16_t).
     *
     * @param intervalUs     Step interval in microseconds.
     * @param positionTarget Target position (0 .. MOTOR_STEPS_PER_REV-1).
     */
    void timerUpdateAutoDir(uint32_t intervalUs, uint16_t positionTarget)
    {
        int16_t delta = circularError(positionTarget, isrCurrentPos);
        timerUpdate((delta >= 0) ? (int8_t)1 : (int8_t)-1, intervalUs, positionTarget);
    }

    /**
     * @brief Updates the hardware timer with the given step interval and direction.
     *
     * @param intervalUs Step interval in microseconds.
     * @param dir        Direction: +1 = forward, -1 = backward.
     * @param positionTarget  Target
     */
    void timerUpdate(int8_t dir, uint32_t intervalUs, uint16_t positionTarget)
    {
        if (positionTarget == isrCurrentPos)
        {
            timerStop(); // already at target — stop immediately
            LOG_DEBUG(LOG_MOT, String(F("MOT: timerStart(), already at target — stop immediately")));
            return;
        }

        LOG_DEBUG(LOG_MOT, String(F("MOT: timerUpdate(), dir=")) + (dir > 0 ? F("CW") : F("CCW")) + F(", intervalUs=") + intervalUs +
                               (positionTarget < MOTOR_STEPS_PER_REV ? String(F(", target=")) + positionTarget : String()));
        isrDir = dir;
        isrIntervalUs = intervalUs; // HW Timer will be updated synchronously in ISR
        isrPositionTarget = positionTarget;
    }

    uint16_t timerPositionSensor()
    {
        uint16_t positionSensor = isrPositionSensor;
        if (isrPositionSensor < MOTOR_STEPS_PER_REV) // Sensor position detected
            isrPositionSensor = MOTOR_STEPS_PER_REV; // Acknowledge
        return positionSensor;
    }

    uint16_t timerPosition()
    {
        return isrCurrentPos;
    }

    bool timerStepDone()
    {
        bool stepDone = isrStepDone;
        if (isrStepDone)         // Step done
            isrStepDone = false; // Acknowledge
        return stepDone;
    }

    bool timerDone()
    {
        return isrDone;
    }

    /**
     * @brief Stops the hardware timer and disables the ISR.
     */
    void timerStop()
    {
        isrDone = true;
        timer1_disable();
    }

    /**
     * @brief Set isrCurrentPos to zero
     */
    void timerZero()
    {
        isrCurrentPos = 0;
    }

    // -------------------------------------------------------------------------
    //  Hardware timer ISR
    // -------------------------------------------------------------------------

    /**
     * @brief Hardware timer ISR — fires independently of the Arduino loop.
     *
     * @details Executes one motor step on every invocation:
     * 1. Sets the direction pin via direct register access (no digitalWrite overhead).
     * 2. Generates a step pulse (optionally stretched to ~1 µs via MOTOR_STEP_PULSE_STRETCH).
     * 3. Updates isrCurrentPos circularly.
     * 4. Sets isrStepDone to notify update() to do some ISR support.
     * 5. Reloads the timer for the next step (TIM_SINGLE mode).
     * Global Variables:
     * @param isrDir Step direction -1= backward, 1=forward
     * @param isrIntervalUs Step interval [us]
     * @param isrCurrentPos Current position of motor
     * @param isrPositionTarget Target position to step to (if < MOTOR_STEPS_PER_REV, else ignored)
     * Global variables with 2-way handshake:
     * @param isrPositionSensor Position where hall-sensor changed its value 0->1 or 1->0
     * ISR sets to isrPositionSensor < MOTOR_STEPS_PER_REV on hall-sensor event
     * UPDATE() sets isrPositionSensor=MOTOR_STEPS_PER_REV, marking it's processed/inactive
     * @param isrStepDone Step done
     * ISR sets to isrStepDone to true, if step was done
     * UPDATE() sets isrStepDone to false, marking it's processed/inactive
     * @param isrDone ISR done
     * ISR sets to isrDone to true, if timer stopped because isrPositionTarget reached
     * @param isrHall Hall sensor activated by magnet
     * @note Placed in IRAM so it can execute while the flash cache is busy.
     */
    static void IRAM_ATTR motorISR()
    {
        // Record isrCurrentPos on Hall sensor edge. The position reflects the most
        // recently completed step, which has already settled mechanically.
        static bool prevHall = false;
        isrHall = (GPI & (1 << PIN_HALL)) == 0; // active LOW (INPUT_PULLUP)
        if (isrHall != prevHall)
        {
            prevHall = isrHall;
            isrPositionSensor = isrCurrentPos;
        }

        if (isrDir > 0)
            GPOC = (1 << PIN_MOTOR_DIR); // LOW  = forward direction
        else
            GPOS = (1 << PIN_MOTOR_DIR); // HIGH = backward direction

        GPOS = (1 << PIN_MOTOR_STEP); // Rising edge

// Extend the high pulse from 125 ns to ~1 µs to meet driver timing requirements.
#if MOTOR_STEP_PULSE_STRETCH
        for (uint8_t i = 0; i < 12; i++) // 0=125ns | 12=1µs | 64=5µs
            __asm__ __volatile__("nop");
#endif

        GPOC = (1 << PIN_MOTOR_STEP); // Falling edge

        // Update position circularly
        int16_t next = (int16_t)isrCurrentPos + isrDir;
        if (next >= (int16_t)MOTOR_STEPS_PER_REV)
            next = 0;
        if (next < 0)
            next = MOTOR_STEPS_PER_REV - 1;
        isrCurrentPos = (uint16_t)next;

        isrStepDone = true; // notify update()

        if (isrCurrentPos == isrPositionTarget) // Stop when target position reached
        {
            timer1_disable(); // TIM_LOOP does not stop automatically
            isrDone = true;
        }
        else
            timer1_write(isrIntervalUs * TIMER_TICKS_PER_US); // update reload value for next step
    }

    // -------------------------------------------------------------------------
    //  Grid mode — acceleration ramp (Austin algorithm)
    // -------------------------------------------------------------------------

    /**
     * @brief Starts an acceleration ramp towards a target position.
     *
     * @details Implements the Austin algorithm for trapezoidal speed profiles:
     * - Start interval: c0 = 1,000,000 * sqrt(2 / accel)  [µs]
     * - Subsequent:     cn = cn-1 * (1 - 2/(4n+1))        (acceleration phase)
     *                   cn = cn-1 * (1 + 2/(4n+1))        (deceleration phase)
     * - Minimum:        cMin = 1,000,000 / maxSpeed        [µs]
     *
     * @param delta Signed number of steps to move. Positive = forward, negative = backward.
     *              The function does nothing if delta == 0.
     */
    static void rampStart(int16_t delta)
    {
        if (delta == 0)
            return;

        rampStepsTotal = abs(delta);
        rampStepsDone = 0;
        rampMinUs = 1000000UL / (uint32_t)Settings::motorSpeed;
        rampStartMs = millis();
        rampTargetPos = (uint16_t)((isrCurrentPos + delta + MOTOR_STEPS_PER_REV) % MOTOR_STEPS_PER_REV);

        float c0 = 1000000.0f * sqrtf(2.0f / (float)Settings::motorAccel);
        rampCurrentUs = max((uint32_t)c0, rampMinUs);

        // Pre-simulate the Austin sequence to find how many steps are needed to
        // reach full speed (rampMinUs). This determines whether the ramp is
        // triangular (Case 1: full speed never reached) or trapezoidal (Case 2:
        // a plateau exists). In Case 2, braking is initiated early so it mirrors
        // the acceleration phase exactly.
        {
            int32_t maxHalf = rampStepsTotal / 2;
            uint32_t simC = rampCurrentUs;
            rampAccelSteps = 0;
            while (simC > rampMinUs && rampAccelSteps < maxHalf)
            {
                rampAccelSteps++;
                simC = (uint32_t)((float)simC * (1.0f - 2.0f / (4.0f * (float)rampAccelSteps + 1.0f)));
                if (simC < rampMinUs)
                    break; // reached full speed
            }
            if (rampAccelSteps == 0)
                rampAccelSteps = 1; // guard: at least one accel step
        }
        rampBrakeAt = rampStepsTotal - rampAccelSteps;
        rampBrakingLeft = 0;

        rampRunning = true;
        timerStart(delta > 0 ? (int8_t)1 : (int8_t)-1, rampCurrentUs, rampTargetPos);

        LOG_DEBUG(LOG_MOT, String(F("MOT: rampStart with RampSteps=")) + rampStepsTotal + F(", RampCurrentUs=") + rampCurrentUs + F("µs"));
    }

    /**
     * @brief Advances the ramp by one step and updates the step interval.
     *
     * @details Must be called from update() after each ISR step notification while
     * a ramp is active. Handles both the acceleration and deceleration phases,
     * and captures ramp statistics when the target is reached.
     */
    static void rampUpdate()
    {
        if (timerDone())
        {
            rampRunning = false;
            // Capture ramp statistics for the monitor / web interface warning
            lastRampStats.durationMs = millis() - rampStartMs;
            lastRampStats.valid = true;
            LOG_DEBUG(LOG_MOT, String(F("MOT: RampReady RampDuration=")) + lastRampStats.durationMs + F("ms"));
            return;
        }
        rampStepsDone++;
        if (rampBrakingLeft == 0 && rampStepsDone >= rampBrakeAt)
            rampBrakingLeft = rampAccelSteps; // Case 1 or 2: start braking phase

        if (rampBrakingLeft > 0)
        {
            // Deceleration: exact mirror of the acceleration sequence.
            // Multiplier = inverse of Austin accel step at the same distance from end.
            // (1 + 2/(4n-1)) is the exact inverse of (1 - 2/(4n+1)).
            rampCurrentUs = (uint32_t)((float)rampCurrentUs * (1.0f + 2.0f / (4.0f * (float)rampBrakingLeft - 1.0f)));
            rampBrakingLeft--;
        }
        else
        {
            // Acceleration phase: decrease interval (speed up)
            rampCurrentUs = (uint32_t)((float)rampCurrentUs * (1.0f - 2.0f / (4.0f * (float)rampStepsDone + 1.0f)));
            if (rampCurrentUs < rampMinUs)
                rampCurrentUs = rampMinUs;
        }
        isrIntervalUs = rampCurrentUs; // effective on the next timer1_write in the ISR
    }

    // -------------------------------------------------------------------------
    //  Analog mode — PLL
    // -------------------------------------------------------------------------

    /*!
     * @brief Adjusts the step interval to keep the motor in sync with the current time.
     *
     * @details Called approximately 60 times per revolution from update(). Compares the
     * actual motor position with the time-derived target position and applies a
     * proportional correction to isrIntervalUs:
     *   corrected = analogBaseUs - errorPos * 16
     *
     * The result is clamped to [analogBaseUs/2 .. analogBaseUs*2] to prevent
     * runaway in case of large transient errors.
     *
     * @note The ESP8266 timer1 23-bit limit imposes a maximum of ~26.8 s per step.
     *       An error is logged if the corrected interval would exceed this limit.
     */
    static void adjustPLL()
    {
        uint16_t targetPos = TimeState::calcMotorPosition();
        int16_t errorPos = circularError(targetPos, isrCurrentPos);

        // Proportional correction: ~16 µs per step of position error.
        // Clamped to [analogBaseUs/2 .. analogBaseUs*2] to prevent overflow.
        int32_t corrected = (int32_t)analogBaseUs - (int32_t)errorPos * 16;
        isrIntervalUs = (uint32_t)constrain(corrected, (int32_t)analogBaseUs / 2, (int32_t)analogBaseUs * 2);
        LOG_DEBUG(LOG_MOT, String(F("MOT: PLL motorPositionError=")) + errorPos + F("steps, isrIntervalUs=") + isrIntervalUs + F("us"));
        // timer1 is a 23-bit counter. TIM_DIV256 @ 80MHz → 1 tick = 3.2 µs → max interval = 26,843,542 µs
        if (isrIntervalUs > 26843542)
            LOG_ERROR(LOG_MOT, String(F("MOT: isrIntervalUs too high: ")) + isrIntervalUs + F("µs, limit is 26843542µs"));
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    /**
     * @brief Initialises the motor driver pins, attaches the ISR and performs
     *        the initial synchronisation.
     *
     * @details Must be called once during application setup after Settings and TimeState
     * have been initialised.
     */
    void init()
    {
        pinMode(PIN_MOTOR_STEP, OUTPUT);
        pinMode(PIN_MOTOR_DIR, OUTPUT);
        pinMode(PIN_MOTOR_EN, OUTPUT);

        if (WiFiSetup::isAPMode)
            return; // motor stays disabled (external pull-up on EN); ISR not attached

        digitalWrite(PIN_MOTOR_EN, LOW); // enable motor driver
        timer1_attachInterrupt(motorISR);
        LOG_INFO(LOG_MOT, F("MOT: Motor initialized"));
#if MOTOR_AH_EN
        MotorHoming::init(); // always init Hall sensor pin, regardless of autoHoming setting
        if (Settings::motorAutoHoming)
        {
            MotorHoming::start();
            homingRunning = true;
        }
        else
#endif
            MotorHoming::manualHoming();
        homingRunning = true;
    }

    /**
     * @brief Resynchronises the motor to NTP update or changed settings.
     *
     * @details Must be called whenever the environment changes: NTP update, settings change,
     * or mode switch. Stops any running movement, recalculates the target position
     * and starts a seek if the positional error exceeds 3 motor steps.
     */
    void resync()
    {
        LOG_DEBUG(LOG_MOT, String(F("MOT: resync() called")));
#if MOTOR_AH_EN
        if (homingRunning)
            return; // do not interrupt an active homing sequence
#endif
        timerStop();
        rampRunning = false;
        seekRunning = false;
        LOG_DEBUG(LOG_MOT, String(F("MOT: resync()-Timer stoped. isrDone=")) + isrDone);
        digitalWrite(PIN_MOTOR_EN, LOW); // enable motor driver
        if (NightMode::motorMode == 0 || NightMode::motorMode == 4)
        {
            if (oldMotorMode != NightMode::motorMode && isrCurrentPos != 0)
            { // No time display -> park motor at 12 o'clock
                LOG_DEBUG(LOG_MOT, String(F("MOT: motorMode changed to ")) + NightMode::motorMode + F(". Seeking to 12 o'clock"));
                seekRunning = true;
                timerStartAutoDir(isrSeekIntervalUs, 0);
            }
        }
        else
        { // Time display active -> seek to (new) target position
            LOG_DEBUG(LOG_MOT, String(F("MOT: resync() Motor1-3=")) + NightMode::motorMode);
            analogBaseUs = calcAnalogBaseUs();
            seekRunning = true;
            timerStartAutoDir(isrSeekIntervalUs, TimeState::calcMotorPosition());
        }
        oldMotorMode = NightMode::motorMode;
        nextGridChangeMs = 0;    // force immediate grid check on next update()
        lastSeekCorrectMs = 0;   // force immediate seek correction on next update()
    }

    /**
     * @brief Main motor update function — must be called on every Arduino loop iteration.
     *
     * @details Handles three operating phases in sequence:
     * 1. **Disabled** (motorTimeUnit == 0): stops the timer and disables the driver.
     * 2. **Grid mode idle check**: starts a new ramp when the grid position has
     *    changed and no movement is currently active.
     * 3. **ISR step processing**: when the ISR signals a completed step, handles
     *    seek completion, PLL correction (analog mode), or ramp advancement (grid mode).
     */
    void update()
    {
        static bool homingRunningOld1;
        static bool seekRunningOld;
        static bool rampRunningOld;
        if (homingRunning != homingRunningOld1 || seekRunning != seekRunningOld || rampRunning != rampRunningOld)
        {
            homingRunningOld1 = homingRunning;
            seekRunningOld = seekRunning;
            rampRunningOld = rampRunning;
            LOG_DEBUG(LOG_MOT, String(F("MOT: update(): homingRunning=")) + homingRunning + F(", seekRunning=") + seekRunning + F(", rampRunning=") + rampRunning + F(", motorMode=") + NightMode::motorMode + F(", isrCurrentPos=") + isrCurrentPos + F(", isrIntervalUs=") + isrIntervalUs + F(", isrDone=") + isrDone);
        }

        if (homingRunning || NightMode::motorMode == 4)
        {
            bool wasRunning = homingRunning;
#if MOTOR_AH_EN
            if (Settings::motorAutoHoming)
                homingRunning = MotorHoming::update();
            else
#endif
            {
                if (homingRunning && isrDone)
                { // Manual homing move just completed — declare physical position as 12 o'clock
                    timerZero();
                    homingRunning = false;
                }
            }
            // On homing completion: resync so any settings changes during homing take effect
            if (wasRunning && !homingRunning && NightMode::motorMode != 4)
                resync();
            return;
        }

        if (seekRunning)
        {
            if (NightMode::motorMode >= 1 && NightMode::motorMode <= 3)
            {
                bool nearTarget  = circularDistance(isrPositionTarget, isrCurrentPos) <= 2;
                bool periodicDue = (millis() - lastSeekCorrectMs) >= 500;
                if (nearTarget || periodicDue)
                {
                    timerUpdateAutoDir(isrSeekIntervalUs, TimeState::calcMotorPosition());
                    lastSeekCorrectMs = millis();
                }
            }
            if (isrDone)
                seekRunning = false;
            return;
        }

        if (NightMode::motorMode == 0)
        {
            timerStop();
            digitalWrite(PIN_MOTOR_EN, HIGH); // disable motor driver
            return;
        }

        if (Settings::motorGrid == 0)
        { // Analog mode
            if (isrDone)
                timerStart(1, analogBaseUs); // (re)start after seek or initial entry
            // ISR runs autonomously; PLL corrects isrIntervalUs 60x per revolution
            else if (circularDistance(isrCurrentPos, pllLastPos) >= MOTOR_STEPS_PER_REV / 60)
            {
                pllLastPos = isrCurrentPos;
                adjustPLL();
            }
            return;
        }

        // Grid mode: one rampUpdate() per ISR step; start new ramp when idle
        if (isrStepDone)
        {
            isrStepDone = false;
            if (rampRunning)
                rampUpdate();
        }
        if (!rampRunning && (int32_t)(millis() - nextGridChangeMs) >= 0)
        {
            int16_t delta = circularError(TimeState::calcMotorPosition(), isrCurrentPos);
            LOG_DEBUG(LOG_MOT, String(F("MOT: calcMotorPosition with isrCurrentPos=")) + isrCurrentPos + F(", delta=") + delta);
            if (abs(delta) > 3)
                rampStart(delta);
            nextGridChangeMs = TimeState::calcNextGridChangeMs() + 20;
        }
    }

} // namespace Motor
