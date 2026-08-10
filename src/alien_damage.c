#include "alien_damage.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "object_heading.h"
#include "alien_spatial.h"
#include "alien_torch.h"

enum {
    /* defs.i: ObjT/EntT fields used by modules/ai.s:ai_TakeDamage. */
    ALIEN_DAMAGE_SLOT_POINT_INDEX = 0u,
    ALIEN_DAMAGE_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_DAMAGE_SLOT_ZONE_ID = 12u,
    ALIEN_DAMAGE_SLOT_HIT_POINTS = 18u,
    ALIEN_DAMAGE_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_DAMAGE_SLOT_CURRENT_MODE = 20u,
    ALIEN_DAMAGE_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_DAMAGE_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_DAMAGE_SLOT_TIMER1 = 34u,
    ALIEN_DAMAGE_SLOT_TIMER2 = 40u,
    ALIEN_DAMAGE_SLOT_WHICH_ANIMATION = 55u,
    /* bss/tables_bss.s:ObjectWorkspace_vl byte one. */
    ALIEN_DAMAGE_WORKSPACE_SPECIAL_FRAME = 1u
};

static void alien_damage_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_damage_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_damage_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int16_t alien_damage_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int16_t alien_damage_add16(int16_t left, int16_t right)
{
    return alien_damage_word_from_u16((uint16_t)((uint16_t)left + (uint16_t)right));
}

static int16_t alien_damage_asr16_2(int16_t value)
{
    if (value >= 0) {
        return (int16_t)((uint16_t)value >> 2u);
    }
    return (int16_t)-(((int32_t)-value + 3) >> 2u);
}

int alien_damage_take(ObjectRuntime *objects, uint32_t slot_index,
                      AlienRuntime *alien_runtime,
                      ObjectAnimationRuntime *animation_runtime,
                      const GameMath *math, GameRandom *random,
                      const PlayerRuntime *player, AlienDamageState *out_state,
                      char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *animation_workspace;
    int16_t accumulated_damage;
    int16_t damage_threshold;
    uint8_t hit_points;
    AlienDamageState state;

    if (!objects || !alien_runtime || !animation_runtime || !math || !random || !player ||
        !out_state || slot_index >= objects->active_slot_count ||
        slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_damage_set_error(error, error_size, "ai_TakeDamage received invalid source state");
        return 0;
    }
    if (!object_animation_runtime_reserve(animation_runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }
    animation_workspace = object_animation_runtime_workspace(animation_runtime, slot_index);
    if (!animation_workspace) {
        alien_damage_set_error(error, error_size,
                               "ai_TakeDamage source workspace is unavailable");
        return 0;
    }

    /* ai_TakeDamage: add.b damage to AI_DamagePtr, then ASR.W #2. */
    accumulated_damage = alien_damage_add16(
        alien_runtime->damage[slot_index], (int16_t)slot[ALIEN_DAMAGE_SLOT_DAMAGE_TAKEN]);
    alien_runtime->damage[slot_index] = accumulated_damage;
    damage_threshold = alien_damage_asr16_2(accumulated_damage);
    hit_points = slot[ALIEN_DAMAGE_SLOT_HIT_POINTS];
    slot[ALIEN_DAMAGE_SLOT_DAMAGE_TAKEN] = 0u;
    if ((int16_t)hit_points <= damage_threshold) {
        /* The source branches immediately to ai_JustDied before timer/mode writes. */
        state.route = ALIEN_DAMAGE_ROUTE_JUST_DIED;
        state.got_out = 0u;
        *out_state = state;
        return 1;
    }

    alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_TIMER1, 0u);
    alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_TIMER2, 0u);
    if ((game_random_next(random) & 3u) != 0u) {
        uint8_t *point;
        int16_t point_index = alien_damage_word_from_u16(
            alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_POINT_INDEX));
        ObjectHeading heading;

        if (point_index < 0 || (uint32_t)point_index >= objects->point_count ||
            !object_runtime_get_point_bytes(objects, (uint32_t)point_index, &point)) {
            alien_damage_set_error(error, error_size,
                                   "ai_TakeDamage alien point is outside the source runtime");
            return 0;
        }
        animation_workspace[ALIEN_DAMAGE_WORKSPACE_SPECIAL_FRAME] = UINT8_MAX;
        slot[ALIEN_DAMAGE_SLOT_CURRENT_MODE] = 1u;
        alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_TIMER2, 0u);
        alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_TIMER1, 0u);
        slot[ALIEN_DAMAGE_SLOT_WHICH_ANIMATION] = 1u;
        heading.old_x = alien_damage_word_from_u16(alien_damage_read_be16(point));
        heading.old_z = alien_damage_word_from_u16(alien_damage_read_be16(point + 4u));
        heading.new_x = (int16_t)(uint16_t)player->x;
        heading.new_z = (int16_t)(uint16_t)player->z;
        heading.range = -20;
        heading.speed = 100;
        /* objectmove.s:AngRet persists when HeadTowardsAng sees a zero vector. */
        heading.angle = alien_runtime->heading_angle;
        heading.got_there = 0u;
        if (!object_heading_towards_angle(math, &heading, error, error_size)) {
            return 0;
        }
        alien_runtime->heading_angle = heading.angle;
        alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_CURRENT_ANGLE, heading.angle);
    } else {
        slot[ALIEN_DAMAGE_SLOT_CURRENT_MODE] = 4u;
        slot[ALIEN_DAMAGE_SLOT_WHICH_ANIMATION] = 2u;
        animation_workspace[ALIEN_DAMAGE_WORKSPACE_SPECIAL_FRAME] = UINT8_MAX;
    }
    state.route = ALIEN_DAMAGE_ROUTE_NONFATAL;
    state.got_out = UINT8_MAX;
    *out_state = state;
    return 1;
}

int alien_damage_update_reaction(
    ObjectRuntime *objects, uint32_t slot_index,
    ObjectAnimationRuntime *animation_runtime, const GameLink *game_link,
    const GameMath *math, const LevelRuntime *level, LightingRuntime *lighting,
    const PlayerRuntime *player, const AlienSetup *setup,
    int16_t torch_new_x, int16_t torch_new_z,
    AlienDamageReactionState *out_state, char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *previous_slot;
    uint8_t *point;
    int16_t zone_index;
    int16_t vertical_position;
    int16_t point_index;
    ObjectHeading heading;
    AlienDamageReactionState state;

    if (!objects || !animation_runtime || !game_link || !math || !level || !lighting ||
        !player || !setup || !out_state || slot_index == 0u ||
        slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &previous_slot)) {
        alien_damage_set_error(error, error_size,
                              "ai_DoTakeDamage received invalid source state");
        return 0;
    }
    zone_index = alien_damage_word_from_u16(
        alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_ZONE_ID));
    point_index = alien_damage_word_from_u16(
        alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_POINT_INDEX));
    if (zone_index < 0 || (uint16_t)zone_index >= level->zone_count || point_index < 0 ||
        (uint32_t)point_index >= objects->point_count ||
        !object_runtime_get_point_bytes(objects, (uint32_t)point_index, &point)) {
        alien_damage_set_error(error, error_size,
                              "ai_DoTakeDamage has an invalid source zone or point");
        return 0;
    }

    memset(&state, 0, sizeof(state));
    vertical_position = alien_damage_word_from_u16(
        alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_VERTICAL_POSITION));
    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, setup, player->yaw,
            &state.animation, error, error_size) ||
        !alien_spatial_store_room_stats_still(
            objects, slot_index, level, (uint16_t)zone_index, setup->thing_height,
            error, error_size)) {
        return 0;
    }
    /* ai_DoTakeDamage restores its prior word Y for DefaultMode >= one. */
    if (setup->default_mode >= 1) {
        alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_VERTICAL_POSITION,
                                (uint16_t)vertical_position);
    }
    if (state.animation.finished != 0u) {
        slot[ALIEN_DAMAGE_SLOT_CURRENT_MODE] = 0u;
        slot[ALIEN_DAMAGE_SLOT_WHICH_ANIMATION] = 0u;
        alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_TIMER2, 0u);
    }
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, setup,
                           torch_new_x, torch_new_z, error, error_size)) {
        return 0;
    }
    if (alien_damage_word_from_u16(
            alien_damage_read_be16(previous_slot + ALIEN_DAMAGE_SLOT_ZONE_ID)) >= 0) {
        alien_damage_write_be16(previous_slot + ALIEN_DAMAGE_SLOT_ZONE_ID,
                                alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_ZONE_ID));
        alien_damage_write_be16(previous_slot + ALIEN_DAMAGE_SLOT_ENTITY_ZONE_ID,
                                alien_damage_read_be16(slot + ALIEN_DAMAGE_SLOT_ENTITY_ZONE_ID));
    }

    heading.old_x = alien_damage_word_from_u16(alien_damage_read_be16(point));
    heading.old_z = alien_damage_word_from_u16(alien_damage_read_be16(point + 4u));
    heading.new_x = (int16_t)(uint16_t)player->x;
    heading.new_z = (int16_t)(uint16_t)player->z;
    heading.range = -20;
    heading.speed = 20;
    heading.angle = 0u;
    heading.got_there = 0u;
    if (!object_heading_towards_angle(math, &heading, error, error_size)) {
        return 0;
    }
    alien_damage_write_be16(slot + ALIEN_DAMAGE_SLOT_CURRENT_ANGLE,
                            (uint16_t)(heading.angle + state.animation.facing));
    state.got_out = 0u;
    *out_state = state;
    return 1;
}
