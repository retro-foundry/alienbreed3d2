#include "alien_prowl.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i: ObjT/EntT fields used by modules/ai.s:ai_Widget. */
    ALIEN_PROWL_SLOT_POINT_INDEX = 0u,
    ALIEN_PROWL_SLOT_TEAM_NUMBER = 21u,
    ALIEN_PROWL_SLOT_CURRENT_CONTROL_POINT = 28u,
    ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT = 32u,
    ALIEN_PROWL_WORKSPACE_LAST_ZONE = 2u,
    ALIEN_PROWL_WORKSPACE_LAST_CONTROL_POINT = 3u,
    ALIEN_PROWL_WORKSPACE_SEEN_BY = 4u,
    ALIEN_PROWL_WORKSPACE_DAMAGE_DONE = 5u,
    ALIEN_PROWL_WORKSPACE_DAMAGE_TAKEN = 6u,
    /* move.w #7,d7 followed by DBRA in ai_Widget. */
    ALIEN_PROWL_RANDOM_ATTEMPT_COUNT = 8u
};

static void alien_prowl_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_prowl_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_prowl_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int16_t alien_prowl_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int alien_prowl_get_next(const LevelNavigation *navigation,
                                uint16_t current_control_point,
                                uint16_t target_control_point, uint8_t flying,
                                LevelNavigationLink *out_link,
                                char *error, size_t error_size)
{
    if (current_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT ||
        target_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT) {
        alien_prowl_set_error(error, error_size,
                              "ai_Widget control point is outside GetNextCPt's map");
        return 0;
    }
    return level_navigation_get_next(navigation, (uint8_t)current_control_point,
                                     (uint8_t)target_control_point, flying != 0u,
                                     out_link, error, error_size);
}

static int alien_prowl_copy_object_point_words(const ObjectRuntime *objects,
                                                AlienProwlWidgetState *state,
                                                char *error, size_t error_size)
{
    size_t byte_count;

    if (!objects || !objects->point_bytes || !state ||
        objects->point_count > SIZE_MAX / OBJECT_RUNTIME_POINT_BYTE_COUNT) {
        alien_prowl_set_error(error, error_size,
                              "ai_Widget has no source object-point a2 base");
        return 0;
    }
    byte_count = (size_t)objects->point_count * OBJECT_RUNTIME_POINT_BYTE_COUNT;
    if (byte_count < sizeof(state->words)) {
        alien_prowl_set_error(error, error_size,
                              "ai_Widget object-point a2 base is shorter than Obj_DoCollision");
        return 0;
    }
    for (uint32_t word_index = 0u; word_index < ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
         ++word_index) {
        state->words[word_index] = alien_prowl_word_from_u16(
            alien_prowl_read_be16(objects->point_bytes + (size_t)word_index * sizeof(uint16_t)));
    }
    return 1;
}

static uint16_t alien_prowl_player_control_point(const LevelZone *zone,
                                                  const PlayerRuntime *player)
{
    return player->stood_in_top != 0u ? (uint8_t)zone->control_point :
                                        (uint8_t)(zone->control_point >> 8u);
}

static int alien_prowl_choose_random_target(const LevelNavigation *navigation,
                                            uint16_t control_point_count,
                                            uint16_t current_control_point,
                                            uint8_t flying, GameRandom *random,
                                            uint16_t *in_out_target_control_point,
                                            LevelNavigationLink *out_link,
                                            char *error, size_t error_size)
{
    uint16_t candidate;
    LevelNavigationLink link = {0};

    if (!random || !in_out_target_control_point || !out_link || control_point_count == 0u ||
        control_point_count > LEVEL_NAVIGATION_CONTROL_POINT_LIMIT) {
        alien_prowl_set_error(error, error_size,
                              "ai_Widget random target has invalid source control-point state");
        return 0;
    }
    /* moveq #0,d1 / move.w GetRand,d1 / divs Lvl_NumControlPoints,d1 / swap d1. */
    candidate = (uint16_t)(game_random_next(random) % control_point_count);
    for (uint16_t attempt = 0u; attempt < ALIEN_PROWL_RANDOM_ATTEMPT_COUNT; ++attempt) {
        *in_out_target_control_point = candidate;
        if (!alien_prowl_get_next(navigation, current_control_point, candidate, flying, &link,
                                  error, error_size)) {
            return 0;
        }
        if (link.next_control_point != current_control_point &&
            link.next_control_point != UINT8_C(0x7f)) {
            break;
        }
        /* .plus_again mutates d1 even after the final DBRA iteration. */
        candidate = (uint16_t)(candidate + 1u);
        if (candidate >= control_point_count) {
            candidate = 0u;
        }
        *in_out_target_control_point = candidate;
    }
    *out_link = link;
    return 1;
}

int alien_prowl_widget(AlienRuntime *alien_runtime, ObjectRuntime *objects,
                       uint32_t slot_index, const LevelRuntime *level,
                       const LevelNavigation *navigation,
                       const PlayerRuntime *player, int16_t player_noise_volume,
                       uint8_t flying, GameRandom *random,
                       AlienProwlWidgetState *out_state,
                       char *error, size_t error_size)
{
    uint8_t *slot;
    uint16_t point_index;
    int8_t team_number;
    int16_t *workspace;
    uint16_t current_control_point;
    uint16_t target_control_point;
    LevelNavigationLink link;
    AlienProwlWidgetState state;
    int copied_team_memory = 0;

    if (!alien_runtime || !objects || !level || !navigation || !player || !random ||
        !out_state || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_prowl_set_error(error, error_size, "ai_Widget received invalid source state");
        return 0;
    }
    point_index = alien_prowl_read_be16(slot + ALIEN_PROWL_SLOT_POINT_INDEX);
    if (point_index >= ALIEN_RUNTIME_ENTITY_COUNT) {
        alien_prowl_set_error(error, error_size,
                              "ai_Widget object point exceeds AI workspace");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    target_control_point = alien_prowl_read_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT);

    if (player_noise_volume != 0) {
        LevelZone player_zone;
        uint16_t player_control_point;

        if (player->zone_index >= level->zone_count ||
            !level_runtime_get_zone(level, player->zone_index, &player_zone, error, error_size)) {
            alien_prowl_set_error(error, error_size,
                                  "ai_Widget noise path has an invalid player zone");
            return 0;
        }
        player_control_point = alien_prowl_player_control_point(&player_zone, player);
        current_control_point = alien_prowl_read_be16(
            slot + ALIEN_PROWL_SLOT_CURRENT_CONTROL_POINT);
        if (!alien_prowl_get_next(navigation, current_control_point, player_control_point,
                                  flying, &link, error, error_size)) {
            return 0;
        }
        target_control_point = link.next_control_point == UINT8_C(0x7f) ?
            current_control_point : link.next_control_point;
        alien_prowl_write_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT,
                               target_control_point);
    }

    team_number = (int8_t)slot[ALIEN_PROWL_SLOT_TEAM_NUMBER];
    if (team_number >= 0) {
        if ((uint8_t)team_number >= ALIEN_RUNTIME_TEAM_COUNT) {
            alien_prowl_set_error(error, error_size,
                                  "ai_Widget team exceeds source workspace");
            return 0;
        }
        if (alien_runtime->team_workspace[(uint8_t)team_number]
                                       [ALIEN_PROWL_WORKSPACE_SEEN_BY] >= 0) {
            if ((uint16_t)alien_runtime->team_workspace[(uint8_t)team_number]
                                                        [ALIEN_PROWL_WORKSPACE_SEEN_BY] ==
                point_index) {
                alien_runtime->team_workspace[(uint8_t)team_number]
                                           [ALIEN_PROWL_WORKSPACE_SEEN_BY] = -1;
            } else {
                workspace = alien_runtime->entity_workspace[point_index];
                workspace[ALIEN_PROWL_WORKSPACE_DAMAGE_DONE] = 0;
                workspace[ALIEN_PROWL_WORKSPACE_DAMAGE_TAKEN] = 0;
                memcpy(workspace, alien_runtime->team_workspace[(uint8_t)team_number],
                       sizeof(alien_runtime->team_workspace[(uint8_t)team_number]));
                alien_prowl_write_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT,
                                       (uint16_t)workspace[
                                           ALIEN_PROWL_WORKSPACE_LAST_CONTROL_POINT]);
                workspace[ALIEN_PROWL_WORKSPACE_LAST_ZONE] = -1;
                copied_team_memory = 1;
            }
        }
        /* a2 stays at this exact team record through Obj_DoCollision. */
        memcpy(state.words, alien_runtime->team_workspace[(uint8_t)team_number],
               sizeof(state.words));
    } else if (!alien_prowl_copy_object_point_words(objects, &state, error, error_size)) {
        return 0;
    }

    if (copied_team_memory == 0) {
        workspace = alien_runtime->entity_workspace[point_index];
        workspace[ALIEN_PROWL_WORKSPACE_DAMAGE_DONE] = 0;
        workspace[ALIEN_PROWL_WORKSPACE_DAMAGE_TAKEN] = 0;
        if (workspace[ALIEN_PROWL_WORKSPACE_LAST_ZONE] >= 0) {
            alien_prowl_write_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT,
                                   (uint16_t)workspace[
                                       ALIEN_PROWL_WORKSPACE_LAST_CONTROL_POINT]);
            workspace[ALIEN_PROWL_WORKSPACE_LAST_ZONE] = -1;
        }
    }

    current_control_point = alien_prowl_read_be16(slot + ALIEN_PROWL_SLOT_CURRENT_CONTROL_POINT);
    target_control_point = alien_prowl_read_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT);
    if (!alien_prowl_get_next(navigation, current_control_point, target_control_point, flying,
                              &link, error, error_size)) {
        return 0;
    }
    if (link.next_control_point == UINT8_C(0x7f) ||
        (flying == 0u && link.only_see != 0u)) {
        if (!alien_prowl_choose_random_target(
                navigation, (uint16_t)level->control_point_count, current_control_point,
                flying, random, &target_control_point, &link, error, error_size)) {
            return 0;
        }
        alien_prowl_write_be16(slot + ALIEN_PROWL_SLOT_TARGET_CONTROL_POINT,
                               target_control_point);
    }
    state.middle_control_point = link.next_control_point;
    state.only_see = link.only_see;
    *out_state = state;
    return 1;
}

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by modules/ai.s:ai_ProwlFly. */
    ALIEN_PROWL_MODE_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_PROWL_MODE_SLOT_ZONE_ID = 12u,
    ALIEN_PROWL_MODE_SLOT_SEES_PLAYER = 17u,
    ALIEN_PROWL_MODE_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_PROWL_MODE_SLOT_CURRENT_MODE = 20u,
    ALIEN_PROWL_MODE_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_PROWL_MODE_SLOT_CURRENT_CONTROL_POINT = 28u,
    ALIEN_PROWL_MODE_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_PROWL_MODE_SLOT_TARGET_CONTROL_POINT = 32u,
    ALIEN_PROWL_MODE_SLOT_TIMER1 = 34u,
    ALIEN_PROWL_MODE_SLOT_TIMER2 = 40u,
    ALIEN_PROWL_MODE_SLOT_WHICH_ANIMATION = 55u,
    ALIEN_PROWL_MODE_SLOT_IN_UPPER_ZONE = 63u,
    /* ai_ProwlRandom/ai_ProwlRandomFlying inputs for MoveObject. */
    ALIEN_PROWL_MODE_STEP_UP = 20 * 256,
    ALIEN_PROWL_MODE_STEP_DOWN = 30 * 256,
    ALIEN_PROWL_MODE_FLY_STEP_DOWN = 1000 * 256,
    ALIEN_PROWL_MODE_WALL_FLAGS = 0x0200u
};

static int16_t alien_prowl_mode_add16(int16_t left, int16_t right)
{
    return alien_prowl_word_from_u16((uint16_t)((uint16_t)left + (uint16_t)right));
}

static int16_t alien_prowl_mode_sub16(int16_t left, int16_t right)
{
    return alien_prowl_word_from_u16((uint16_t)((uint16_t)left - (uint16_t)right));
}

static int16_t alien_prowl_mode_abs16(int16_t value)
{
    return value >= 0 ? value : alien_prowl_word_from_u16((uint16_t)(0u - (uint16_t)value));
}

static int32_t alien_prowl_mode_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -(((-(int64_t)value) + ((INT64_C(1) << count) - 1)) >> count);
}

static int16_t alien_prowl_mode_sine_offset(int16_t value)
{
    /* `ext.l`, `asl.l #4`, and `swap` is the signed source value divided by 4096. */
    return (int16_t)alien_prowl_mode_asr32((int32_t)value, 12u);
}

static int alien_prowl_mode_get_zone_index(const uint8_t *slot, const LevelRuntime *level,
                                            uint16_t *out_zone_index,
                                            char *error, size_t error_size)
{
    int16_t zone_index;

    if (!slot || !level || !out_zone_index) {
        alien_prowl_set_error(error, error_size, "ai_ProwlFly has no source zone state");
        return 0;
    }
    zone_index = alien_prowl_word_from_u16(
        alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_ZONE_ID));
    if (zone_index < 0 || (uint16_t)zone_index >= level->zone_count) {
        alien_prowl_set_error(error, error_size,
                              "ai_ProwlFly object zone is outside the source level");
        return 0;
    }
    *out_zone_index = (uint16_t)zone_index;
    return 1;
}

static void alien_prowl_mode_add_facing(uint8_t *slot, uint16_t facing)
{
    alien_prowl_write_be16(
        slot + ALIEN_PROWL_MODE_SLOT_CURRENT_ANGLE,
        (uint16_t)(alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_CURRENT_ANGLE) + facing));
}

static int alien_prowl_mode_choose_boredom_target(
    AlienRuntime *alien_runtime, ObjectRuntime *objects, uint32_t slot_index,
    const LevelRuntime *level, const LevelNavigation *navigation, uint16_t zone_index,
    uint8_t flying, GameRandom *random, int16_t old_x, int16_t old_z,
    char *error, size_t error_size)
{
    uint8_t *slot;
    int16_t *boredom;
    int16_t distance;

    if (!alien_runtime || !objects || !level || !navigation || !random ||
        slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_prowl_set_error(error, error_size, "ai_ProwlFly boredom received invalid source state");
        return 0;
    }
    boredom = alien_runtime->boredom[slot_index];
    distance = alien_prowl_mode_add16(
        alien_prowl_mode_abs16(alien_prowl_mode_sub16(old_x, boredom[1u])),
        alien_prowl_mode_abs16(alien_prowl_mode_sub16(old_z, boredom[2u])));
    if (distance >= 50) {
        boredom[1u] = old_x;
        boredom[2u] = old_z;
        boredom[0u] = 100;
        return 1;
    }
    boredom[0u] = alien_prowl_mode_sub16(boredom[0u], 1);
    if (boredom[0u] > 0) {
        return 1;
    }
    if (!alien_spatial_store_current_control_point(objects, slot_index, level, zone_index,
                                                   error, error_size)) {
        return 0;
    }
    {
        uint16_t target_control_point;
        LevelNavigationLink link;

        target_control_point = alien_prowl_read_be16(
            slot + ALIEN_PROWL_MODE_SLOT_TARGET_CONTROL_POINT);
        if (!alien_prowl_choose_random_target(
                navigation, level->control_point_count,
                alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_CURRENT_CONTROL_POINT),
                flying, random, &target_control_point, &link, error, error_size)) {
            return 0;
        }
        alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_TARGET_CONTROL_POINT,
                               target_control_point);
    }
    boredom[0u] = 50;
    return 1;
}

int alien_prowl_random_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t flying, uint16_t frame_ticks, AlienProwlState *out_state,
    char *error, size_t error_size)
{
    const LevelRuntime *level;
    uint8_t *slot;
    uint8_t *previous_slot;
    uint8_t *point;
    uint16_t point_index;
    uint16_t zone_index;
    LevelControlPoint control_point;
    int16_t sine;
    int16_t cosine;
    uint16_t phase;
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int16_t old_vertical_position;
    int16_t dark_result;
    AlienProwlState state;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !dynamic_level ||
        !navigation || !clips || !game_link || !progression || !explosion_runtime || !math ||
        !random || !player || !setup || !out_state ||
        slot_index == 0u || slot_index >= objects->active_slot_count ||
        slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        player->zone_index >= dynamic_level->runtime.zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &previous_slot)) {
        alien_prowl_set_error(error, error_size, "ai_ProwlFly received invalid source state");
        return 0;
    }
    if (!object_animation_runtime_reserve(animation_runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }
    level = &dynamic_level->runtime;
    point_index = alien_prowl_read_be16(slot + ALIEN_PROWL_SLOT_POINT_INDEX);
    if (point_index >= objects->point_count || point_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        !object_runtime_get_point_bytes(objects, point_index, &point) ||
        !alien_prowl_mode_get_zone_index(slot, level, &zone_index, error, error_size)) {
        alien_prowl_set_error(error, error_size,
                              "ai_ProwlFly has an invalid source point or zone");
        return 0;
    }
    memset(&state, 0, sizeof(state));

    if (slot[ALIEN_PROWL_MODE_SLOT_DAMAGE_TAKEN] != 0u) {
        state.damage_taken = UINT8_MAX;
        if (!alien_damage_take(objects, slot_index, alien_runtime, animation_runtime, math,
                               random, player, &state.damage, error, error_size)) {
            return 0;
        }
        if (state.damage.route == ALIEN_DAMAGE_ROUTE_JUST_DIED) {
            if (!alien_death_just_died(
                    objects, slot_index, alien_runtime, level, game_link, progression,
                    animation_runtime, explosion_runtime, math, random, &state.death,
                    error, error_size)) {
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
    old_x = alien_prowl_word_from_u16(alien_prowl_read_be16(point));
    old_z = alien_prowl_word_from_u16(alien_prowl_read_be16(point + 4u));
    if (!alien_prowl_mode_choose_boredom_target(
            alien_runtime, objects, slot_index, level, navigation, zone_index, flying, random,
            old_x, old_z, error, error_size) ||
        !alien_prowl_widget(alien_runtime, objects, slot_index, level, navigation, player,
                            player->noise_volume, flying, random, &state.widget,
                            error, error_size)) {
        return 0;
    }
    /* ai_ProwlFly indexes Lvl_ControlPointCoordsPtr_l directly after GetNextCPt. */
    if (!level_runtime_get_control_point_source_address(
            level, state.widget.middle_control_point, &control_point, error, error_size)) {
        return 0;
    }

    /* ai_ProwlFly's point/phase-derived control-point displacement. */
    phase = (uint16_t)((uint16_t)(state.widget.middle_control_point << 2u) + point_index);
    phase = (uint16_t)((int32_t)(int16_t)phase * INT16_C(0x1347)) & UINT16_C(4095);
    if (!game_math_sine(math, (uint16_t)(phase << 1u), &sine, error, error_size) ||
        !game_math_cosine(math, (uint16_t)(phase << 1u), &cosine, error, error_size)) {
        return 0;
    }
    new_x = alien_prowl_mode_add16(control_point.x, alien_prowl_mode_sine_offset(sine));
    new_z = alien_prowl_mode_add16(control_point.z, alien_prowl_mode_sine_offset(cosine));

    memset(&state.heading, 0, sizeof(state.heading));
    state.heading.old_x = old_x;
    state.heading.old_z = old_z;
    state.heading.new_x = new_x;
    state.heading.new_z = new_z;
    state.heading.range = 40;
    if (state.animation.action != 0u) {
        state.heading.speed = (int16_t)((int32_t)(int16_t)(state.animation.action << 2u) *
                                        setup->prowl_speed);
    }
    state.heading.angle = alien_runtime->heading_angle;
    if (!object_heading_towards_angle(math, &state.heading, error, error_size)) {
        return 0;
    }
    alien_runtime->heading_angle = state.heading.angle;
    alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_CURRENT_ANGLE, state.heading.angle);
    new_x = state.heading.new_x;
    new_z = state.heading.new_z;
    if (state.heading.got_there != 0u) {
        alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_CURRENT_CONTROL_POINT,
                               state.widget.middle_control_point);
        if (state.widget.middle_control_point == alien_prowl_read_be16(
                slot + ALIEN_PROWL_MODE_SLOT_TARGET_CONTROL_POINT)) {
            if (level->control_point_count == 0u) {
                alien_prowl_set_error(error, error_size,
                                      "ai_ProwlFly target selection divides by zero control points");
                return 0;
            }
            alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_TARGET_CONTROL_POINT,
                                   (uint16_t)(game_random_next(random) %
                                              level->control_point_count));
        }
    }

    old_vertical_position = alien_prowl_word_from_u16(
        alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_VERTICAL_POSITION));
    memset(&state.movement, 0, sizeof(state.movement));
    state.movement.zone_index = zone_index;
    state.movement.old_x = old_x;
    state.movement.old_z = old_z;
    state.movement.new_x = new_x;
    state.movement.new_z = new_z;
    state.movement.old_y = (int32_t)old_vertical_position * 128 -
        alien_prowl_mode_asr32(setup->thing_height, 1u);
    state.movement.new_y = state.movement.old_y;
    state.movement.thing_height = setup->thing_height;
    state.movement.step_up = ALIEN_PROWL_MODE_STEP_UP;
    state.movement.step_down = flying != 0u ? ALIEN_PROWL_MODE_FLY_STEP_DOWN :
                                             ALIEN_PROWL_MODE_STEP_DOWN;
    state.movement.extension_length = setup->extended_wall_length;
    state.movement.wall_flags = ALIEN_PROWL_MODE_WALL_FLAGS;
    state.movement.away_from_wall = setup->away_from_wall;
    state.movement.stood_in_top = slot[ALIEN_PROWL_MODE_SLOT_IN_UPPER_ZONE];
    {
        ObjectCollisionTrace collision;

        memset(&collision, 0, sizeof(collision));
        collision.collision_id = point_index;
        collision.old_x = old_x;
        collision.old_z = old_z;
        collision.new_x = state.movement.new_x;
        collision.new_z = state.movement.new_z;
        collision.new_y = state.movement.new_y;
        collision.thing_height = state.movement.thing_height;
        collision.stood_in_top = state.movement.stood_in_top;
        if (!object_collision_check(objects, game_link, state.widget.words,
                                    ALIEN_RUNTIME_WORKSPACE_WORD_COUNT, &collision,
                                    &state.hit_object, error, error_size)) {
            return 0;
        }
    }
    if (state.hit_object != 0u) {
        state.movement.new_x = old_x;
        state.movement.new_z = old_z;
    } else {
        if (!object_movement_trace(dynamic_level, &state.movement, error, error_size)) {
            return 0;
        }
        slot[ALIEN_PROWL_MODE_SLOT_IN_UPPER_ZONE] = state.movement.stood_in_top;
    }
    if (alien_prowl_word_from_u16(
            alien_prowl_read_be16(previous_slot + ALIEN_PROWL_MODE_SLOT_ZONE_ID)) >= 0) {
        alien_prowl_write_be16(previous_slot + ALIEN_PROWL_MODE_SLOT_ZONE_ID,
                               alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_ZONE_ID));
        alien_prowl_write_be16(previous_slot + ALIEN_PROWL_MODE_SLOT_ENTITY_ZONE_ID,
                               alien_prowl_read_be16(
                                   slot + ALIEN_PROWL_MODE_SLOT_ENTITY_ZONE_ID));
    }
    if (!alien_spatial_store_room_stats(
            objects, slot_index, level, state.movement.zone_index,
            state.movement.new_x, state.movement.new_z, setup->thing_height,
            error, error_size)) {
        return 0;
    }
    if (flying != 0u) {
        alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_VERTICAL_POSITION,
                               (uint16_t)old_vertical_position);
        if (!alien_flight_move_toward_control_point_height(
                objects, slot_index, level, state.movement.zone_index,
                state.widget.middle_control_point, setup->thing_height,
                error, error_size)) {
            return 0;
        }
    }
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, setup,
                           state.movement.new_x, state.movement.new_z,
                           error, error_size) ||
        !alien_perception_look_for_player_one(
            alien_runtime, objects, slot_index, level, clips, player,
            state.movement.zone_index,
            state.movement.new_x, state.movement.new_z, error, error_size)) {
        return 0;
    }

    slot[ALIEN_PROWL_MODE_SLOT_CURRENT_MODE] = 0u;
    slot[ALIEN_PROWL_MODE_SLOT_WHICH_ANIMATION] = 0u;
    if (slot[ALIEN_PROWL_MODE_SLOT_SEES_PLAYER] != 0u) {
        uint8_t in_front;

        if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                           error, error_size)) {
            return 0;
        }
        if (in_front != 0u) {
            LevelZone player_zone;

            alien_prowl_write_be16(
                slot + ALIEN_PROWL_MODE_SLOT_TIMER1,
                (uint16_t)alien_prowl_mode_sub16(
                    alien_prowl_word_from_u16(
                        alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_TIMER1)),
                    (int16_t)frame_ticks));
            if (alien_prowl_word_from_u16(
                    alien_prowl_read_be16(slot + ALIEN_PROWL_MODE_SLOT_TIMER1)) > 0) {
                alien_prowl_mode_add_facing(slot, state.animation.facing);
                *out_state = state;
                return 1;
            }
            if (!level_runtime_get_zone(level, player->zone_index, &player_zone,
                                        error, error_size) ||
                !alien_dark_check(objects, slot_index, player_zone.id,
                                  player->room_brightness, random, &dark_result,
                                  error, error_size)) {
                return 0;
            }
            if (dark_result != 0) {
                uint8_t can_attack = 0u;

                if (flying != 0u || setup->response_mode == 2 || setup->response_mode == 5) {
                    can_attack = UINT8_MAX;
                } else if (!alien_decision_check_attack_on_ground(
                               objects, slot_index, level, navigation, player, &can_attack,
                               error, error_size)) {
                    return 0;
                }
                if (can_attack != 0u) {
                    alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_TIMER2, 0u);
                    slot[ALIEN_PROWL_MODE_SLOT_CURRENT_MODE] = 1u;
                    slot[ALIEN_PROWL_MODE_SLOT_WHICH_ANIMATION] = 1u;
                    alien_prowl_mode_add_facing(slot, state.animation.facing);
                    *out_state = state;
                    return 1;
                }
                if (!alien_memory_store_player_position(alien_runtime, objects, slot_index,
                                                        level, player, error, error_size)) {
                    return 0;
                }
            }
        }
    }
    alien_prowl_write_be16(slot + ALIEN_PROWL_MODE_SLOT_TIMER1,
                           (uint16_t)setup->reaction_time);
    alien_prowl_mode_add_facing(slot, state.animation.facing);
    *out_state = state;
    return 1;
}
