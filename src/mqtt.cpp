/**
 * @file mqtt.cpp
 * @brief MQTT client implementation for the Ringclock Ultimate.
 *
 * Uses the PubSubClient library to connect to a local MQTT broker (e.g. Mosquitto
 * on a Raspberry Pi running FHEM). Runs alongside the AsyncWebServer without
 * interfering with it — both control paths are fully independent.
 *
 * Reconnection strategy: after a failed connect attempt the client waits
 * MQTT_RECONNECT_INTERVAL_MS before retrying, avoiding broker floods.
 *
 * All incoming set/<key> messages are applied immediately to Settings and
 * take effect on the next loop iteration, exactly as WebSocket messages do.
 * Motor settings trigger Motor::resync() automatically.
 */

#include "mqtt.h"
#include "settings.h"
#include "logging.h"
#include "motor.h"
#include "brightness.h"
#include "renderer.h"
#include "color.h"
#include "layer_sfx.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>

namespace MQTT
{
    // -------------------------------------------------------------------------
    //  Constants
    // -------------------------------------------------------------------------

    static const uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000; // Minimum ms between reconnect attempts.
    static const uint32_t MQTT_MONITOR_INTERVAL_MS   = 1000;  // Status publish interval [ms].

    // -------------------------------------------------------------------------
    //  Module state
    // -------------------------------------------------------------------------

    static WiFiClient   wifiClient;
    static PubSubClient mqttClient(wifiClient);

    static uint32_t lastReconnectMs  = 0;     // Timestamp of the last connection attempt.
    static uint32_t lastMonitorMs    = 0;     // Timestamp of the last status publish.
    static bool     pendingPublishAll = false; // Set by requestPublishAll(); consumed in update().
    static uint8_t  connectRetries   = 0;     // Consecutive failed connect attempts.

    // -------------------------------------------------------------------------
    //  Internal helpers
    // -------------------------------------------------------------------------

    /**
     * @brief Builds a full topic string: <base>/<suffix>
     * @param suffix Topic suffix (e.g. "set/motorSpeed").
     * @return Full topic string.
     */
    static String topic(const char *suffix)
    {
        return Settings::mqttTopicBase + "/" + suffix;
    }

    /**
     * @brief Applies an incoming set/<key> message to Settings.
     * @details Mirrors the WebSocket handler logic so both control paths
     * behave identically. Motor settings trigger Motor::resync().
     * @param key   Setting key extracted from the topic (e.g. "motorSpeed").
     * @param value Payload string.
     */
    static void applySet(const String &key, const String &value)
    {
        bool motorChanged = false;

        // Brightness
        if      (key == "autoBrightness")   Settings::autoBrightness   = value.toInt();
        else if (key == "manualBrightness")  Settings::manualBrightness = value.toInt();
        else if (key == "autoMin")           Settings::autoMin          = value.toInt();
        else if (key == "autoMax")           Settings::autoMax          = value.toInt();
        else if (key == "autoLuxMax")        Settings::autoLuxMax       = value.toInt();

        // Ambient
        else if (key == "ambientEnabled")      Settings::ambientEnabled      = value.toInt();
        else if (key == "hourMarksEnabled")    Settings::hourMarksEnabled    = value.toInt();
        else if (key == "quarterMarksEnabled") Settings::quarterMarksEnabled = value.toInt();
        else if (key == "ambientColor")        Settings::ambientColor        = Color::hexToHsv(value);
        else if (key == "hourMarkColor")       Settings::hourMarkColor       = Color::hexToHsv(value);
        else if (key == "quarterMarkColor")    Settings::quarterMarkColor    = Color::hexToHsv(value);

        // Motor
        else if (key == "motorMode") { Settings::motorMode = value.toInt(); motorChanged = true; }
        else if (key == "motorGrid")     { Settings::motorGrid     = value.toInt(); motorChanged = true; }
        else if (key == "motorSpeed")    { Settings::motorSpeed    = value.toInt(); motorChanged = true; }
        else if (key == "motorAccel")    { Settings::motorAccel    = value.toInt(); motorChanged = true; }

        // SFX
        else if (key == "sfxShortCircuitInterval") Settings::sfxShortCircuitInterval = value.toInt();
        else if (key == "sfxRadarInterval")        Settings::sfxRadarInterval        = value.toInt();
        else if (key == "sfxShootingStarInterval") Settings::sfxShootingStarInterval = value.toInt();
        else if (key == "sfxHeartbeatInterval")    Settings::sfxHeartbeatInterval    = value.toInt();
        else if (key == "sfxHeartbeatIntensity")   Settings::sfxHeartbeatIntensity   = value.toInt();

        // Night mode
        else if (key == "nightModeEnabled") Settings::nightModeEnabled = value.toInt();
        else if (key == "nightStart")       Settings::nightStart       = value.toInt();
        else if (key == "nightEnd")         Settings::nightEnd         = value.toInt();
        else if (key == "nightBrightness")  Settings::nightBrightness  = value.toInt();
        else if (key == "nightFeatures")    Settings::nightFeatures    = value.toInt();

        // System
        else if (key == "powerLimit") Settings::powerLimit = value.toInt();
        else if (key == "logLevel")   Log::setLevel((Log::Level)value.toInt());
        else if (key == "timezone")   Settings::timezone   = value;

        // MQTT (apply immediately, save separately)
        else if (key == "mqttEnabled")   Settings::mqttEnabled   = value.toInt();
        else if (key == "mqttBroker")    Settings::mqttBroker    = value;
        else if (key == "mqttPort")      Settings::mqttPort      = value.toInt();
        else if (key == "mqttUser")      Settings::mqttUser      = value;
        else if (key == "mqttPassword")  Settings::mqttPassword  = value;
        else if (key == "mqttClientId")  Settings::mqttClientId  = value;
        else if (key == "mqttTopicBase") Settings::mqttTopicBase = value;

        else
        {
            LOG_DEBUG(LOG_MQTT, F("MQT: Unknown set key: "), key);
            return;
        }

        if (motorChanged)
            Motor::resync();

        LOG_DEBUG(LOG_MQTT, F("MQT: Applied "), key + "=" + value);

        // Echo the new value back to <base>/get/<key>
        publishValue(key.c_str(), value);
    }

    /**
     * @brief Returns the current value of a setting as a String.
     * @param key Setting key.
     * @return Current value, or empty string if key is unknown.
     */
    static String getValue(const String &key)
    {
        // Brightness
        if      (key == "autoBrightness")   return String(Settings::autoBrightness);
        else if (key == "manualBrightness")  return String(Settings::manualBrightness);
        else if (key == "autoMin")           return String(Settings::autoMin);
        else if (key == "autoMax")           return String(Settings::autoMax);
        else if (key == "autoLuxMax")        return String(Settings::autoLuxMax);

        // Ambient
        else if (key == "ambientEnabled")      return String(Settings::ambientEnabled);
        else if (key == "hourMarksEnabled")    return String(Settings::hourMarksEnabled);
        else if (key == "quarterMarksEnabled") return String(Settings::quarterMarksEnabled);
        else if (key == "ambientColor")        return Color::hsvToHex(Settings::ambientColor);
        else if (key == "hourMarkColor")       return Color::hsvToHex(Settings::hourMarkColor);
        else if (key == "quarterMarkColor")    return Color::hsvToHex(Settings::quarterMarkColor);

        // Motor
        else if (key == "motorMode") return String(Settings::motorMode);
        else if (key == "motorGrid")     return String(Settings::motorGrid);
        else if (key == "motorSpeed")    return String(Settings::motorSpeed);
        else if (key == "motorAccel")    return String(Settings::motorAccel);

        // SFX
        else if (key == "sfxShortCircuitInterval") return String(Settings::sfxShortCircuitInterval);
        else if (key == "sfxRadarInterval")        return String(Settings::sfxRadarInterval);
        else if (key == "sfxShootingStarInterval") return String(Settings::sfxShootingStarInterval);
        else if (key == "sfxHeartbeatInterval")    return String(Settings::sfxHeartbeatInterval);
        else if (key == "sfxHeartbeatIntensity")   return String(Settings::sfxHeartbeatIntensity);

        // Night mode
        else if (key == "nightModeEnabled") return String(Settings::nightModeEnabled);
        else if (key == "nightStart")       return String(Settings::nightStart);
        else if (key == "nightEnd")         return String(Settings::nightEnd);
        else if (key == "nightBrightness")  return String(Settings::nightBrightness);
        else if (key == "nightFeatures")    return String(Settings::nightFeatures);

        // System
        else if (key == "powerLimit") return String(Settings::powerLimit);
        else if (key == "logLevel")   return String((int)Log::getLevel());
        else if (key == "timezone")   return Settings::timezone;

        // MQTT (no password — never publish credentials)
        else if (key == "mqttEnabled")   return String(Settings::mqttEnabled);
        else if (key == "mqttBroker")    return Settings::mqttBroker;
        else if (key == "mqttPort")      return String(Settings::mqttPort);
        else if (key == "mqttUser")      return Settings::mqttUser;
        else if (key == "mqttClientId")  return Settings::mqttClientId;
        else if (key == "mqttTopicBase") return Settings::mqttTopicBase;

        return "";
    }

    /**
     * @brief PubSubClient message callback.
     * @details Called by PubSubClient for every incoming message.
     * Dispatches to applySet(), getValue() or command handlers.
     */
    static void onMessage(char *topicRaw, byte *payload, unsigned int length)
    {
        String t       = String(topicRaw);
        String base    = Settings::mqttTopicBase + "/";
        String value   = "";
        for (unsigned int i = 0; i < length; i++)
            value += (char)payload[i];

        LOG_DEBUG(LOG_MQTT, F("MQT: RX "), t + " = " + value);

        // <base>/set/<key>
        if (t.startsWith(base + "set/"))
        {
            String key = t.substring((base + "set/").length());
            applySet(key, value);
            return;
        }

        // <base>/get/<key>
        if (t.startsWith(base + "get/"))
        {
            String key = t.substring((base + "get/").length());
            String v   = getValue(key);
            if (v.length() > 0)
                publishValue(key.c_str(), v);
            return;
        }

        // <base>/cmd/<command>
        if (t.startsWith(base + "cmd/"))
        {
            String cmd = t.substring((base + "cmd/").length());
            if (cmd == "save")
            {
                Settings::save();
                LOG_INFO(LOG_MQTT, F("MQT: Settings saved via MQTT command."));
            }
            else if (cmd == "reboot")
            {
                LOG_INFO(LOG_MQTT, F("MQT: Reboot triggered via MQTT command."));
                delay(200);
                ESP.restart();
            }
            else if (cmd == "resync")
            {
                Motor::resync();
                LOG_INFO(LOG_MQTT, F("MQT: Motor resync triggered via MQTT command."));
            }
            else if (cmd == "sfxShortCircuitTrigger")
            {
                SFXLayer::sfxShortCircuitTrigger();
                LOG_INFO(LOG_MQTT, F("MQT: SFX short circuit triggered via MQTT command."));
            }
            else if (cmd == "sfxRadarTrigger")
            {
                SFXLayer::sfxRadarTrigger();
                LOG_INFO(LOG_MQTT, F("MQT: SFX radar triggered via MQTT command."));
            }
            else if (cmd == "sfxShootingStarTrigger")
            {
                SFXLayer::sfxShootingStarTrigger();
                LOG_INFO(LOG_MQTT, F("MQT: SFX shooting star triggered via MQTT command."));
            }
            else if (cmd == "sfxHeartbeatTrigger")
            {
                SFXLayer::sfxHeartbeatTrigger();
                LOG_INFO(LOG_MQTT, F("MQT: SFX heartbeat triggered via MQTT command."));
            }
        }
    }

    /**
     * @brief Connects (or reconnects) to the MQTT broker.
     * @details Sets the Last Will, connects with credentials if configured,
     * subscribes to all relevant topics and publishes the online status and
     * full settings snapshot on success.
     * @return true if the connection was established successfully.
     */
    static bool connectToBroker()
    {
        String willTopic = topic("status/online");
        String clientId  = Settings::mqttClientId;

        LOG_INFO(LOG_MQTT, F("MQT: Connecting to broker "), Settings::mqttBroker + ":" + Settings::mqttPort);

        wifiClient.setTimeout(MQTT_TCP_TIMEOUT_MS / 1000); // WiFiClient::setTimeout takes seconds
        if (!wifiClient.connect(Settings::mqttBroker.c_str(), (uint16_t)Settings::mqttPort))
        {
            LOG_ERROR(LOG_MQTT, F("MQT: TCP connect timeout"));
            return false;
        }

        bool ok;
        if (Settings::mqttUser.length() > 0)
            ok = mqttClient.connect(clientId.c_str(),
                                    Settings::mqttUser.c_str(),
                                    Settings::mqttPassword.c_str(),
                                    willTopic.c_str(), 0, true, "0");
        else
            ok = mqttClient.connect(clientId.c_str(),
                                    nullptr, nullptr,
                                    willTopic.c_str(), 0, true, "0");

        if (!ok)
        {
            LOG_ERROR(LOG_MQTT, F("MQT: Connection failed, rc="), mqttClient.state());
            return false;
        }

        LOG_INFO(LOG_MQTT, F("MQT: Connected."));

        // Publish online status (retained so broker remembers it)
        mqttClient.publish(willTopic.c_str(), "1", true);

        // Subscribe to control topics
        mqttClient.subscribe((topic("set/#")).c_str());
        //mqttClient.subscribe((topic("get/#")).c_str());
        mqttClient.subscribe((topic("cmd/#")).c_str());

        // Publish full settings snapshot so FHEM has an up-to-date state
        publishAllSettings();

        return true;
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void init()
    {
        if (!Settings::mqttEnabled)
        {
            LOG_INFO(LOG_MQTT, F("MQT: MQTT disabled, skipping init."));
            return;
        }

        mqttClient.setServer(Settings::mqttBroker.c_str(), Settings::mqttPort);
        mqttClient.setCallback(onMessage);
        mqttClient.setBufferSize(512); // larger buffer for settings payloads
        connectRetries = 0;

        LOG_INFO(LOG_MQTT, F("MQT: MQTT client initialised."));
    }

    void update()
    {
        if (!Settings::mqttEnabled)
            return;

        // Maintain connection — reconnect with backoff if disconnected
        if (!mqttClient.connected())
        {
            if (connectRetries >= MQTT_MAX_RETRIES)
                return; // broker unreachable — gave up after MQTT_MAX_RETRIES attempts

            uint32_t now = millis();
            if (now - lastReconnectMs >= MQTT_RECONNECT_INTERVAL_MS)
            {
                lastReconnectMs = now;
                if (connectToBroker())
                    connectRetries = 0;
                else if (++connectRetries >= MQTT_MAX_RETRIES)
                    LOG_ERROR(LOG_MQTT, F("MQT: Broker unreachable — giving up after "), String(MQTT_MAX_RETRIES) + F(" attempts. Reboot to retry."));
            }
            return;
        }

        connectRetries = 0; // reset on established connection

        mqttClient.loop();

        // Deferred full-settings publish — requested from WebSocket callback context
        if (pendingPublishAll)
        {
            pendingPublishAll = false;
            publishAllSettings();
        }

        // Publish monitoring status once per second
        uint32_t now = millis();
        if (now - lastMonitorMs >= MQTT_MONITOR_INTERVAL_MS)
        {
            lastMonitorMs = now;
            mqttClient.publish(topic("status/lux").c_str(),
                               String(Brightness::getLux(), 1).c_str());
            mqttClient.publish(topic("status/current_mA").c_str(),
                               String(Renderer::getLastCurrentmA()).c_str());
            mqttClient.publish(topic("status/rssi").c_str(),
                               String(WiFi.RSSI()).c_str());
            mqttClient.publish(topic("status/uptime").c_str(),
                               String(millis() / 1000).c_str());
        }
    }

    void publishAllSettings()
    {
        // All known setting keys — publish current value to <base>/get/<key>
        static const char *keys[] = {
            "autoBrightness", "manualBrightness", "autoMin", "autoMax", "autoLuxMax",
            "ambientEnabled", "hourMarksEnabled", "quarterMarksEnabled",
            "ambientColor", "hourMarkColor", "quarterMarkColor",
            "motorMode", "motorGrid", "motorSpeed", "motorAccel",
            "sfxShortCircuitInterval", "sfxRadarInterval",
            "sfxShootingStarInterval", "sfxHeartbeatInterval", "sfxHeartbeatIntensity",
            "nightModeEnabled", "nightStart", "nightEnd",
            "nightBrightness", "nightFeatures",
            "powerLimit", "logLevel", "timezone",
            "mqttEnabled", "mqttBroker", "mqttPort", "mqttUser",
            "mqttClientId", "mqttTopicBase"
            // mqttPassword intentionally omitted — never publish credentials
        };

        for (const char *key : keys)
        {
            String v = getValue(key);
            if (v.length() > 0)
                publishValue(key, v);
        }
        LOG_DEBUG(LOG_MQTT, F("MQT: Full settings snapshot published."));
    }

    void requestPublishAll()
    {
        pendingPublishAll = true;
    }

    void publishValue(const char *key, const String &value)
    {
        if (!mqttClient.connected()) return;
        String t = topic("get/") + key;
        mqttClient.publish(t.c_str(), value.c_str());
    }

    bool isConnected()
    {
        return Settings::mqttEnabled && mqttClient.connected();
    }

} // namespace MQTT
