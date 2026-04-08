/**
 * @file circular_math.h
 * @brief Arithmetic helpers for circular (modular) position arithmetic.
 *
 * @details All functions operate on positions in the range
 * [0 .. MOTOR_STEPS_PER_REV-1] and correctly handle wrap-around at the
 * revolution boundary. Implemented as inline functions so this header can be
 * included in multiple translation units without linker conflicts.
 */

#pragma once
#include <Arduino.h>
#include "config.h"

/**
 * @brief Returns the shortest unsigned distance between two circular positions.
 *
 * The result is always positive and in the range [0 .. MOTOR_STEPS_PER_REV/2].
 *
 * @param a First position  (0 .. MOTOR_STEPS_PER_REV-1).
 * @param b Second position (0 .. MOTOR_STEPS_PER_REV-1).
 * @return Shortest distance in steps.
 */
inline uint16_t circularDistance(uint16_t a, uint16_t b)
{
    uint16_t diff = (a > b) ? (a - b) : (b - a);
    if (diff > MOTOR_STEPS_PER_REV / 2)
        diff = MOTOR_STEPS_PER_REV - diff;
    return diff;
}

/**
 * @brief Returns the signed shortest path from @p current to @p target.
 *
 * A positive result means the target is reached by moving forward;
 * a negative result means moving backward is shorter.
 *
 * @param target  Desired position  (0 .. MOTOR_STEPS_PER_REV-1).
 * @param current Current position  (0 .. MOTOR_STEPS_PER_REV-1).
 * @return Signed error in steps (range: -MOTOR_STEPS_PER_REV/2 .. +MOTOR_STEPS_PER_REV/2).
 */
inline int16_t circularError(uint16_t target, uint16_t current)
{
    int16_t error = (int16_t)target - (int16_t)current;
    const int16_t half = (int16_t)(MOTOR_STEPS_PER_REV / 2);
    if (error >  half) error -= (int16_t)MOTOR_STEPS_PER_REV;
    if (error < -half) error += (int16_t)MOTOR_STEPS_PER_REV;
    return error;
}

/**
 * @brief Adds a signed offset to a circular position, wrapping within
 *        [0 .. MOTOR_STEPS_PER_REV-1].
 *
 * @param pos    Base position (0 .. MOTOR_STEPS_PER_REV-1).
 * @param offset Signed offset in steps (positive or negative).
 * @return Resulting position, wrapped circularly.
 */
inline uint16_t circularAdd(uint16_t pos, int32_t offset)
{
    int32_t result = (int32_t)pos + offset;
    return (uint16_t)(((result % (int32_t)MOTOR_STEPS_PER_REV) +
                       (int32_t)MOTOR_STEPS_PER_REV) %
                      (int32_t)MOTOR_STEPS_PER_REV);
}

/**
 * @brief Calculates the midpoint between @p enter and @p exitPos along
 *        @p travelDir.
 *
 * Accounts for circular wrap-around: the width is computed in the travel
 * direction so the midpoint lies on the arc actually traversed.
 *
 * @param enter     Position where the zone was entered.
 * @param exitPos   Position where the zone was exited.
 * @param travelDir Direction of travel: +1 = forward, -1 = backward.
 * @return Midpoint position (0 .. MOTOR_STEPS_PER_REV-1).
 */
inline uint16_t circularMidpoint(uint16_t enter, uint16_t exitPos, int8_t travelDir)
{
    int16_t width = (int16_t)exitPos - (int16_t)enter;
    if (travelDir > 0 && width < 0) width += (int16_t)MOTOR_STEPS_PER_REV;
    if (travelDir < 0 && width > 0) width -= (int16_t)MOTOR_STEPS_PER_REV;
    int32_t mid = (int32_t)enter + (int32_t)(width / 2);
    return (uint16_t)((mid + MOTOR_STEPS_PER_REV) % MOTOR_STEPS_PER_REV);
}

/**
 * @brief Computes the circular average of @p count positions.
 *
 * Uses the first value as a reference and accumulates signed circular
 * differences to avoid wrap-around artefacts. Valid as long as all values
 * lie within half a revolution of one another.
 *
 * @param values Array of positions (0 .. MOTOR_STEPS_PER_REV-1).
 * @param count  Number of elements.
 * @return Circular average position (0 .. MOTOR_STEPS_PER_REV-1).
 */
inline uint16_t circularAverage(uint16_t *values, uint8_t count)
{
    if (count == 0)
        return 0;
    uint16_t      ref  = values[0];
    int32_t       sum  = 0;
    const int16_t half = (int16_t)(MOTOR_STEPS_PER_REV / 2);
    for (uint8_t i = 0; i < count; i++)
    {
        int16_t diff = (int16_t)values[i] - (int16_t)ref;
        if (diff >  half) diff -= (int16_t)MOTOR_STEPS_PER_REV;
        if (diff < -half) diff += (int16_t)MOTOR_STEPS_PER_REV;
        sum += diff;
    }
    int32_t result = (int32_t)ref + sum / (int32_t)count;
    return (uint16_t)((result + MOTOR_STEPS_PER_REV) % MOTOR_STEPS_PER_REV);
}
