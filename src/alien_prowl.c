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
