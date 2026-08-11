#include "object_activatables.h"

#include <limits.h>
#include <stdio.h>

#include "object_collectables.h"

enum {
    /* defs.i ObjT/EntT/ShotT source offsets. */
    OBJECT_SLOT_POINT_INDEX = 0u,
    OBJECT_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_SLOT_GRAPHICS_WORD = 6u,
    OBJECT_SLOT_GRAPHICS_LONG = 8u,
    OBJECT_SLOT_ZONE_ID = 12u,
    OBJECT_SLOT_TYPE_ID = 16u,
    OBJECT_SLOT_ENTITY_TYPE = 54u,
    OBJECT_SLOT_WHICH_ANIMATION = 55u,
    OBJECT_SLOT_CURRENT_ANGLE = 30u,
    OBJECT_SLOT_TIMER1 = 34u,
    OBJECT_SLOT_TIMER2 = 40u,
    OBJECT_SLOT_WORRY = 62u,
    OBJECT_SLOT_IN_UPPER_ZONE = 63u,
    OBJECT_TYPE_OBJECT = 1u,
    OBJECT_BEHAVIOUR_ACTIVATABLE = 1u
};

static void object_activatables_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_activatables_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_activatables_read_be16s(const uint8_t *source)
{
    return (int16_t)object_activatables_read_be16(source);
}

static void object_activatables_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_activatables_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int32_t object_activatables_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -((-(int64_t)value + 127) >> 7);
}

static int16_t object_activatables_abs16(int16_t value)
{
    return value < 0 ? (int16_t)(0u - (uint16_t)value) : value;
}

static int object_activatables_player_hits_slot(const PlayerRuntime *player,
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

    player_vertical = (int16_t)object_activatables_asr32_7(
        (int32_t)((uint32_t)player->tmp_y + (uint32_t)(player->tmp_height / 2)));
    object_vertical = object_activatables_read_be16s(slot + OBJECT_SLOT_VERTICAL_POSITION);
    vertical_delta = object_activatables_abs16(
        (int16_t)((int32_t)player_vertical - object_vertical));
    if (vertical_delta > (int16_t)definition->collision_height) {
        return 0;
    }
    point_x = object_activatables_read_be16s(point_bytes + 0u);
    point_z = object_activatables_read_be16s(point_bytes + 4u);
    player_x = player_runtime_position_to_world(player->tmp_x);
    player_z = player_runtime_position_to_world(player->tmp_z);
    horizontal_x = (int16_t)((int32_t)point_x - player_x);
    horizontal_z = (int16_t)((int32_t)point_z - player_z);
    distance_squared = (int32_t)horizontal_x * horizontal_x +
        (int32_t)horizontal_z * horizontal_z;
    radius_squared = (int32_t)(int16_t)definition->collision_radius *
        (int32_t)(int16_t)definition->collision_radius;
    return distance_squared < radius_squared;
}

static int object_activatables_place_slot(const LevelRuntime *level,
                                          const GameObjectDefinition *definition, uint8_t *slot,
                                          char *error, size_t error_size)
{
    LevelZone zone;
    int16_t zone_id = object_activatables_read_be16s(slot + OBJECT_SLOT_ZONE_ID);
    int32_t height;

    if (zone_id < 0 || (uint16_t)zone_id >= level->zone_count ||
        !level_runtime_get_zone(level, (uint16_t)zone_id, &zone, error, error_size)) {
        object_activatables_set_error(error, error_size,
                                      "activatable has an invalid source zone");
        return 0;
    }
    if (definition->floor_ceiling == 0u) {
        height = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_floor : zone.floor;
    } else {
        height = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_roof : zone.roof;
    }
    object_activatables_write_be16(slot + OBJECT_SLOT_VERTICAL_POSITION,
                                   (uint16_t)object_activatables_asr32_7(height));
    return 1;
}

static int object_activatables_apply_animation(const GameLink *game_link,
                                                const GameObjectDefinition *definition,
                                                GameObjectAnimationKind kind,
                                                uint8_t *slot,
                                                char *error, size_t error_size)
{
    GameObjectAnimationFrame frame;
    uint16_t object_type = slot[OBJECT_SLOT_ENTITY_TYPE];
    uint16_t frame_index = object_activatables_read_be16(slot + OBJECT_SLOT_TIMER1);
    int16_t vertical_adjustment;

    if (!game_link_get_object_animation_frame(game_link, kind, object_type, frame_index,
                                              &frame, error, error_size)) {
        return 0;
    }
    object_activatables_write_be32(slot + OBJECT_SLOT_GRAPHICS_LONG, 0u);
    if (definition->graphics_type == 1u) {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_activatables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, UINT16_MAX);
        object_activatables_write_be16(
            slot + OBJECT_SLOT_CURRENT_ANGLE,
            (uint16_t)((uint32_t)object_activatables_read_be16(slot + OBJECT_SLOT_CURRENT_ANGLE) +
                       frame.word_2));
    } else if (definition->graphics_type > 1u) {
        object_activatables_write_be16(
            slot + OBJECT_SLOT_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame.byte_0);
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_activatables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    } else {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_activatables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    }
    vertical_adjustment = (int16_t)((int16_t)frame.signed_byte_4 * 2);
    object_activatables_write_be16(
        slot + OBJECT_SLOT_VERTICAL_POSITION,
        (uint16_t)((uint32_t)object_activatables_read_be16(slot + OBJECT_SLOT_VERTICAL_POSITION) +
                   (uint16_t)vertical_adjustment));
    object_activatables_write_be16(slot + OBJECT_SLOT_TIMER1, frame.next_timer1);
    return 1;
}

static int object_activatables_update_range_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint32_t first_slot, uint32_t slot_limit,
    uint16_t frame_ticks, MessageRuntime *messages, uint8_t messages_enabled,
    uint64_t message_time_milliseconds, GameAudioEvents *audio_events,
    char *error, size_t error_size)
{
    if (!objects || !level || !game_link || !player || !inventory || !limits || !messages ||
        objects->active_slot_count > objects->slot_count || first_slot > slot_limit ||
        slot_limit > objects->active_slot_count ||
        player->zone_index >= level->zone_count) {
        object_activatables_set_error(error, error_size,
                                      "activatable update received invalid source state");
        return 0;
    }

    for (uint32_t slot_index = first_slot; slot_index < slot_limit; ++slot_index) {
        uint8_t *slot;
        uint8_t *point_bytes;
        GameObjectDefinition definition;
        uint16_t point_index;
        int16_t zone_id;
        int active;
        int player_hits;

        if (!object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
            object_activatables_set_error(error, error_size,
                                          "owned activatable slot is outside the runtime list");
            return 0;
        }
        if (slot[OBJECT_SLOT_TYPE_ID] != OBJECT_TYPE_OBJECT) {
            continue;
        }
        zone_id = object_activatables_read_be16s(slot + OBJECT_SLOT_ZONE_ID);
        if (zone_id < 0 || slot[OBJECT_SLOT_WORRY] == 0u) {
            continue;
        }
        if (!game_link_get_object_definition(game_link, slot[OBJECT_SLOT_ENTITY_TYPE],
                                             &definition, error, error_size)) {
            return 0;
        }
        if (definition.behaviour != OBJECT_BEHAVIOUR_ACTIVATABLE) {
            continue;
        }
        if (!object_activatables_place_slot(level, &definition, slot, error, error_size)) {
            return 0;
        }
        slot[OBJECT_SLOT_WORRY] &= 0x80u;
        point_index = object_activatables_read_be16(slot + OBJECT_SLOT_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, point_index, &point_bytes)) {
            object_activatables_set_error(error, error_size,
                                          "activatable has an invalid source point");
            return 0;
        }
        active = slot[OBJECT_SLOT_WHICH_ANIMATION] != 0u;
        if (!object_activatables_apply_animation(
                game_link, &definition,
                active != 0 ? GAME_LINK_OBJECT_ANIMATION_ACTION :
                              GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                slot, error, error_size)) {
            return 0;
        }
        player_hits = object_activatables_player_hits_slot(player, slot, point_bytes,
                                                            &definition);
        if (active == 0) {
            if (player_hits != 0 && player->tmp_used != 0u) {
                uint8_t collected;

                /* Activatable ignores Plr1_CollectItem's result, as the source does. */
                if (!object_collectables_collect_item_single_player(
                        level, game_link, &definition, slot, point_bytes, point_index,
                        inventory, limits, messages, messages_enabled,
                        message_time_milliseconds, audio_events, &collected,
                        error, error_size)) {
                    return 0;
                }
                object_activatables_write_be16(slot + OBJECT_SLOT_TIMER1, 0u);
                slot[OBJECT_SLOT_WHICH_ANIMATION] = UINT8_MAX;
                object_activatables_write_be16(slot + OBJECT_SLOT_TIMER2, 0u);
            }
        } else {
            int deactivate = player_hits != 0 && player->tmp_used != 0u;
            uint16_t elapsed = (uint16_t)(object_activatables_read_be16(
                slot + OBJECT_SLOT_TIMER2) + frame_ticks);

            object_activatables_write_be16(slot + OBJECT_SLOT_TIMER2, elapsed);
            if (definition.active_timeout >= 0 &&
                (int16_t)elapsed >= definition.active_timeout) {
                deactivate = 1;
            }
            if (deactivate != 0) {
                object_activatables_write_be16(slot + OBJECT_SLOT_TIMER1, 0u);
                slot[OBJECT_SLOT_WHICH_ANIMATION] = 0u;
            }
        }
    }
    return 1;
}

int object_activatables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    MessageRuntime *messages, uint8_t messages_enabled,
    uint64_t message_time_milliseconds, GameAudioEvents *audio_events,
    char *error, size_t error_size)
{
    return object_activatables_update_range_single_player(
        objects, level, game_link, player, inventory, limits, 0u,
        objects ? objects->active_slot_count : 0u, frame_ticks,
        messages, messages_enabled, message_time_milliseconds, audio_events,
        error, error_size);
}

int object_activatables_update_slot_single_player(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    MessageRuntime *messages, uint8_t messages_enabled,
    uint64_t message_time_milliseconds, GameAudioEvents *audio_events,
    char *error, size_t error_size)
{
    if (!objects || slot_index >= objects->active_slot_count) {
        object_activatables_set_error(error, error_size,
                                      "source activatable slot is outside ObjectHandler's list");
        return 0;
    }
    return object_activatables_update_range_single_player(
        objects, level, game_link, player, inventory, limits, slot_index, slot_index + 1u,
        frame_ticks, messages, messages_enabled, message_time_milliseconds, audio_events,
        error, error_size);
}
