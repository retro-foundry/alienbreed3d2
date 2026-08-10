#include "object_heading.h"

#include <limits.h>
#include <stdio.h>

static void object_heading_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int16_t object_heading_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_heading_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int16_t object_heading_neg16(int16_t value)
{
    return (int16_t)(0u - (uint16_t)value);
}

static int32_t object_heading_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_heading_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t object_heading_asr32_1(int32_t value)
{
    if (value >= 0) {
        return value / 2;
    }
    return (int32_t)(((uint32_t)value >> 1u) | UINT32_C(0x80000000));
}

static int16_t object_heading_asr16_1(int16_t value)
{
    if (value >= 0) {
        return (int16_t)(value / 2);
    }
    return (int16_t)(((uint16_t)value >> 1u) | UINT16_C(0x8000));
}

static int32_t object_heading_muls16(int16_t left, int16_t right)
{
    return (int32_t)((int64_t)left * right);
}

static int object_heading_divs16(int32_t dividend, int16_t divisor,
                                 int16_t *out_quotient,
                                 char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        object_heading_set_error(error, error_size,
                                 "object heading DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        object_heading_set_error(error, error_size,
                                 "object heading DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

/* objectmove.s:CalcDist/HeadTowards/HeadTowardsAng source Newton word iterations. */
static int object_heading_source_distance(int32_t squared_distance,
                                          uint8_t iteration_count,
                                          int16_t *out_distance,
                                          char *error, size_t error_size)
{
    uint32_t source_bits = (uint32_t)squared_distance;
    uint32_t distance;
    int highest_bit;

    for (highest_bit = 31; highest_bit >= 0; --highest_bit) {
        if ((source_bits & (UINT32_C(1) << (uint32_t)highest_bit)) != 0u) {
            break;
        }
    }
    if (highest_bit < 0) {
        *out_distance = 0;
        return 1;
    }
    distance = UINT32_C(1) << (uint32_t)(highest_bit / 2);
    for (uint8_t iteration = 0u; iteration < iteration_count; ++iteration) {
        int16_t distance_word = (int16_t)distance;
        int32_t correction = object_heading_sub32(
            object_heading_muls16(distance_word, distance_word), squared_distance);
        int16_t quotient;
        int16_t next_distance;

        correction = object_heading_asr32_1(correction);
        if (!object_heading_divs16(correction, distance_word, &quotient,
                                   error, error_size)) {
            return 0;
        }
        next_distance = object_heading_sub16(distance_word, quotient);
        distance = (distance & UINT32_C(0xffff0000)) | (uint16_t)next_distance;
        if ((int32_t)distance <= 0) {
            distance = 1u;
        }
    }
    *out_distance = (int16_t)distance;
    return 1;
}

int object_heading_calculate_distance(ObjectApproach *approach,
                                      char *error, size_t error_size)
{
    int32_t squared_distance;

    if (!approach) {
        object_heading_set_error(error, error_size, "CalcDist received invalid source state");
        return 0;
    }
    approach->x_difference = object_heading_sub16(approach->new_x, approach->old_x);
    approach->z_difference = object_heading_sub16(approach->new_z, approach->old_z);
    squared_distance = object_heading_add32(
        object_heading_muls16(approach->x_difference, approach->x_difference),
        object_heading_muls16(approach->z_difference, approach->z_difference));
    /* CalcDist clears distaway before its zero-length early return. */
    approach->distance = 0;
    if (squared_distance == 0) {
        return 1;
    }
    return object_heading_source_distance(squared_distance, 2u, &approach->distance,
                                          error, error_size);
}

int object_heading_towards(ObjectApproach *approach,
                           char *error, size_t error_size)
{
    int16_t movement;
    int16_t component;

    if (!object_heading_calculate_distance(approach, error, error_size)) {
        return 0;
    }
    /* HeadTowards returns here without changing GotThere when the points coincide. */
    if (approach->distance == 0) {
        return 1;
    }
    if (approach->distance <= approach->range) {
        approach->got_there = UINT8_MAX;
        /*
         * Unlike HeadTowardsAng, HeadTowards backtracks its existing target
         * by Range units instead of replacing it with the old point.
         */
        if (!object_heading_divs16(
                object_heading_muls16(approach->x_difference, approach->range),
                approach->distance, &component, error, error_size)) {
            return 0;
        }
        approach->new_x = object_heading_add16(approach->new_x,
                                                object_heading_neg16(component));
        if (!object_heading_divs16(
                object_heading_muls16(approach->z_difference, approach->range),
                approach->distance, &component, error, error_size)) {
            return 0;
        }
        approach->new_z = object_heading_add16(approach->new_z,
                                                object_heading_neg16(component));
        return 1;
    }

    approach->got_there = 0u;
    movement = object_heading_add16(approach->speed, approach->range);
    if (movement >= approach->distance) {
        movement = approach->distance;
        approach->got_there = UINT8_MAX;
    }
    movement = object_heading_sub16(movement, approach->range);
    if (!object_heading_divs16(
            object_heading_muls16(approach->x_difference, movement), approach->distance,
            &component, error, error_size)) {
        return 0;
    }
    approach->new_x = object_heading_add16(approach->old_x, component);
    if (!object_heading_divs16(
            object_heading_muls16(approach->z_difference, movement), approach->distance,
            &component, error, error_size)) {
        return 0;
    }
    approach->new_z = object_heading_add16(approach->old_z, component);
    return 1;
}

int object_heading_towards_angle(const GameMath *math, ObjectHeading *heading,
                                 char *error, size_t error_size)
{
    int16_t x_difference;
    int16_t z_difference;
    int32_t distance_squared;
    int16_t distance;
    int16_t movement;
    int16_t sine_ratio;
    int16_t cosine_ratio;
    int32_t fixed_x_difference;
    int32_t fixed_z_difference;
    int16_t angle_word = 0;
    int16_t angle_step = GAME_MATH_COSINE_OFFSET_BYTES;

    if (!math || !heading) {
        object_heading_set_error(error, error_size,
                                 "HeadTowardsAng received invalid source state");
        return 0;
    }
    x_difference = object_heading_sub16(heading->new_x, heading->old_x);
    z_difference = object_heading_sub16(heading->new_z, heading->old_z);
    distance_squared = object_heading_add32(
        object_heading_muls16(x_difference, x_difference),
        object_heading_muls16(z_difference, z_difference));
    heading->got_there = distance_squared == 0 ? UINT8_MAX : 0u;
    if (distance_squared == 0) {
        return 1;
    }
    if (!object_heading_source_distance(distance_squared, 3u, &distance, error, error_size)) {
        return 0;
    }

    /* objectmove.s compares signed Range then advances by speed beyond it. */
    if (distance <= heading->range) {
        heading->got_there = UINT8_MAX;
        heading->new_x = heading->old_x;
        heading->new_z = heading->old_z;
    } else {
        int16_t speed_plus_range = object_heading_add16(heading->speed, heading->range);

        if (speed_plus_range >= distance) {
            speed_plus_range = distance;
            heading->got_there = UINT8_MAX;
        }
        movement = object_heading_sub16(speed_plus_range, heading->range);
        if (!object_heading_divs16(object_heading_muls16(x_difference, movement), distance,
                                   &heading->new_x, error, error_size) ||
            !object_heading_divs16(object_heading_muls16(z_difference, movement), distance,
                                   &heading->new_z, error, error_size)) {
            return 0;
        }
        heading->new_x = object_heading_add16(heading->old_x, heading->new_x);
        heading->new_z = object_heading_add16(heading->old_z, heading->new_z);
    }

    fixed_x_difference = (int32_t)((uint32_t)(uint16_t)x_difference << 16u);
    fixed_z_difference = (int32_t)((uint32_t)(uint16_t)z_difference << 16u);
    if (!object_heading_divs16(object_heading_asr32_1(fixed_x_difference),
                               object_heading_add16(distance, 1), &sine_ratio,
                               error, error_size) ||
        !object_heading_divs16(object_heading_asr32_1(fixed_z_difference),
                               object_heading_add16(distance, 1), &cosine_ratio,
                               error, error_size)) {
        return 0;
    }
    for (uint8_t iteration = 0u; iteration < 4u; ++iteration) {
        int16_t table_sine;
        int16_t table_cosine;
        int32_t comparison;
        uint16_t table_angle = (uint16_t)((uint16_t)angle_word << 1u);

        if (!game_math_sine(math, table_angle, &table_sine, error, error_size) ||
            !game_math_cosine(math, table_angle, &table_cosine, error, error_size)) {
            return 0;
        }
        comparison = object_heading_sub32(
            object_heading_muls16(table_cosine, sine_ratio),
            object_heading_muls16(table_sine, cosine_ratio));
        if (comparison >= 0) {
            angle_word = object_heading_add16(angle_word, angle_step);
            angle_word = object_heading_add16(angle_word, angle_step);
        }
        angle_word = object_heading_sub16(angle_word, angle_step);
        angle_word = (int16_t)((uint16_t)angle_word & UINT16_C(4095));
        angle_step = object_heading_asr16_1(angle_step);
    }
    heading->angle = (uint16_t)object_heading_add16(angle_word, angle_word);
    return 1;
}
