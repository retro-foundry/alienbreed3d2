#ifndef AB3D2_ALIEN_PAUSE_H
#define AB3D2_ALIEN_PAUSE_H

#include <stddef.h>
#include <stdint.h>

#include "alien_damage.h"
#include "alien_dark.h"
#include "alien_decision.h"
#include "alien_death.h"
#include "alien_perception.h"
#include "alien_torch.h"

/* Source outputs consumed by modules/ai.s:ai_PauseBriefly's immediate caller. */
typedef struct {
    AlienAnimationState animation;
    AlienDamageState damage;
    AlienJustDiedState death;
    uint8_t damage_taken;
    uint8_t got_out;
} AlienPauseState;

/*
 * modules/ai.s:ai_PauseBriefly. `frame_ticks` is the source
 * Anim_TempFrames_w word captured by hires.s before gameplay updates.  The
 * caller must supply that captured value; this helper does not create a
 * native timing policy.
 */
int alien_pause_briefly_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    const LevelRuntime *level, const AssetBlob *clips, const GameLink *game_link,
    GameProgression *progression, ObjectExplosionRuntime *explosion_runtime,
    const GameMath *math, GameRandom *random, const PlayerRuntime *player,
    const AlienSetup *setup, uint16_t frame_ticks, AlienPauseState *out_state,
    char *error, size_t error_size);

#endif
