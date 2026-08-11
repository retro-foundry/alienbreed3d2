#ifndef AB3D2_ALIEN_DISPATCH_H
#define AB3D2_ALIEN_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "alien_attack.h"
#include "alien_charge.h"
#include "alien_main.h"
#include "alien_pause.h"
#include "alien_prowl.h"

/*
 * Caller-owned source globals which AI_MainRoutine's charge/approach routes
 * consume. objectmove.s:newx/newz remain process-global in
 * AlienRuntime.motion; keeping a second pair here would break the source
 * handoff into ai_DoTakeDamage's early ai_DoTorch call.
 */
typedef struct {
    AlienChargeWorkspace movement;
} AlienDispatchWorkspace;

typedef struct {
    AlienMainRoute route;
    AlienMainBehavior behavior;
    /* modules/ai.s:ai_JustDied's Msg_PushLine input, if this mode produced one. */
    AlienDeathNarrative narrative;
    AlienProwlState prowl;
    AlienChargeState charge;
    AlienPauseState pause;
    AlienHitscanAttackState hitscan;
    AlienProjectileAttackState projectile;
    AlienDeathState death;
    AlienDamageReactionState damage_reaction;
} AlienDispatchState;

/*
 * modules/ai.s:AI_MainRoutine and its ai_DoDefault/ai_DoResponse/
 * ai_DoFollowup selections. The caller has already performed ItsAnAlien's
 * source setup. ObjectHandler consumes any returned narrative request after
 * the selected source mode has completed.
 */
int alien_dispatch_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    const ObjectObservation *observation, GameAudioEvents *audio_events,
    uint16_t frame_ticks,
    AlienDispatchWorkspace *workspace, AlienDispatchState *out_state,
    char *error, size_t error_size);

#endif
