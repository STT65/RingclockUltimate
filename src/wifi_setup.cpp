/**
 * @file wifi_setup.cpp
 * @brief Implementation of the Wi-Fi setup and connection management.
 */

#include "wifi_setup.h"
#include "config.h"
#include "captive_portal.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

namespace WiFiSetup
{

    bool isAPMode = false;

    // Generate a unique SSID with MAC address suffix
    static String buildName(const char *prefix)
    {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s-%02X%02X", prefix, mac[4], mac[5]);
        return String(buf);
    }

    bool hasCredentials()
    {
        // WiFi.SSID() returns the SSID stored internally in the flash memory.
        // If the length is greater than 0, we know that credentials exist.
        return WiFi.SSID().length() > 0;
    }

    void saveCredentials(const String &ssid, const String &pass)
    {
        Serial.println("Received Wi-Fi credentials. Saving internally...");
        // WiFi.persistent(true) ensures that begin() writes the data to the flash.
        WiFi.persistent(true);
        WiFi.hostname(buildName(WIFI_SSID).c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        // From this point on, the credentials are safely stored in the ESP.
    }


    void startAP()
    {
        Serial.println("Starting Access Point...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(buildName(AP_SSID).c_str(), AP_PASSWORD);
        Serial.print("AP active. SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());

        // Start the captive portal DNS
        CaptivePortal::start();
    }

    void startStation()
    {
        Serial.println("Starting STA mode...");
        Serial.print("Connecting to stored Wi-Fi: ");
        Serial.println(WiFi.SSID());
        WiFi.mode(WIFI_STA);
        WiFi.hostname(buildName(WIFI_SSID));
        WiFi.begin(); // without parameters the credentials safely stored in flash are used
    }

    void begin()
    {
        // Ensure that persistent storage is enabled
        WiFi.persistent(true);

        if (!hasCredentials())
        {
            Serial.println("No Wi-Fi credentials found in flash -> AP mode");
            isAPMode = true;
            startAP();
            return;
        }

        startStation();

        unsigned long start = millis();
        // Wait up to 8 seconds for a connection
        while (WiFi.status() != WL_CONNECTED && millis() - start < 8000)
        {
            Serial.print(".");
            delay(200);
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("Connected!");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            isAPMode = false;

            String hostname = buildName(WIFI_SSID);
            if (MDNS.begin(hostname.c_str()))
            {
                Serial.print("mDNS started: http://");
                Serial.print(hostname);
                Serial.println(".local");
            }
            else
            {
                Serial.println("mDNS start failed");
            }
        }
        else
        {
            Serial.println("Connection failed -> AP mode");
            // Disconnect the previous attempt, but do NOT erase the flash memory
            WiFi.disconnect(false);
            delay(200);

            isAPMode = true;
            startAP();
        }
    }

} // namespace WiFiSetup