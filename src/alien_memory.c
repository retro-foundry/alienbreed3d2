#include "alien_memory.h"

#include <stdio.h>

enum {
    /* defs.i:ObjT/EntT fields consumed by modules/ai.s:ai_StorePlayerPosition. */
    ALIEN_MEMORY_SLOT_POINT_INDEX = 0u,
    ALIEN_MEMORY_SLOT_TEAM_NUMBER = 21u
};

static void alien_memory_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_memory_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint16_t alien_memory_player_control_point(const LevelZone *zone,
                                                   const PlayerRuntime *player)
{
    /* ZoneT_ControlPoint_w's byte order selects lower at +0 and upper at +1. */
    return player->stood_in_top != 0u ? (uint16_t)(zone->control_point & 0x00ffu) :
                                        (uint16_t)(zone->control_point >> 8u);
}

int alien_memory_store_player_position(AlienRuntime *alien_runtime,
                                       ObjectRuntime *objects, uint32_t slot_index,
                                       const LevelRuntime *level,
                                       const PlayerRuntime *player,
                                       char *error, size_t error_size)
{
    uint8_t *slot;
    uint16_t point_index;
    int8_t team_number;
    LevelZone player_zone;
    int16_t player_x;
    int16_t player_z;
    int16_t player_zone_id;
    int16_t player_control_point;

    if (!alien_runtime || !objects || !level || !player ||
        slot_index >= objects->active_slot_count || player->zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        !level_runtime_get_zone(level, player->zone_index, &player_zone, error, error_size)) {
        alien_memory_set_error(error, error_size,
                               "ai_StorePlayerPosition received invalid source state");
        return 0;
    }
    point_index = alien_memory_read_be16(slot + ALIEN_MEMORY_SLOT_POINT_INDEX);
    if (point_index >= ALIEN_RUNTIME_ENTITY_COUNT) {
        alien_memory_set_error(error, error_size,
                               "ai_StorePlayerPosition object point exceeds AI workspace");
        return 0;
    }
    player_x = player_runtime_position_to_world(player->x);
    player_z = player_runtime_position_to_world(player->z);
    player_zone_id = (int16_t)player_zone.id;
    player_control_point = (int16_t)alien_memory_player_control_point(&player_zone, player);

    /* modules/ai.s writes the entity's LastX/LastY/LastZone/LastControlPoint words. */
    alien_runtime->entity_workspace[point_index][0u] = player_x;
    alien_runtime->entity_workspace[point_index][1u] = player_z;
    alien_runtime->entity_workspace[point_index][2u] = player_zone_id;
    alien_runtime->entity_workspace[point_index][3u] = player_control_point;

    team_number = (int8_t)slot[ALIEN_MEMORY_SLOT_TEAM_NUMBER];
    if (team_number < 0) {
        return 1;
    }
    if ((uint8_t)team_number >= ALIEN_RUNTIME_TEAM_COUNT) {
        alien_memory_set_error(error, error_size,
                               "ai_StorePlayerPosition team exceeds source workspace");
        return 0;
    }
    alien_runtime->team_workspace[(uint8_t)team_number][0u] = player_x;
    alien_runtime->team_workspace[(uint8_t)team_number][1u] = player_z;
    alien_runtime->team_workspace[(uint8_t)team_number][2u] = player_zone_id;
    alien_runtime->team_workspace[(uint8_t)team_number][3u] = player_control_point;
    alien_runtime->team_workspace[(uint8_t)team_number][4u] = (int16_t)point_index;
    return 1;
}
