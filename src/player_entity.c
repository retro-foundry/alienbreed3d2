#include "player_entity.h"

#include <stdint.h>
#include <stdio.h>

enum {
    /* defs.i:ObjT/EntT/ShotT offsets used by hires.s:Plr1_Use. */
    PLAYER_ENTITY_POINT_INDEX_OFFSET = 0u,
    PLAYER_ENTITY_VERTICAL_POSITION_OFFSET = 4u,
    PLAYER_ENTITY_ZONE_ID_OFFSET = 12u,
    PLAYER_ENTITY_TYPE_ID_OFFSET = 16u,
    PLAYER_ENTITY_HIT_POINTS_OFFSET = 18u,
    PLAYER_ENTITY_CURRENT_ANGLE_OFFSET = 30u,
    PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET = 63u,
    PLAYER_ENTITY_TYPE_PLAYER1 = 4u
};

static void player_entity_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t player_entity_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void player_entity_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void player_entity_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int32_t player_entity_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -((-(int64_t)value + 127) >> 7);
}

int player_entity_sync_single_player(ObjectRuntime *objects, const LevelRuntime *level,
                                     const PlayerRuntime *player,
                                     char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint16_t point_index;
    LevelZone zone;
    int32_t middle_height;

    if (!objects || !level || !player || player->zone_index >= level->zone_count ||
        !object_runtime_get_player1_slot_bytes(objects, &slot)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use received an invalid player entity or source zone");
        return 0;
    }
    point_index = player_entity_read_be16(slot + PLAYER_ENTITY_POINT_INDEX_OFFSET);
    if (!object_runtime_get_point_bytes(objects, point_index, &point) ||
        !level_runtime_get_zone(level, player->zone_index, &zone, error, error_size)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use player entity references an invalid source point or zone");
        return 0;
    }

    /* hires.s:Plr1_Use publishes current player position to its ObjT point. */
    player_entity_write_be32(point + 0u, (uint32_t)player->x);
    player_entity_write_be32(point + 4u, (uint32_t)player->z);
    slot[PLAYER_ENTITY_TYPE_ID_OFFSET] = PLAYER_ENTITY_TYPE_PLAYER1;
    slot[PLAYER_ENTITY_HIT_POINTS_OFFSET] = 10u;
    player_entity_write_be16(slot + PLAYER_ENTITY_CURRENT_ANGLE_OFFSET, player->tmp_yaw);
    slot[PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET] = player->stood_in_top;
    player_entity_write_be16(slot + PLAYER_ENTITY_ZONE_ID_OFFSET, zone.id);
    middle_height = (int32_t)((uint32_t)player->tmp_y +
                              (uint32_t)(player->tmp_height / 2));
    player_entity_write_be16(slot + PLAYER_ENTITY_VERTICAL_POSITION_OFFSET,
                             (uint16_t)player_entity_asr32_7(middle_height));
    return 1;
}
