#include "alien_dispatch.h"

#include <stdio.h>
#include <string.h>

static void alien_dispatch_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void alien_dispatch_publish_narrative(AlienDispatchState *state,
                                             const AlienJustDiedState *death)
{
    if (death->narrative.bytes) {
        state->narrative = death->narrative;
    }
}

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
    char *error, size_t error_size)
{
    AlienDispatchState state;
    uint8_t *slot;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !dynamic_level ||
        !clips || !game_link || !progression || !explosion_runtime || !math || !random ||
        !player || !setup || !observation || !workspace || !out_state ||
        slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_dispatch_set_error(error, error_size,
                                 "AI_MainRoutine dispatcher received invalid source state");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    if (!alien_main_route(objects, slot_index, &state.route, error, error_size) ||
        !alien_main_select_behavior(state.route, setup, &state.behavior, error, error_size)) {
        return 0;
    }

    switch (state.behavior) {
    case ALIEN_MAIN_BEHAVIOR_NONE:
        break;
    case ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM:
        if (!alien_prowl_random_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
                navigation, clips, game_link, progression, explosion_runtime, math, random,
                player, setup, 0u, frame_ticks, &state.prowl, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.prowl.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM_FLYING:
        if (!alien_prowl_random_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
                navigation, clips, game_link, progression, explosion_runtime, math, random,
                player, setup, UINT8_MAX, frame_ticks, &state.prowl, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.prowl.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_CHARGE:
    case ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE:
        if (!alien_charge_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
                navigation, clips, game_link, progression, explosion_runtime, math, random,
                player, setup,
                state.behavior == ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE ? UINT8_MAX : 0u,
                frame_ticks, &workspace->movement, &state.charge, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.charge.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_CHARGE_FLYING:
    case ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE_FLYING:
        if (!alien_charge_flying_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
                clips, game_link, progression, explosion_runtime, math, random, player, setup,
                state.behavior == ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE_FLYING ? UINT8_MAX : 0u,
                frame_ticks, &workspace->movement, &state.charge, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.charge.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN:
    case ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN_FLYING: {
        AlienAttackSetup attack;

        if (!alien_attack_setup_from_slot(objects, slot_index, game_link, &attack,
                                          error, error_size)) {
            return 0;
        }
        if (attack.is_hitscan != 0u) {
            if (!alien_attack_with_hitscan_update(
                    objects, slot_index, alien_runtime, animation_runtime, lighting,
                    dynamic_level, clips, game_link, progression, explosion_runtime, math,
                    random, player, setup, observation, &state.hitscan, error, error_size)) {
                return 0;
            }
            alien_dispatch_publish_narrative(&state, &state.hitscan.death);
        } else if (!alien_attack_with_projectile_update(
                       objects, slot_index, alien_runtime, animation_runtime, lighting,
                       &dynamic_level->runtime, clips, game_link, progression,
                       explosion_runtime, math, random, player, setup, audio_events,
                       &state.projectile, error, error_size)) {
            return 0;
        } else {
            alien_dispatch_publish_narrative(&state, &state.projectile.death);
        }
        break;
    }
    case ALIEN_MAIN_BEHAVIOR_PAUSE_BRIEFLY:
        if (!alien_pause_briefly_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting,
                &dynamic_level->runtime, clips, game_link, progression, explosion_runtime,
                math, random, player, setup, frame_ticks, &state.pause, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.pause.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_APPROACH:
    case ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE:
    case ALIEN_MAIN_BEHAVIOR_APPROACH_FLYING:
    case ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING:
        if (!alien_approach_update(
                objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
                navigation, clips, game_link, progression, explosion_runtime, math, random,
                player, setup,
                state.behavior == ALIEN_MAIN_BEHAVIOR_APPROACH_FLYING ||
                        state.behavior == ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING ?
                    UINT8_MAX : 0u,
                state.behavior == ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE ||
                        state.behavior == ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING ?
                    UINT8_MAX : 0u,
                frame_ticks, &workspace->movement, &state.charge, error, error_size)) {
            return 0;
        }
        alien_dispatch_publish_narrative(&state, &state.charge.death);
        break;
    case ALIEN_MAIN_BEHAVIOR_DIE:
        if (!alien_death_update(
                objects, slot_index, animation_runtime, game_link, math,
                &dynamic_level->runtime, setup, player->yaw, &state.death,
                error, error_size)) {
            return 0;
        }
        break;
    case ALIEN_MAIN_BEHAVIOR_TAKE_DAMAGE:
        if (!alien_damage_update_reaction(
                objects, slot_index, animation_runtime, game_link, math,
                &dynamic_level->runtime, lighting, player, setup,
                alien_runtime->motion.new_x, alien_runtime->motion.new_z,
                &state.damage_reaction, error, error_size)) {
            return 0;
        }
        /* HeadTowardsAng leaves its accepted target step in shared newx/newz. */
        alien_runtime->motion.new_x = state.damage_reaction.heading.new_x;
        alien_runtime->motion.new_z = state.damage_reaction.heading.new_z;
        break;
    }
    *out_state = state;
    return 1;
}
