#include "object_passives.h"

#include <stdio.h>

enum {
    /* defs.i ObjT/EntT/ShotT source offsets. */
    OBJECT_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_SLOT_GRAPHICS_WORD = 6u,
    OBJECT_SLOT_GRAPHICS_LONG = 8u,
    OBJECT_SLOT_ZONE_ID = 12u,
    OBJECT_SLOT_TYPE_ID = 16u,
    OBJECT_SLOT_HIT_POINTS = 18u,
    OBJECT_SLOT_DAMAGE_TAKEN = 19u,
    OBJECT_SLOT_ENTITY_TYPE = 54u,
    OBJECT_SLOT_TIMER1 = 34u,
    OBJECT_SLOT_CURRENT_ANGLE = 30u,
    OBJECT_SLOT_WORRY = 62u,
    OBJECT_SLOT_IN_UPPER_ZONE = 63u,
    OBJECT_TYPE_OBJECT = 1u,
    OBJECT_BEHAVIOUR_DESTRUCTIBLE = 2u,
    OBJECT_BEHAVIOUR_DECORATION = 3u
};

static void object_passives_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_passives_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_passives_read_be16s(const uint8_t *source)
{
    return (int16_t)object_passives_read_be16(source);
}

static void object_passives_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_passives_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int32_t object_passives_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -((-(int64_t)value + 127) >> 7);
}

static int object_passives_place_slot(const LevelRuntime *level,
                                      const GameObjectDefinition *definition, uint8_t *slot,
                                      char *error, size_t error_size)
{
    LevelZone zone;
    int16_t zone_id = object_passives_read_be16s(slot + OBJECT_SLOT_ZONE_ID);
    int32_t height;

    if (zone_id < 0 || (uint16_t)zone_id >= level->zone_count ||
        !level_runtime_get_zone(level, (uint16_t)zone_id, &zone, error, error_size)) {
        object_passives_set_error(error, error_size,
                                  "passive object has an invalid source zone");
        return 0;
    }
    if (definition->floor_ceiling == 0u) {
        height = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_floor : zone.floor;
    } else {
        height = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_roof : zone.roof;
    }
    object_passives_write_be16(slot + OBJECT_SLOT_VERTICAL_POSITION,
                               (uint16_t)object_passives_asr32_7(height));
    return 1;
}

static int object_passives_apply_animation(const GameLink *game_link,
                                           const GameObjectDefinition *definition,
                                           GameObjectAnimationKind kind, uint8_t *slot,
                                           char *error, size_t error_size)
{
    GameObjectAnimationFrame frame;
    uint16_t object_type = slot[OBJECT_SLOT_ENTITY_TYPE];
    uint16_t frame_index = object_passives_read_be16(slot + OBJECT_SLOT_TIMER1);
    int16_t vertical_adjustment;

    if (!game_link_get_object_animation_frame(game_link, kind, object_type, frame_index,
                                              &frame, error, error_size)) {
        return 0;
    }
    object_passives_write_be32(slot + OBJECT_SLOT_GRAPHICS_LONG, 0u);
    if (definition->graphics_type == 1u) {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_passives_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, UINT16_MAX);
        object_passives_write_be16(
            slot + OBJECT_SLOT_CURRENT_ANGLE,
            (uint16_t)((uint32_t)object_passives_read_be16(
                slot + OBJECT_SLOT_CURRENT_ANGLE) + frame.word_2));
    } else if (definition->graphics_type > 1u) {
        object_passives_write_be16(
            slot + OBJECT_SLOT_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame.byte_0);
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_passives_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    } else {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_passives_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    }
    vertical_adjustment = (int16_t)((int16_t)frame.signed_byte_4 * 2);
    object_passives_write_be16(
        slot + OBJECT_SLOT_VERTICAL_POSITION,
        (uint16_t)((uint32_t)object_passives_read_be16(slot + OBJECT_SLOT_VERTICAL_POSITION) +
                   (uint16_t)vertical_adjustment));
    object_passives_write_be16(slot + OBJECT_SLOT_TIMER1, frame.next_timer1);
    return 1;
}

int object_passives_update_slot(ObjectRuntime *objects, uint32_t slot_index,
                                const LevelRuntime *level, const GameLink *game_link,
                                const GameObjectDefinition *definition,
                                char *error, size_t error_size)
{
    uint8_t *slot;

    if (!objects || !level || !game_link || !definition ||
        slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count) {
        object_passives_set_error(error, error_size,
                                  "passive object update received invalid source state");
        return 0;
    }
    if (!object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        object_passives_set_error(error, error_size,
                                  "passive object slot is outside the owned source list");
        return 0;
    }
    if (slot[OBJECT_SLOT_TYPE_ID] != OBJECT_TYPE_OBJECT ||
        object_passives_read_be16s(slot + OBJECT_SLOT_ZONE_ID) < 0) {
        return 1;
    }
    if (definition->behaviour == OBJECT_BEHAVIOUR_DESTRUCTIBLE) {
        if ((uint16_t)slot[OBJECT_SLOT_DAMAGE_TAKEN] < definition->hit_points) {
            /* StillHere calls AI_LookForPlayer1; AI worry selection is not ported yet. */
            slot[OBJECT_SLOT_HIT_POINTS] = 1u;
            return 1;
        }
        if (slot[OBJECT_SLOT_HIT_POINTS] != 0u) {
            /* Narrative messages are UI work; retain only the state transition. */
            object_passives_write_be16(slot + OBJECT_SLOT_TIMER1, 0u);
        }
        slot[OBJECT_SLOT_HIT_POINTS] = 0u;
        if (slot[OBJECT_SLOT_WORRY] == 0u) {
            return 1;
        }
        return object_passives_place_slot(level, definition, slot, error, error_size) &&
            object_passives_apply_animation(game_link, definition,
                                            GAME_LINK_OBJECT_ANIMATION_ACTION, slot,
                                            error, error_size);
    }
    if (definition->behaviour == OBJECT_BEHAVIOUR_DECORATION) {
        if (slot[OBJECT_SLOT_WORRY] == 0u) {
            return 1;
        }
        return object_passives_place_slot(level, definition, slot, error, error_size) &&
            object_passives_apply_animation(game_link, definition,
                                            GAME_LINK_OBJECT_ANIMATION_DEFAULT, slot,
                                            error, error_size);
    }
    object_passives_set_error(error, error_size,
                              "passive object does not have a source passive behaviour");
    return 0;
}
