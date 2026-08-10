#ifndef AB3D2_ALIEN_DEATH_H
#define AB3D2_ALIEN_DEATH_H

#include <stddef.h>
#include <stdint.h>

#include "alien_animation.h"
#include "alien_setup.h"
#include "game_progression.h"
#include "level_runtime.h"
#include "object_explosion.h"

/* modules/ai.s:ai_DoDie's ai_GetOut_w and animation handoff. */
typedef struct {
    AlienAnimationState animation;
    uint8_t got_out;
} AlienDeathState;

/*
 * modules/ai.s:ai_DoDie.  The later AI dispatcher owns selecting this mode;
 * this helper only applies that source branch once it has been selected.
 */
int alien_death_update(ObjectRuntime *objects, uint32_t slot_index,
                       ObjectAnimationRuntime *animation_runtime,
                       const GameLink *game_link, const GameMath *math,
                       const LevelRuntime *level, const AlienSetup *setup,
                       uint16_t viewer_yaw, AlienDeathState *out_state,
                       char *error, size_t error_size);

/* Exact Msg_PushLine input published by modules/ai.s:ai_JustDied, if any. */
typedef struct {
    const uint8_t *bytes;
    uint16_t byte_count;
    uint16_t length_and_tag;
} AlienDeathNarrative;

typedef struct {
    AlienDeathNarrative narrative;
    uint8_t splat_type;
    uint8_t got_out;
    uint32_t fragment_count;
    uint32_t child_count;
} AlienJustDiedState;

/*
 * modules/ai.s:ai_JustDied. The source Msg_PushLine call is returned as an
 * exact narrative request. The source-order dispatcher returns it to
 * ObjectHandler, which hands it to the GPU-neutral message ring.
 */
int alien_death_just_died(ObjectRuntime *objects, uint32_t slot_index,
                          const LevelRuntime *level, const GameLink *game_link,
                          GameProgression *progression,
                          ObjectAnimationRuntime *animation_runtime,
                          ObjectExplosionRuntime *explosion_runtime,
                          const GameMath *math, GameRandom *random,
                          AlienJustDiedState *out_state,
                          char *error, size_t error_size);

#endif
