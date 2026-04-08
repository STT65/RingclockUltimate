/**
 * @file wifi_setup.h
 * @brief Wi-Fi setup and connection management.
 *
 * @details This module handles the Wi-Fi connection logic using the ESP8266's
 * internal flash memory for secure credential storage. It automatically switches
 * between Station (STA) mode for normal network operation and Access Point (AP)
 * mode with a captive portal for the initial configuration.
 */

#pragma once
#include <Arduino.h>

namespace WiFiSetup
{

    /**
     * @brief Indicates whether the device is currently operating in Access Point (AP) mode.
     * @details True if running as an AP (e.g., during setup), false if connected to a Wi-Fi network as a Station.
     */
    extern bool isAPMode;

    /**
     * @brief Initializes the Wi-Fi module and attempts to connect to a network.
     * @details Checks for stored credentials in the protected ESP8266 flash memory.
     * If credentials exist, it attempts to connect in Station (STA) mode.
     * If the connection fails or no credentials are found, it automatically falls back to AP mode.
     */
    void begin();

    /**
     * @brief Saves new Wi-Fi credentials securely to the internal flash memory.
     * @details Enables persistent storage and applies the new credentials using WiFi.begin().
     * * @param ssid The SSID (network name) of the Wi-Fi network.
     * @param pass The password of the Wi-Fi network.
     */
    void saveCredentials(const String &ssid, const String &pass);

    /**
     * @brief Starts the Access Point (AP) mode.
     * @details Generates a unique SSID based on the device's MAC address, configures the AP,
     * and starts the DNS server required for the captive portal functionality.
     */
    void startAP();

    /**
     * @brief Starts the Station (STA) mode.
     * @details Attempts to connect to the Wi-Fi network using the credentials
     * previously saved in the ESP8266's internal persistent flash memory.
     */
    void startStation();

    /**
     * @brief Checks if Wi-Fi credentials are saved in the internal flash memory.
     * @details Reads the stored SSID directly from the ESP8266 Wi-Fi configuration.
     * * @return true if an SSID is stored, false otherwise.
     */
    bool hasCredentials();

} // namespace WiFiSetup