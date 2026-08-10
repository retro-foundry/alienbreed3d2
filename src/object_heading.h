#ifndef AB3D2_OBJECT_HEADING_H
#define AB3D2_OBJECT_HEADING_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"

/* Source workspace for objectmove.s:HeadTowardsAng. */
typedef struct {
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int16_t range;
    int16_t speed;
    /* The source AngRet global remains unchanged for a zero-length proposal. */
    uint16_t angle;
    uint8_t got_there;
} ObjectHeading;

/* Source workspace shared by objectmove.s:CalcDist and HeadTowards. */
typedef struct {
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int16_t x_difference;
    int16_t z_difference;
    int16_t distance;
    int16_t range;
    int16_t speed;
    uint8_t got_there;
} ObjectApproach;

/* Direct objectmove.s:CalcDist translation. */
int object_heading_calculate_distance(ObjectApproach *approach,
                                      char *error, size_t error_size);

/* Direct objectmove.s:HeadTowards translation. */
int object_heading_towards(ObjectApproach *approach,
                           char *error, size_t error_size);

/*
 * Direct objectmove.s:HeadTowardsAng translation. It advances `new_x/new_z`
 * toward the original proposal and writes the source's coarse AngRet result.
 */
int object_heading_towards_angle(const GameMath *math, ObjectHeading *heading,
                                 char *error, size_t error_size);

#endif
