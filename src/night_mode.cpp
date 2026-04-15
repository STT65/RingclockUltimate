/**
 * @file night_mode.cpp
 * @brief Night mode state machine implementation.
 *
 * Evaluates the configured time window on every call to update() and
 * manages the active/inactive state. On every loop iteration the effective
 * runtime variables are refreshed:
 *
 * - nightSfxOff        → sfx*Interval variables set to 0
 * - nightSecondHandOff → secondHandRingMask set to 0
 * - nightMarkersOff    → hourMarksEnabled / quarterMarksEnabled set to false
 * - nightMotorHome     → motorMode set to 0 (Off); Motor::resync() triggered on change
 *
 * nightDimLeds is handled separately in brightness.cpp via isActive().
 */

#include "night_mode.h"
#include "settings.h"
#include "time_state.h"
#include "motor.h"
#include "logging.h"

namespace NightMode
{
    // -------------------------------------------------------------------------
    //  State
    // -------------------------------------------------------------------------

    static bool active = false; ///< Current night-mode state.

    // -------------------------------------------------------------------------
    //  Public effective runtime variables (defined here, declared extern in .h)
    // -------------------------------------------------------------------------

    bool     autoBrightness          = false;
    int      manualBrightness        = 0;
    int      motorMode               = 0;
    uint16_t sfxShortCircuitInterval = 0;
    uint16_t sfxRadarInterval        = 0;
    uint16_t sfxShootingStarInterval = 0;
    uint16_t sfxHeartbeatInterval    = 0;
    uint8_t  secondHandRingMask      = 0;
    bool     hourMarksEnabled        = false;
    bool     quarterMarksEnabled     = false;

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
    //  Internal: recompute all effective runtime variables from current state
    // -------------------------------------------------------------------------

    static void refreshShadows()
    {
        bool dimLeds   = active && (Settings::nightFeatures & Settings::NIGHT_DIM_LEDS);
        bool sfxOff    = active && (Settings::nightFeatures & Settings::NIGHT_SFX_OFF);
        bool sHandOff  = active && (Settings::nightFeatures & Settings::NIGHT_SECOND_HAND_OFF);
        bool motorHome = active && (Settings::nightFeatures & Settings::NIGHT_MOTOR_HOME);
        bool markOff   = active && (Settings::nightFeatures & Settings::NIGHT_MARKERS_OFF);

        autoBrightness          = dimLeds   ? false                      : Settings::autoBrightness;
        manualBrightness        = dimLeds   ? Settings::nightBrightness  : Settings::manualBrightness;
        motorMode               = motorHome ? 0                          : Settings::motorMode;
        sfxShortCircuitInterval = sfxOff    ? 0 : Settings::sfxShortCircuitInterval;
        sfxRadarInterval        = sfxOff    ? 0 : Settings::sfxRadarInterval;
        sfxShootingStarInterval = sfxOff    ? 0 : Settings::sfxShootingStarInterval;
        sfxHeartbeatInterval    = sfxOff    ? 0 : Settings::sfxHeartbeatInterval;
        secondHandRingMask      = sHandOff  ? 0 : Settings::secondHand.ringMask;
        hourMarksEnabled        = markOff   ? false : Settings::hourMarksEnabled;
        quarterMarksEnabled     = markOff   ? false : Settings::quarterMarksEnabled;
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    bool isActive()
    {
        return active;
    }

    void update()
    {
        bool inWindow = checkWindow();

        // Hysteresis: require 3 consecutive identical readings before switching
        // state. This prevents spurious transitions caused by NTP corrections
        // that briefly shift the time across a minute boundary.
        static uint8_t confirmCount = 0;
        if (inWindow == active) {
            confirmCount = 0;
        } else if (++confirmCount >= 3) {
            confirmCount = 0;
            active = inWindow;
            LOG_INFO(LOG_NIGHT, active ? F("NMD: Night mode started.") : F("NMD: Night mode ended."));
        }

        // Recompute effective values every cycle and trigger motor resync if
        // the effective motor mode changed (covers both night transitions and
        // Settings changes while night mode is active).
        int prevMotorMode = motorMode;
        refreshShadows();
        if (motorMode != prevMotorMode)
            Motor::resync();
    }

} // namespace NightMode
