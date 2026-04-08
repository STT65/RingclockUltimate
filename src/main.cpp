/**
 * @file main.cpp
 * @brief Application entry point for the Ringclock Ultimate.
 *
 * Setup sequence:
 * 1. LittleFS mount
 * 2. WiFi (STA or AP mode)
 * 3. Settings load (settings.json + mqtt.json)
 * 4. WebServer + OTA
 * 5. Peripherals (Brightness, TimeState, Motor, Renderer)
 * 6. MQTT (after WiFi and Settings are ready)
 *
 * Loop:
 * - In AP mode: only DNS captive portal + WebServer are processed.
 * - In STA mode: full update cycle runs every iteration.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#include "settings.h"
#include "webserver.h"
#include "renderer.h"
#include "brightness.h"
#include "time_state.h"
#include "motor.h"
#include "ota_update.h"
#include "wifi_setup.h"
#include "captive_portal.h"
#include "night_mode.h"
#include "mqtt.h"

void setup()
{
    Serial.begin(115200);
    delay(200);

    if (!LittleFS.begin())
        Serial.println("LittleFS mount failed!");

    // WiFi — AP mode if no credentials stored, STA mode otherwise
    WiFiSetup::begin();

    // Load all settings (settings.json + mqtt.json + motor.json)
    Settings::load();
    Settings::loadMqtt();
    Settings::loadHoming();

    // WebServer first so the setup page is reachable immediately
    WebServer::init();
    OTAUpdate::init();

    Brightness::init();
    TimeState::init();
    Motor::init();
    Renderer::init();

    // MQTT last — requires WiFi connection and Settings to be ready
    MQTT::init();
}

void loop()
{
    if (WiFiSetup::isAPMode)
    {
        // AP mode: only DNS captive portal and web server needed
        CaptivePortal::process();
    }
    else
    {
        MDNS.update();
        TimeState::update();
        NightMode::update();
        Brightness::update();
        OTAUpdate::update();
        Renderer::renderFrame();
        Motor::update();
        MQTT::update();
    }

    // WebServer update runs in both modes (monitoring + WiFi scan)
    WebServer::update();
}
