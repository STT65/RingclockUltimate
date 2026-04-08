/**
 * @file ota_update.h
 * @brief Over-the-air (OTA) firmware and filesystem update via a dedicated HTTP server.
 *
 * Runs a separate synchronous ESP8266WebServer on port OTA_PORT alongside the
 * main AsyncWebServer. This is intentional: ESPAsyncWebServer buffers upload
 * chunks in heap memory which causes crashes on large binaries. The synchronous
 * server feeds the ESP8266 watchdog via handleClient() and streams chunks
 * directly to flash via the Updater API without intermediate buffering.
 *
 * Two update endpoints are provided:
 *
 * - http://<ip>:OTA_PORT/update   — flash new firmware (.bin)
 * - http://<ip>:OTA_PORT/updatefs — flash a new LittleFS image (.bin)
 *                                   (updates web interface files)
 *
 * Both endpoints serve a simple upload form. The correct Updater mode
 * (U_FLASH or U_FS) is selected automatically based on the endpoint path.
 *
 * Optional HTTP basic authentication is enabled when OTA_USERNAME and
 * OTA_PASSWORD are defined in config.h.
 *
 * Call order:
 * - Call init() once during application setup.
 * - Call update() on every Arduino loop iteration.
 */

#pragma once
#include <Arduino.h>

namespace OTAUpdate
{
    /**
     * @brief Initialises the OTA HTTP server and registers both update endpoints.
     * @details Starts a synchronous ESP8266WebServer on OTA_PORT, attaches
     * ESP8266HTTPUpdateServer to /update (firmware) and registers a manual
     * upload handler for /updatefs (LittleFS image). A root redirect to
     * /update is also registered for convenience.
     */
    void init();

    /**
     * @brief Processes pending OTA HTTP requests.
     * @details Calls ESP8266WebServer::handleClient() which also feeds the
     * watchdog timer, preventing resets during long uploads.
     * Must be called on every Arduino loop iteration.
     */
    void update();

} // namespace OTAUpdate
