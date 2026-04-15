/**
 * @file layer_sfx.cpp
 * @brief Special effects (SFX) layer implementation.
 *
 * Implements four visual effects composited on top of the ambient and
 * time layers. Each effect maintains its own state struct and is driven
 * by millis()-based timing so that it runs independently of the loop rate.
 */

#include "layer_sfx.h"
#include "renderer.h"
#include "settings.h"
#include "night_mode.h"
#include "logging.h"
#include "config.h"
#include "color.h"
#include <math.h>
#include <Arduino.h>

namespace SFXLayer
{
    // -------------------------------------------------------------------------
    //  Effect state structs
    // -------------------------------------------------------------------------

    /** @brief State for the radar scan effect. */
    struct RadarState
    {
        bool     active         = false;
        uint32_t lastStepTime   = 0;  // millis() timestamp of the last position step.
        uint16_t currentPos     = 0;  // Current LED index of the scan head (0..LEDS_PER_RING-1).
        uint8_t  currentRound   = 0;  // Number of completed revolutions.
        uint8_t  pingBrightness = 0;  // Current brightness of the background ping fade.
    };

    /** @brief Generic state for single-shot timed effects. */
    struct EffectState
    {
        bool     active    = false;
        uint32_t startTime = 0; // millis() timestamp when the effect was triggered.
    };

    static RadarState  sfxRadarState;
    static EffectState shortCircuitState;
    static EffectState starState;
    static EffectState heartbeatState;

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void init()
    {
        shortCircuitState.active = false;
        sfxRadarState.active     = false;
        starState.active         = false;
        heartbeatState.active    = false;
    }

    // -------------------------------------------------------------------------
    //  Internal effect renderers
    // -------------------------------------------------------------------------

    /**
     * @brief Renders the short-circuit effect.
     * @details Runs for exactly 1000 ms after being triggered. On each frame,
     * there is a ~60 % chance that a random subset of LEDs is lit in cold white,
     * producing an irregular flickering appearance.
     * @param buffer Pointer to the ring's LED buffer.
     */
    static void renderShortCircuit(Color::RGB *buffer, uint8_t ledsPerRing)
    {
        if (!shortCircuitState.active)
            return;

        if (millis() - shortCircuitState.startTime > 1000)
        {
            shortCircuitState.active = false;
            return;
        }

        // ~60 % chance per frame to flash; irregular step size for a "broken" look
        if (random(0, 10) > 4)
        {
            Color::RGB coldWhite(200, 220, 255);
            long step = random(1, 6);
            for (int i = 0; i < ledsPerRing; i += step)
                buffer[i] = coldWhite;
        }
    }

    /**
     * @brief Renders the heartbeat effect.
     * @details Modulates the entire composited frame (ambient + time hands +
     * lower SFX layers) in-place using a lubb-dupp sine envelope with a period
     * of 1200 ms. The effect runs for exactly 5000 ms after being triggered,
     * then deactivates automatically.
     *
     * Modulation range is controlled by Settings::sfxHeartbeatIntensity:
     * - 0   : no modulation (1.0 throughout)
     * - 255 : maximum range — peak brightness 100%, valley brightness 50%
     *
     * Formula: intensity = 1.0 - brightnessFactor * depth * 0.5
     * where depth = sfxHeartbeatIntensity / 255.
     * Between beats (brightnessFactor = 0): intensity = 1.0 — no change to system brightness.
     * At beat peak (brightnessFactor = 1, depth = 1): intensity = 0.5 — dims to 50 %.
     *
     * @param buffer Pointer to the ring's LED buffer.
     */
    static void renderHeartbeat(Color::RGB *buffer, uint8_t ledsPerRing)
    {
        if (!heartbeatState.active)
            return;

        if (millis() - heartbeatState.startTime > 5000)
        {
            heartbeatState.active = false;
            return;
        }

        if (Settings::sfxHeartbeatIntensity == 0)
            return;

        const uint32_t period = 1200; // full lubb-dupp cycle [ms]
        uint32_t x = millis() % period;
        float brightnessFactor = 0.0f;

        // First beat (lubb)
        if (x < 200)
            brightnessFactor = sin((x / 200.0f) * M_PI);
        // Second beat (dupp) — slightly quieter
        else if (x >= 250 && x < 450)
            brightnessFactor = sin(((x - 250) / 200.0f) * M_PI) * 0.7f;

        // Pulse to dark: beats dim the display; between beats = full system brightness.
        // At depth=1: peak dims to 50 %, between beats stays at 100 % system brightness.
        float depth     = Settings::sfxHeartbeatIntensity / 255.0f;
        float intensity = 1.0f - brightnessFactor * depth * 0.5f;

        // Multiply existing buffer in-place — no colour override
        for (int i = 0; i < ledsPerRing; i++)
        {
            buffer[i].r = (uint8_t)(buffer[i].r * intensity);
            buffer[i].g = (uint8_t)(buffer[i].g * intensity);
            buffer[i].b = (uint8_t)(buffer[i].b * intensity);
        }
    }

    /**
     * @brief Renders the radar scan effect.
     * @details Simulates a classic sonar/radar display. A bright green scan head
     * advances one LED every SFX_RADAR_STEP_TIME ms and trails a quadratically
     * fading phosphor tail of length SFX_RADAR_TAIL. A background "ping" pulse
     * fades out after each full revolution. The effect stops after SFX_RADAR_ROUNDS
     * complete revolutions.
     * @param buffer Pointer to the ring's LED buffer.
     */
    // Radar global state is driven by ring 0's LED count so that animation
    // duration is consistent. Other rings scale the position proportionally.
    static void renderRadar(Color::RGB *buffer, uint8_t ledsPerRing)
    {
        if (!sfxRadarState.active)
            return;

        if (sfxRadarState.currentRound >= SFX_RADAR_ROUNDS)
        {
            sfxRadarState.active = false;
            return;
        }

        const uint8_t  refLeds  = RING_LEDS[0];
        const uint32_t stepTime = 2000 / refLeds;  // ms per LED step (ring 0 reference)

        // ----------------------------------------------------------------
        // Step 1: Advance scan head position at fixed time intervals
        // ----------------------------------------------------------------
        bool stepped = false;
        if ((millis() - sfxRadarState.lastStepTime) >= stepTime)
        {
            sfxRadarState.lastStepTime += stepTime; // catch up if delayed
            sfxRadarState.currentPos++;
            stepped = true;
            if (sfxRadarState.currentPos >= refLeds)
            {
                sfxRadarState.currentPos = 0;
                sfxRadarState.currentRound++;
            }
        }

        // ----------------------------------------------------------------
        // Step 2: Update ping brightness (triggered at position 1, then fades)
        // ----------------------------------------------------------------
        if (stepped)
        {
            if (sfxRadarState.currentPos == 1)
                sfxRadarState.pingBrightness = 200;
            else
                sfxRadarState.pingBrightness = (sfxRadarState.pingBrightness >= 10)
                    ? sfxRadarState.pingBrightness - 10 : 0;
        }

        // Scale position and tail to this ring's size
        uint8_t scaledPos  = (uint8_t)((uint32_t)sfxRadarState.currentPos * ledsPerRing / refLeds);
        uint8_t scaledTail = ledsPerRing / 4;

        // Fill background with current ping level
        for (int i = 0; i < ledsPerRing; i++)
            buffer[i] = Color::RGB(0, sfxRadarState.pingBrightness, 0);

        // ----------------------------------------------------------------
        // Step 3: Draw fading tail (quadratic falloff)
        // ----------------------------------------------------------------
        for (int i = 0; i < scaledTail; i++)
        {
            int16_t drawPos = scaledPos - i;
            if (drawPos < 0) drawPos += ledsPerRing;

            uint32_t inv_i     = scaledTail - i;
            uint8_t  greenValue = (255 * inv_i * inv_i) / (scaledTail * scaledTail);
            buffer[drawPos] = Color::RGB(0, greenValue, 0);
        }

        // ----------------------------------------------------------------
        // Step 4: Bright white-tinted scan head
        // ----------------------------------------------------------------
        buffer[scaledPos] = Color::RGB(150, 255, 150);
    }

    /**
     * @brief Renders the shooting star effect.
     * @details A bright warm-white head with a short warm tail travels around
     * the ring over a fixed duration of 1024 ms, then deactivates.
     * Applied to every ring, identical to the other SFX effects.
     * @param buffer Pointer to the ring's LED buffer.
     */
    static void renderShootingStar(Color::RGB *buffer, uint8_t ledsPerRing)
    {
        if (!starState.active)
            return;

        const uint32_t duration = 1024;
        uint32_t elapsed = millis() - starState.startTime;

        if (elapsed > duration)
        {
            starState.active = false;
            LOG_DEBUG(LOG_SFX, F("SFX: ShootingStar finished."));
            return;
        }

        uint16_t pos = (ledsPerRing * elapsed) / duration;
        if (pos >= ledsPerRing) pos = ledsPerRing - 1;

        // Bright warm-white head with a short fading tail
        buffer[pos] = Color::RGB(255, 255, 200);
        if (pos > 0) buffer[pos - 1] = Color::RGB(100, 100, 50);
        if (pos > 1) buffer[pos - 2] = Color::RGB(40,  40,  20);
    }

    // -------------------------------------------------------------------------
    //  Public render entry point
    // -------------------------------------------------------------------------

    uint32_t render(uint8_t ringIndex, Color::RGB *buffer)
    {
        const uint8_t n = RING_LEDS[ringIndex];

        renderHeartbeat(buffer, n);     // modulates existing ambient in-place
        renderShootingStar(buffer, n);  // overlays streak on all rings
        renderRadar(buffer, n);         // overlays green sweep
        renderShortCircuit(buffer, n);  // overwrites everything (highest priority)

        // Determine the earliest point at which any active effect needs a new frame.
        uint32_t next = UINT32_MAX;

        if (heartbeatState.active)
            next = min(next, (uint32_t)(millis() + 16));  // sine modulation — 60 fps sufficient

        if (shortCircuitState.active)
            next = min(next, (uint32_t)(millis() + 16));  // random flicker — 60 fps

        if (sfxRadarState.active)
        {
            const uint32_t stepTime = 2000 / RING_LEDS[0];
            next = min(next, sfxRadarState.lastStepTime + stepTime);
        }

        if (starState.active)
        {
            uint32_t elapsed = millis() - starState.startTime;
            uint16_t pos     = (uint16_t)((RING_LEDS[0] * elapsed) / 1024);
            if (pos < RING_LEDS[0] - 1)
                next = min(next, starState.startTime + (uint32_t)(pos + 1) * 1024 / RING_LEDS[0]);
        }

        return next;
    }

    // -------------------------------------------------------------------------
    //  Manual triggers
    // -------------------------------------------------------------------------

    void sfxShortCircuitTrigger()
    {
        shortCircuitState.active    = true;
        shortCircuitState.startTime = millis();
        Renderer::scheduleFrame();
        LOG_INFO(LOG_SFX, F("SFX: Short circuit started."));
    }

    void sfxRadarTrigger()
    {
        sfxRadarState.active         = true;
        sfxRadarState.lastStepTime   = millis();
        sfxRadarState.currentPos     = 0;
        sfxRadarState.currentRound   = 0;
        sfxRadarState.pingBrightness = 0;
        Renderer::scheduleFrame();
        LOG_INFO(LOG_SFX, F("SFX: Radar scan started."));
    }

    void sfxShootingStarTrigger()
    {
        starState.active    = true;
        starState.startTime = millis();
        Renderer::scheduleFrame();
        LOG_INFO(LOG_SFX, F("SFX: Shooting star started."));
    }

    void sfxHeartbeatTrigger()
    {
        heartbeatState.active    = true;
        heartbeatState.startTime = millis();
        Renderer::scheduleFrame();
        LOG_INFO(LOG_SFX, F("SFX: Heartbeat started."));
    }

    // -------------------------------------------------------------------------
    //  Interval check
    // -------------------------------------------------------------------------

    void checkIntervals(uint8_t currentHour, uint8_t currentMinute)
    {
        // Fire at most once per minute
        static int lastCheckedMinute = -1;

        int totalMinutes = (currentHour * 60) + currentMinute;
        if (totalMinutes == lastCheckedMinute)
            return;
        lastCheckedMinute = totalMinutes;

        // Collect all effects that are due this minute, then fire only the one
        // with the largest interval — rarer effects take priority over frequent ones.
        struct Candidate { uint16_t interval; void (*trigger)(); };
        Candidate best = { 0, nullptr };

        auto check = [&](uint16_t interval, void (*trigger)())
        {
            if (interval > 0 && (totalMinutes % interval) == 0 && interval > best.interval)
                best = { interval, trigger };
        };

        check(NightMode::sfxShortCircuitInterval, sfxShortCircuitTrigger);
        check(NightMode::sfxRadarInterval,        sfxRadarTrigger);
        check(NightMode::sfxShootingStarInterval, sfxShootingStarTrigger);
        check(NightMode::sfxHeartbeatInterval,    sfxHeartbeatTrigger);

        if (best.trigger)
            best.trigger();
    }

} // namespace SFXLayer
