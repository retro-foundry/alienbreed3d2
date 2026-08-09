#ifndef AB3D2_OBJECT_COLLISION_H
#define AB3D2_OBJECT_COLLISION_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "object_runtime.h"

/*
 * Caller-owned registers/workspace for objectmove.s:Obj_DoCollision.  CollId
 * is a source ObjT point word: the routine uses it both to locate its source
 * ObjT at `Lvl_ObjectDataPtr_l + CollId * ObjT_SizeOf_l` and to skip matching
 * candidate point words.  This preserves that source contract rather than
 * treating it as a native slot ID.
 */
typedef struct {
    uint16_t collision_id;
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int32_t new_y;
    int32_t thing_height;
    uint8_t stood_in_top;
} ObjectCollisionTrace;

/*
 * objectmove.s:Obj_DoCollision. `source_a2_words` is the raw caller-owned
 * a2 table addressed as `2(a2, ObjT_TypeID_b * 8)` and
 * `4(a2, ObjT_TypeID_b * 8)`. Its producer is deliberately explicit: the
 * maintained AI paths do not establish one stable a2 table for every call.
 * `out_hit_wall` receives the source byte result, zero or $ff.
 */
int object_collision_check(const ObjectRuntime *objects, const GameLink *game_link,
                           const int16_t *source_a2_words, size_t source_a2_word_count,
                           const ObjectCollisionTrace *trace, uint8_t *out_hit_wall,
                           char *error, size_t error_size);

#endif
