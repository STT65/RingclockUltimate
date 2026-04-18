#include "config.h"
#include "settings.h"
#include "time_state.h"
#include "logging.h"
#include "night_mode.h"
#include <NTPClient.h>
#include <WiFiUdp.h>

namespace TimeState
{

    //  Internal state

    static WiFiUDP ntpUDP;
    static NTPClient ntp(ntpUDP, "pool.ntp.org", 0, NTP_QUERY * 3600000);

    static TimeInfo current;
    static bool     ntpUpdated  = false;
    static int32_t  tzOffsetSec = 0; // cached local-UTC offset in seconds, updated every frame

    //  Initialisation

    void init()
    {
        LOG_INFO(LOG_TIME, F("TST: Initialising time module..."));
        ntp.begin();
        ntp.forceUpdate();
        ntpUpdated = true;
    }

    static uint32_t getLocalTimeMs(); // forward declaration

    //  Time update

    void update()
    {
        if (ntp.update())
        {
            ntpUpdated = true;
        }

        // Apply POSIX timezone (handles DST automatically)
        setenv("TZ", Settings::timezone.c_str(), 1);
        tzset();

        // Compute TZ offset via localtime() — needed once per frame to handle DST correctly
        time_t utc = (time_t)ntp.getEpochTime();
        struct tm *lt = localtime(&utc);
        int32_t localSec = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
        int32_t utcSec   = (int32_t)(utc % 86400L);
        tzOffsetSec = localSec - utcSec;
        if (tzOffsetSec >  43200) tzOffsetSec -= 86400;
        if (tzOffsetSec < -43200) tzOffsetSec += 86400;

        // All time values from single local 24 h ms source — no race condition
        uint32_t localMs = getLocalTimeMs();
        current.h  = localMs / 3600000UL;
        current.m  = (localMs / 60000UL) % 60;
        current.s  = (localMs / 1000UL)  % 60;
        current.ms = localMs % 1000;
    }

    //  Time struct accessor

    const TimeInfo &get()
    {
        return current;
    }

    //  Manual NTP synchronisation

    void forceSync()
    {
        ntp.forceUpdate();
        ntpUpdated = true;
        LOG_INFO(LOG_TIME, F("NTP sync forced."));
    }

    //  Check whether the time source was updated

    bool timeSourceUpdated()
    {
        if (ntpUpdated)
        {
            ntpUpdated = false;
            return true;
        }
        return false;
    }

    /**
     * @brief Returns the target motor position for the current time.
     *
     * Calculates the motor position in the range 0 .. MOTOR_STEPS_PER_REV-1
     * based on the current NTP time and the configured motor settings.
     *
     * @note Two movement modes are supported:
     *       - **Analog mode** (motorGrid=0): The time is mapped continuously,
     *         making use of every motor position without gaps.
     *       - **Grid mode** (motorGrid>0): The *displayed time* is snapped to a
     *         coarser time grid (e.g. full seconds, 5-second steps) before the
     *         motor position is calculated. This is a time-domain quantization,
     *         not a quantization of the motor steps themselves.
     *
     * @return uint16_t  Target motor position (0 .. MOTOR_STEPS_PER_REV-1).
     *                   Returns 0 if motorMode is invalid.
     */
    /** @brief Returns local time as milliseconds within the current 12h period, with ms precision.
     *  Single getTimeMs() call ensures second and sub-second are consistent (no race at rollover). */
    /** @brief Returns local time as milliseconds within the current 24 h day (0..86399999).
     *  Single getTimeMs() call — no race condition at second rollover. */
    static uint32_t getLocalTimeMs()
    {
        // Apply cached TZ offset to the UTC-based 24 h counter.
        // +86400000 before modulo ensures the result is positive for any valid offset.
        return (ntp.getTimeMs() + (uint32_t)(tzOffsetSec * 1000L + 86400000L)) % 86400000UL;
    }

    uint16_t calcMotorPosition()
    {
        uint32_t total = getLocalTimeMs(); // local time in ms within 24 h day (0..86399999)
        TimeInfo t;
        const uint16_t steps = MOTOR_STEPS_PER_REV;
        switch (NightMode::motorMode)
        {
        case 1: // Motor shows hours
            t.h = (total / 3600000UL) % 12;
            t.m = 0;
            t.s = 0;
            switch (Settings::motorGrid)
            {
            case 0: // Analog mode → no grid (here we need seconds)
                t.m = (total / 60000UL) % 60;
                t.s = (total / 1000UL) % 60;
                break;
            case 1: // Grid mode: 1/5h → 60 grids (aligns with min/sec grid)
                t.m = (total / 60000UL) % 60;
                t.m -= t.m % 12;
                break;
            case 2: // Grid mode: ¼h → 48 grids
                t.m = (total / 60000UL) % 60;
                t.m -= t.m % 15;
                break;
            case 3: // Grid mode: ½h → 24 grids
                t.m = (total / 60000UL) % 60;
                t.m -= t.m % 30;
                break;
            case 4: // Grid mode: 1h → 12 grids
                break;
            case 5: // Grid mode: 3h → 4 grids
                t.h -= t.h % 3;
                break;
            }
            // Formula is precise enough for MOTOR_STEPS_PER_REV < 43200 steps
            return (uint16_t)(((uint32_t)t.h * 3600UL + t.m * 60UL + t.s) * steps / 43200UL);
            break;

        case 2: // Motor shows minutes
            t.m = (total / 60000UL) % 60;
            t.s = 0;
            switch (Settings::motorGrid)
            {
            case 0: // Analog mode → no grid
                t.s = (total / 1000UL) % 60;
                break;
            case 1: // Grid mode: ¼min → 240 grids
                t.s = (total / 1000UL) % 60;
                t.s -= t.s % 15;
                break;
            case 2: // Grid mode: ½min → 120 grids
                t.s = (total / 1000UL) % 60;
                t.s -= t.s % 30;
                break;
            case 3: // Grid mode: 1min → 60 grids
                break;
            case 4: // Grid mode: 5min →  12 grids
                t.m -= t.m % 5;
                break;
            case 5: // Grid mode: 15min →   4 grids
                t.m -= t.m % 15;
                break;
            }
            // Formula is precise enough for MOTOR_STEPS_PER_REV < 3600 steps
            return (uint16_t)(((uint32_t)t.m * 60UL + t.s) * steps / 3600UL);
            break;

        case 3: // Motor shows seconds
            t.s = (total / 1000UL) % 60;
            t.ms = 0;
            switch (Settings::motorGrid)
            {
            case 0: // Analog mode → no grid
                t.ms = total % 1000;
                break;
            case 1: // Grid mode: ¼s → 240 grids
                t.ms = total % 1000;
                t.ms -= t.ms % 250;
                break;
            case 2: // Grid mode:  ½s → 120 grids
                t.ms = total % 1000;
                t.ms -= t.ms % 500;
                break;
            case 3: // Grid mode: 1s → 60 grids
                break;
            case 4: // Grid mode: 5s → 12 grids
                t.s -= t.s % 5;
                break;
            case 5: // Grid mode: 15s → 4 grids
                t.s -= t.s % 15;
                break;
            }
            // Formula is precise enough for MOTOR_STEPS_PER_REV < 60000 steps
            return (uint16_t)(((uint32_t)t.s * 1000UL + t.ms) * steps / 60000UL);
            break;
        }
        return 0;
    }

    uint32_t calcNextGridChangeMs()
    {
        if (Settings::motorGrid == 0)
            return 0; // analog mode — no grid

        uint32_t total = getLocalTimeMs();
        uint32_t period = 0; // grid period in ms

        switch (NightMode::motorMode)
        {
        case 1: // hours
            switch (Settings::motorGrid)
            {
            case 1: period =  12UL * 60000UL;   break; // 1/5h
            case 2: period =  15UL * 60000UL;   break; // ¼h
            case 3: period =  30UL * 60000UL;   break; // ½h
            case 4: period =        3600000UL;  break; // 1h
            case 5: period =  3UL * 3600000UL;  break; // 3h
            }
            break;
        case 2: // minutes
            switch (Settings::motorGrid)
            {
            case 1: period =  15000UL; break; // ¼min
            case 2: period =  30000UL; break; // ½min
            case 3: period =  60000UL; break; // 1min
            case 4: period = 300000UL; break; // 5min
            case 5: period = 900000UL; break; // 15min
            }
            break;
        case 3: // seconds
            switch (Settings::motorGrid)
            {
            case 1: period =    250UL; break; // ¼s
            case 2: period =    500UL; break; // ½s
            case 3: period =   1000UL; break; // 1s
            case 4: period =   5000UL; break; // 5s
            case 5: period =  15000UL; break; // 15s
            }
            break;
        }

        if (period == 0)
            return 0;

        uint32_t msUntilNext = period - (total % period);
        return millis() + msUntilNext;
    }
} // namespace TimeState
