/**
 * @file captive_portal.h
 * @brief Public interface for the DNS-based captive portal.
 *
 * Redirects all DNS queries to the ESP8266 access point IP address so that
 * devices connecting to the AP are automatically directed to the Wi-Fi
 * setup page.
 *
 * Call order:
 * - Call start() once after WiFi.softAP() has been brought up.
 * - Call process() on every Arduino loop iteration while in AP mode.
 */

#pragma once

namespace CaptivePortal
{
    /**
     * @brief Starts the captive portal DNS server.
     * @details Binds a DNS server to port 53 and redirects all domain queries
     * to the soft-AP IP address, causing connecting devices to open the
     * Wi-Fi setup page automatically.
     */
    void start();

    /**
     * @brief Processes the next pending DNS request.
     * Must be called on every Arduino loop iteration while in AP mode.
     */
    void process();

} // namespace CaptivePortal
