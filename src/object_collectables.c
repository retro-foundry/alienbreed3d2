#include "object_collectables.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* defs.i source offsets in an ObjT object/ShotT overlay. */
    OBJECT_SLOT_POINT_INDEX = 0u,
    OBJECT_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_SLOT_GRAPHICS_WORD = 6u,
    OBJECT_SLOT_GRAPHICS_LONG = 8u,
    OBJECT_SLOT_ZONE_ID = 12u,
    OBJECT_SLOT_TYPE_ID = 16u,
    OBJECT_SLOT_DISPLAY_TEXT = 24u,
    OBJECT_SLOT_CURRENT_ANGLE = 30u,
    OBJECT_SLOT_TIMER1 = 34u,
    OBJECT_SLOT_TIMER2 = 40u,
    OBJECT_SLOT_ENTITY_TYPE = 54u,
    OBJECT_SLOT_WHICH_ANIMATION = 55u,
    OBJECT_SLOT_DOORS_AND_LIFTS_HELD = 50u,
    OBJECT_SLOT_WORRY = 62u,
    OBJECT_SLOT_IN_UPPER_ZONE = 63u,
    OBJECT_TYPE_OBJECT = 1u,
    OBJECT_BEHAVIOUR_COLLECTABLE = 0u,
    /* defs.i:GLFT_OBJ_NAME_LENGTH. */
    OBJECT_COLLECTABLES_OBJECT_NAME_LENGTH = 20u
};

/* data/text_data.s:Game_CantCollectItemText_vb, passed as a 160-byte narrative. */
static const uint8_t object_collectables_cant_collect_message[] =
    "I can't carry any more of these just now.";

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

static void object_collectables_write_be32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static int object_collectables_push_success_message(
    const LevelRuntime *level, const GameLink *game_link, const uint8_t *slot,
    MessageRuntime *messages, uint8_t messages_enabled, char *error, size_t error_size)
{
    int16_t display_text = object_collectables_read_be16s(slot + OBJECT_SLOT_DISPLAY_TEXT);

    if (display_text >= 0) {
        LevelNarrativeMessage message;

        /* newaliencontrol.s:Plr1_CollectItem:.can_collect display-text branch. */
        if (!level_runtime_get_narrative_message(level, (uint16_t)display_text,
                                                 &message, error, error_size) ||
            message.byte_count != MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH ||
            !message_runtime_push_line(
                messages, message.bytes,
                (uint16_t)(MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH |
                           (MESSAGE_RUNTIME_TAG_NARRATIVE << MESSAGE_RUNTIME_TAG_SHIFT)),
                messages_enabled, error, error_size)) {
            return 0;
        }
        return 1;
    }
    {
        const uint8_t *object_names;
        size_t object_names_size;
        uint8_t object_type = slot[OBJECT_SLOT_ENTITY_TYPE];

        /* .notext passes the raw fixed-width GLFT_ObjectNames_l entry. */
        if (object_type >= GAME_LINK_OBJECT_COUNT ||
            !game_link_table(game_link, GAME_LINK_TABLE_OBJECT_NAMES,
                             &object_names, &object_names_size) ||
            object_names_size != GAME_LINK_OBJECT_COUNT *
                                 OBJECT_COLLECTABLES_OBJECT_NAME_LENGTH) {
            object_collectables_set_error(error, error_size,
                                         "collectable object-name message is invalid");
            return 0;
        }
        if (!message_runtime_push_line(
                messages,
                object_names + (size_t)object_type * OBJECT_COLLECTABLES_OBJECT_NAME_LENGTH,
                (uint16_t)(OBJECT_COLLECTABLES_OBJECT_NAME_LENGTH |
                           (MESSAGE_RUNTIME_TAG_DEFAULT << MESSAGE_RUNTIME_TAG_SHIFT)),
                messages_enabled, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

static int object_collectables_push_failed_message(uint8_t *slot, MessageRuntime *messages,
                                                   uint8_t messages_enabled,
                                                   uint64_t message_time_milliseconds,
                                                   char *error, size_t error_size)
{
    uint16_t timer2 = object_collectables_read_be16(slot + OBJECT_SLOT_TIMER2);

    /* newaliencontrol.s:Plr1_CollectItem's signed Timer2 gate. */
    if ((int16_t)timer2 <= 0) {
        if (!message_runtime_push_line_dedup_last(
                messages, object_collectables_cant_collect_message,
                (uint16_t)(MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH |
                           (MESSAGE_RUNTIME_TAG_NARRATIVE << MESSAGE_RUNTIME_TAG_SHIFT)),
                messages_enabled, message_time_milliseconds, error, error_size)) {
            return 0;
        }
        timer2 = 200u;
    }
    /* The source subtracts after both its skip and message paths. */
    object_collectables_write_be16(slot + OBJECT_SLOT_TIMER2, (uint16_t)(timer2 - 1u));
    return 1;
}

int object_collectables_collect_item_single_player(
    const LevelRuntime *level, const GameLink *game_link,
    const GameObjectDefinition *definition, uint8_t *slot,
    const uint8_t *point_bytes, uint16_t point_index,
    GameInventory *inventory, const GameInventoryConsumableLimits *limits,
    MessageRuntime *messages, uint8_t messages_enabled,
    uint64_t message_time_milliseconds, GameAudioEvents *audio_events,
    uint8_t *out_collected, char *error, size_t error_size)
{
    GameInventory grant;
    int collectable;

    if (out_collected) {
        *out_collected = 0u;
    }
    if (!level || !game_link || !definition || !slot || !point_bytes || !inventory ||
        !limits || !messages || !out_collected ||
        slot[OBJECT_SLOT_ENTITY_TYPE] >= GAME_LINK_OBJECT_COUNT) {
        object_collectables_set_error(error, error_size,
                                      "Plr1_CollectItem received invalid source state");
        return 0;
    }
    if (!game_link_get_object_inventory_grant(
            game_link, slot[OBJECT_SLOT_ENTITY_TYPE], &grant, error, error_size)) {
        return 0;
    }
    collectable =
        object_collectables_read_be32(slot + OBJECT_SLOT_DOORS_AND_LIFTS_HELD) != 0u ||
        game_inventory_can_collect_single_player(inventory, &grant, limits);
    if (collectable == 0) {
        return object_collectables_push_failed_message(
            slot, messages, messages_enabled, message_time_milliseconds,
            error, error_size);
    }
    if (!object_collectables_push_success_message(
            level, game_link, slot, messages, messages_enabled, error, error_size)) {
        return 0;
    }
    game_inventory_apply_grant(inventory, &grant, limits);
    /* newaliencontrol.s:Plr1_CollectItem ODefT_SFX_w (negative is silent). */
    game_audio_events_emit(audio_events, definition->sound_effect, 80,
                           (int16_t)(object_collectables_read_be32(point_bytes) >> 16),
                           (int16_t)(object_collectables_read_be32(point_bytes + 4u) >> 16),
                           point_index, GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
    *out_collected = UINT8_MAX;
    return 1;
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
    player_x = player_runtime_position_to_world(player->tmp_x);
    player_z = player_runtime_position_to_world(player->tmp_z);
    horizontal_x = (int16_t)((int32_t)point_x - player_x);
    horizontal_z = (int16_t)((int32_t)point_z - player_z);
    distance_squared = (int32_t)horizontal_x * horizontal_x +
        (int32_t)horizontal_z * horizontal_z;
    radius_squared = (int32_t)(int16_t)definition->collision_radius *
        (int32_t)(int16_t)definition->collision_radius;
    /* newaliencontrol.s:CheckHit returns true only for a strict less-than. */
    return distance_squared < radius_squared;
}

/* newaliencontrol.s:Collectable -> DEFANIMOBJ. */
static int object_collectables_apply_default_animation(const GameLink *game_link,
                                                       const GameObjectDefinition *definition,
                                                       uint8_t *slot,
                                                       char *error, size_t error_size)
{
    GameObjectAnimationFrame frame;
    uint16_t frame_index = object_collectables_read_be16(slot + OBJECT_SLOT_TIMER1);
    int16_t vertical_adjustment;

    if (!game_link_get_object_animation_frame(
            game_link, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
            slot[OBJECT_SLOT_ENTITY_TYPE], frame_index, &frame, error, error_size)) {
        return 0;
    }
    object_collectables_write_be32(slot + OBJECT_SLOT_GRAPHICS_LONG, 0u);
    if (definition->graphics_type == 1u) {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_collectables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, UINT16_MAX);
        object_collectables_write_be16(
            slot + OBJECT_SLOT_CURRENT_ANGLE,
            (uint16_t)((uint32_t)object_collectables_read_be16(
                slot + OBJECT_SLOT_CURRENT_ANGLE) + frame.word_2));
    } else if (definition->graphics_type > 1u) {
        object_collectables_write_be16(
            slot + OBJECT_SLOT_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame.byte_0);
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_collectables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    } else {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_collectables_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    }
    vertical_adjustment = (int16_t)((int16_t)frame.signed_byte_4 * 2);
    object_collectables_write_be16(
        slot + OBJECT_SLOT_VERTICAL_POSITION,
        (uint16_t)((uint32_t)object_collectables_read_be16(
            slot + OBJECT_SLOT_VERTICAL_POSITION) + (uint16_t)vertical_adjustment));
    object_collectables_write_be16(slot + OBJECT_SLOT_TIMER1, frame.next_timer1);
    return 1;
}

static int object_collectables_update_range_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    GameAudioEvents *audio_events,
    uint32_t first_slot, uint32_t slot_limit,
    uint32_t *out_collected_count,
    char *error, size_t error_size)
{
    uint32_t collected_count = 0u;

    if (!objects || !level || !game_link || !player || !inventory || !limits || !messages ||
        objects->active_slot_count > objects->slot_count || first_slot > slot_limit ||
        slot_limit > objects->active_slot_count ||
        player->zone_index >= level->zone_count) {
        object_collectables_set_error(error, error_size,
                                     "collectable update received invalid source state");
        return 0;
    }

    for (uint32_t slot_index = first_slot; slot_index < slot_limit; ++slot_index) {
        uint8_t *slot;
        int16_t zone_id;
        uint8_t entity_type;
        GameObjectDefinition definition;
        uint16_t point_index;
        uint8_t *point_bytes;
        LevelZone zone;
        int32_t floor_or_roof;
        uint8_t collected;

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
        if (zone_id < 0) {
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
        /* Collectable only enters its placement, animation, and collision body when worried. */
        if (slot[OBJECT_SLOT_WORRY] == 0u) {
            continue;
        }
        point_index = object_collectables_read_be16(slot + OBJECT_SLOT_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, point_index, &point_bytes) ||
            !level_runtime_get_zone(level, (uint16_t)zone_id, &zone, error, error_size)) {
            object_collectables_set_error(error, error_size,
                                         "collectable source record has an invalid point or zone");
            return 0;
        }

        /*
         * newaliencontrol.s:Collectable uses ShotT_InUpperZone_b from the
         * object itself.  Its only visibility gate is ShotT_Worry_b: PVS can
         * worry a pickup in a visible neighbouring zone before the player
         * enters that zone, so do not substitute the player's zone/layer.
         */
        if (definition.floor_ceiling == 0u) {
            floor_or_roof = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_floor : zone.floor;
        } else {
            floor_or_roof = slot[OBJECT_SLOT_IN_UPPER_ZONE] != 0u ? zone.upper_roof : zone.roof;
        }
        object_collectables_write_be16(slot + OBJECT_SLOT_VERTICAL_POSITION,
                                       (uint16_t)object_collectables_asr32(floor_or_roof, 7u));
        /* DEFANIMOBJ's high bit is retained while its one-frame worry is consumed. */
        slot[OBJECT_SLOT_WORRY] &= 0x80u;
        if (!object_collectables_apply_default_animation(game_link, &definition, slot,
                                                         error, error_size)) {
            return 0;
        }

        if (!object_collectables_player_hits_slot(player, slot, point_bytes, &definition)) {
            continue;
        }
        if (!object_collectables_collect_item_single_player(
                level, game_link, &definition, slot, point_bytes, point_index,
                inventory, limits, messages, messages_enabled,
                message_time_milliseconds, audio_events, &collected,
                error, error_size)) {
            return 0;
        }
        if (collected == 0u) {
            continue;
        }
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

int object_collectables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    uint32_t *out_collected_count,
    char *error, size_t error_size)
{
    return object_collectables_update_range_single_player(
        objects, level, game_link, player, inventory, limits, messages, messages_enabled,
        message_time_milliseconds, NULL, 0u,
        objects ? objects->active_slot_count : 0u, out_collected_count, error, error_size);
}

int object_collectables_update_slot_single_player(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    uint32_t *out_collected_count,
    char *error, size_t error_size)
{
    return object_collectables_update_slot_single_player_with_audio(
        objects, slot_index, level, game_link, player, inventory, limits, messages,
        messages_enabled, message_time_milliseconds, NULL, out_collected_count, error, error_size);
}

int object_collectables_update_slot_single_player_with_audio(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    GameAudioEvents *audio_events, uint32_t *out_collected_count,
    char *error, size_t error_size)
{
    if (!objects || slot_index >= objects->active_slot_count) {
        object_collectables_set_error(error, error_size,
                                     "source collectable slot is outside ObjectHandler's list");
        return 0;
    }
    return object_collectables_update_range_single_player(
        objects, level, game_link, player, inventory, limits, messages, messages_enabled,
        message_time_milliseconds, audio_events,
        slot_index, slot_index + 1u,
        out_collected_count, error, error_size);
}
