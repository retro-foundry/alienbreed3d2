#ifndef AB3D2_OBJECT_RUNTIME_H
#define AB3D2_OBJECT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"

enum {
    /* defs.i:ObjT_SizeOf_l and the Lvl_ObjectPointsPtr_l Vec2L record. */
    OBJECT_RUNTIME_SLOT_BYTE_COUNT = 64u,
    OBJECT_RUNTIME_POINT_BYTE_COUNT = 8u
};

/*
 * Owned, big-endian runtime storage mirroring the parts of twolev.bin that
 * Game_Begin later mutates. It includes the terminating ObjT slot so a future
 * ObjectHandler translation can retain the source's first-word -1 sentinel.
 * This module intentionally supplies no object update, collision, animation,
 * sprite, or projectile behavior.
 */
typedef struct {
    uint8_t *slot_bytes;
    uint32_t slot_count;
    uint32_t active_slot_count;
    uint8_t *point_bytes;
    uint32_t point_count;
} ObjectRuntime;

int object_runtime_init(ObjectRuntime *out_runtime, const LevelRuntime *level_runtime,
                        char *error, size_t error_size);
void object_runtime_destroy(ObjectRuntime *runtime);

/* Direct mutable source-layout views; the list terminator is a valid slot. */
int object_runtime_get_slot_bytes(ObjectRuntime *runtime, uint32_t slot_index,
                                  uint8_t **out_bytes);
int object_runtime_get_point_bytes(ObjectRuntime *runtime, uint32_t point_index,
                                   uint8_t **out_bytes);

#endif
