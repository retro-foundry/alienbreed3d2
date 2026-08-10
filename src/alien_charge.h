#ifndef AB3D2_ALIEN_CHARGE_H
#define AB3D2_ALIEN_CHARGE_H

#include <stddef.h>
#include <stdint.h>

#include "alien_animation.h"
#include "alien_damage.h"
#include "alien_death.h"
#include "alien_flight.h"
#include "alien_run_around.h"
#include "alien_runtime.h"
#include "alien_setup.h"
#include "game_progression.h"
#include "game_random.h"
#include "level_dynamic_state.h"
#include "level_navigation.h"
#include "lighting_runtime.h"
#include "object_heading.h"
#include "object_movement.h"
#include "object_teleport.h"

/*
 * Shared source globals read by modules/ai.s:ai_ChargeCommon before it resets
 * its own movement state. The complete ItsAnAlien dispatcher will own this
 * cross-object workspace; this unbound mode deliberately receives it rather
 * than supplying a native replacement.
 */
typedef struct {
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int32_t new_y;
    uint8_t stood_in_top;
    uint8_t exit_first;
} AlienChargeWorkspace;

/* Source outputs from modules/ai.s:ai_Charge/ai_ChargeToSide. */
typedef struct {
    AlienAnimationState animation;
    AlienDamageState damage;
    AlienJustDiedState death;
    ObjectTeleportState teleport;
    ObjectHeading heading;
    ObjectMovementTrace movement;
    uint8_t damage_taken;
    uint8_t got_out;
    uint8_t hit_player;
    uint8_t hit_object;
    uint8_t damaged_player;
} AlienChargeState;

/*
 * modules/ai.s:ai_Charge and ai_ChargeToSide through their shared
 * ai_ChargeCommon body. `to_side` maps ai_ToSide_w; `frame_ticks` is the
 * source Anim_TempFrames_w word. The mode remains unbound until every route
 * of ItsAnAlien can run in source ObjectHandler order.
 */
int alien_charge_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t to_side, uint16_t frame_ticks, AlienChargeWorkspace *workspace,
    AlienChargeState *out_state, char *error, size_t error_size);

/*
 * modules/ai.s:ai_ChargeFlying and ai_ChargeToSideFlying through their
 * shared ai_ChargeFlyingCommon body. Unlike the ground pair, this source path
 * preserves its moving vertical position across ai_GetRoomStats, then calls
 * ai_FlyToPlayerHeight and does not require a grounded navigation query.
 */
int alien_charge_flying_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const AssetBlob *clips,
    const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t to_side, uint16_t frame_ticks, AlienChargeWorkspace *workspace,
    AlienChargeState *out_state, char *error, size_t error_size);

#endif
