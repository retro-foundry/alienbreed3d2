#include "alien_decision.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* defs.i: ObjT/EntT fields read by modules/ai.s's two predicates. */
    ALIEN_DECISION_SLOT_POINT_INDEX = 0u,
    ALIEN_DECISION_SLOT_CURRENT_CONTROL_POINT = 28u,
    ALIEN_DECISION_SLOT_CURRENT_ANGLE = 30u
};

static void alien_decision_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_decision_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_decision_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int16_t alien_decision_subtract_words(int16_t left, int16_t right)
{
    return alien_decision_word_from_u16((uint16_t)((uint16_t)left - (uint16_t)right));
}

int alien_decision_check_in_front(const ObjectRuntime *objects, uint32_t slot_index,
                                  const PlayerRuntime *player, const GameMath *math,
                                  uint8_t *out_in_front,
                                  char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    int16_t point_index;
    int16_t point_x;
    int16_t point_z;
    int16_t player_x;
    int16_t player_z;
    int16_t x_difference;
    int16_t z_difference;
    int16_t sine;
    int16_t cosine;
    int32_t x_projection;
    int32_t z_projection;
    int32_t total_projection;

    if (!objects || !player || !math || !out_in_front ||
        slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot)) {
        alien_decision_set_error(error, error_size,
                                 "ai_CheckInFront received invalid source state");
        return 0;
    }
    point_index = alien_decision_word_from_u16(
        alien_decision_read_be16(slot + ALIEN_DECISION_SLOT_POINT_INDEX));
    if (point_index < 0 || (uint32_t)point_index >= objects->point_count ||
        !object_runtime_get_point_bytes((ObjectRuntime *)objects, (uint32_t)point_index,
                                        &point)) {
        alien_decision_set_error(error, error_size,
                                 "ai_CheckInFront object point is outside the source table");
        return 0;
    }

    /* modules/ai.s stores these two high source Vec2L words in newx/newz. */
    point_x = alien_decision_word_from_u16(alien_decision_read_be16(point));
    point_z = alien_decision_word_from_u16(alien_decision_read_be16(point + 4u));
    player_x = alien_decision_word_from_u16((uint16_t)player->tmp_x);
    player_z = alien_decision_word_from_u16((uint16_t)player->tmp_z);
    x_difference = alien_decision_subtract_words(player_x, point_x);
    z_difference = alien_decision_subtract_words(player_z, point_z);
    if (!game_math_sine(math,
                        alien_decision_read_be16(slot + ALIEN_DECISION_SLOT_CURRENT_ANGLE),
                        &sine, error, error_size) ||
        !game_math_cosine(math,
                          alien_decision_read_be16(slot + ALIEN_DECISION_SLOT_CURRENT_ANGLE),
                          &cosine, error, error_size)) {
        return 0;
    }
    x_projection = (int32_t)sine * (int32_t)x_difference;
    z_projection = (int32_t)cosine * (int32_t)z_difference;
    total_projection = (int32_t)((uint32_t)x_projection + (uint32_t)z_projection);
    /* `sgt d0` after the source's ADD.L sets only the returned low byte. */
    *out_in_front = total_projection > 0 ? UINT8_MAX : 0u;
    return 1;
}

int alien_decision_check_attack_on_ground(const ObjectRuntime *objects,
                                          uint32_t slot_index,
                                          const LevelRuntime *level,
                                          const LevelNavigation *navigation,
                                          const PlayerRuntime *player,
                                          uint8_t *out_can_attack,
                                          char *error, size_t error_size)
{
    uint8_t *slot;
    LevelZone player_zone;
    int16_t current_control_point;
    uint8_t player_control_point;
    LevelNavigationLink next_link;

    if (!objects || !level || !navigation || !player || !out_can_attack ||
        slot_index >= objects->active_slot_count || player->zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot) ||
        !level_runtime_get_zone(level, player->zone_index, &player_zone, error, error_size)) {
        alien_decision_set_error(error, error_size,
                                 "ai_CheckAttackOnGround received invalid source state");
        return 0;
    }
    player_control_point = player->stood_in_top != 0u ?
        (uint8_t)player_zone.control_point : (uint8_t)(player_zone.control_point >> 8u);
    current_control_point = alien_decision_word_from_u16(
        alien_decision_read_be16(slot + ALIEN_DECISION_SLOT_CURRENT_CONTROL_POINT));
    if (current_control_point == (int16_t)player_control_point) {
        *out_can_attack = UINT8_MAX;
        return 1;
    }
    if (current_control_point < 0 ||
        current_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT ||
        player_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT) {
        alien_decision_set_error(error, error_size,
                                 "ai_CheckAttackOnGround control point is outside GetNextCPt's map");
        return 0;
    }
    /* Source callers reach this predicate only after `tst.b AI_FlyABit_w` is zero. */
    if (!level_navigation_get_next(navigation, (uint8_t)current_control_point,
                                   player_control_point, 0, &next_link,
                                   error, error_size)) {
        return 0;
    }
    *out_can_attack = next_link.next_control_point == player_control_point ? UINT8_MAX : 0u;
    return 1;
}
