/**
 * @file webserver.cpp
 * @brief AsyncWebServer and WebSocket handler for the Ringclock Ultimate web interface.
 *
 * Provides the main HTTP server (port 80) and a WebSocket endpoint (/ws) for
 * real-time bidirectional communication with the browser-based configuration UI.
 *
 * Responsibilities:
 * - Serve static files from LittleFS (index.html, script.js, style.css).
 * - Push live monitoring data (lux, current, RSSI, uptime, ramp stats,
 *   MQTT connection status) to all connected clients once per second.
 * - Receive settings changes from the browser and apply them immediately.
 * - Handle system commands (save, reboot, resync, eraseWifi, SFX triggers).
 * - In AP mode: serve the Wi-Fi setup page and handle captive portal redirects.
 */

#include "webserver.h"
#include "wifi_setup.h"
#include "mqtt.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "brightness.h"
#include "night_mode.h"
#include "renderer.h"
#include "settings.h"
#include "motor.h"
#include "motor_homing.h"
#include "time_state.h"
#include "logging.h"
#include "layer_sfx.h"
#include "color.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// -------------------------------------------------------------------------
//  Internal helpers
// -------------------------------------------------------------------------

/**
 * @brief Processes completed Wi-Fi scan results and broadcasts them via WebSocket.
 * @details Only active in AP mode. Restarts the scan after each result set.
 */
static void processWifiScan()
{
    if (!WiFiSetup::isAPMode || ws.count() == 0)
        return;

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING || n < 0)
        return;

    StaticJsonDocument<512> doc;
    JsonArray arr = doc.createNestedArray("scan");
    for (int i = 0; i < n; i++)
    {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["enc"] = WiFi.encryptionType(i);
    }

    String json;
    serializeJson(doc, json);
    ws.textAll(json);

    WiFi.scanDelete();
    WiFi.scanNetworks(true); // restart scan while AP mode is active
}

// -------------------------------------------------------------------------
//  Monitoring broadcast
// -------------------------------------------------------------------------

/**
 * @brief Broadcasts live monitoring data to all connected WebSocket clients.
 * @details Called once per second from update(). Includes sensor readings,
 * ramp statistics and MQTT connection status.
 */
void sendMonitoring()
{
    StaticJsonDocument<512> doc;

    doc["lux"] = Brightness::getLux();
    doc["current_mA"] = Renderer::getLastCurrentmA();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["uptime"] = millis() / 1000;
    {
        const TimeState::TimeInfo &t = TimeState::get();
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.h, t.m, t.s);
        doc["localTime"] = buf;
    }
    doc["mqttConnected"] = MQTT::isConnected();

    // Ramp monitor — only included after the first ramp has completed
    const Motor::RampStats &rs = Motor::getRampStats();
    if (rs.valid)
    {
        doc["rampDurationMs"] = rs.durationMs;
        doc["rampStepsTotal"] = rs.stepsTotal;
        doc["rampMissedSteps"] = rs.missedSteps;
    }

    doc["nightActive"] = NightMode::isActive();

#if MOTOR_AH_EN
    {
        MotorHoming::State hs = MotorHoming::getHomingState();
        const char *hsStr = hs == MotorHoming::State::Idle      ? "idle"
                            : hs == MotorHoming::State::Travel  ? "travel"
                            : hs == MotorHoming::State::Measure ? "measure"
                            : hs == MotorHoming::State::Done    ? "done"
                                                                : "error";
        doc["homingState"] = hsStr;
    }
#endif

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

// -------------------------------------------------------------------------
//  Settings broadcast
// -------------------------------------------------------------------------

/**
 * @brief Sends the complete current settings state to a single WebSocket client.
 * @details Called when a client connects and sends a "getSettings" command.
 * @param client Target WebSocket client.
 */
static void buildSettingsJson(String &out)
{
    DynamicJsonDocument doc(2560);

    // Brightness
    doc["autoBrightness"] = Settings::autoBrightness;
    doc["manualBrightness"] = Settings::manualBrightness;
    doc["autoMin"] = Settings::autoMin;
    doc["autoMax"] = Settings::autoMax;
    doc["autoLuxMax"] = Settings::autoLuxMax;
    doc["powerLimit"] = Settings::powerLimit;
    doc["logLevel"] = (int)Log::getLevel();
    doc["timezone"] = Settings::timezone;

    // Ambient
    doc["ambientEnabled"] = Settings::ambientEnabled;
    doc["hourMarksEnabled"] = Settings::hourMarksEnabled;
    doc["quarterMarksEnabled"] = Settings::quarterMarksEnabled;
    doc["ambientColor"] = Color::hsvToHex(Settings::ambientColor);
    doc["hourMarkColor"] = Color::hsvToHex(Settings::hourMarkColor);
    doc["quarterMarkColor"] = Color::hsvToHex(Settings::quarterMarkColor);

    // Second hand
    doc["sHandCol"] = Color::hsvToHex(Settings::secondHand.handColor);
    doc["sTailStartCol"] = Color::hsvToHex(Settings::secondHand.tailStartColor);
    doc["sTailFwdEndCol"] = Color::hsvToHex(Settings::secondHand.tailFwdEndColor);
    doc["sTailBackEndCol"] = Color::hsvToHex(Settings::secondHand.tailBackEndColor);
    doc["sTailFwdLen"] = Settings::secondHand.tailFwdLength;
    doc["sTailBackLen"] = Settings::secondHand.tailBackLength;
    doc["sRingMask"] = Settings::secondHand.ringMask;

    // Minute hand
    doc["mHandCol"] = Color::hsvToHex(Settings::minuteHand.handColor);
    doc["mTailStartCol"] = Color::hsvToHex(Settings::minuteHand.tailStartColor);
    doc["mTailFwdEndCol"] = Color::hsvToHex(Settings::minuteHand.tailFwdEndColor);
    doc["mTailBackEndCol"] = Color::hsvToHex(Settings::minuteHand.tailBackEndColor);
    doc["mTailFwdLen"] = Settings::minuteHand.tailFwdLength;
    doc["mTailBackLen"] = Settings::minuteHand.tailBackLength;
    doc["mRingMask"] = Settings::minuteHand.ringMask;

    // Hour hand
    doc["hHandCol"] = Color::hsvToHex(Settings::hourHand.handColor);
    doc["hTailStartCol"] = Color::hsvToHex(Settings::hourHand.tailStartColor);
    doc["hTailFwdEndCol"] = Color::hsvToHex(Settings::hourHand.tailFwdEndColor);
    doc["hTailBackEndCol"] = Color::hsvToHex(Settings::hourHand.tailBackEndColor);
    doc["hTailFwdLen"] = Settings::hourHand.tailFwdLength;
    doc["hTailBackLen"] = Settings::hourHand.tailBackLength;
    doc["hRingMask"] = Settings::hourHand.ringMask;

    // SFX
    doc["sfxShortCircuitInterval"] = Settings::sfxShortCircuitInterval;
    doc["sfxRadarInterval"] = Settings::sfxRadarInterval;
    doc["sfxShootingStarInterval"] = Settings::sfxShootingStarInterval;
    doc["sfxHeartbeatInterval"] = Settings::sfxHeartbeatInterval;
    doc["sfxHeartbeatIntensity"] = Settings::sfxHeartbeatIntensity;

    // Night mode
    doc["nightModeEnabled"] = Settings::nightModeEnabled;
    doc["nightStart"] = Settings::nightStart;
    doc["nightEnd"] = Settings::nightEnd;
    doc["nightBrightness"] = Settings::nightBrightness;
    doc["nightFeatures"] = Settings::nightFeatures;
    doc["nightActive"] = NightMode::isActive(); // read-only status

    // Motor
    doc["motorMode"]        = Settings::motorMode;
    doc["motorGrid"]        = Settings::motorGrid;
    doc["motorSpeed"]       = Settings::motorSpeed;
    doc["motorAccel"]       = Settings::motorAccel;
#if MOTOR_AH_EN
    doc["motorAutoHoming"]  = Settings::motorAutoHoming;
    {
        MotorHoming::State hs = MotorHoming::getHomingState();
        const char *hsStr = hs == MotorHoming::State::Idle      ? "idle"
                            : hs == MotorHoming::State::Travel  ? "travel"
                            : hs == MotorHoming::State::Measure ? "measure"
                            : hs == MotorHoming::State::Done    ? "done"
                                                                : "error";
        doc["homingState"] = hsStr;
    }
#endif

    // MQTT (password intentionally omitted — never sent to browser)
    doc["mqttEnabled"] = Settings::mqttEnabled;
    doc["mqttBroker"] = Settings::mqttBroker;
    doc["mqttPort"] = Settings::mqttPort;
    doc["mqttUser"] = Settings::mqttUser;
    doc["mqttClientId"] = Settings::mqttClientId;
    doc["mqttTopicBase"] = Settings::mqttTopicBase;
    doc["mqttConnected"] = MQTT::isConnected();

    serializeJson(doc, out);
    LOG_JSON(LOG_WEB, "WEB: Settings sent to client", doc);
}

static void sendAllSettings(AsyncWebSocketClient *client)
{
    String json;
    buildSettingsJson(json);
    client->text(json);
}

// -------------------------------------------------------------------------
//  WebSocket event handler
// -------------------------------------------------------------------------

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT)
    {
        if (WiFiSetup::isAPMode)
        {
            LOG_INFO(LOG_WEB, F("WEB: Client connected in AP mode, starting WiFi scan."));
            WebServer::startWifiScan();
        }
        else
        {
            LOG_INFO(LOG_WEB, F("WEB: Client connected."));
        }
        return;
    }

    if (type == WS_EVT_DISCONNECT)
    {
        LOG_INFO(LOG_WEB, F("WEB: Client disconnected."));
        return;
    }

    if (type != WS_EVT_DATA)
        return;

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err)
    {
        LOG_ERROR(LOG_WEB, F("WEB: JSON deserialization error: "), err.c_str());
        return;
    }
    LOG_JSON(LOG_WEB, "WEB: Received", doc);

    bool motorConfigurationChanged = false;
    bool mqttChanged = false;
    bool settingsChanged = false;

    // ----------------------------------------------------------------
    // Brightness
    // ----------------------------------------------------------------
    if (doc.containsKey("autoBrightness"))
        Settings::autoBrightness = doc["autoBrightness"].as<bool>();
    if (doc.containsKey("manualBrightness"))
        Settings::manualBrightness = doc["manualBrightness"].as<int>();
    if (doc.containsKey("autoMin"))
        Settings::autoMin = doc["autoMin"].as<int>();
    if (doc.containsKey("autoMax"))
        Settings::autoMax = doc["autoMax"].as<int>();
    if (doc.containsKey("autoLuxMax"))
        Settings::autoLuxMax = doc["autoLuxMax"].as<int>();

    // ----------------------------------------------------------------
    // Ambient
    // ----------------------------------------------------------------
    if (doc.containsKey("ambientEnabled"))
        Settings::ambientEnabled = doc["ambientEnabled"].as<bool>();
    if (doc.containsKey("hourMarksEnabled"))
        Settings::hourMarksEnabled = doc["hourMarksEnabled"].as<bool>();
    if (doc.containsKey("quarterMarksEnabled"))
        Settings::quarterMarksEnabled = doc["quarterMarksEnabled"].as<bool>();
    if (doc.containsKey("ambientColor"))
        Settings::ambientColor = Color::hexToHsv(doc["ambientColor"].as<String>());
    if (doc.containsKey("hourMarkColor"))
        Settings::hourMarkColor = Color::hexToHsv(doc["hourMarkColor"].as<String>());
    if (doc.containsKey("quarterMarkColor"))
        Settings::quarterMarkColor = Color::hexToHsv(doc["quarterMarkColor"].as<String>());

    // ----------------------------------------------------------------
    // Second hand
    // ----------------------------------------------------------------
    if (doc.containsKey("sHandCol"))
        Settings::secondHand.handColor = Color::hexToHsv(doc["sHandCol"].as<String>());
    if (doc.containsKey("sTailStartCol"))
        Settings::secondHand.tailStartColor = Color::hexToHsv(doc["sTailStartCol"].as<String>());
    if (doc.containsKey("sTailFwdEndCol"))
        Settings::secondHand.tailFwdEndColor = Color::hexToHsv(doc["sTailFwdEndCol"].as<String>());
    if (doc.containsKey("sTailBackEndCol"))
        Settings::secondHand.tailBackEndColor = Color::hexToHsv(doc["sTailBackEndCol"].as<String>());
    if (doc.containsKey("sTailFwdLen"))
        Settings::secondHand.tailFwdLength = doc["sTailFwdLen"].as<int>();
    if (doc.containsKey("sTailBackLen"))
        Settings::secondHand.tailBackLength = doc["sTailBackLen"].as<int>();
    if (doc.containsKey("sRingMask"))
        Settings::secondHand.ringMask = doc["sRingMask"].as<uint8_t>();

    // ----------------------------------------------------------------
    // Minute hand
    // ----------------------------------------------------------------
    if (doc.containsKey("mHandCol"))
        Settings::minuteHand.handColor = Color::hexToHsv(doc["mHandCol"].as<String>());
    if (doc.containsKey("mTailStartCol"))
        Settings::minuteHand.tailStartColor = Color::hexToHsv(doc["mTailStartCol"].as<String>());
    if (doc.containsKey("mTailFwdEndCol"))
        Settings::minuteHand.tailFwdEndColor = Color::hexToHsv(doc["mTailFwdEndCol"].as<String>());
    if (doc.containsKey("mTailBackEndCol"))
        Settings::minuteHand.tailBackEndColor = Color::hexToHsv(doc["mTailBackEndCol"].as<String>());
    if (doc.containsKey("mTailFwdLen"))
        Settings::minuteHand.tailFwdLength = doc["mTailFwdLen"].as<int>();
    if (doc.containsKey("mTailBackLen"))
        Settings::minuteHand.tailBackLength = doc["mTailBackLen"].as<int>();
    if (doc.containsKey("mRingMask"))
        Settings::minuteHand.ringMask = doc["mRingMask"].as<uint8_t>();

    // ----------------------------------------------------------------
    // Hour hand
    // ----------------------------------------------------------------
    if (doc.containsKey("hHandCol"))
        Settings::hourHand.handColor = Color::hexToHsv(doc["hHandCol"].as<String>());
    if (doc.containsKey("hTailStartCol"))
        Settings::hourHand.tailStartColor = Color::hexToHsv(doc["hTailStartCol"].as<String>());
    if (doc.containsKey("hTailFwdEndCol"))
        Settings::hourHand.tailFwdEndColor = Color::hexToHsv(doc["hTailFwdEndCol"].as<String>());
    if (doc.containsKey("hTailBackEndCol"))
        Settings::hourHand.tailBackEndColor = Color::hexToHsv(doc["hTailBackEndCol"].as<String>());
    if (doc.containsKey("hTailFwdLen"))
        Settings::hourHand.tailFwdLength = doc["hTailFwdLen"].as<int>();
    if (doc.containsKey("hTailBackLen"))
        Settings::hourHand.tailBackLength = doc["hTailBackLen"].as<int>();
    if (doc.containsKey("hRingMask"))
        Settings::hourHand.ringMask = doc["hRingMask"].as<uint8_t>();

    // ----------------------------------------------------------------
    // SFX
    // ----------------------------------------------------------------
    if (doc.containsKey("sfxShortCircuitInterval"))
        Settings::sfxShortCircuitInterval = doc["sfxShortCircuitInterval"].as<uint16_t>();
    if (doc.containsKey("sfxRadarInterval"))
        Settings::sfxRadarInterval = doc["sfxRadarInterval"].as<uint16_t>();
    if (doc.containsKey("sfxShootingStarInterval"))
        Settings::sfxShootingStarInterval = doc["sfxShootingStarInterval"].as<uint16_t>();
    if (doc.containsKey("sfxHeartbeatInterval"))
        Settings::sfxHeartbeatInterval = doc["sfxHeartbeatInterval"].as<uint16_t>();
    if (doc.containsKey("sfxHeartbeatIntensity"))
        Settings::sfxHeartbeatIntensity = doc["sfxHeartbeatIntensity"].as<uint8_t>();

    // ----------------------------------------------------------------
    // Motor
    // ----------------------------------------------------------------
    if (doc.containsKey("motorMode"))
    {
        Settings::motorMode = doc["motorMode"].as<int>();
        motorConfigurationChanged = true;
    }
    if (doc.containsKey("motorGrid"))
    {
        Settings::motorGrid = doc["motorGrid"].as<int>();
        motorConfigurationChanged = true;
    }
    if (doc.containsKey("motorSpeed"))
    {
        Settings::motorSpeed = doc["motorSpeed"].as<int>();
        motorConfigurationChanged = true;
    }
    if (doc.containsKey("motorAccel"))
    {
        Settings::motorAccel = doc["motorAccel"].as<int>();
        motorConfigurationChanged = true;
    }
    if (doc.containsKey("motorAutoHoming"))
    {
#if MOTOR_AH_EN
        Settings::motorAutoHoming = doc["motorAutoHoming"].as<bool>();
#else
        Settings::motorAutoHoming = false; // clamp: sensor not compiled in
#endif
        motorConfigurationChanged = true;
    }
    if (motorConfigurationChanged)
        Motor::resync();

    // ----------------------------------------------------------------
    // Night mode
    // ----------------------------------------------------------------
    if (doc.containsKey("nightModeEnabled"))
        Settings::nightModeEnabled = doc["nightModeEnabled"].as<bool>();
    if (doc.containsKey("nightStart"))
        Settings::nightStart = doc["nightStart"].as<int>();
    if (doc.containsKey("nightEnd"))
        Settings::nightEnd = doc["nightEnd"].as<int>();
    if (doc.containsKey("nightBrightness"))
        Settings::nightBrightness = doc["nightBrightness"].as<int>();
    if (doc.containsKey("nightFeatures"))
        Settings::nightFeatures = doc["nightFeatures"].as<uint8_t>();

    // ----------------------------------------------------------------
    // System
    // ----------------------------------------------------------------
    if (doc.containsKey("powerLimit"))
        Settings::powerLimit = doc["powerLimit"].as<int>();
    if (doc.containsKey("logLevel"))
        Log::setLevel((Log::Level)doc["logLevel"].as<int>());
    if (doc.containsKey("timezone"))
        Settings::timezone = doc["timezone"].as<String>();

    // ----------------------------------------------------------------
    // MQTT settings
    // ----------------------------------------------------------------
    if (doc.containsKey("mqttEnabled"))
    {
        Settings::mqttEnabled = doc["mqttEnabled"].as<bool>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttBroker"))
    {
        Settings::mqttBroker = doc["mqttBroker"].as<String>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttPort"))
    {
        Settings::mqttPort = doc["mqttPort"].as<uint16_t>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttUser"))
    {
        Settings::mqttUser = doc["mqttUser"].as<String>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttPassword"))
    {
        Settings::mqttPassword = doc["mqttPassword"].as<String>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttClientId"))
    {
        Settings::mqttClientId = doc["mqttClientId"].as<String>();
        mqttChanged = true;
    }
    if (doc.containsKey("mqttTopicBase"))
    {
        Settings::mqttTopicBase = doc["mqttTopicBase"].as<String>();
        mqttChanged = true;
    }

    if (mqttChanged)
    {
        Settings::saveMqtt();
        MQTT::init(); // reinitialise with new parameters
        LOG_INFO(LOG_WEB, F("WEB: MQTT settings updated and saved."));
    }

    // If any settings key was present (not a pure command message), sync to FHEM.
    // requestPublishAll() is safe to call from the WebSocket async callback — it only
    // sets a flag; the actual publish runs in the main-loop context via MQTT::update().
    settingsChanged = !doc.containsKey("command");
    if (settingsChanged)
        MQTT::requestPublishAll();

    // ----------------------------------------------------------------
    // Commands
    // ----------------------------------------------------------------
    if (doc.containsKey("command"))
    {
        String cmd = doc["command"].as<String>();

        if (cmd == "save")
        {
            Settings::save();
            LOG_INFO(LOG_WEB, F("WEB: Settings saved to flash."));
        }
        else if (cmd == "reboot")
        {
            LOG_INFO(LOG_WEB, F("WEB: Reboot triggered."));
            delay(500);
            ESP.restart();
        }
        else if (cmd == "eraseWifi")
        {
            LOG_INFO(LOG_WEB, F("WEB: Erasing WiFi credentials and rebooting to AP mode."));
            WiFi.disconnect(true);
            delay(500);
            ESP.restart();
        }
        else if (cmd == "getSettings")
        {
            LOG_INFO(LOG_WEB, F("WEB: Sending settings to client."));
            sendAllSettings(client);
        }
        else if (cmd == "sfxShortCircuitTrigger")
            SFXLayer::sfxShortCircuitTrigger();
        else if (cmd == "sfxRadarTrigger")
            SFXLayer::sfxRadarTrigger();
        else if (cmd == "sfxShootingStarTrigger")
            SFXLayer::sfxShootingStarTrigger();
        else if (cmd == "sfxHeartbeatTrigger")
            SFXLayer::sfxHeartbeatTrigger();
        else if (cmd == "motorJog")
            MotorHoming::jog(doc["value"].as<int16_t>());
        else if (cmd == "motorAcceptPosition")
        {
            MotorHoming::acceptPosition();
            LOG_INFO(LOG_WEB, F("WEB: Motor calibration accepted and saved."));
        }
#if MOTOR_AH_EN
        else if (cmd == "motorStartHoming")
            MotorHoming::startHoming();
#endif
    }
}

// -------------------------------------------------------------------------
//  Captive portal routes (AP mode only)
// -------------------------------------------------------------------------

static void setupCaptiveRoutes()
{
    auto redirect = [](AsyncWebServerRequest *req)
    { req->redirect("/wifiSetup.html"); };

    server.on("/generate_204", HTTP_GET, redirect);        // Android
    server.on("/fwlink", HTTP_GET, redirect);              // Windows
    server.on("/connecttest.txt", HTTP_GET, redirect);     // Windows legacy
    server.on("/hotspot-detect.html", HTTP_GET, redirect); // Apple
    server.on("/ncsi.txt", HTTP_GET, redirect);            // Windows NCSI
    server.on("/success.html", HTTP_GET, redirect);        // Apple variant

    server.onNotFound([](AsyncWebServerRequest *req)
                      { req->redirect("/wifiSetup.html"); });
}

// -------------------------------------------------------------------------
//  Public API
// -------------------------------------------------------------------------

void WebServer::startWifiScan()
{
    LOG_INFO(LOG_WEB, F("WEB: Starting WiFi scan."));
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
}

void WebServer::init()
{
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    if (WiFiSetup::isAPMode)
    {
        LOG_INFO(LOG_WEB, F("WEB: Starting in AP / setup mode."));
        WiFi.scanNetworks(true); // start scan immediately — not dependent on WS connect
        setupCaptiveRoutes();
        server.serveStatic("/", LittleFS, "/").setDefaultFile("wifiSetup.html");

        // HTTP endpoint for WiFi scan results — reliable on all browsers incl. iOS Safari
        server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
            int n = WiFi.scanComplete();
            StaticJsonDocument<512> doc;
            JsonArray arr = doc.createNestedArray("scan");
            if (n > 0)
            {
                for (int i = 0; i < n; i++)
                {
                    JsonObject o = arr.createNestedObject();
                    o["ssid"] = WiFi.SSID(i);
                    o["rssi"] = WiFi.RSSI(i);
                    o["enc"]  = WiFi.encryptionType(i);
                }
            }
            doc["scanning"] = (n == WIFI_SCAN_RUNNING || n < 0);
            String json;
            serializeJson(doc, json);
            req->send(200, "application/json", json);
            if (n >= 0) { WiFi.scanDelete(); WiFi.scanNetworks(true); } // restart for next poll
        });

        server.on("/saveWifi", HTTP_POST, [](AsyncWebServerRequest *req)
                  {
            String ssid = req->getParam("ssid", true)->value();
            String pass = req->getParam("pass", true)->value();
            WiFiSetup::saveCredentials(ssid, pass);
            req->send(200, "text/plain", "OK - Rebooting...");
            delay(500);
            ESP.restart(); });
    }
    else
    {
        LOG_INFO(LOG_WEB, F("WEB: Starting in normal mode."));

        // HTTP endpoint for initial settings load — reliable on all browsers incl. iOS Safari.
        // CORS header allows fetch() from LiveServer (different origin) during development.
        server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
            String json;
            buildSettingsJson(json);
            AsyncWebServerResponse *res = req->beginResponse(200, "application/json", json);
            res->addHeader("Access-Control-Allow-Origin", "*");
            req->send(res);
        });

        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html")
              .setCacheControl("no-cache, no-store, must-revalidate");
    }

    server.begin();
    LOG_INFO(LOG_WEB, F("WEB: Server started on port 80."));
}

void WebServer::update()
{
    uint32_t now = millis();
    static uint32_t next = 0;
    if ((int32_t)(now - next) < 0)
        return;
    // Schedule next call 20ms after the start of the next second (NTP-synchronised)
    next = now + (1020 - TimeState::get().ms);

    if (WiFiSetup::isAPMode)
        processWifiScan(); // AP mode: Wi-Fi scan only
    else
        sendMonitoring(); // normal mode: monitoring only
}
