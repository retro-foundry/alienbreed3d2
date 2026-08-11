#include "mechanism_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    /* c/zone_liftable.h:DoorRaise. */
    MECHANISM_DOOR_RAISE_PLAYER_USE = 0,
    MECHANISM_DOOR_RAISE_PLAYER_TOUCH = 1,
    MECHANISM_DOOR_RAISE_BULLET_TOUCH = 2,
    MECHANISM_DOOR_RAISE_ALIEN_TOUCH = 3,
    MECHANISM_DOOR_RAISE_ON_TIMEOUT = 4,
    MECHANISM_DOOR_RAISE_NEVER = 5,
    /* defs.i:ZoneT_Roof_l and c/zone_liftable.h:ZLiftable offsets. */
    MECHANISM_ZONE_FLOOR_OFFSET = 2,
    MECHANISM_ZONE_ROOF_OFFSET = 6,
    MECHANISM_ZONE_WATER_OFFSET = 18,
    MECHANISM_LIFTABLE_SIZE = 36,
    MECHANISM_LIFTABLE_POSITION_OFFSET = 22,
    MECHANISM_LIFTABLE_VELOCITY_OFFSET = 24,
    MECHANISM_DOOR_GRAPHICS_POSITION_OFFSET = 2,
    MECHANISM_DOOR_WALL_GRAPHICS_SCROLL_OFFSET = 12,
    MECHANISM_DOOR_WALL_GRAPHICS_POSITION_OFFSET = 24
};

static uint16_t mechanism_runtime_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static int16_t mechanism_runtime_read_be16s(const uint8_t *bytes)
{
    return (int16_t)mechanism_runtime_read_be16(bytes);
}

static uint32_t mechanism_runtime_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int32_t mechanism_runtime_read_be32s(const uint8_t *bytes)
{
    return (int32_t)mechanism_runtime_read_be32(bytes);
}

static void mechanism_runtime_write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void mechanism_runtime_write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void mechanism_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int16_t mechanism_runtime_neg16(int16_t value)
{
    return (int16_t)(uint16_t)(0u - (uint16_t)value);
}

static int16_t mechanism_runtime_asr16_2(int16_t value)
{
    if (value >= 0) {
        return (int16_t)(value / 4);
    }
    return (int16_t)-(((int32_t)-value + 3) / 4);
}

static int32_t mechanism_runtime_asr32_6(int32_t value)
{
    if (value >= 0) {
        return value >> 6;
    }
    return -((-(int64_t)value + 63) >> 6);
}

static int mechanism_runtime_get_door_header(LevelDynamicState *dynamic_level,
                                              const LevelLiftable *door,
                                              uint8_t **out_header)
{
    if (!dynamic_level || !door || door->wall_data_offset < MECHANISM_LIFTABLE_SIZE) {
        return 0;
    }
    return level_dynamic_state_get_graphics_range(
        dynamic_level, door->wall_data_offset - MECHANISM_LIFTABLE_SIZE,
        MECHANISM_LIFTABLE_SIZE, out_header);
}

static int mechanism_runtime_write_zone_height(LevelDynamicState *dynamic_level,
                                               int16_t zone_id, uint32_t height_offset,
                                               int32_t scaled_position)
{
    uint8_t *zone_offset_bytes;
    uint8_t *roof_bytes;
    uint32_t zone_offset;
    uint64_t zone_table_entry;

    if (!dynamic_level || zone_id < 0 || (uint16_t)zone_id >= dynamic_level->runtime.zone_count) {
        return 0;
    }
    zone_table_entry = (uint64_t)dynamic_level->runtime.zone_offsets_table_offset +
        (uint64_t)(uint16_t)zone_id * 4u;
    if (zone_table_entry > UINT32_MAX ||
        !level_dynamic_state_get_graphics_range(dynamic_level, (uint32_t)zone_table_entry,
                                                4u, &zone_offset_bytes)) {
        return 0;
    }
    zone_offset = mechanism_runtime_read_be32(zone_offset_bytes);
    if (height_offset > UINT32_MAX || zone_offset > UINT32_MAX - height_offset ||
        !level_dynamic_state_get_level_range(dynamic_level,
                                             zone_offset + height_offset,
                                             4u, &roof_bytes)) {
        return 0;
    }
    mechanism_runtime_write_be32(roof_bytes, (uint32_t)scaled_position);
    return 1;
}

static int mechanism_runtime_player_is_in_liftable_zone(const LevelDynamicState *dynamic_level,
                                                         const PlayerRuntime *player,
                                                         int16_t liftable_zone_index,
                                                         int *out_is_in_zone,
                                                         char *error, size_t error_size)
{
    LevelZone player_zone;
    LevelZone liftable_zone;

    if (!dynamic_level || !player || !out_is_in_zone || liftable_zone_index < 0 ||
        player->zone_index >= dynamic_level->runtime.zone_count ||
        (uint16_t)liftable_zone_index >= dynamic_level->runtime.zone_count ||
        !level_runtime_get_zone(&dynamic_level->runtime, player->zone_index, &player_zone,
                                error, error_size) ||
        !level_runtime_get_zone(&dynamic_level->runtime, (uint16_t)liftable_zone_index,
                                &liftable_zone, error, error_size)) {
        return 0;
    }
    *out_is_in_zone = player_zone.id == liftable_zone.id;
    return 1;
}

static uint16_t mechanism_runtime_door_raise_mask(uint8_t raise_condition,
                                                   const PlayerRuntime *player)
{
    switch (raise_condition) {
    case MECHANISM_DOOR_RAISE_PLAYER_USE:
        return player->tmp_used != 0u ? 0x0100u : 0u;
    case MECHANISM_DOOR_RAISE_PLAYER_TOUCH:
        return 0x0900u;
    case MECHANISM_DOOR_RAISE_BULLET_TOUCH:
        return 0x0400u;
    case MECHANISM_DOOR_RAISE_ALIEN_TOUCH:
        return 0x0200u;
    case MECHANISM_DOOR_RAISE_ON_TIMEOUT:
        return 0x8000u;
    case MECHANISM_DOOR_RAISE_NEVER:
    default:
        return 0u;
    }
}

static int mechanism_runtime_update_door_graphics(LevelDynamicState *dynamic_level,
                                                   const LevelMechanisms *mechanisms,
                                                   uint16_t door_index,
                                                   const LevelLiftable *door,
                                                   int16_t position,
                                                   int32_t scaled_position,
                                                   char *error, size_t error_size)
{
    uint8_t *graphics_bytes;
    int16_t position_shifted = mechanism_runtime_asr16_2(position);
    uint16_t scroll = (uint16_t)(0u - (uint16_t)position_shifted) & 0x00ffu;

    if (!level_dynamic_state_get_graphics_range(
            dynamic_level, door->graphics_offset + MECHANISM_DOOR_GRAPHICS_POSITION_OFFSET,
            2u, &graphics_bytes)) {
        mechanism_runtime_set_error(error, error_size,
                                    "source door graphics position is outside the mutable data");
        return 0;
    }
    mechanism_runtime_write_be16(graphics_bytes, (uint16_t)position);
    for (uint16_t wall_index = 0u; wall_index < door->wall_count; ++wall_index) {
        LevelLiftableWall wall;
        uint8_t *wall_graphics_bytes;

        if (!level_mechanisms_get_door_wall(mechanisms, door_index, wall_index, &wall,
                                            error, error_size) ||
            !level_dynamic_state_get_graphics_range(
                dynamic_level,
                wall.graphics_offset + MECHANISM_DOOR_WALL_GRAPHICS_SCROLL_OFFSET,
                2u, &wall_graphics_bytes)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source door wall graphics is outside the mutable data");
            return 0;
        }
        mechanism_runtime_write_be16(wall_graphics_bytes,
                                     (uint16_t)(wall.unknown_long + scroll));
        if (!level_dynamic_state_get_graphics_range(
                dynamic_level,
                wall.graphics_offset + MECHANISM_DOOR_WALL_GRAPHICS_POSITION_OFFSET,
                4u, &wall_graphics_bytes)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source door wall position is outside the mutable data");
            return 0;
        }
        mechanism_runtime_write_be32(wall_graphics_bytes, (uint32_t)scaled_position);
    }
    return 1;
}

static int mechanism_runtime_update_door_walls(LevelDynamicState *dynamic_level,
                                                const LevelMechanisms *mechanisms,
                                                uint16_t door_index,
                                                const LevelLiftable *door,
                                                uint16_t requested_flags,
                                                uint16_t stored_flags,
                                                int *out_activated,
                                                char *error, size_t error_size)
{
    int activated = 0;

    for (uint16_t wall_index = 0u; wall_index < door->wall_count; ++wall_index) {
        LevelLiftableWall wall;
        uint16_t previous_flags;

        if (!level_mechanisms_get_door_wall(mechanisms, door_index, wall_index, &wall,
                                            error, error_size) ||
            wall.edge_index < 0 ||
            !level_dynamic_state_get_edge_flags(dynamic_level, (uint16_t)wall.edge_index,
                                                &previous_flags) ||
            !level_dynamic_state_set_edge_flags(dynamic_level, (uint16_t)wall.edge_index,
                                                stored_flags)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source door wall references an invalid mutable EdgeT");
            return 0;
        }
        if ((previous_flags & requested_flags) != 0u) {
            activated = 1;
        }
    }
    if (activated != 0) {
        *out_activated = 1;
    }
    return 1;
}

static int mechanism_runtime_update_lift_graphics(LevelDynamicState *dynamic_level,
                                                   const LevelMechanisms *mechanisms,
                                                   uint16_t lift_index,
                                                   const LevelLiftable *lift,
                                                   int16_t position,
                                                   int32_t scaled_position,
                                                   char *error, size_t error_size)
{
    uint8_t *graphics_bytes;
    int16_t position_shifted = mechanism_runtime_asr16_2(position);
    uint16_t rounded_position = (uint16_t)((uint16_t)position_shifted << 2);
    uint16_t scroll = (uint16_t)(0u - (uint16_t)position_shifted) & 0x00ffu;

    if (!level_dynamic_state_get_graphics_range(
            dynamic_level, lift->graphics_offset + MECHANISM_DOOR_GRAPHICS_POSITION_OFFSET,
            2u, &graphics_bytes)) {
        mechanism_runtime_set_error(error, error_size,
                                    "source lift graphics position is outside the mutable data");
        return 0;
    }
    mechanism_runtime_write_be16(graphics_bytes, rounded_position);
    for (uint16_t wall_index = 0u; wall_index < lift->wall_count; ++wall_index) {
        LevelLiftableWall wall;
        uint8_t *wall_graphics_bytes;

        if (!level_mechanisms_get_lift_wall(mechanisms, lift_index, wall_index, &wall,
                                            error, error_size) ||
            !level_dynamic_state_get_graphics_range(
                dynamic_level,
                wall.graphics_offset + MECHANISM_DOOR_WALL_GRAPHICS_SCROLL_OFFSET,
                2u, &wall_graphics_bytes)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source lift wall graphics is outside the mutable data");
            return 0;
        }
        mechanism_runtime_write_be16(wall_graphics_bytes,
                                     (uint16_t)(wall.unknown_long + scroll));
        if (!level_dynamic_state_get_graphics_range(
                dynamic_level, wall.graphics_offset + 20u, 4u, &wall_graphics_bytes)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source lift wall position is outside the mutable data");
            return 0;
        }
        mechanism_runtime_write_be32(wall_graphics_bytes, (uint32_t)scaled_position);
    }
    return 1;
}

static int mechanism_runtime_update_lift_walls(LevelDynamicState *dynamic_level,
                                                const LevelMechanisms *mechanisms,
                                                uint16_t lift_index,
                                                const LevelLiftable *lift,
                                                uint16_t requested_flags,
                                                uint16_t stored_flags,
                                                int *out_activated,
                                                char *error, size_t error_size)
{
    int activated = 0;

    for (uint16_t wall_index = 0u; wall_index < lift->wall_count; ++wall_index) {
        LevelLiftableWall wall;
        uint16_t previous_flags;

        if (!level_mechanisms_get_lift_wall(mechanisms, lift_index, wall_index, &wall,
                                            error, error_size) ||
            wall.edge_index < 0 ||
            !level_dynamic_state_get_edge_flags(dynamic_level, (uint16_t)wall.edge_index,
                                                &previous_flags) ||
            !level_dynamic_state_set_edge_flags(dynamic_level, (uint16_t)wall.edge_index,
                                                stored_flags)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source lift wall references an invalid mutable EdgeT");
            return 0;
        }
        if ((previous_flags & requested_flags) != 0u) {
            activated = 1;
        }
    }
    if (activated != 0) {
        *out_activated = 1;
    }
    return 1;
}

static uint16_t mechanism_runtime_lift_request_mask(uint8_t condition,
                                                     const PlayerRuntime *player,
                                                     int player_stood_on_lift,
                                                     int16_t requested_speed,
                                                     int16_t *out_velocity)
{
    if (!out_velocity) {
        return 0u;
    }
    switch (condition) {
    case 0u:
        if (player->tmp_used == 0u) {
            return 0u;
        }
        *out_velocity = requested_speed;
        return player_stood_on_lift != 0 ? 0x8000u : 0x0100u;
    case 1u:
        *out_velocity = requested_speed;
        return player_stood_on_lift != 0 ? 0x8000u : 0x0900u;
    case 2u:
        *out_velocity = requested_speed;
        return 0x8000u;
    default:
        return 0u;
    }
}

void mechanism_runtime_init(MechanismRuntime *runtime)
{
    if (runtime) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

static void mechanism_runtime_emit_liftable_sound(GameAudioEvents *audio_events,
                                                  int16_t one_based_sample,
                                                  const LevelLiftable *liftable,
                                                  uint16_t source_id)
{
    /* newanims.s subtracts one from all four ZLiftableT sound fields. */
    game_audio_events_emit(audio_events, (int16_t)(one_based_sample - 1), 50,
                           liftable->word9, liftable->word10, source_id,
                           GAME_AUDIO_RESTART_SOURCE, 1u, 0u);
}

int mechanism_runtime_update_doors_single_player_with_audio(
    MechanismRuntime *runtime, LevelDynamicState *dynamic_level,
    const LevelMechanisms *mechanisms, const PlayerRuntime *player,
    uint16_t frame_ticks, GameAudioEvents *audio_events, char *error, size_t error_size)
{
    if (!runtime || !dynamic_level || !mechanisms || !player ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        mechanisms->door_count > LEVEL_MECHANISMS_MAX_DOORS) {
        mechanism_runtime_set_error(error, error_size,
                                    "source door update received invalid runtime state");
        return 0;
    }

    for (uint16_t door_index = 0u; door_index < mechanisms->door_count; ++door_index) {
        LevelLiftable door;
        uint8_t *header;
        int16_t position;
        int16_t velocity;
        int16_t moved_position;
        int16_t new_velocity;
        int16_t requested_velocity = 0;
        int32_t scaled_position;
        uint16_t requested_flags = 0u;
        int door_closed;
        int door_open;
        int activated = 0;
        int locked;
        int safety_open;

        if (!level_mechanisms_get_door(mechanisms, door_index, &door, error, error_size) ||
            !mechanism_runtime_get_door_header(dynamic_level, &door, &header)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source door header is outside the mutable data");
            return 0;
        }
        position = mechanism_runtime_read_be16s(header + MECHANISM_LIFTABLE_POSITION_OFFSET);
        velocity = mechanism_runtime_read_be16s(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET);
        moved_position = (int16_t)(uint16_t)((uint16_t)position +
            (uint16_t)((int32_t)velocity * (int16_t)frame_ticks));
        new_velocity = velocity;
        door_closed = moved_position >= door.bottom;
        if (door_closed != 0) {
            moved_position = door.bottom;
            new_velocity = 0;
        }
        door_open = moved_position <= door.top;
        if (door_open != 0) {
            moved_position = door.top;
            new_velocity = 0;
            if (velocity != 0) {
                runtime->door_open_timers[door_index] = 0u;
            }
        }

        if (door_closed != 0) {
            requested_flags = mechanism_runtime_door_raise_mask(door.raise_condition, player);
            requested_velocity = mechanism_runtime_neg16(door.opening_speed);
        } else if (door_open != 0) {
            uint16_t elapsed = (uint16_t)(runtime->door_open_timers[door_index] + frame_ticks);

            runtime->door_open_timers[door_index] = elapsed;
            if ((int16_t)elapsed >= door.open_duration) {
                requested_flags = 0x8000u;
                requested_velocity = door.closing_speed;
            }
        }

        /* DoorRoutine's player-in-door safety branch precedes its lock check. */
        if (!mechanism_runtime_player_is_in_liftable_zone(dynamic_level, player, door.zone_id,
                                                          &safety_open, error, error_size)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source door has an invalid player/zone relationship");
            return 0;
        }
        safety_open = safety_open != 0 && door_open == 0 && new_velocity >= 0;
        if (safety_open != 0) {
            requested_flags = 0x8000u;
            requested_velocity = -16;
        }
        locked = (runtime->door_and_lift_locks & (uint16_t)(1u << door_index)) != 0u;
        if (locked != 0 && safety_open == 0) {
            requested_flags = 0u;
        }

        scaled_position = (int32_t)mechanism_runtime_asr16_2(moved_position) * 256;
        mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_POSITION_OFFSET,
                                     (uint16_t)moved_position);
        mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET,
                                     (uint16_t)new_velocity);
        if (!mechanism_runtime_write_zone_height(dynamic_level, door.zone_id,
                                                 MECHANISM_ZONE_ROOF_OFFSET,
                                                 scaled_position) ||
            !mechanism_runtime_update_door_graphics(dynamic_level, mechanisms, door_index, &door,
                                                    moved_position, scaled_position,
                                                    error, error_size) ||
            !mechanism_runtime_update_door_walls(dynamic_level, mechanisms, door_index, &door,
                                                 requested_flags,
                                                 locked != 0 && safety_open == 0 ? 0u : 0x8000u,
                                                 &activated, error, error_size)) {
            if (error && error_size > 0u && error[0] == '\0') {
                mechanism_runtime_set_error(error, error_size, "source door update failed");
            }
            return 0;
        }
        if (activated != 0) {
            mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET,
                                         (uint16_t)requested_velocity);
            mechanism_runtime_emit_liftable_sound(
                audio_events, requested_velocity < 0 ? door.opening_sound_fx : door.closing_sound_fx,
                &door, (uint16_t)(UINT16_C(0x1000) + door_index));
        }
        if (door_closed != 0 && velocity != 0) {
            mechanism_runtime_emit_liftable_sound(audio_events, door.closed_sound_fx, &door,
                                                  (uint16_t)(UINT16_C(0x1000) + door_index));
        } else if (door_open != 0 && velocity != 0) {
            mechanism_runtime_emit_liftable_sound(audio_events, door.opened_sound_fx, &door,
                                                  (uint16_t)(UINT16_C(0x1000) + door_index));
        }
        if (door_closed != 0) {
            runtime->current_door_state &= (uint16_t)~(uint16_t)(1u << door_index);
        } else {
            runtime->current_door_state |= (uint16_t)(1u << door_index);
        }
    }

    /* DoorRoutine clears Anim_DoorAndLiftLocks_l after its 999 terminator. */
    runtime->door_and_lift_locks = 0u;
    return 1;
}

int mechanism_runtime_update_doors_single_player(MechanismRuntime *runtime,
                                                 LevelDynamicState *dynamic_level,
                                                 const LevelMechanisms *mechanisms,
                                                 const PlayerRuntime *player,
                                                 uint16_t frame_ticks,
                                                 char *error, size_t error_size)
{
    return mechanism_runtime_update_doors_single_player_with_audio(
        runtime, dynamic_level, mechanisms, player, frame_ticks, NULL, error, error_size);
}

int mechanism_runtime_update_lifts_single_player_with_audio(
    MechanismRuntime *runtime, LevelDynamicState *dynamic_level,
    const LevelMechanisms *mechanisms, PlayerRuntime *player,
    uint16_t frame_ticks, GameAudioEvents *audio_events, char *error, size_t error_size)
{
    if (!runtime || !dynamic_level || !mechanisms || !player ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        mechanisms->lift_count > LEVEL_MECHANISMS_MAX_LIFTS) {
        mechanism_runtime_set_error(error, error_size,
                                    "source lift update received invalid runtime state");
        return 0;
    }

    /* objmoveanim clears these immediately before newanims.s:LiftRoutine. */
    player->floor_speed = 0;
    player->stood_on_lift = 0u;
    for (uint16_t lift_index = 0u; lift_index < mechanisms->lift_count; ++lift_index) {
        LevelLiftable lift;
        uint8_t *header;
        int16_t position;
        int16_t velocity;
        int16_t moved_position;
        int16_t new_velocity;
        int16_t requested_velocity = 0;
        int32_t scaled_position;
        uint16_t requested_flags = 0u;
        int lift_at_bottom;
        int lift_at_top;
        int player_stood_on_lift;
        int activated = 0;
        int locked;

        if (!level_mechanisms_get_lift(mechanisms, lift_index, &lift, error, error_size) ||
            !mechanism_runtime_get_door_header(dynamic_level, &lift, &header) ||
            !mechanism_runtime_player_is_in_liftable_zone(dynamic_level, player, lift.zone_id,
                                                          &player_stood_on_lift,
                                                          error, error_size)) {
            mechanism_runtime_set_error(error, error_size,
                                        "source lift header or player/zone relationship is invalid");
            return 0;
        }
        position = mechanism_runtime_read_be16s(header + MECHANISM_LIFTABLE_POSITION_OFFSET);
        velocity = mechanism_runtime_read_be16s(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET);
        runtime->lift_heights[lift_index] = position;
        moved_position = (int16_t)(uint16_t)((uint16_t)position +
            (uint16_t)((int32_t)velocity * (int16_t)frame_ticks));
        new_velocity = velocity;
        lift_at_bottom = moved_position >= lift.bottom;
        if (lift_at_bottom != 0) {
            moved_position = lift.bottom;
            new_velocity = 0;
        }
        lift_at_top = moved_position <= lift.top;
        if (lift_at_top != 0) {
            moved_position = lift.top;
            new_velocity = 0;
        }

        player->stood_on_lift = player_stood_on_lift != 0 ? UINT8_MAX : 0u;
        if (player_stood_on_lift != 0) {
            /* LiftRoutine keeps the pre-clamp source word as PlrT_FloorSpd_w. */
            player->floor_speed = velocity;
        }
        if (lift_at_top != 0) {
            requested_flags = mechanism_runtime_lift_request_mask(
                lift.lower_condition, player, player_stood_on_lift, lift.closing_speed,
                &requested_velocity);
        } else if (lift_at_bottom != 0) {
            requested_flags = mechanism_runtime_lift_request_mask(
                lift.raise_condition, player, player_stood_on_lift,
                mechanism_runtime_neg16(lift.opening_speed), &requested_velocity);
        }
        locked = (runtime->lift_only_locks & (uint16_t)(1u << lift_index)) != 0u;
        if (locked != 0) {
            requested_flags = 0u;
        }

        scaled_position = (int32_t)mechanism_runtime_asr16_2(moved_position) * 256;
        mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_POSITION_OFFSET,
                                     (uint16_t)moved_position);
        mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET,
                                     (uint16_t)new_velocity);
        if (!mechanism_runtime_write_zone_height(dynamic_level, lift.zone_id,
                                                 MECHANISM_ZONE_FLOOR_OFFSET,
                                                 scaled_position) ||
            !mechanism_runtime_update_lift_graphics(dynamic_level, mechanisms, lift_index,
                                                    &lift, moved_position, scaled_position,
                                                    error, error_size) ||
            !mechanism_runtime_update_lift_walls(dynamic_level, mechanisms, lift_index, &lift,
                                                 requested_flags,
                                                 locked != 0 ? 0u : 0x8000u,
                                                 &activated, error, error_size)) {
            if (error && error_size > 0u && error[0] == '\0') {
                mechanism_runtime_set_error(error, error_size, "source lift update failed");
            }
            return 0;
        }
        if (activated != 0) {
            mechanism_runtime_write_be16(header + MECHANISM_LIFTABLE_VELOCITY_OFFSET,
                                         (uint16_t)requested_velocity);
            mechanism_runtime_emit_liftable_sound(
                audio_events, requested_velocity < 0 ? lift.opening_sound_fx : lift.closing_sound_fx,
                &lift, (uint16_t)(UINT16_C(0x2000) + lift_index));
        }
        if (lift_at_bottom != 0 && velocity != 0) {
            mechanism_runtime_emit_liftable_sound(audio_events, lift.closed_sound_fx, &lift,
                                                  (uint16_t)(UINT16_C(0x2000) + lift_index));
        } else if (lift_at_top != 0 && velocity != 0) {
            mechanism_runtime_emit_liftable_sound(audio_events, lift.opened_sound_fx, &lift,
                                                  (uint16_t)(UINT16_C(0x2000) + lift_index));
        }
    }

    /* LiftRoutine clears Anim_LiftOnlyLocks_w after its 999 terminator. */
    runtime->lift_only_locks = 0u;
    return mechanism_runtime_update_water_animations(dynamic_level, mechanisms, frame_ticks,
                                                     error, error_size);
}

int mechanism_runtime_update_lifts_single_player(MechanismRuntime *runtime,
                                                 LevelDynamicState *dynamic_level,
                                                 const LevelMechanisms *mechanisms,
                                                 PlayerRuntime *player,
                                                 uint16_t frame_ticks,
                                                 char *error, size_t error_size)
{
    return mechanism_runtime_update_lifts_single_player_with_audio(
        runtime, dynamic_level, mechanisms, player, frame_ticks, NULL, error, error_size);
}

int mechanism_runtime_update_water_animations(LevelDynamicState *dynamic_level,
                                              const LevelMechanisms *mechanisms,
                                              uint16_t frame_ticks,
                                              char *error, size_t error_size)
{
    if (!dynamic_level || !mechanisms ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        mechanisms->water_animation_count != LEVEL_MECHANISMS_WATER_ANIMATION_COUNT) {
        mechanism_runtime_set_error(error, error_size,
                                    "DoWaterAnims received invalid runtime state");
        return 0;
    }
    for (uint16_t animation_index = 0u;
         animation_index < mechanisms->water_animation_count;
         ++animation_index) {
        LevelWaterAnimation animation;
        uint8_t *state;
        int32_t position;
        int16_t velocity;

        if (!level_mechanisms_get_water_animation(mechanisms, animation_index, &animation,
                                                  error, error_size) ||
            !level_dynamic_state_get_graphics_range(
                dynamic_level, animation.state_offset, 14u, &state)) {
            mechanism_runtime_set_error(error, error_size,
                                        "DoWaterAnims state is outside mutable graphics data");
            return 0;
        }
        position = (int32_t)((uint32_t)mechanism_runtime_read_be32(state + 8u) +
            (uint32_t)((int32_t)mechanism_runtime_read_be16s(state + 12u) *
                       (int16_t)frame_ticks));
        velocity = mechanism_runtime_read_be16s(state + 12u);
        /* The source caps first at +0, then at +4, and reverses at either bound. */
        if (position <= mechanism_runtime_read_be32s(state + 0u)) {
            position = mechanism_runtime_read_be32s(state + 0u);
            velocity = mechanism_runtime_neg16(velocity);
        } else if (position >= mechanism_runtime_read_be32s(state + 4u)) {
            position = mechanism_runtime_read_be32s(state + 4u);
            velocity = mechanism_runtime_neg16(velocity);
        }
        mechanism_runtime_write_be32(state + 8u, (uint32_t)position);
        mechanism_runtime_write_be16(state + 12u, (uint16_t)velocity);
        for (uint16_t target_index = 0u; target_index < animation.target_count;
             ++target_index) {
            LevelWaterAnimationTarget target;
            uint8_t *graphics_record;

            if (!level_mechanisms_get_water_animation_target(
                    mechanisms, animation_index, target_index, &target, error, error_size) ||
                target.zone_index >= dynamic_level->runtime.zone_count ||
                !level_dynamic_state_get_graphics_range(
                    dynamic_level, target.graphics_offset + 2u, 2u, &graphics_record) ||
                !mechanism_runtime_write_zone_height(dynamic_level, (int16_t)target.zone_index,
                                                     MECHANISM_ZONE_WATER_OFFSET, position)) {
                mechanism_runtime_set_error(error, error_size,
                                            "DoWaterAnims target is outside mutable source data");
                return 0;
            }
            mechanism_runtime_write_be16(graphics_record,
                                         (uint16_t)mechanism_runtime_asr32_6(position));
        }
    }
    return 1;
}
