#include "alien_spatial.h"

#include <stdio.h>

enum {
    /* defs.i: ObjT/EntT fields used by modules/ai.s's spatial helpers. */
    ALIEN_SPATIAL_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_SPATIAL_SLOT_ZONE_ID = 12u,
    ALIEN_SPATIAL_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_SPATIAL_SLOT_CURRENT_CONTROL_POINT = 28u,
    ALIEN_SPATIAL_SLOT_IN_UPPER_ZONE = 63u
};

static void alien_spatial_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_spatial_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_spatial_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static int32_t alien_spatial_asr32_1(int32_t value)
{
    if (value >= 0) {
        return value >> 1;
    }
    return -(((-(int64_t)value) + 1) >> 1);
}

static int32_t alien_spatial_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -(((-(int64_t)value) + 127) >> 7);
}

static int alien_spatial_get_slot_and_zone(ObjectRuntime *objects, uint32_t slot_index,
                                           const LevelRuntime *level, uint16_t zone_index,
                                           uint8_t **out_slot, LevelZone *out_zone,
                                           char *error, size_t error_size)
{
    if (!objects || !level || !out_slot || !out_zone ||
        slot_index >= objects->active_slot_count || zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, out_slot) ||
        !level_runtime_get_zone(level, zone_index, out_zone, error, error_size)) {
        alien_spatial_set_error(error, error_size,
                                "alien room helper received invalid source state");
        return 0;
    }
    return 1;
}

static void alien_spatial_apply_room_stats(uint8_t *slot, const LevelZone *zone,
                                           int32_t thing_height)
{
    int32_t floor = slot[ALIEN_SPATIAL_SLOT_IN_UPPER_ZONE] != 0u ?
        zone->upper_floor : zone->floor;
    int32_t centre_height = alien_spatial_asr32_7(
        (int32_t)((uint32_t)floor - (uint32_t)alien_spatial_asr32_1(thing_height)));

    /* ai_GetRoomStatsStill: zone ID, floor-centred word Y, then entity zone. */
    alien_spatial_write_be16(slot + ALIEN_SPATIAL_SLOT_ZONE_ID, zone->id);
    alien_spatial_write_be16(slot + ALIEN_SPATIAL_SLOT_VERTICAL_POSITION,
                             (uint16_t)centre_height);
    alien_spatial_write_be16(slot + ALIEN_SPATIAL_SLOT_ENTITY_ZONE_ID, zone->id);
}

int alien_spatial_store_room_stats(ObjectRuntime *objects, uint32_t slot_index,
                                   const LevelRuntime *level, uint16_t zone_index,
                                   int16_t new_x, int16_t new_z, int32_t thing_height,
                                   char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    LevelZone zone;
    int16_t point_index;

    if (!alien_spatial_get_slot_and_zone(objects, slot_index, level, zone_index,
                                         &slot, &zone, error, error_size)) {
        return 0;
    }
    point_index = (int16_t)alien_spatial_read_be16(slot);
    if (point_index < 0 || (uint32_t)point_index >= objects->point_count ||
        !object_runtime_get_point_bytes(objects, (uint32_t)point_index, &point)) {
        alien_spatial_set_error(error, error_size,
                                "ai_GetRoomStats object point is outside the source table");
        return 0;
    }
    /* ai_GetRoomStats stores only the first word of each source Vec2L. */
    alien_spatial_write_be16(point, (uint16_t)new_x);
    alien_spatial_write_be16(point + 4u, (uint16_t)new_z);
    alien_spatial_apply_room_stats(slot, &zone, thing_height);
    return 1;
}

int alien_spatial_store_room_stats_still(ObjectRuntime *objects, uint32_t slot_index,
                                         const LevelRuntime *level, uint16_t zone_index,
                                         int32_t thing_height,
                                         char *error, size_t error_size)
{
    uint8_t *slot;
    LevelZone zone;

    if (!alien_spatial_get_slot_and_zone(objects, slot_index, level, zone_index,
                                         &slot, &zone, error, error_size)) {
        return 0;
    }
    alien_spatial_apply_room_stats(slot, &zone, thing_height);
    return 1;
}

int alien_spatial_store_current_control_point(ObjectRuntime *objects, uint32_t slot_index,
                                              const LevelRuntime *level,
                                              uint16_t zone_index,
                                              char *error, size_t error_size)
{
    uint8_t *slot;
    LevelZone zone;
    uint8_t control_point;

    if (!alien_spatial_get_slot_and_zone(objects, slot_index, level, zone_index,
                                         &slot, &zone, error, error_size)) {
        return 0;
    }
    control_point = slot[ALIEN_SPATIAL_SLOT_IN_UPPER_ZONE] != 0u ?
        (uint8_t)zone.control_point : (uint8_t)(zone.control_point >> 8u);
    alien_spatial_write_be16(slot + ALIEN_SPATIAL_SLOT_CURRENT_CONTROL_POINT,
                             control_point);
    return 1;
}
