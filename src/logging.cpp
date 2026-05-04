/**
 * @file logging.cpp
 * @brief Implementation of the non-template logging functions.
 *
 * Only the functions that cannot be defined as templates in the header
 * are implemented here: setLevel(), getLevel(), canLog() and _startLine().
 * All templated log functions (info, error, debug, json) are defined
 * inline in logging.h.
 */

#include "logging.h"

namespace Log
{
    static Level currentLevel = (Level)LOG_LEVEL_DEFAULT; // Active log level; mirrors compile-time floor.

    void setLevel(Level lvl)
    {
        currentLevel = lvl;
    }

    Level getLevel()
    {
        return currentLevel;
    }

    bool canLog(Level lvl)
    {
        return currentLevel >= lvl;
    }

    void _startLine(const char *label)
    {
        Serial.print('[');
        Serial.print(label);
        Serial.print(']');
#if LOG_TIMESTAMP
        Serial.print('@');
        Serial.print(millis());
#endif
        Serial.print(' ');
    }

} // namespace Log
