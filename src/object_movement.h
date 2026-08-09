#ifndef AB3D2_OBJECT_MOVEMENT_H
#define AB3D2_OBJECT_MOVEMENT_H

#include <stddef.h>
#include <stdint.h>

#include "level_dynamic_state.h"

/*
 * Source workspace consumed by objectmove.s:MoveObject when Obj_ExtLen_w is
 * zero. newplayershoot.s:plr1_HitscanFailed uses exactly this configuration
 * to trace a miss through the authored collision graph.
 */
typedef struct {
    uint16_t zone_index;
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int32_t old_y;
    int32_t new_y;
    int32_t thing_height;
    int32_t step_up;
    int32_t step_down;
    uint16_t wall_flags;
    int8_t away_from_wall;
    uint8_t stood_in_top;
    uint8_t wall_bounce;
    uint8_t exit_first;
    uint8_t hit_wall;
    int32_t wall_hit_height;
    int16_t wall_x_size;
    int16_t wall_z_size;
    int16_t wall_length;
} ObjectMovementTrace;

/*
 * Direct objectmove.s:MoveObject translation for Obj_ExtLen_w == 0. It
 * mutates the source EdgeT flags in `dynamic_level` and returns the source
 * collision point, height, top/lower layer, and final zone through `trace`.
 */
int object_movement_trace_zero_extension(LevelDynamicState *dynamic_level,
                                         ObjectMovementTrace *trace,
                                         char *error, size_t error_size);

#endif
