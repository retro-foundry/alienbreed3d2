#include "object_collectables.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* defs.i source offsets in an ObjT object/ShotT overlay. */
    OBJECT_SLOT_POINT_INDEX = 0u,
    OBJECT_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_SLOT_ZONE_ID = 12u,
    OBJECT_SLOT_TYPE_ID = 16u,
    OBJECT_SLOT_ENTITY_TYPE = 54u,
    OBJECT_SLOT_WHICH_ANIMATION = 55u,
    OBJECT_SLOT_DOORS_AND_LIFTS_HELD = 50u,
    OBJECT_SLOT_WORRY = 62u,
    OBJECT_TYPE_OBJECT = 1u,
    OBJECT_BEHAVIOUR_COLLECTABLE = 0u
};

static void object_collectables_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_collectables_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_collectables_read_be16s(const uint8_t *source)
{
    return (int16_t)object_collectables_read_be16(source);
}

static uint32_t object_collectables_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static void object_collectables_write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

/* 68000 ASR follows a negative value toward negative infinity. */
static int32_t object_collectables_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -((-(int64_t)value + ((INT64_C(1) << count) - 1)) >> count);
}

static int16_t object_collectables_abs16(int16_t value)
{
    return value < 0 ? (int16_t)(0u - (uint16_t)value) : value;
}

static int object_collectables_player_hits_slot(const PlayerRuntime *player,
                                                const uint8_t *slot,
                                                const uint8_t *point_bytes,
                                                const GameObjectDefinition *definition)
{
    int16_t player_vertical;
    int16_t object_vertical;
    int16_t vertical_delta;
    int16_t point_x;
    int16_t point_z;
    int16_t player_x;
    int16_t player_z;
    int16_t horizontal_x;
    int16_t horizontal_z;
    int32_t distance_squared;
    int32_t radius_squared;

    /* newaliencontrol.s:Plr1_CheckObjectCollide. */
    player_vertical = (int16_t)object_collectables_asr32(
        (int32_t)((uint32_t)player->tmp_y + (uint32_t)(player->tmp_height / 2)), 7u);
    object_vertical = object_collectables_read_be16s(slot + OBJECT_SLOT_VERTICAL_POSITION);
    vertical_delta = object_collectables_abs16(
        (int16_t)((int32_t)player_vertical - object_vertical));
    if (vertical_delta > (int16_t)definition->collision_height) {
        return 0;
    }

    /* ObjT point indexes address Vec2L, but the source reads each first word. */
    point_x = object_collectables_read_be16s(point_bytes + 0u);
    point_z = object_collectables_read_be16s(point_bytes + 4u);
    player_x = (int16_t)(uint16_t)player->tmp_x;
    player_z = (int16_t)(uint16_t)player->tmp_z;
    horizontal_x = (int16_t)((int32_t)point_x - player_x);
    horizontal_z = (int16_t)((int32_t)point_z - player_z);
    distance_squared = (int32_t)horizontal_x * horizontal_x +
        (int32_t)horizontal_z * horizontal_z;
    radius_squared = (int32_t)(int16_t)definition->collision_radius *
        (int32_t)(int16_t)definition->collision_radius;
    /* newaliencontrol.s:CheckHit returns true only for a strict less-than. */
    return distance_squared < radius_squared;
}

int object_collectables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint32_t *out_collected_count,
    char *error, size_t error_size)
{
    uint32_t collected_count = 0u;

    if (!objects || !level || !game_link || !player || !inventory || !limits ||
        objects->active_slot_count > objects->slot_count ||
        player->zone_index >= level->zone_count) {
        object_collectables_set_error(error, error_size,
                                     "collectable update received invalid source state");
        return 0;
    }

    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        uint8_t *slot;
        int16_t zone_id;
        uint8_t entity_type;
        GameObjectDefinition definition;
        uint16_t point_index;
        uint8_t *point_bytes;
        LevelZone zone;
        int32_t floor_or_roof;
        GameInventory grant;
        int collectable;

        if (!object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
            object_collectables_set_error(error, error_size,
                                         "owned ObjT slot is outside the runtime list");
            return 0;
        }
        if (slot[OBJECT_SLOT_TYPE_ID] != OBJECT_TYPE_OBJECT ||
            slot[OBJECT_SLOT_WHICH_ANIMATION] != 0u) {
            continue;
        }
        zone_id = object_collectables_read_be16s(slot + OBJECT_SLOT_ZONE_ID);
        if (zone_id < 0 || (uint16_t)zone_id != player->zone_index ||
            slot[OBJECT_SLOT_WORRY + 1u] != player->stood_in_top) {
            continue;
        }
        entity_type = slot[OBJECT_SLOT_ENTITY_TYPE];
        if (!game_link_get_object_definition(game_link, entity_type, &definition,
                                             error, error_size)) {
            return 0;
        }
        if (definition.behaviour != OBJECT_BEHAVIOUR_COLLECTABLE) {
            continue;
        }
        point_index = object_collectables_read_be16(slot + OBJECT_SLOT_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, point_index, &point_bytes) ||
            !level_runtime_get_zone(level, (uint16_t)zone_id, &zone, error, error_size)) {
            object_collectables_set_error(error, error_size,
                                         "collectable source record has an invalid point or zone");
            return 0;
        }

        /* ItsAnObject/Collectable's visible-object floor/roof placement. */
        if (definition.floor_ceiling == 0u) {
            floor_or_roof = player->stood_in_top != 0u ? zone.upper_floor : zone.floor;
        } else {
            floor_or_roof = player->stood_in_top != 0u ? zone.upper_roof : zone.roof;
        }
        object_collectables_write_be16(slot + OBJECT_SLOT_VERTICAL_POSITION,
                                       (uint16_t)object_collectables_asr32(floor_or_roof, 7u));

        if (!object_collectables_player_hits_slot(player, slot, point_bytes, &definition)) {
            continue;
        }
        if (!game_link_get_object_inventory_grant(game_link, entity_type, &grant,
                                                  error, error_size)) {
            return 0;
        }
        collectable = object_collectables_read_be32(slot + OBJECT_SLOT_DOORS_AND_LIFTS_HELD) != 0u ||
            game_inventory_can_collect_single_player(inventory, &grant, limits);
        if (!collectable) {
            continue;
        }
        game_inventory_apply_grant(inventory, &grant, limits);
        /* Plr1_CollectItem / Collectable remove the source slot on success. */
        object_collectables_write_be16(slot + OBJECT_SLOT_ZONE_ID, UINT16_MAX);
        slot[OBJECT_SLOT_WORRY] = 0u;
        if (collected_count == UINT32_MAX) {
            object_collectables_set_error(error, error_size, "too many collected source objects");
            return 0;
        }
        ++collected_count;
    }
    if (out_collected_count) {
        *out_collected_count = collected_count;
    }
    return 1;
}
