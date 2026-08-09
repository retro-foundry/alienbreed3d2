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
 * is process-lifetime state and is not reset when a new level begins.
 */
typedef struct {
    uint8_t thistime;
    uint8_t workspace[OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT]
                     [OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT];
} ObjectAnimationRuntime;

/* Source BSS/data initialization before the first VBlank game update. */
void object_animation_runtime_init(ObjectAnimationRuntime *runtime);

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
