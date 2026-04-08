/**
 * @file logging.h
 * @brief Lightweight serial logging system with configurable verbosity levels.
 *
 * Provides four log levels (none, error, info, debug) and templated functions
 * so that any type supported by Serial.print() can be logged without casting.
 *
 * Two-tier filtering:
 * - **Compile-time floor** (@ref LOG_LEVEL_DEFAULT in config.h): levels below
 *   this value are replaced by empty inline stubs — no code, no flash strings.
 * - **Runtime gate** (Log::setLevel()): suppresses output within the levels
 *   that were compiled in.
 *
 * Usage examples:
 *
 * - Log::info(F("Motor: initialized"));
 * - Log::info("motorSpeed", Settings::motorSpeed);
 * - Log::error(F("BH1750 not found!"));
 * - Log::debug(String(F("PLL error=")) + errorPos);
 * - Log::json(Log::LOG_DEBUG, "Settings", doc);
 */

#pragma once
#include <Arduino.h>
#include "config.h"

namespace Log
{
    // -------------------------------------------------------------------------
    //  Log level
    // -------------------------------------------------------------------------

    /**
     * @brief Verbosity levels in ascending order.
     * Only messages at or below the active level are printed.
     */
    enum Level
    {
        LOG_NONE  = 0, // All output suppressed.
        LOG_ERROR = 1, // Errors only.
        LOG_INFO  = 2, // Errors and informational messages.
        LOG_DEBUG = 3  // All messages including debug output.
    };

    /**
     * @brief Sets the active log level.
     * @param lvl New log level; messages above this level are suppressed.
     */
    void setLevel(Level lvl);

    /**
     * @brief Returns the currently active log level.
     * @return Active Level value.
     */
    Level getLevel();

    /**
     * @brief Returns true if messages at @p lvl would currently be printed.
     * @param lvl Level to test.
     * @return true if lvl <= currentLevel.
     */
    bool canLog(Level lvl);

    // -------------------------------------------------------------------------
    //  Internal helper — not part of the public API
    // -------------------------------------------------------------------------

    /// @private
    void _startLine(const char *label);

    // -------------------------------------------------------------------------
    //  info()
    // -------------------------------------------------------------------------

    /**
     * @brief Logs an informational message (single argument).
     * @tparam T Any type supported by Serial.println().
     * @param msg Message to log.
     */
    template <typename T>
    void info(T msg)
    {
        if (canLog(LOG_INFO))
        {
            _startLine("INFO ");
            Serial.println(msg);
        }
    }

    /**
     * @brief Logs an informational message with a label and a value.
     * @details Prints as: [INFO ] prefix: val
     * @tparam T Type of the label (e.g. const char*, F()-string).
     * @tparam U Type of the value (e.g. int, float, String).
     * @param prefix Label describing the value.
     * @param val    Value to log.
     */
    template <typename T, typename U>
    void info(T prefix, U val)
    {
        if (canLog(LOG_INFO))
        {
            _startLine("INFO ");
            Serial.print(prefix);
            Serial.print(": ");
            Serial.println(val);
        }
    }

    // -------------------------------------------------------------------------
    //  error()
    // -------------------------------------------------------------------------

    /**
     * @brief Logs an error message (single argument).
     * @tparam T Any type supported by Serial.println().
     * @param msg Error message to log.
     */
    template <typename T>
    void error(T msg)
    {
        if (canLog(LOG_ERROR))
        {
            _startLine("ERROR");
            Serial.println(msg);
        }
    }

    /**
     * @brief Logs an error message with a label and a value.
     * @details Prints as: [ERROR] prefix: val
     * @tparam T Type of the label.
     * @tparam U Type of the value.
     * @param prefix Label describing the error context.
     * @param val    Value or detail to log.
     */
    template <typename T, typename U>
    void error(T prefix, U val)
    {
        if (canLog(LOG_ERROR))
        {
            _startLine("ERROR");
            Serial.print(prefix);
            Serial.print(": ");
            Serial.println(val);
        }
    }

    // -------------------------------------------------------------------------
    //  debug()
    //
    //  Compiled in only when LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG.
    //  Otherwise replaced by no-op stubs so call sites need no #ifdef guards.
    // -------------------------------------------------------------------------

#if LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG

    /**
     * @brief Logs a debug message (single argument).
     * @tparam T Any type supported by Serial.println().
     * @param msg Debug message to log.
     */
    template <typename T>
    void debug(T msg)
    {
        if (canLog(LOG_DEBUG))
        {
            _startLine("DEBUG");
            Serial.println(msg);
        }
    }

    /**
     * @brief Logs a debug message with a label and a value.
     * @details Prints as: [DEBUG] prefix: val
     * @tparam T Type of the label.
     * @tparam U Type of the value.
     * @param prefix Label describing the value.
     * @param val    Value to log.
     */
    template <typename T, typename U>
    void debug(T prefix, U val)
    {
        if (canLog(LOG_DEBUG))
        {
            _startLine("DEBUG");
            Serial.print(prefix);
            Serial.print(": ");
            Serial.println(val);
        }
    }

#else

    /// @brief No-op stub — debug output compiled out (LOG_LEVEL_DEFAULT < LOG_LEVEL_DEBUG).
    template <typename T>
    inline void debug(T) {}

    /// @brief No-op stub — debug output compiled out (LOG_LEVEL_DEFAULT < LOG_LEVEL_DEBUG).
    template <typename T, typename U>
    inline void debug(T, U) {}

#endif

    // -------------------------------------------------------------------------
    //  json()
    // -------------------------------------------------------------------------

    /**
     * @brief Logs a ArduinoJson document at the specified level.
     * @details Serializes the document to Serial in a single line.
     * @tparam T ArduinoJson document type (JsonDocument, StaticJsonDocument, etc.).
     * @param lvl    Level at which to log.
     * @param prefix Label printed before the JSON output.
     * @param doc    JSON document to serialize.
     */
    template <typename T>
    void json(Level lvl, const char *prefix, const T &doc)
    {
        if (canLog(lvl))
        {
            _startLine(lvl == LOG_ERROR ? "ERROR" : (lvl == LOG_DEBUG ? "DEBUG" : "INFO "));
            Serial.print(prefix);
            Serial.print(": ");
            serializeJson(doc, Serial);
            Serial.println();
        }
    }

} // namespace Log

// -------------------------------------------------------------------------
//  Per-module logging macros
//
//  Usage:  LOG_DEBUG(LOG_HOM, F("HOM: Enter zone @") + pos);
//          LOG_INFO (LOG_MOT, F("MOT: initialized"));
//          LOG_ERROR(LOG_WEB, F("WEB: connection failed"));
//
//  When the module constant is 0 the entire expression is eliminated by the
//  compiler — no code, no flash strings. LOG_DEBUG is additionally guarded by
//  LOG_LEVEL_DEFAULT so it disappears completely in non-debug builds.
// -------------------------------------------------------------------------

#if LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(mod, ...) do { if (mod) Log::debug(__VA_ARGS__); } while(0)
#else
  #define LOG_DEBUG(mod, ...) do {} while(0)
#endif

#define LOG_INFO( mod, ...) do { if (mod) Log::info (__VA_ARGS__); } while(0)
#define LOG_ERROR(mod, ...) do { if (mod) Log::error(__VA_ARGS__); } while(0)

#if LOG_LEVEL_DEFAULT >= LOG_LEVEL_DEBUG
  #define LOG_JSON(mod, prefix, doc) do { if (mod) Log::json(Log::LOG_DEBUG, prefix, doc); } while(0)
#else
  #define LOG_JSON(mod, prefix, doc) do {} while(0)
#endif
