/**
 * @file ota_update.cpp
 * @brief OTA firmware and LittleFS filesystem update implementation.
 *
 * Provides two independent update paths on a dedicated synchronous
 * ESP8266WebServer (port OTA_PORT):
 *
 * /update   — firmware update via ESP8266HTTPUpdateServer (U_FLASH).
 *             Handled entirely by the library; no manual chunk management needed.
 *
 * /updatefs — LittleFS image update (U_FS), implemented manually using the
 *             ESP8266 Updater API. After a successful flash the device reboots
 *             automatically so the new web interface files take effect immediately.
 *
 * Both endpoints use HTTP basic authentication when OTA_USERNAME / OTA_PASSWORD
 * are defined in config.h. Leave them undefined (or define as empty strings) to
 * disable authentication.
 *
 * Why a separate synchronous server?
 * ESPAsyncWebServer buffers upload chunks in heap memory. A typical firmware
 * or LittleFS binary (~500 KB) exhausts the ESP8266 heap and causes a silent
 * hang or crash before the upload completes. The synchronous ESP8266WebServer
 * calls handleClient() from the main loop, which also feeds the watchdog timer,
 * so long uploads complete reliably without resets.
 */

#include "ota_update.h"
#include "logging.h"
#include "config.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>

#ifndef UPDATE_SIZE_UNKNOWN
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#endif

namespace OTAUpdate
{
    // -------------------------------------------------------------------------
    //  Module state
    // -------------------------------------------------------------------------

    static ESP8266WebServer otaServer(OTA_PORT); // Dedicated OTA HTTP server.

    // -------------------------------------------------------------------------
    //  Shared HTML page (served for both GET /update and GET /updatefs)
    // -------------------------------------------------------------------------

    static const char OTA_PAGE[] PROGMEM =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'><title>OTA Update</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{margin:0;padding:20px;background:#111;color:#eee;"
        "font-family:'Segoe UI',sans-serif;text-align:center}"
        ".c{background:#222;border-radius:8px;padding:20px;margin:0 auto 16px;"
        "max-width:500px;text-align:left;box-shadow:0 4px 10px rgba(0,0,0,.5)}"
        "h1{color:#eee;font-size:1.4rem}p.s{color:#888;margin:0 0 20px}"
        "h2{color:#4caf50;font-size:1rem;margin:0 0 8px}"
        "p{color:#888;font-size:.85rem;margin:0 0 10px}"
        "input[type=file]{width:100%;padding:8px;border-radius:4px;"
        "border:1px solid #333;background:#333;color:#eee;cursor:pointer}"
        "input[type=file]::file-selector-button{padding:5px 10px;border:none;"
        "border-radius:4px;background:#444;color:#eee;cursor:pointer;margin-right:8px}"
        "button{width:100%;margin-top:12px;padding:11px;border:none;"
        "border-radius:5px;background:#2e7d32;color:#fff;font-weight:bold;"
        "font-size:.95rem;cursor:pointer}"
        "button:hover{background:#4caf50}"
        "</style></head><body>"
        "<h1>Ringclock Ultimate</h1>"
        "<p class='s'>Over-the-Air Update</p>"
        "<div class='c'><h2>&#128421; Firmware</h2>"
        "<p>Select firmware.bin and click Flash.</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='update' accept='.bin' required>"
        "<button type='submit'>Flash Firmware</button>"
        "</form></div>"
        "<div class='c'><h2>&#128193; Filesystem</h2>"
        "<p>Select littlefs.bin to update the web interface.</p>"
        "<form method='POST' action='/updatefs' enctype='multipart/form-data'>"
        "<input type='file' name='updatefs' accept='.bin' required>"
        "<button type='submit'>Flash Filesystem</button>"
        "</form></div>"
        "</body></html>";

    // -------------------------------------------------------------------------
    //  Internal helpers — authentication wrapper
    // -------------------------------------------------------------------------

    static bool checkAuth()
    {
#if defined(OTA_USERNAME) && defined(OTA_PASSWORD)
        if (!otaServer.authenticate(OTA_USERNAME, OTA_PASSWORD))
        {
            otaServer.requestAuthentication();
            return false;
        }
#endif
        return true;
    }

    /**
     * @brief Serves the unified OTA update page (GET /update and GET /updatefs).
     * @details Streams the page in chunks of 512 bytes directly from PROGMEM
     * to avoid copying the full ~3 KB string into the ESP8266 heap at once.
     */
    static void handleUpdatePage()
    {
        if (!checkAuth()) return;

        const size_t pageLen   = strlen_P(OTA_PAGE);
        const size_t chunkSize = 512;
        char buf[chunkSize + 1];

        otaServer.setContentLength(pageLen);
        otaServer.send(200, "text/html", "");

        size_t offset = 0;
        while (offset < pageLen)
        {
            size_t len = min(chunkSize, pageLen - offset);
            memcpy_P(buf, OTA_PAGE + offset, len);
            buf[len] = 0;
            otaServer.sendContent(buf);
            offset += len;
        }
    }

    /**
     * @brief Handles the POST result for both firmware and LittleFS uploads.
     */
    static void handleUpdateResult()
    {
        bool ok = !Update.hasError();
        if (ok)
        {
            String page =
                F("<!DOCTYPE html><html><head>"
                  "<meta http-equiv='refresh' content='8;url=http://");
            page += WiFi.localIP().toString();
            page += F("/'></head><body style='background:#111;color:#eee;"
                      "font-family:sans-serif;text-align:center;padding-top:60px'>"
                      "<h2>Update successful!</h2>"
                      "<p>Device is rebooting&hellip; redirecting to web interface in 8 s.</p>"
                      "</body></html>");
            otaServer.send(200, "text/html", page);
        }
        else
        {
            otaServer.send(200, "text/plain", "Update FAILED. Check serial log.");
        }
        delay(500);
        ESP.restart();
    }

    /**
     * @brief Handles incoming upload chunks.
     * @details Selects U_FLASH or U_FS based on the field name in the multipart form.
     */
    static void handleUpload()
    {
        if (!checkAuth()) return;

        HTTPUpload &upload = otaServer.upload();

        if (upload.status == UPLOAD_FILE_START)
        {
            // Determine update type from the form field name
            int updateType = (upload.name == "updatefs") ? U_FS : U_FLASH;
            // For firmware updates use ESP.getFreeSketchSpace() to prevent the
            // updater from erasing beyond the OTA partition into LittleFS.
            size_t updateSize = (updateType == U_FLASH)
                                    ? ESP.getFreeSketchSpace()
                                    : UPDATE_SIZE_UNKNOWN;
            LOG_INFO(LOG_OTA, F("OTA: Upload started: "), upload.filename);
            if (!Update.begin(updateSize, updateType))
            {
                LOG_ERROR(LOG_OTA, F("OTA: Updater begin failed"));
                Update.printError(Serial);
            }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            if (!Update.hasError())
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                {
                    LOG_ERROR(LOG_OTA, F("OTA: Updater write error"));
                    Update.printError(Serial);
                }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            if (Update.end(true))
                LOG_INFO(LOG_OTA, F("OTA: Upload complete, size="), upload.totalSize);
            else
            {
                LOG_ERROR(LOG_OTA, F("OTA: Updater end failed"));
                Update.printError(Serial);
            }
        }
    }

    // -------------------------------------------------------------------------
    //  Public API
    // -------------------------------------------------------------------------

    void init()
    {
        LOG_INFO(LOG_OTA, F("OTA: Initialising update server on port "), OTA_PORT);

        // Both GET endpoints serve the same unified page
        otaServer.on("/",         HTTP_GET,  handleUpdatePage);
        otaServer.on("/update",   HTTP_GET,  handleUpdatePage);
        otaServer.on("/updatefs", HTTP_GET,  handleUpdatePage);

        // Both POST endpoints use the same upload handler — field name selects the mode
        otaServer.on("/update",   HTTP_POST, handleUpdateResult, handleUpload);
        otaServer.on("/updatefs", HTTP_POST, handleUpdateResult, handleUpload);

        otaServer.begin();
        LOG_INFO(LOG_OTA, F("OTA: Server ready at http://<ip>:"), OTA_PORT);
    }

    void update()
    {
        // handleClient() processes requests and feeds the ESP8266 watchdog.
        otaServer.handleClient();
    }

} // namespace OTAUpdate
