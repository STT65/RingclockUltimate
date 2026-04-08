/**
 * @file captive_portal.cpp
 * @brief DNS-based captive portal for the Wi-Fi setup mode.
 *
 * Runs a DNS server that maps all domain names to the soft-AP IP address.
 * This causes devices connecting to the AP to automatically open the
 * Wi-Fi setup page in their browser (captive portal behaviour).
 */

#include "captive_portal.h"
#include "logging.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>

static DNSServer    dnsServer;          // DNS server instance.
static const byte   DNS_PORT = 53;      // Standard DNS port.

void CaptivePortal::start()
{
    IPAddress apIP = WiFi.softAPIP();
    LOG_INFO(LOG_CAP, "CAP: Captive Portal DNS on IP", apIP);

    // Redirect all domain queries to the AP IP address
    dnsServer.start(DNS_PORT, "*", apIP);
}

void CaptivePortal::process()
{
    dnsServer.processNextRequest();
}
