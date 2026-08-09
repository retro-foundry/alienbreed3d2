#include "alien_flight.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* defs.i: EntT/ShotT fields used by modules/ai.s flight helpers. */
    ALIEN_FLIGHT_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_FLIGHT_SLOT_VERTICAL_VELOCITY = 48u,
    ALIEN_FLIGHT_SLOT_IN_UPPER_ZONE = 63u,
    ALIEN_FLIGHT_MIN_VELOCITY = -32,
    ALIEN_FLIGHT_MAX_VELOCITY = 32
};

static void alien_flight_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_flight_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_flight_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int16_t alien_flight_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int16_t alien_flight_add_words(int16_t left, int16_t right)
{
    return alien_flight_word_from_u16((uint16_t)((uint16_t)left + (uint16_t)right));
}

static int16_t alien_flight_subtract_words(int16_t left, int16_t right)
{
    return alien_flight_word_from_u16((uint16_t)((uint16_t)left - (uint16_t)right));
}

static int32_t alien_flight_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -(((-(int64_t)value) + ((INT64_C(1) << count) - 1)) >> count);
}

static int alien_flight_get_slot_and_zone(ObjectRuntime *objects, uint32_t slot_index,
                                          const LevelRuntime *level, uint16_t zone_index,
                                          uint8_t **out_slot, LevelZone *out_zone,
                                          char *error, size_t error_size)
{
    if (!objects || !level || !out_slot || !out_zone ||
        slot_index >= objects->active_slot_count || zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, out_slot) ||
        !level_runtime_get_zone(level, zone_index, out_zone, error, error_size)) {
        alien_flight_set_error(error, error_size,
                               "alien flight helper received invalid source state");
        return 0;
    }
    return 1;
}

static void alien_flight_apply_floor_ceiling(uint8_t *slot, const LevelZone *zone,
                                             int32_t thing_height)
{
    int16_t half_height = alien_flight_word_from_u16(
        (uint16_t)alien_flight_asr32(thing_height, 8u));
    int16_t centre = alien_flight_word_from_u16(
        alien_flight_read_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_POSITION));
    int16_t lower_extent = alien_flight_subtract_words(centre, half_height);
    int16_t upper_extent = alien_flight_add_words(centre, half_height);
    int16_t floor = alien_flight_word_from_u16((uint16_t)alien_flight_asr32(
        slot[ALIEN_FLIGHT_SLOT_IN_UPPER_ZONE] != 0u ? zone->upper_floor : zone->floor, 7u));
    int16_t roof = alien_flight_word_from_u16((uint16_t)alien_flight_asr32(
        slot[ALIEN_FLIGHT_SLOT_IN_UPPER_ZONE] != 0u ? zone->upper_roof : zone->roof, 7u));

    /* ai_CheckFloorCeiling's source word comparisons and two boundary repairs. */
    if (upper_extent >= floor) {
        upper_extent = floor;
        lower_extent = alien_flight_subtract_words(upper_extent, half_height);
        lower_extent = alien_flight_subtract_words(lower_extent, half_height);
    }
    if (lower_extent <= roof) {
        lower_extent = roof;
        upper_extent = alien_flight_add_words(lower_extent, half_height);
        upper_extent = alien_flight_add_words(upper_extent, half_height);
    }
    centre = alien_flight_subtract_words(upper_extent, half_height);
    alien_flight_write_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_POSITION,
                            (uint16_t)centre);
}

int alien_flight_check_floor_ceiling(ObjectRuntime *objects, uint32_t slot_index,
                                     const LevelRuntime *level, uint16_t zone_index,
                                     int32_t thing_height,
                                     char *error, size_t error_size)
{
    uint8_t *slot;
    LevelZone zone;

    if (!alien_flight_get_slot_and_zone(objects, slot_index, level, zone_index,
                                        &slot, &zone, error, error_size)) {
        return 0;
    }
    alien_flight_apply_floor_ceiling(slot, &zone, thing_height);
    return 1;
}

int alien_flight_move_toward_height(ObjectRuntime *objects, uint32_t slot_index,
                                    const LevelRuntime *level, uint16_t zone_index,
                                    int16_t target_height, int32_t thing_height,
                                    char *error, size_t error_size)
{
    uint8_t *slot;
    LevelZone zone;
    int16_t current_height;
    int16_t velocity;

    if (!alien_flight_get_slot_and_zone(objects, slot_index, level, zone_index,
                                        &slot, &zone, error, error_size)) {
        return 0;
    }
    current_height = alien_flight_word_from_u16(
        alien_flight_read_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_POSITION));
    velocity = alien_flight_word_from_u16(
        alien_flight_read_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_VELOCITY));
    if (target_height > current_height) {
        velocity = alien_flight_add_words(velocity, 2);
        if (velocity >= ALIEN_FLIGHT_MAX_VELOCITY) {
            velocity = ALIEN_FLIGHT_MAX_VELOCITY;
        }
    } else {
        velocity = alien_flight_subtract_words(velocity, 2);
        if (velocity <= ALIEN_FLIGHT_MIN_VELOCITY) {
            velocity = ALIEN_FLIGHT_MIN_VELOCITY;
        }
    }
    alien_flight_write_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_VELOCITY,
                            (uint16_t)velocity);
    alien_flight_write_be16(slot + ALIEN_FLIGHT_SLOT_VERTICAL_POSITION,
                            (uint16_t)alien_flight_add_words(current_height, velocity));
    alien_flight_apply_floor_ceiling(slot, &zone, thing_height);
    return 1;
}

int alien_flight_move_toward_player_height(ObjectRuntime *objects, uint32_t slot_index,
                                           const LevelRuntime *level, uint16_t zone_index,
                                           const PlayerRuntime *player,
                                           int32_t thing_height,
                                           char *error, size_t error_size)
{
    if (!player) {
        alien_flight_set_error(error, error_size,
                               "ai_FlyToPlayerHeight requires the source player state");
        return 0;
    }
    return alien_flight_move_toward_height(
        objects, slot_index, level, zone_index,
        alien_flight_word_from_u16((uint16_t)alien_flight_asr32(player->y, 7u)),
        thing_height, error, error_size);
}

int alien_flight_move_toward_control_point_height(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    uint16_t zone_index, uint16_t control_point_index, int32_t thing_height,
    char *error, size_t error_size)
{
    LevelControlPoint control_point;

    if (!level || !level_runtime_get_control_point(level, control_point_index,
                                                    &control_point, error, error_size)) {
        alien_flight_set_error(error, error_size,
                               "ai_FlyToCPTHeight control point is outside the source table");
        return 0;
    }
    return alien_flight_move_toward_height(objects, slot_index, level, zone_index,
                                           control_point.height, thing_height,
                                           error, error_size);
}
