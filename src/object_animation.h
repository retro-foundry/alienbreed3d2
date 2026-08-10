#ifndef AB3D2_OBJECT_ANIMATION_H
#define AB3D2_OBJECT_ANIMATION_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "game_random.h"
#include "object_runtime.h"

enum {
    /* bss/tables_bss.s:ObjectWorkspace_vl is ds.l 600, eight bytes per ObjT. */
    OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT = 300u,
    OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT = 8u
};

/*
 * Native ownership of bss/tables_bss.s:ObjectWorkspace_vl and hires.s:thistime.
 * This storage is shared with the later modules/ai.s animation consumers, so it
 * is process-lifetime state and is not reset when a new level begins. The
 * source DOALLANIMS loop has no 300-slot guard before its `addq #8,a5`.
 * Loaded authored lists may therefore continue past the fixed BSS prefix; the
 * native tail retains the same eight-byte-per-slot mapping without treating a
 * valid source terminator-delimited list as an error or writing past host memory.
 */
typedef struct {
    uint8_t thistime;
    uint8_t workspace[OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT]
                     [OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT];
    uint8_t *extended_workspace;
    uint32_t extended_workspace_slot_count;
} ObjectAnimationRuntime;

/* Source BSS/data initialization before the first VBlank game update. */
void object_animation_runtime_init(ObjectAnimationRuntime *runtime);
void object_animation_runtime_destroy(ObjectAnimationRuntime *runtime);

/* Ensure the source's terminator-delimited ObjT list has a matching workspace. */
int object_animation_runtime_reserve(ObjectAnimationRuntime *runtime, uint32_t slot_count,
                                     char *error, size_t error_size);
/* Returns the eight-byte source workspace for one ObjT slot, or NULL if unreserved. */
uint8_t *object_animation_runtime_workspace(ObjectAnimationRuntime *runtime,
                                            uint32_t slot_index);

/*
 * hires.s:DOALLANIMS, called first by hires.s:dosomething.  It preserves the
 * source five-tick low-byte counter, live ObjT sentinel/zone/worry gates, and
 * alien timer/action/special-frame state.  The source MakeSomeNoise call at
 * animation byte five remains absent until the native audio event path exists.
 */
int object_animation_update_single_player(ObjectAnimationRuntime *runtime,
                                          ObjectRuntime *objects,
                                          const GameLink *game_link,
                                          GameRandom *random,
                                          char *error, size_t error_size);

#endif
