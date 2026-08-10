#include "alien_pause.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i: ObjT/EntT fields used by modules/ai.s:ai_PauseBriefly. */
    ALIEN_PAUSE_SLOT_POINT_INDEX = 0u,
    ALIEN_PAUSE_SLOT_ZONE_ID = 12u,
    ALIEN_PAUSE_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_PAUSE_SLOT_CURRENT_MODE = 20u,
    ALIEN_PAUSE_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_PAUSE_SLOT_TIMER1 = 34u,
    ALIEN_PAUSE_SLOT_TIMER2 = 40u,
    ALIEN_PAUSE_SLOT_SEES_PLAYER = 17u,
    ALIEN_PAUSE_SLOT_WHICH_ANIMATION = 55u
};

static void alien_pause_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_pause_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_pause_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int16_t alien_pause_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int16_t alien_pause_subtract16(int16_t left, uint16_t right)
{
    return alien_pause_word_from_u16((uint16_t)((uint16_t)left - right));
}

static void alien_pause_add_facing(uint8_t *slot, uint16_t facing)
{
    alien_pause_write_be16(slot + ALIEN_PAUSE_SLOT_CURRENT_ANGLE,
                           (uint16_t)(alien_pause_read_be16(
                               slot + ALIEN_PAUSE_SLOT_CURRENT_ANGLE) + facing));
}

int alien_pause_briefly_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    const LevelRuntime *level, const AssetBlob *clips, const GameLink *game_link,
    GameProgression *progression, ObjectExplosionRuntime *explosion_runtime,
    const GameMath *math, GameRandom *random, const PlayerRuntime *player,
    const AlienSetup *setup, uint16_t frame_ticks, AlienPauseState *out_state,
    char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    AlienPauseState state;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !level || !clips ||
        !game_link || !progression || !explosion_runtime || !math || !random || !player ||
        !setup || !out_state || slot_index >= objects->active_slot_count ||
        slot_index >= OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT ||
        player->zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_pause_set_error(error, error_size, "ai_PauseBriefly received invalid source state");
        return 0;
    }
    memset(&state, 0, sizeof(state));

    if (slot[ALIEN_PAUSE_SLOT_DAMAGE_TAKEN] != 0u) {
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
        /* ai_PauseBriefly returns without applying ai_AnimFacing after ai_TakeDamage. */
        *out_state = state;
        return 1;
    }

    alien_pause_write_be16(slot + ALIEN_PAUSE_SLOT_TIMER2, 0u);
    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, setup, player->yaw,
            &state.animation, error, error_size)) {
        return 0;
    }
    alien_pause_write_be16(
        slot + ALIEN_PAUSE_SLOT_TIMER1,
        (uint16_t)alien_pause_subtract16(
            alien_pause_word_from_u16(alien_pause_read_be16(slot + ALIEN_PAUSE_SLOT_TIMER1)),
            frame_ticks));
    if (alien_pause_word_from_u16(
            alien_pause_read_be16(slot + ALIEN_PAUSE_SLOT_TIMER1)) > 0) {
        alien_pause_add_facing(slot, state.animation.facing);
        *out_state = state;
        return 1;
    }

    if (!object_runtime_get_point_bytes(
            objects, alien_pause_read_be16(slot + ALIEN_PAUSE_SLOT_POINT_INDEX), &point)) {
        alien_pause_set_error(error, error_size,
                              "ai_PauseBriefly alien point is outside source state");
        return 0;
    }
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, setup,
                           alien_pause_word_from_u16(alien_pause_read_be16(point)),
                           alien_pause_word_from_u16(alien_pause_read_be16(point + 4u)),
                           error, error_size) ||
        !alien_perception_look_for_player_one(
            objects, slot_index, level, clips, player,
            alien_pause_read_be16(slot + ALIEN_PAUSE_SLOT_ZONE_ID),
            alien_pause_word_from_u16(alien_pause_read_be16(point)),
            alien_pause_word_from_u16(alien_pause_read_be16(point + 4u)),
            error, error_size)) {
        return 0;
    }

    slot[ALIEN_PAUSE_SLOT_CURRENT_MODE] = 0u;
    if (slot[ALIEN_PAUSE_SLOT_SEES_PLAYER] != 0u) {
        uint8_t in_front;

        if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                           error, error_size)) {
            return 0;
        }
        if (in_front != 0u) {
            int16_t dark_result;
            LevelZone player_zone;

            if (!level_runtime_get_zone(level, player->zone_index, &player_zone,
                                        error, error_size) ||
                !alien_dark_check(objects, slot_index, player_zone.id,
                                  player->room_brightness, random, &dark_result,
                                  error, error_size)) {
                return 0;
            }
            if (dark_result != 0) {
                slot[ALIEN_PAUSE_SLOT_WHICH_ANIMATION] = 1u;
                slot[ALIEN_PAUSE_SLOT_CURRENT_MODE] = 1u;
                alien_pause_add_facing(slot, state.animation.facing);
                *out_state = state;
                return 1;
            }
        }
    }
    slot[ALIEN_PAUSE_SLOT_WHICH_ANIMATION] = 0u;
    alien_pause_add_facing(slot, state.animation.facing);
    *out_state = state;
    return 1;
}
