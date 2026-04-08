/**
 * @file settings.h
 * @brief Global configuration storage and persistence interface for the Ringclock Ultimate.
 *
 * Central data hub for all application parameters, organized into logical groups:
 * Brightness, Ambient, Time (hands), Motor, SFX, Night Mode, System and MQTT.
 * All members are static to provide a single source of truth across all modules.
 * Persistence is handled via JSON serialization to LittleFS.
 *
 * Three files are used:
 * - SETTINGS_FILE (settings.json)          — all general parameters.
 * - MQTT_SETTINGS_FILE (mqtt.json)         — MQTT connection parameters (separate for
 *   security and independent backup/restore during LittleFS updates).
 * - MOTOR_CALIBRATION_FILE (motor.json)    — motor zero-point calibration offset
 *   (separate so OTA data updates do not erase the motor calibration).
 */

#pragma once
#include <Arduino.h>
#include "color.h"
#include "config.h"

/**
 * @brief Static class managing all configurable system parameters.
 * @details All members are static to ensure a single source of truth across
 * all application modules. Persistence is handled via JSON serialization
 * to LittleFS (settings.json and mqtt.json).
 */
class Settings
{
public:
    // -------------------------------------------------------------------------
    /** @name Brightness Control
     *  Parameters for manual or sensor-based light intensity.
     * @{ */
    static bool autoBrightness;  // Toggle for automatic adjustment via BH1750 sensor.
    static int  manualBrightness; // Fixed brightness value (0–255) for manual mode.
    static int  autoMin;          // Minimum brightness floor for automatic mode.
    static int  autoMax;          // Maximum brightness ceiling for automatic mode.
    static int  autoLuxMax;       // Ambient lux threshold for reaching maximum brightness.
    /** @} */

    // -------------------------------------------------------------------------
    /** @name Ambient Lighting
     *  Configuration for the background orientation layers.
     * @{ */
    static bool       ambientEnabled;      // Master toggle for the base ambient layer.
    static bool       hourMarksEnabled;    // Toggle for hour marks at LEDs 0, 5, 10, …, 55.
    static bool       quarterMarksEnabled; // Toggle for quarter marks at LEDs 0, 15, 30, 45.
    static Color::HSV ambientColor;        // Base color for the general background lighting.
    static Color::HSV hourMarkColor;       // Color for the hour mark LEDs.
    static Color::HSV quarterMarkColor;    // Color for the quarter-hour mark LEDs.
    /** @} */

    // -------------------------------------------------------------------------
    /**
     * @brief Visual parameters for a single clock hand (time layer).
     */
    struct HandSettings
    {
        Color::HSV handColor;        // Color of the primary pointer LED.
        Color::HSV tailStartColor;   // Shared start color for forward and backward tails.
        Color::HSV tailFwdEndColor;  // End color for the leading tail gradient.
        Color::HSV tailBackEndColor; // End color for the trailing tail gradient.
        int        tailFwdLength;    // Forward tail length (0–10, 11 = fill to 12 o'clock).
        int        tailBackLength;   // Backward tail length (0–10, 11 = fill to 12 o'clock).
        uint8_t    ringMask;         // Bitmask: bit 0 = ring 0, bit 1 = ring 1, bit 2 = ring 2.
    };

    /** @name Time Hand Instances
     *  Settings for the three primary time indicators.
     * @{ */
    static HandSettings secondHand; // Configuration for the seconds indicator.
    static HandSettings minuteHand; // Configuration for the minutes indicator.
    static HandSettings hourHand;   // Configuration for the hours indicator.
    /** @} */

    // -------------------------------------------------------------------------
    /** @name Stepper Motor
     *  Settings for the stepper motor mechanical display.
     * @{ */
    static int motorMode; // Operating mode: 0=Off, 1=Hours, 2=Minutes, 3=Seconds, 4=Homing.
    static int motorGrid;     // 0=Analog mode, >0=Grid mode (time-unit specific step count).
    static int motorSpeed;    // Maximum steps per second (50–400).
    static int motorAccel;    // Acceleration in steps/s² (50–8000).
    static int16_t autoHomingFinePitch; // Persistent zero-point correction [steps] (auto homing with Hall sensor).
    static int16_t manHomingFinePitch;  // Persistent zero-point correction [steps] (manual homing without sensor).
    static bool motorAutoHoming;        // true = use Hall sensor for automatic homing (requires MOTOR_AH_EN=1).
                                      // With Hall sensor (MOTOR_AH_EN=1): steps forward
                                      // from the sensor midpoint to 12 o'clock.
                                      // Without sensor (MOTOR_AH_EN=0): steps from the
                                      // power-on detent snap to 12 o'clock. Stored in motor.json.
    /** @} */

    // -------------------------------------------------------------------------
    /** @name Special Effects (SFX)
     *  Visual overlay effects and animations.
     * @{ */
    static uint16_t sfxRadarInterval;        // Radar scan interval in minutes (0 = disabled).
    static uint16_t sfxShortCircuitInterval; // Short-circuit flash interval in minutes (0 = disabled).
    static uint16_t sfxShootingStarInterval; // Shooting star interval in minutes (0 = disabled).
    static uint16_t sfxHeartbeatInterval;    // Heartbeat interval in minutes (0 = disabled).
    static uint8_t  sfxHeartbeatIntensity;   // Heartbeat modulation depth (0 = no effect, 255 = 50–100 % range).
    /** @} */

    // -------------------------------------------------------------------------
    /** @name Night Mode
     *  Scheduled dimming and feature overrides during a configured time window.
     * @{ */
    static bool    nightModeEnabled; // Master switch: enables the night-mode time window.
    static int     nightStart;       // Window start in minutes since midnight (e.g. 1320 = 22:00).
    static int     nightEnd;         // Window end in minutes since midnight (e.g. 360 = 06:00).
    static int     nightBrightness;  // Target brightness during night (0–255).

    /**
     * @brief Bitmask of features that are active during the night window.
     * Use the NIGHT_* constants below to read and write individual bits.
     */
    static uint8_t nightFeatures;

    static constexpr uint8_t NIGHT_DIM_LEDS        = 0x01; // Dim LEDs to nightBrightness.
    static constexpr uint8_t NIGHT_SFX_OFF         = 0x02; // Disable all SFX effects.
    static constexpr uint8_t NIGHT_SECOND_HAND_OFF = 0x04; // Hide the second hand.
    static constexpr uint8_t NIGHT_MOTOR_HOME      = 0x08; // Park motor at 12 o'clock and stop.
    static constexpr uint8_t NIGHT_MARKERS_OFF     = 0x10; // Hide hour and quarter-hour markers.
    /** @} */

    // -------------------------------------------------------------------------
    /** @name System
     *  Global safety and diagnostic settings.
     * @{ */
    static int powerLimit; // Maximum allowed LED current draw in mA.
    static int    logLevel;        // Serial logging verbosity: 0=Off, 1=Error, 2=Info, 3=Debug.
    static String timezone;        // POSIX timezone string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3").
    /** @} */

    // -------------------------------------------------------------------------
    /** @name MQTT
     *  Connection parameters for the optional MQTT client.
     *  Stored separately in mqtt.json (see loadMqtt() / saveMqtt()).
     * @{ */
    static bool    mqttEnabled;   // Enables/disables the MQTT client.
    static String  mqttBroker;    // Broker hostname or IP address.
    static uint16_t mqttPort;     // Broker port (default 1883).
    static String  mqttUser;      // MQTT username (empty = no authentication).
    static String  mqttPassword;  // MQTT password (empty = no authentication).
    static String  mqttClientId;  // MQTT client identifier (default "ringclock").
    static String  mqttTopicBase; // Base topic prefix for all published/subscribed topics.
    /** @} */

    // -------------------------------------------------------------------------
    //  Persistence
    // -------------------------------------------------------------------------

    /**
     * @brief Loads all non-MQTT settings from LittleFS (settings.json).
     * @details Defaults are retained for any key missing from the file.
     * Should be called once during application setup before any module reads Settings.
     */
    static void load();

    /**
     * @brief Saves all non-MQTT settings to LittleFS (settings.json).
     * @details Triggered explicitly by the web interface Save button or
     * via the MQTT command topic.
     */
    static void save();

    /**
     * @brief Loads MQTT connection parameters from LittleFS (mqtt.json).
     * @details Stored separately from settings.json so that credentials
     * can be backed up and restored independently during LittleFS updates.
     */
    static void loadMqtt();
    static void loadHoming();

    /**
     * @brief Saves MQTT connection parameters to LittleFS (mqtt.json).
     */
    static void saveMqtt();

    /**
     * @brief Saves motor calibration offset to LittleFS (motor.json).
     */
    static void saveMotor();
};
