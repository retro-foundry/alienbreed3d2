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
    MECHANISM_ZONE_ROOF_OFFSET = 6,
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

static int mechanism_runtime_write_door_roof(LevelDynamicState *dynamic_level,
                                              int16_t zone_id, int32_t scaled_position)
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
    if (zone_offset > UINT32_MAX - MECHANISM_ZONE_ROOF_OFFSET ||
        !level_dynamic_state_get_level_range(dynamic_level,
                                             zone_offset + MECHANISM_ZONE_ROOF_OFFSET,
                                             4u, &roof_bytes)) {
        return 0;
    }
    mechanism_runtime_write_be32(roof_bytes, (uint32_t)scaled_position);
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

void mechanism_runtime_init(MechanismRuntime *runtime)
{
    if (runtime) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

int mechanism_runtime_update_doors_single_player(MechanismRuntime *runtime,
                                                 LevelDynamicState *dynamic_level,
                                                 const LevelMechanisms *mechanisms,
                                                 const PlayerRuntime *player,
                                                 uint16_t frame_ticks,
                                                 char *error, size_t error_size)
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
        safety_open = door.zone_id >= 0 && player->zone_index == (uint16_t)door.zone_id &&
            door_open == 0 && new_velocity >= 0;
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
        if (!mechanism_runtime_write_door_roof(dynamic_level, door.zone_id, scaled_position) ||
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
