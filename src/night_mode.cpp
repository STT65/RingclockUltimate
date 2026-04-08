/**
 * @file night_mode.cpp
 * @brief Night mode state machine implementation.
 *
 * Evaluates the configured time window on every call to update() and
 * manages the transition between day and night states. Each enabled
 * feature flag triggers the corresponding side effect:
 *
 * - nightDimLeds       → brightness.cpp reads NightMode::isActive()
 * - nightSfxOff        → layer_sfx.cpp reads NightMode::isActive()
 * - nightSecondHandOff → layer_time.cpp reads NightMode::isActive()
 * - nightMarkersOff    → layer_ambient.cpp reads NightMode::isActive()
 * - nightMotorHome     → motor is parked (motorMode=0) on entry,
 *                        restored on exit
 */

#include "night_mode.h"
#include "settings.h"
#include "time_state.h"
#include "motor.h"
#include "logging.h"

namespace NightMode
{
    static bool active         = false; // Current night-mode state.
    static int  savedMotorMode = -1;    // Motor mode saved before parking (-1 = not saved).

    static bool     sfxSaved                = false;
    static uint16_t savedSfxRadarInterval;
    static uint16_t savedSfxShortCircuitInterval;
    static uint16_t savedSfxShootingStarInterval;
    static uint16_t savedSfxHeartbeatInterval;

    static uint8_t savedSecondHandRingMask  = 0xFF; // 0xFF = not saved

    static bool markersSaved       = false;
    static bool savedHourMarks;
    static bool savedQuarterMarks;

    bool isActive()
    {
        return active;
    }

    // -------------------------------------------------------------------------
    //  Internal: evaluate time window
    // -------------------------------------------------------------------------

    static bool checkWindow()
    {
        if (!Settings::nightModeEnabled)
            return false;

        const auto &t      = TimeState::get();
        int         cur    = t.h * 60 + t.m;
        int         start  = Settings::nightStart;
        int         end    = Settings::nightEnd;

        if (start < end)
            // Window within a single calendar day (e.g. 01:00–05:00)
            return cur >= start && cur < end;
        else
            // Window spans midnight (e.g. 22:00–06:00)
            return cur >= start || cur < end;
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void update()
    {
        bool inWindow = checkWindow();

        // Hysteresis: require 3 consecutive identical readings before switching
        // state. This prevents spurious transitions caused by NTP corrections
        // that briefly shift the time across a minute boundary.
        static uint8_t confirmCount = 0;
        if (inWindow == active)
        {
            confirmCount = 0; // already in expected state — reset counter
            return;
        }
        if (++confirmCount < 3)
            return; // not yet confirmed
        confirmCount = 0;
        active = inWindow;

        if (active)
        {
            // ----------------------------------------------------------------
            // Day → Night transition
            // ----------------------------------------------------------------
            LOG_INFO(LOG_NIGHT, F("NMD: Night mode started."));

            if ((Settings::nightFeatures & Settings::NIGHT_MOTOR_HOME) && Settings::motorMode != 0)
            {
                savedMotorMode      = Settings::motorMode;
                Settings::motorMode = 0;
                Motor::resync();
            }
            if ((Settings::nightFeatures & Settings::NIGHT_SFX_OFF) && !sfxSaved)
            {
                savedSfxRadarInterval        = Settings::sfxRadarInterval;
                savedSfxShortCircuitInterval = Settings::sfxShortCircuitInterval;
                savedSfxShootingStarInterval = Settings::sfxShootingStarInterval;
                savedSfxHeartbeatInterval    = Settings::sfxHeartbeatInterval;
                sfxSaved                     = true;
                Settings::sfxRadarInterval        = 0;
                Settings::sfxShortCircuitInterval = 0;
                Settings::sfxShootingStarInterval = 0;
                Settings::sfxHeartbeatInterval    = 0;
            }
            if ((Settings::nightFeatures & Settings::NIGHT_SECOND_HAND_OFF) && savedSecondHandRingMask == 0xFF)
            {
                savedSecondHandRingMask        = Settings::secondHand.ringMask;
                Settings::secondHand.ringMask  = 0;
            }
            if ((Settings::nightFeatures & Settings::NIGHT_MARKERS_OFF) && !markersSaved)
            {
                savedHourMarks              = Settings::hourMarksEnabled;
                savedQuarterMarks           = Settings::quarterMarksEnabled;
                markersSaved                = true;
                Settings::hourMarksEnabled    = false;
                Settings::quarterMarksEnabled = false;
            }
        }
        else
        {
            // ----------------------------------------------------------------
            // Night → Day transition
            // ----------------------------------------------------------------
            LOG_INFO(LOG_NIGHT, F("NMD: Night mode ended."));

            if ((Settings::nightFeatures & Settings::NIGHT_MOTOR_HOME) && savedMotorMode >= 0)
            {
                Settings::motorMode = savedMotorMode;
                savedMotorMode      = -1;
                Motor::resync();
            }
            if (sfxSaved)
            {
                Settings::sfxRadarInterval        = savedSfxRadarInterval;
                Settings::sfxShortCircuitInterval = savedSfxShortCircuitInterval;
                Settings::sfxShootingStarInterval = savedSfxShootingStarInterval;
                Settings::sfxHeartbeatInterval    = savedSfxHeartbeatInterval;
                sfxSaved                          = false;
            }
            if (savedSecondHandRingMask != 0xFF)
            {
                Settings::secondHand.ringMask  = savedSecondHandRingMask;
                savedSecondHandRingMask         = 0xFF;
            }
            if (markersSaved)
            {
                Settings::hourMarksEnabled    = savedHourMarks;
                Settings::quarterMarksEnabled = savedQuarterMarks;
                markersSaved                  = false;
            }
        }
    }

} // namespace NightMode
