#ifndef AB3D2_OBJECT_OBSERVATION_H
#define AB3D2_OBJECT_OBSERVATION_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"
#include "object_runtime.h"
#include "player_runtime.h"

enum {
    /* defs.i:MAX_LEVEL_OBJ_DIST_COUNT and MAX_OBJS_IN_LINE_COUNT. */
    OBJECT_OBSERVATION_DISTANCE_COUNT = 256u + 32u,
    OBJECT_OBSERVATION_IN_LINE_COUNT = 400u
};

/*
 * Source-shaped bss workspace written by modules/transform.s:CalcPLR1InLine.
 * It is simulation state consumed by newplayershoot.s, not renderer output.
 */
typedef struct {
    uint16_t distances[OBJECT_OBSERVATION_DISTANCE_COUNT];
    uint8_t in_line[OBJECT_OBSERVATION_IN_LINE_COUNT];
} ObjectObservation;

void object_observation_init(ObjectObservation *observation);

/*
 * Replays CalcPLR1InLine against the mutable ObjT and object-point buffers.
 * It deliberately transforms every source object point; no PVS or portal
 * visibility list participates in this gameplay workspace.
 */
int object_observation_update_single_player(ObjectObservation *observation,
                                             const ObjectRuntime *objects,
                                             const PlayerRuntime *player,
                                             const GameMath *math,
                                             char *error, size_t error_size);

#endif
