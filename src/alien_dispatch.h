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
 * Caller-owned source globals which AI_MainRoutine's selected routes consume.
 * ai_DoTakeDamage alone requires the retained newx/newz pair for ai_DoTorch;
 * the charge/approach paths also consume shared collision/teleport workspace.
 */
typedef struct {
    AlienChargeWorkspace movement;
    int16_t damage_torch_x;
    int16_t damage_torch_z;
} AlienDispatchWorkspace;

typedef struct {
    AlienMainRoute route;
    AlienMainBehavior behavior;
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
 * source setup and owns any later message consumption. This boundary stays
 * unbound until ObjectHandler receives the complete source tick context.
 */
int alien_dispatch_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    const ObjectObservation *observation, uint16_t frame_ticks,
    AlienDispatchWorkspace *workspace, AlienDispatchState *out_state,
    char *error, size_t error_size);

#endif
