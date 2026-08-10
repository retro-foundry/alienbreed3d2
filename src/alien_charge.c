#include "alien_charge.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "alien_decision.h"
#include "alien_dark.h"
#include "alien_memory.h"
#include "alien_perception.h"
#include "alien_spatial.h"
#include "alien_torch.h"
#include "object_collision.h"

enum {
    /* defs.i: ObjT/EntT fields used by modules/ai.s:ai_ChargeCommon. */
    ALIEN_CHARGE_SLOT_POINT_INDEX = 0u,
    ALIEN_CHARGE_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_CHARGE_SLOT_ZONE_ID = 12u,
    ALIEN_CHARGE_SLOT_SEES_PLAYER = 17u,
    ALIEN_CHARGE_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_CHARGE_SLOT_CURRENT_MODE = 20u,
    ALIEN_CHARGE_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_CHARGE_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_CHARGE_SLOT_TIMER1 = 34u,
    ALIEN_CHARGE_SLOT_TIMER2 = 40u,
    ALIEN_CHARGE_SLOT_WHICH_ANIMATION = 55u,
    ALIEN_CHARGE_SLOT_IN_UPPER_ZONE = 63u,
    ALIEN_CHARGE_PLAYER_DAMAGE_TAKEN = 19u,
    ALIEN_CHARGE_PLAYER_IMPACT_X = 42u,
    ALIEN_CHARGE_PLAYER_IMPACT_Z = 44u,
    ALIEN_CHARGE_STEP_UP = 32 * 256,
    ALIEN_CHARGE_STEP_DOWN = 30 * 256,
    ALIEN_CHARGE_FLY_STEP_DOWN = 1000 * 256,
    ALIEN_CHARGE_WALL_FLAGS = 0x0200u,
    ALIEN_CHARGE_ATTACK_MODE = 1u
};

static void alien_charge_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_charge_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_charge_read_be16s(const uint8_t *source)
{
    return (int16_t)alien_charge_read_be16(source);
}

static void alien_charge_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int16_t alien_charge_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t alien_charge_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int32_t alien_charge_asr32_1(int32_t value)
{
    if (value >= 0) {
        return value >> 1u;
    }
    return (int32_t)-(((int64_t)-value + 1) >> 1u);
}

static int32_t alien_charge_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7u;
    }
    return (int32_t)-(((int64_t)-value + 127) >> 7u);
}

static int alien_charge_divs16(int32_t dividend, int16_t divisor,
                                int16_t *out_quotient,
                                char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeCommon DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeCommon DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

static void alien_charge_add_facing(uint8_t *slot, uint16_t facing)
{
    alien_charge_write_be16(
        slot + ALIEN_CHARGE_SLOT_CURRENT_ANGLE,
        (uint16_t)(alien_charge_read_be16(slot + ALIEN_CHARGE_SLOT_CURRENT_ANGLE) + facing));
}

static int alien_charge_copy_previous_zone_pair(ObjectRuntime *objects, uint32_t slot_index,
                                                 uint8_t *slot,
                                                 char *error, size_t error_size)
{
    uint8_t *previous_slot;

    if (!slot || slot_index == 0u ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &previous_slot)) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeCommon has no preceding source auxiliary slot");
        return 0;
    }
    if (alien_charge_read_be16s(previous_slot + ALIEN_CHARGE_SLOT_ZONE_ID) >= 0) {
        alien_charge_write_be16(previous_slot + ALIEN_CHARGE_SLOT_ZONE_ID,
                                alien_charge_read_be16(slot + ALIEN_CHARGE_SLOT_ZONE_ID));
        alien_charge_write_be16(previous_slot + ALIEN_CHARGE_SLOT_ENTITY_ZONE_ID,
                                alien_charge_read_be16(
                                    slot + ALIEN_CHARGE_SLOT_ENTITY_ZONE_ID));
    }
    return 1;
}

static int alien_charge_apply_player_impact(ObjectRuntime *objects,
                                            const AlienAnimationState *animation,
                                            const AlienChargeWorkspace *workspace,
                                            uint16_t frame_ticks,
                                            AlienChargeState *state,
                                            char *error, size_t error_size)
{
    uint8_t *player_slot;
    int16_t impact_x;
    int16_t impact_z;
    int16_t frame_word = (int16_t)frame_ticks;

    if (!objects || !animation || !workspace || !state ||
        !object_runtime_get_player1_slot_bytes(objects, &player_slot)) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeCommon has no source Player 1 object");
        return 0;
    }
    player_slot[ALIEN_CHARGE_PLAYER_DAMAGE_TAKEN] =
        (uint8_t)(player_slot[ALIEN_CHARGE_PLAYER_DAMAGE_TAKEN] +
                  (uint8_t)(animation->action << 1u));
    if (!alien_charge_divs16(alien_charge_sub16(workspace->new_x, workspace->old_x),
                             frame_word, &impact_x, error, error_size) ||
        !alien_charge_divs16(alien_charge_sub16(workspace->new_z, workspace->old_z),
                             frame_word, &impact_z, error, error_size)) {
        return 0;
    }
    alien_charge_write_be16(
        player_slot + ALIEN_CHARGE_PLAYER_IMPACT_X,
        (uint16_t)alien_charge_add16(
            alien_charge_read_be16s(player_slot + ALIEN_CHARGE_PLAYER_IMPACT_X), impact_x));
    alien_charge_write_be16(
        player_slot + ALIEN_CHARGE_PLAYER_IMPACT_Z,
        (uint16_t)alien_charge_add16(
            alien_charge_read_be16s(player_slot + ALIEN_CHARGE_PLAYER_IMPACT_Z), impact_z));
    state->damaged_player = UINT8_MAX;
    return 1;
}

static int alien_charge_apply_player_damage(ObjectRuntime *objects,
                                            const AlienAnimationState *animation,
                                            AlienChargeState *state,
                                            char *error, size_t error_size)
{
    uint8_t *player_slot;

    if (!objects || !animation || !state ||
        !object_runtime_get_player1_slot_bytes(objects, &player_slot)) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeFlyingCommon has no source Player 1 object");
        return 0;
    }
    player_slot[ALIEN_CHARGE_PLAYER_DAMAGE_TAKEN] =
        (uint8_t)(player_slot[ALIEN_CHARGE_PLAYER_DAMAGE_TAKEN] +
                  (uint8_t)(animation->action << 1u));
    state->damaged_player = UINT8_MAX;
    return 1;
}

static int alien_charge_update_common(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t to_side, uint8_t flying, uint8_t approach, uint16_t frame_ticks,
    AlienChargeWorkspace *workspace,
    AlienChargeState *out_state, char *error, size_t error_size)
{
    const LevelRuntime *level;
    uint8_t *slot;
    uint8_t *point;
    uint16_t point_index;
    uint16_t zone_index;
    AlienChargeState state;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !dynamic_level ||
        !clips || !game_link || !progression || !explosion_runtime || !math ||
        !random || !player || !setup || !workspace || !out_state || slot_index == 0u ||
        slot_index >= objects->active_slot_count || slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        (flying == 0u && !navigation) ||
        player->zone_index >= dynamic_level->runtime.zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_charge_set_error(error, error_size, "ai_ChargeCommon received invalid source state");
        return 0;
    }
    if (!object_animation_runtime_reserve(animation_runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }
    level = &dynamic_level->runtime;
    point_index = alien_charge_read_be16(slot + ALIEN_CHARGE_SLOT_POINT_INDEX);
    zone_index = alien_charge_read_be16(slot + ALIEN_CHARGE_SLOT_ZONE_ID);
    if (point_index >= objects->point_count || zone_index >= level->zone_count ||
        !object_runtime_get_point_bytes(objects, point_index, &point)) {
        alien_charge_set_error(error, error_size,
                               "ai_ChargeCommon has an invalid source point or zone");
        return 0;
    }
    memset(&state, 0, sizeof(state));

    if (slot[ALIEN_CHARGE_SLOT_DAMAGE_TAKEN] != 0u) {
        state.damage_taken = UINT8_MAX;
        if (!alien_damage_take(objects, slot_index, alien_runtime, animation_runtime, math,
                               random, player, &state.damage, error, error_size)) {
            return 0;
        }
        if (state.damage.route == ALIEN_DAMAGE_ROUTE_JUST_DIED) {
            if (!alien_death_just_died(objects, slot_index, level, game_link, progression,
                                       animation_runtime, explosion_runtime, math, random,
                                       &state.death, error, error_size)) {
                return 0;
            }
            state.got_out = state.death.got_out;
        } else {
            state.got_out = state.damage.got_out;
        }
        if (state.got_out != 0u) {
            *out_state = state;
            return 1;
        }
    }

    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, setup, player->yaw,
            &state.animation, error, error_size)) {
        return 0;
    }

    {
        ObjectCollisionTrace teleport_trace;

        memset(&teleport_trace, 0, sizeof(teleport_trace));
        teleport_trace.collision_id = point_index;
        teleport_trace.old_x = workspace->old_x;
        teleport_trace.old_z = workspace->old_z;
        teleport_trace.new_x = workspace->new_x;
        teleport_trace.new_z = workspace->new_z;
        teleport_trace.new_y = workspace->new_y;
        teleport_trace.thing_height = setup->thing_height;
        teleport_trace.stood_in_top = workspace->stood_in_top;
        if (!object_teleport_check(
                level, objects, game_link, zone_index, state.animation.collision_a2_words,
                sizeof(state.animation.collision_a2_words) /
                    sizeof(state.animation.collision_a2_words[0u]),
                &teleport_trace, &state.teleport, error, error_size)) {
            return 0;
        }
        workspace->new_x = teleport_trace.new_x;
        workspace->new_z = teleport_trace.new_z;
        workspace->new_y = teleport_trace.new_y;
    }

    if (state.teleport.teleported != 0u) {
        int16_t teleported_vertical_position = alien_charge_add16(
            (int16_t)alien_charge_read_be16(slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION),
            (int16_t)alien_charge_asr32_7(state.teleport.floor_delta));
        alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION,
                                (uint16_t)teleported_vertical_position);
        zone_index = state.teleport.zone_index;
    } else {
        int16_t player_sine;
        int16_t player_cosine;
        ObjectCollisionTrace collision;

        workspace->new_x = player_runtime_position_to_world(player->x);
        workspace->new_z = player_runtime_position_to_world(player->z);
        if (to_side != 0u) {
            AlienRunAroundState run_around;

            if (!game_math_sine(math, player->yaw, &player_sine, error, error_size) ||
                !game_math_cosine(math, player->yaw, &player_cosine, error, error_size)) {
                return 0;
            }
            run_around.old_x = workspace->old_x;
            run_around.old_z = workspace->old_z;
            run_around.new_x = workspace->new_x;
            run_around.new_z = workspace->new_z;
            run_around.player_sine = player_sine;
            run_around.player_cosine = player_cosine;
            run_around.player_temporary_x = player_runtime_position_to_world(player->tmp_x);
            run_around.player_temporary_z = player_runtime_position_to_world(player->tmp_z);
            run_around.object_x = alien_charge_read_be16s(point);
            run_around.object_z = alien_charge_read_be16s(point + 4u);
            if (!alien_run_around_apply(&run_around, error, error_size)) {
                return 0;
            }
            workspace->new_x = run_around.new_x;
            workspace->new_z = run_around.new_z;
        }

        workspace->old_x = alien_charge_read_be16s(point);
        workspace->old_z = alien_charge_read_be16s(point + 4u);
        memset(&state.heading, 0, sizeof(state.heading));
        state.heading.old_x = workspace->old_x;
        state.heading.old_z = workspace->old_z;
        state.heading.new_x = workspace->new_x;
        state.heading.new_z = workspace->new_z;
        if (approach != 0u) {
            state.heading.speed = state.animation.action == 0u ? 0 :
                (int16_t)((int32_t)(int16_t)(state.animation.action << 2u) *
                          setup->followup_speed);
        } else {
            state.heading.speed = (int16_t)((int32_t)setup->response_speed *
                                            (int16_t)frame_ticks);
        }
        state.heading.range = 160;
        state.heading.angle = alien_runtime->heading_angle;
        if (!object_heading_towards_angle(math, &state.heading, error, error_size)) {
            return 0;
        }
        alien_runtime->heading_angle = state.heading.angle;
        workspace->new_x = state.heading.new_x;
        workspace->new_z = state.heading.new_z;
        workspace->new_y =
            (int32_t)alien_charge_read_be16s(slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION) * 128 -
            alien_charge_asr32_1(setup->thing_height);

        memset(&state.movement, 0, sizeof(state.movement));
        state.movement.zone_index = zone_index;
        state.movement.old_x = workspace->old_x;
        state.movement.old_z = workspace->old_z;
        state.movement.new_x = workspace->new_x;
        state.movement.new_z = workspace->new_z;
        state.movement.old_y = workspace->new_y;
        state.movement.new_y = workspace->new_y;
        state.movement.thing_height = setup->thing_height;
        state.movement.step_up = ALIEN_CHARGE_STEP_UP;
        state.movement.step_down = flying != 0u ? ALIEN_CHARGE_FLY_STEP_DOWN :
                                                  ALIEN_CHARGE_STEP_DOWN;
        state.movement.extension_length = setup->extended_wall_length;
        state.movement.wall_flags = ALIEN_CHARGE_WALL_FLAGS;
        state.movement.away_from_wall = setup->away_from_wall;
        state.movement.stood_in_top = slot[ALIEN_CHARGE_SLOT_IN_UPPER_ZONE];
        state.movement.exit_first = workspace->exit_first;

        memset(&collision, 0, sizeof(collision));
        collision.collision_id = point_index;
        collision.old_x = workspace->old_x;
        collision.old_z = workspace->old_z;
        collision.new_x = workspace->new_x;
        collision.new_z = workspace->new_z;
        collision.new_y = workspace->new_y;
        collision.thing_height = setup->thing_height;
        collision.stood_in_top = state.movement.stood_in_top;
        if (!object_collision_check(objects, game_link, state.animation.collision_a2_words,
                                    sizeof(state.animation.collision_a2_words) /
                                        sizeof(state.animation.collision_a2_words[0u]),
                                    &collision, &state.hit_player, error, error_size)) {
            return 0;
        }
        if (state.hit_player != 0u) {
            workspace->new_x = workspace->old_x;
            workspace->new_z = workspace->old_z;
            state.movement.new_x = workspace->new_x;
            state.movement.new_z = workspace->new_z;
            state.heading.got_there = UINT8_MAX;
        } else if (!object_collision_check(
                       objects, game_link, state.animation.collision_a2_words,
                       sizeof(state.animation.collision_a2_words) /
                           sizeof(state.animation.collision_a2_words[0u]),
                       &collision, &state.hit_object, error, error_size)) {
            return 0;
        } else if (state.hit_object != 0u) {
            workspace->new_x = workspace->old_x;
            workspace->new_z = workspace->old_z;
            state.movement.new_x = workspace->new_x;
            state.movement.new_z = workspace->new_z;
        } else {
            if (!object_movement_trace(dynamic_level, &state.movement, error, error_size)) {
                return 0;
            }
            workspace->new_x = state.movement.new_x;
            workspace->new_z = state.movement.new_z;
            workspace->new_y = state.movement.new_y;
            workspace->stood_in_top = state.movement.stood_in_top;
            zone_index = state.movement.zone_index;
            slot[ALIEN_CHARGE_SLOT_IN_UPPER_ZONE] = state.movement.stood_in_top;
            alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_CURRENT_ANGLE,
                                    state.heading.angle);
        }
    }

    /* A successful CheckTeleport branches directly to .no_munch. */
    if ((approach != 0u || flying == 0u) && state.teleport.teleported == 0u &&
        !alien_charge_copy_previous_zone_pair(objects, slot_index, slot, error, error_size)) {
        return 0;
    }
    if (approach == 0u && state.heading.got_there != 0u && state.animation.action != 0u) {
        if (flying != 0u) {
            if (!alien_charge_apply_player_damage(objects, &state.animation, &state,
                                                  error, error_size)) {
                return 0;
            }
        } else if (!alien_charge_apply_player_impact(objects, &state.animation, workspace,
                                                      frame_ticks, &state, error, error_size)) {
            return 0;
        }
    }
    if (approach != 0u) {
        int16_t flying_vertical_position;

        if (!alien_memory_store_player_position(alien_runtime, objects, slot_index, level, player,
                                                error, error_size)) {
            return 0;
        }
        if (flying != 0u &&
            !alien_flight_move_toward_player_height(
                objects, slot_index, level, zone_index, player, setup->thing_height,
                error, error_size)) {
            return 0;
        }
        flying_vertical_position = alien_charge_read_be16s(
            slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION);
        if (!alien_spatial_store_room_stats(objects, slot_index, level, zone_index,
                                            workspace->new_x, workspace->new_z,
                                            setup->thing_height, error, error_size) ||
            !alien_spatial_store_current_control_point(objects, slot_index, level, zone_index,
                                                       error, error_size)) {
            return 0;
        }
        if (flying != 0u) {
            alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION,
                                    (uint16_t)flying_vertical_position);
        }
        if (!alien_torch_apply(lighting, level, math, objects, slot_index, setup,
                               workspace->new_x, workspace->new_z, error, error_size)) {
            return 0;
        }
        slot[ALIEN_CHARGE_SLOT_CURRENT_MODE] = 0u;
        if (flying == 0u) {
            uint8_t can_attack;

            if (!alien_decision_check_attack_on_ground(objects, slot_index, level, navigation,
                                                       player, &can_attack, error, error_size)) {
                return 0;
            }
            if (can_attack == 0u) {
                slot[ALIEN_CHARGE_SLOT_WHICH_ANIMATION] = 0u;
                alien_charge_add_facing(slot, state.animation.facing);
                *out_state = state;
                return 1;
            }
        }
        if (!alien_perception_look_for_player_one(alien_runtime, objects, slot_index,
                                                  level, clips, player, zone_index,
                                                  workspace->new_x, workspace->new_z,
                                                  error, error_size)) {
            return 0;
        }
        if (slot[ALIEN_CHARGE_SLOT_SEES_PLAYER] != 0u) {
            uint8_t in_front;

            if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                               error, error_size)) {
                return 0;
            }
            if (in_front != 0u) {
                int16_t timer = alien_charge_sub16(
                    alien_charge_read_be16s(slot + ALIEN_CHARGE_SLOT_TIMER1),
                    (int16_t)frame_ticks);

                slot[ALIEN_CHARGE_SLOT_CURRENT_MODE] = 2u;
                alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_TIMER1, (uint16_t)timer);
                if (timer <= 0) {
                    LevelZone player_zone;
                    int16_t dark_result;

                    if (!level_runtime_get_zone(level, player->zone_index, &player_zone,
                                                error, error_size) ||
                        !alien_dark_check(objects, slot_index, player_zone.id,
                                          player->room_brightness, random, &dark_result,
                                          error, error_size)) {
                        return 0;
                    }
                    if (dark_result != 0) {
                        slot[ALIEN_CHARGE_SLOT_CURRENT_MODE] = ALIEN_CHARGE_ATTACK_MODE;
                        alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_TIMER2, 0u);
                        slot[ALIEN_CHARGE_SLOT_WHICH_ANIMATION] = 1u;
                        alien_charge_add_facing(slot, state.animation.facing);
                        *out_state = state;
                        return 1;
                    }
                }
            }
        }
        slot[ALIEN_CHARGE_SLOT_WHICH_ANIMATION] = 0u;
        alien_charge_add_facing(slot, state.animation.facing);
        *out_state = state;
        return 1;
    }
    {
        int16_t flying_vertical_position = alien_charge_read_be16s(
            slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION);

        if (!alien_memory_store_player_position(alien_runtime, objects, slot_index, level, player,
                                                error, error_size) ||
        !alien_spatial_store_room_stats(objects, slot_index, level, zone_index,
                                        workspace->new_x, workspace->new_z,
                                        setup->thing_height, error, error_size) ||
        !alien_spatial_store_current_control_point(objects, slot_index, level, zone_index,
                                                   error, error_size)) {
            return 0;
        }
        if (flying != 0u) {
            alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_VERTICAL_POSITION,
                                    (uint16_t)flying_vertical_position);
            if (!alien_flight_move_toward_player_height(
                    objects, slot_index, level, zone_index, player, setup->thing_height,
                    error, error_size)) {
                return 0;
            }
        }
    }
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, setup,
                           workspace->new_x, workspace->new_z, error, error_size) ||
        !alien_perception_look_for_player_one(alien_runtime, objects, slot_index,
                                              level, clips, player, zone_index,
                                              workspace->new_x, workspace->new_z,
                                              error, error_size)) {
        return 0;
    }
    slot[ALIEN_CHARGE_SLOT_CURRENT_MODE] = 0u;
    if (slot[ALIEN_CHARGE_SLOT_SEES_PLAYER] != 0u) {
        uint8_t in_front;
        uint8_t can_attack = flying != 0u ? UINT8_MAX : 0u;

        if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                           error, error_size)) {
            return 0;
        }
        if (in_front != 0u) {
            if (flying == 0u &&
                !alien_decision_check_attack_on_ground(objects, slot_index, level, navigation,
                                                       player, &can_attack, error, error_size)) {
                return 0;
            }
            if (can_attack != 0u) {
                alien_charge_add_facing(slot, state.animation.facing);
                slot[ALIEN_CHARGE_SLOT_CURRENT_MODE] = ALIEN_CHARGE_ATTACK_MODE;
                slot[ALIEN_CHARGE_SLOT_WHICH_ANIMATION] = 1u;
                *out_state = state;
                return 1;
            }
        }
    }
    slot[ALIEN_CHARGE_SLOT_WHICH_ANIMATION] = 0u;
    alien_charge_write_be16(slot + ALIEN_CHARGE_SLOT_TIMER2, 0u);
    alien_charge_add_facing(slot, state.animation.facing);
    *out_state = state;
    return 1;
}

int alien_charge_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t to_side, uint16_t frame_ticks, AlienChargeWorkspace *workspace,
    AlienChargeState *out_state, char *error, size_t error_size)
{
    return alien_charge_update_common(
        objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
        navigation, clips, game_link, progression, explosion_runtime, math, random, player,
        setup, to_side, 0u, 0u, frame_ticks, workspace, out_state, error, error_size);
}

int alien_charge_flying_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const AssetBlob *clips,
    const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t to_side, uint16_t frame_ticks, AlienChargeWorkspace *workspace,
    AlienChargeState *out_state, char *error, size_t error_size)
{
    return alien_charge_update_common(
        objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
        NULL, clips, game_link, progression, explosion_runtime, math, random, player, setup,
        to_side, UINT8_MAX, 0u, frame_ticks, workspace, out_state, error, error_size);
}

int alien_approach_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t flying, uint8_t to_side, uint16_t frame_ticks,
    AlienChargeWorkspace *workspace, AlienChargeState *out_state,
    char *error, size_t error_size)
{
    return alien_charge_update_common(
        objects, slot_index, alien_runtime, animation_runtime, lighting, dynamic_level,
        navigation, clips, game_link, progression, explosion_runtime, math, random, player,
        setup, to_side, flying, UINT8_MAX, frame_ticks, workspace, out_state,
        error, error_size);
}
