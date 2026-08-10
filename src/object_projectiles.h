#ifndef AB3D2_OBJECT_PROJECTILES_H
#define AB3D2_OBJECT_PROJECTILES_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "game_random.h"
#include "level_dynamic_state.h"
#include "lighting_runtime.h"
#include "object_blast.h"
#include "object_motion.h"
#include "object_runtime.h"
#include "object_visibility.h"

/* Source process state consumed by ItsABullet's ComputeBlast call sites. */
typedef struct {
    ObjectBlastRuntime *blast_runtime;
    ObjectMotionRuntime *motion_runtime;
    ObjectVisibilityRuntime *visibility_runtime;
    const AssetBlob *clips;
    GameRandom *random;
} ObjectProjectileSourceRuntime;

/*
 * newanims.s:ItsABullet's ShotT_Status_b != 0 pop branch. This is the
 * stationary impact state created by newplayershoot.s:plr1_HitscanSucceded.
 */
int object_projectiles_update_impact_slot(ObjectRuntime *objects, uint32_t slot_index,
                                          const LevelDynamicState *dynamic_level,
                                          LightingRuntime *lighting_runtime,
                                          const GameLink *game_link,
                                          char *error, size_t error_size);

/*
 * newanims.s:ItsABullet's live (`notpopping`) path. It advances the source
 * lifetime, descriptor/frame, floor/roof response, zero-extension MoveObject
 * trace, wall response, direct target collision, and its immediate
 * anim_BrightenPoints call in source order. Native audio remains deferred.
 * The source-state overload also invokes ComputeBlast at the original roof,
 * floor, wall, timeout, and direct-target call sites.
 */
int object_projectiles_update_flight_animation_slot(ObjectRuntime *objects, uint32_t slot_index,
                                                     LevelDynamicState *dynamic_level,
                                                     LightingRuntime *lighting_runtime,
                                                     const GameLink *game_link,
                                                     uint16_t frame_ticks,
                                                     char *error, size_t error_size);

/* Same ItsABullet live path with its source newx/newz and blast state retained. */
int object_projectiles_update_flight_animation_slot_with_source_state(
    ObjectRuntime *objects, uint32_t slot_index, LevelDynamicState *dynamic_level,
    LightingRuntime *lighting_runtime, ObjectProjectileSourceRuntime *source_runtime,
    const GameLink *game_link, uint16_t frame_ticks, char *error, size_t error_size);

#endif
