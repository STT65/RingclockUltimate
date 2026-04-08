/**
 * @file settings.cpp
 * @brief Implementation of the central configuration management for the Ringclock Ultimate.
 *
 * Handles persistence of all user-definable parameters via JSON serialization
 * to LittleFS. Three files are used:
 * - settings.json — all general parameters.
 * - mqtt.json     — MQTT connection parameters (separate for security).
 * - motor.json    — motor zero-point calibration offset (separate so OTA data
 *                   updates do not erase the motor calibration).
 */

#include "logging.h"
#include "config.h"
#include "settings.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// -------------------------------------------------------------------------
//  Default values for all static members
// -------------------------------------------------------------------------

// Brightness
bool Settings::autoBrightness = false;
int Settings::manualBrightness = 128;
int Settings::autoMin = 20;
int Settings::autoMax = 255;
int Settings::autoLuxMax = 2000;

// Ambient
bool Settings::ambientEnabled = true;
bool Settings::hourMarksEnabled = true;
bool Settings::quarterMarksEnabled = true;
Color::HSV Settings::ambientColor(0, 0, 50);
Color::HSV Settings::hourMarkColor(0, 0, 255);
Color::HSV Settings::quarterMarkColor(0, 0, 180);

// Time hands
Settings::HandSettings Settings::secondHand{
    Color::HSV(170, 255, 255), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), 0, 0, 0xFF};
Settings::HandSettings Settings::minuteHand{
    Color::HSV(85, 255, 255), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), 0, 0, 0xFF};
Settings::HandSettings Settings::hourHand{
    Color::HSV(0, 255, 255), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), Color::HSV(0, 0, 0), 0, 0, 0xFF};

// Motor
int Settings::motorMode = 0;
int Settings::motorGrid = 0;
int Settings::motorSpeed = 200;
int Settings::motorAccel = 200;
int16_t Settings::autoHomingFinePitch = 0;
int16_t Settings::manHomingFinePitch  = 0;
bool    Settings::motorAutoHoming     = true;

// SFX
uint16_t Settings::sfxRadarInterval = 0;
uint16_t Settings::sfxShortCircuitInterval = 0;
uint16_t Settings::sfxShootingStarInterval = 0;
uint16_t Settings::sfxHeartbeatInterval = 0;
uint8_t Settings::sfxHeartbeatIntensity = 0;

// Night mode
bool    Settings::nightModeEnabled = false;
int     Settings::nightStart       = 1320; // 22:00
int     Settings::nightEnd         = 360;  // 06:00
int     Settings::nightBrightness  = 20;
uint8_t Settings::nightFeatures    = Settings::NIGHT_DIM_LEDS; // dim LEDs on by default

// System
int Settings::powerLimit = 2000;
int Settings::logLevel = 2;
String Settings::timezone = "CET-1CEST,M3.5.0,M10.5.0/3"; // Central Europe

// MQTT
bool Settings::mqttEnabled = false;
String Settings::mqttBroker = "";
uint16_t Settings::mqttPort = 1883;
String Settings::mqttUser = "";
String Settings::mqttPassword = "";
String Settings::mqttClientId = "ringclock";
String Settings::mqttTopicBase = "ringclock";


/**
 * @brief Loads application settings from the non-volatile file system.
 * * Opens the SETTINGS_FILE via LittleFS and parses the JSON content.
 * If the file is missing or corrupted, default values are retained.
 * Includes validation for nested color objects (HSV).
 * * @note This should be called during the application setup phase.
 */
void Settings::load()
{
    if (!LittleFS.exists(SETTINGS_FILE))
    {
        LOG_INFO(LOG_SET, F("SET: No settings file found, using defaults."));
        return;
    }

    File f = LittleFS.open(SETTINGS_FILE, "r");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open settings file for reading."));
        return;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error)
    {
        LOG_ERROR(LOG_SET, F("SET: JSON deserialization error"), error.c_str());
        return;
    }
    LOG_JSON(LOG_SET, "SET: Settings loaded", doc);

    JsonObject br = doc["brightness"];
    autoBrightness   = br["autoBrightness"]   | autoBrightness;
    manualBrightness = br["manualBrightness"] | manualBrightness;
    autoMin          = br["autoMin"]          | autoMin;
    autoMax          = br["autoMax"]          | autoMax;
    autoLuxMax       = br["autoLuxMax"]       | autoLuxMax;

    JsonObject am = doc["ambient"];
    ambientEnabled      = am["ambientEnabled"]      | ambientEnabled;
    hourMarksEnabled    = am["hourMarksEnabled"]    | hourMarksEnabled;
    quarterMarksEnabled = am["quarterMarksEnabled"] | quarterMarksEnabled;
    ambientColor        = Color::hexToHsv(am["ambientColor"]     | "#000032");
    hourMarkColor       = Color::hexToHsv(am["hourMarkColor"]    | "#0000ff");
    quarterMarkColor    = Color::hexToHsv(am["quarterMarkColor"] | "#0000b4");

    JsonObject ts = doc["time"]["second"];
    secondHand.handColor      = Color::hexToHsv(ts["handColor"]      | "#000000");
    secondHand.tailStartColor = Color::hexToHsv(ts["tailStartColor"] | "#000000");
    secondHand.tailFwdEndColor  = Color::hexToHsv(ts["tailFwdEndColor"]  | "#000000");
    secondHand.tailBackEndColor = Color::hexToHsv(ts["tailBackEndColor"] | "#000000");
    secondHand.tailFwdLength  = ts["tailFwdLength"]  | secondHand.tailFwdLength;
    secondHand.tailBackLength = ts["tailBackLength"] | secondHand.tailBackLength;
    secondHand.ringMask       = ts["ringMask"]       | secondHand.ringMask;

    JsonObject tm = doc["time"]["minute"];
    minuteHand.handColor      = Color::hexToHsv(tm["handColor"]      | "#000000");
    minuteHand.tailStartColor = Color::hexToHsv(tm["tailStartColor"] | "#000000");
    minuteHand.tailFwdEndColor  = Color::hexToHsv(tm["tailFwdEndColor"]  | "#000000");
    minuteHand.tailBackEndColor = Color::hexToHsv(tm["tailBackEndColor"] | "#000000");
    minuteHand.tailFwdLength  = tm["tailFwdLength"]  | minuteHand.tailFwdLength;
    minuteHand.tailBackLength = tm["tailBackLength"] | minuteHand.tailBackLength;
    minuteHand.ringMask       = tm["ringMask"]       | minuteHand.ringMask;

    JsonObject th = doc["time"]["hour"];
    hourHand.handColor      = Color::hexToHsv(th["handColor"]      | "#000000");
    hourHand.tailStartColor = Color::hexToHsv(th["tailStartColor"] | "#000000");
    hourHand.tailFwdEndColor  = Color::hexToHsv(th["tailFwdEndColor"]  | "#000000");
    hourHand.tailBackEndColor = Color::hexToHsv(th["tailBackEndColor"] | "#000000");
    hourHand.tailFwdLength  = th["tailFwdLength"]  | hourHand.tailFwdLength;
    hourHand.tailBackLength = th["tailBackLength"] | hourHand.tailBackLength;
    hourHand.ringMask       = th["ringMask"]       | hourHand.ringMask;

    JsonObject sf = doc["sfx"];
    sfxShortCircuitInterval = sf["shortCircuitInterval"] | sfxShortCircuitInterval;
    sfxRadarInterval        = sf["radarInterval"]        | sfxRadarInterval;
    sfxShootingStarInterval = sf["shootingStarInterval"] | sfxShootingStarInterval;
    sfxHeartbeatInterval    = sf["heartbeatInterval"]    | sfxHeartbeatInterval;
    sfxHeartbeatIntensity   = sf["heartbeatIntensity"]   | sfxHeartbeatIntensity;

    JsonObject mo = doc["motor"];
    motorMode       = mo["motorMode"]       | motorMode;
    motorGrid       = mo["motorGrid"]       | motorGrid;
    motorSpeed      = mo["motorSpeed"]      | motorSpeed;
    motorAccel      = mo["motorAccel"]      | motorAccel;
    motorAutoHoming = mo["motorAutoHoming"] | motorAutoHoming;
#if !MOTOR_AH_EN
    motorAutoHoming = false; // clamp: sensor not compiled in
#endif

    JsonObject ni = doc["night"];
    nightModeEnabled = ni["nightModeEnabled"] | nightModeEnabled;
    nightStart       = ni["nightStart"]       | nightStart;
    nightEnd         = ni["nightEnd"]         | nightEnd;
    nightBrightness  = ni["nightBrightness"]  | nightBrightness;
    nightFeatures    = ni["nightFeatures"]    | nightFeatures;

    JsonObject sy = doc["system"];
    powerLimit = sy["powerLimit"] | powerLimit;
    logLevel   = sy["logLevel"]   | logLevel;
    timezone   = sy["timezone"]   | timezone;
}

/**
 * @brief Saves current application settings to the file system.
 * * Serializes all static member variables into a JSON document and writes
 * it to LittleFS. This is triggered explicitly via the
 * "Save" function in the web interface.
 */
void Settings::save()
{
    DynamicJsonDocument doc(4096);

    JsonObject br = doc.createNestedObject("brightness");
    br["autoBrightness"]   = autoBrightness;
    br["manualBrightness"] = manualBrightness;
    br["autoMin"]          = autoMin;
    br["autoMax"]          = autoMax;
    br["autoLuxMax"]       = autoLuxMax;

    JsonObject am = doc.createNestedObject("ambient");
    am["ambientEnabled"]      = ambientEnabled;
    am["hourMarksEnabled"]    = hourMarksEnabled;
    am["quarterMarksEnabled"] = quarterMarksEnabled;
    am["ambientColor"]        = Color::hsvToHex(ambientColor);
    am["hourMarkColor"]       = Color::hsvToHex(hourMarkColor);
    am["quarterMarkColor"]    = Color::hsvToHex(quarterMarkColor);

    JsonObject ti = doc.createNestedObject("time");
    JsonObject ts = ti.createNestedObject("second");
    ts["handColor"]      = Color::hsvToHex(secondHand.handColor);
    ts["tailStartColor"] = Color::hsvToHex(secondHand.tailStartColor);
    ts["tailFwdEndColor"]  = Color::hsvToHex(secondHand.tailFwdEndColor);
    ts["tailBackEndColor"] = Color::hsvToHex(secondHand.tailBackEndColor);
    ts["tailFwdLength"]  = secondHand.tailFwdLength;
    ts["tailBackLength"] = secondHand.tailBackLength;
    ts["ringMask"]       = secondHand.ringMask;

    JsonObject tm = ti.createNestedObject("minute");
    tm["handColor"]      = Color::hsvToHex(minuteHand.handColor);
    tm["tailStartColor"] = Color::hsvToHex(minuteHand.tailStartColor);
    tm["tailFwdEndColor"]  = Color::hsvToHex(minuteHand.tailFwdEndColor);
    tm["tailBackEndColor"] = Color::hsvToHex(minuteHand.tailBackEndColor);
    tm["tailFwdLength"]  = minuteHand.tailFwdLength;
    tm["tailBackLength"] = minuteHand.tailBackLength;
    tm["ringMask"]       = minuteHand.ringMask;

    JsonObject th = ti.createNestedObject("hour");
    th["handColor"]      = Color::hsvToHex(hourHand.handColor);
    th["tailStartColor"] = Color::hsvToHex(hourHand.tailStartColor);
    th["tailFwdEndColor"]  = Color::hsvToHex(hourHand.tailFwdEndColor);
    th["tailBackEndColor"] = Color::hsvToHex(hourHand.tailBackEndColor);
    th["tailFwdLength"]  = hourHand.tailFwdLength;
    th["tailBackLength"] = hourHand.tailBackLength;
    th["ringMask"]       = hourHand.ringMask;

    JsonObject sf = doc.createNestedObject("sfx");
    sf["shortCircuitInterval"] = sfxShortCircuitInterval;
    sf["radarInterval"]        = sfxRadarInterval;
    sf["shootingStarInterval"] = sfxShootingStarInterval;
    sf["heartbeatInterval"]    = sfxHeartbeatInterval;
    sf["heartbeatIntensity"]   = sfxHeartbeatIntensity;

    JsonObject mo = doc.createNestedObject("motor");
    mo["motorMode"]       = motorMode;
    mo["motorGrid"]       = motorGrid;
    mo["motorSpeed"]      = motorSpeed;
    mo["motorAccel"]      = motorAccel;
    mo["motorAutoHoming"] = motorAutoHoming;

    JsonObject ni = doc.createNestedObject("night");
    ni["nightModeEnabled"] = nightModeEnabled;
    ni["nightStart"]       = nightStart;
    ni["nightEnd"]         = nightEnd;
    ni["nightBrightness"]  = nightBrightness;
    ni["nightFeatures"]    = nightFeatures;

    JsonObject sy = doc.createNestedObject("system");
    sy["powerLimit"] = powerLimit;
    sy["logLevel"]   = logLevel;
    sy["timezone"]   = timezone;

    File f = LittleFS.open(SETTINGS_FILE, "w");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open settings file for writing."));
        return;
    }
    serializeJson(doc, f);
    f.close();
    LOG_JSON(LOG_SET, "SET: Settings saved", doc);
}

// -------------------------------------------------------------------------
//  loadMqtt() — mqtt.json
// -------------------------------------------------------------------------

void Settings::loadMqtt()
{
    if (!LittleFS.exists(MQTT_SETTINGS_FILE))
    {
        LOG_INFO(LOG_SET, F("SET: No MQTT settings file found, using defaults."));
        return;
    }

    File f = LittleFS.open(MQTT_SETTINGS_FILE, "r");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open MQTT settings file for reading."));
        return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error)
    {
        LOG_ERROR(LOG_SET, F("SET: MQTT JSON deserialization error"), error.c_str());
        return;
    }

    mqttEnabled = doc["mqttEnabled"] | mqttEnabled;
    mqttBroker = doc["mqttBroker"] | mqttBroker;
    mqttPort = doc["mqttPort"] | mqttPort;
    mqttUser = doc["mqttUser"] | mqttUser;
    mqttPassword = doc["mqttPassword"] | mqttPassword;
    mqttClientId = doc["mqttClientId"] | mqttClientId;
    mqttTopicBase = doc["mqttTopicBase"] | mqttTopicBase;

    LOG_INFO(LOG_SET, F("SET: MQTT settings loaded."));
}

// -------------------------------------------------------------------------
//  saveMqtt() — mqtt.json
// -------------------------------------------------------------------------

void Settings::saveMqtt()
{
    DynamicJsonDocument doc(512);

    doc["mqttEnabled"] = mqttEnabled;
    doc["mqttBroker"] = mqttBroker;
    doc["mqttPort"] = mqttPort;
    doc["mqttUser"] = mqttUser;
    doc["mqttPassword"] = mqttPassword;
    doc["mqttClientId"] = mqttClientId;
    doc["mqttTopicBase"] = mqttTopicBase;

    File f = LittleFS.open(MQTT_SETTINGS_FILE, "w");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open MQTT settings file for writing."));
        return;
    }
    serializeJson(doc, f);
    f.close();
    LOG_INFO(LOG_SET, F("SET: MQTT settings saved."));
}

// -------------------------------------------------------------------------
//  loadHoming() — motor.json
// -------------------------------------------------------------------------

void Settings::loadHoming()
{
    if (!LittleFS.exists(MOTOR_CALIBRATION_FILE))
    {
        LOG_INFO(LOG_SET, F("SET: No motor calibration file found, using default offset 0."));
        return;
    }

    File f = LittleFS.open(MOTOR_CALIBRATION_FILE, "r");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open motor calibration file for reading."));
        return;
    }

    DynamicJsonDocument doc(128);
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error)
    {
        LOG_ERROR(LOG_SET, F("SET: Motor calibration JSON deserialization error"), error.c_str());
        return;
    }

    autoHomingFinePitch = doc["autoHomingFinePitch"] | autoHomingFinePitch;
    manHomingFinePitch  = doc["manHomingFinePitch"]  | manHomingFinePitch;
    LOG_INFO(LOG_SET, String(F("SET: Motor calibration loaded, autoOffset=")) + autoHomingFinePitch + F(", manOffset=") + manHomingFinePitch);
}

// -------------------------------------------------------------------------
//  saveMotor() — motor.json
// -------------------------------------------------------------------------

void Settings::saveMotor()
{
    DynamicJsonDocument doc(128);
    doc["autoHomingFinePitch"] = autoHomingFinePitch;
    doc["manHomingFinePitch"]  = manHomingFinePitch;

    File f = LittleFS.open(MOTOR_CALIBRATION_FILE, "w");
    if (!f)
    {
        LOG_ERROR(LOG_SET, F("SET: Cannot open motor calibration file for writing."));
        return;
    }
    serializeJson(doc, f);
    f.close();
    LOG_INFO(LOG_SET, String(F("SET: Motor calibration saved, offset = ")) + autoHomingFinePitch);
}
