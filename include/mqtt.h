/**
 * @file mqtt.h
 * @brief Optional MQTT client for remote monitoring and control.
 *
 * Provides bidirectional MQTT communication alongside the existing WebSocket
 * interface. All settings are accessible via MQTT topics; the client can be
 * enabled, disabled and configured from the web interface.
 *
 * Topic structure (base = Settings::mqttTopicBase, default "ringclock"):
 *
 * Subscribed (incoming commands):
 *   <base>/set/<key>     — write a single setting (e.g. ringclock/set/motorSpeed)
 *   <base>/get/<key>     — request the current value of a setting
 *   <base>/cmd/save                  — save settings to flash
 *   <base>/cmd/reboot                — reboot the device
 *   <base>/cmd/resync                — resynchronise the motor
 *   <base>/cmd/sfxShortCircuitTrigger — trigger short-circuit effect
 *   <base>/cmd/sfxRadarTrigger        — trigger radar scan effect
 *   <base>/cmd/sfxShootingStarTrigger — trigger shooting star effect
 *   <base>/cmd/sfxHeartbeatTrigger    — trigger heartbeat effect
 *
 * Published (outgoing state):
 *   <base>/status/online      — "1" on connect, "0" as Last Will (retained)
 *   <base>/status/lux         — ambient light [lux], published every second
 *   <base>/status/current_mA  — estimated LED current [mA]
 *   <base>/status/rssi        — WiFi signal strength [dBm]
 *   <base>/status/uptime      — system uptime [s]
 *   <base>/get/<key>          — current value, published in response to a get request
 *
 * On connect, the full current settings state is published to <base>/get/<key>
 * for each known key so that FHEM and other brokers have an up-to-date snapshot.
 *
 * Call order:
 * - Call init() once during application setup, after Settings::loadMqtt().
 * - Call update() on every Arduino loop iteration.
 */

#pragma once
#include <Arduino.h>

namespace MQTT
{
    /**
     * @brief Initialises the MQTT client and connects to the broker if enabled.
     * @details Configures the PubSubClient with the broker address, port, Last Will
     * and the incoming message callback. Does nothing if Settings::mqttEnabled is false.
     * Must be called after Settings::loadMqtt() and after WiFi is connected.
     */
    void init();

    /**
     * @brief Maintains the MQTT connection and processes incoming messages.
     * @details Handles automatic reconnection with exponential backoff.
     * Publishes the periodic monitoring status (lux, current, RSSI, uptime)
     * once per second. Must be called on every Arduino loop iteration.
     */
    void update();

    /**
     * @brief Publishes the complete current settings state to the broker.
     * @details Publishes one message per setting to <base>/get/<key>.
     * Called automatically on connect and can be triggered externally
     * (e.g. after settings change via web interface).
     */
    void publishAllSettings();

    /**
     * @brief Schedules a publishAllSettings() call for the next update() iteration.
     * @details Safe to call from any context (WebSocket callbacks, ISRs) because it
     * only sets a flag — the actual MQTT publish happens in the main-loop context
     * via update(), avoiding Soft WDT resets caused by blocking TCP I/O in async
     * callbacks.
     */
    void requestPublishAll();

    /**
     * @brief Publishes a single key/value pair to <base>/get/<key>.
     * @param key   Setting key (e.g. "motorSpeed").
     * @param value Value as string.
     */
    void publishValue(const char *key, const String &value);

    /**
     * @brief Returns true if the MQTT client is currently connected to the broker.
     * @return true = connected, false = disconnected or disabled.
     */
    bool isConnected();

} // namespace MQTT
