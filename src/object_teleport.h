#ifndef AB3D2_OBJECT_TELEPORT_H
#define AB3D2_OBJECT_TELEPORT_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "level_runtime.h"
#include "object_collision.h"
#include "object_runtime.h"

/*
 * objectmove.s:CheckTeleport's observable global-state result. `zone_index`
 * is the source Obj_ZonePtr after the call: it remains `from_zone_index` on
 * a rejected teleport and changes only after a collision-free destination.
 * `floor_delta` is the source destination-minus-source floor value for any
 * teleport probe; a source zone without a teleport returns zero here.
 */
typedef struct {
    uint16_t zone_index;
    int32_t floor_delta;
    uint8_t teleported;
} ObjectTeleportState;

/*
 * Direct objectmove.s:CheckTeleport translation. It temporarily applies the
 * source ZoneT floor difference and source teleport X/Z to `trace`, invokes
 * Obj_DoCollision with the caller-established raw a2 words, then restores
 * new_y. The collider's ObjT zone is intentionally not changed here: source
 * callers update it later through ai_GetRoomStats.
 */
int object_teleport_check(const LevelRuntime *level, const ObjectRuntime *objects,
                          const GameLink *game_link, uint16_t from_zone_index,
                          const int16_t *source_a2_words, size_t source_a2_word_count,
                          ObjectCollisionTrace *trace, ObjectTeleportState *out_state,
                          char *error, size_t error_size);

#endif
