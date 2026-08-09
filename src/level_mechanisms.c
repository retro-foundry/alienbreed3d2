#include "level_mechanisms.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i:ZLiftableT_SizeOf_l. */
    LEVEL_MECHANISMS_LIFTABLE_SIZE = 36,
    /* c/zone_liftable.h:ZDoorWall. */
    LEVEL_MECHANISMS_WALL_SIZE = 10,
    /* c/zone_liftable.h:END_OF_DOOR_LIST. */
    LEVEL_MECHANISMS_LIST_END = 999,
    /* newanims.s:SwitchRoutine's `adda.w #14,a0`. */
    LEVEL_MECHANISMS_SWITCH_SIZE = 14
};

static uint16_t level_mechanisms_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t level_mechanisms_read_be16s(const uint8_t *source)
{
    return (int16_t)level_mechanisms_read_be16(source);
}

static uint32_t level_mechanisms_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static void level_mechanisms_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int level_mechanisms_range_is_valid(size_t offset, size_t length, size_t total_size)
{
    return offset <= total_size && length <= total_size - offset;
}

static void level_mechanisms_read_liftable(const uint8_t *source,
                                           size_t wall_data_offset,
                                           uint16_t wall_count,
                                           LevelLiftable *out_liftable)
{
    LevelLiftable liftable;

    memset(&liftable, 0, sizeof(liftable));
    liftable.bottom = level_mechanisms_read_be16s(source + 0u);
    liftable.top = level_mechanisms_read_be16s(source + 2u);
    liftable.opening_speed = level_mechanisms_read_be16s(source + 4u);
    liftable.closing_speed = level_mechanisms_read_be16s(source + 6u);
    liftable.open_duration = level_mechanisms_read_be16s(source + 8u);
    liftable.opening_sound_fx = level_mechanisms_read_be16s(source + 10u);
    liftable.closing_sound_fx = level_mechanisms_read_be16s(source + 12u);
    liftable.opened_sound_fx = level_mechanisms_read_be16s(source + 14u);
    liftable.closed_sound_fx = level_mechanisms_read_be16s(source + 16u);
    liftable.word9 = level_mechanisms_read_be16s(source + 18u);
    liftable.word10 = level_mechanisms_read_be16s(source + 20u);
    liftable.word11 = level_mechanisms_read_be16s(source + 22u);
    liftable.word12 = level_mechanisms_read_be16s(source + 24u);
    liftable.graphics_offset = level_mechanisms_read_be32(source + 26u);
    liftable.zone_id = level_mechanisms_read_be16s(source + 30u);
    liftable.word16 = level_mechanisms_read_be16s(source + 32u);
    liftable.raise_condition = source[34u];
    liftable.lower_condition = source[35u];
    liftable.wall_data_offset = (uint32_t)wall_data_offset;
    liftable.wall_count = wall_count;
    *out_liftable = liftable;
}

/*
 * DoorRoutine and LiftRoutine both advance through
 * { ZLiftableT, ZDoorWall..., negative word } records. The dynamic routines
 * branch on any negative wall index, while normal shipped lists use -1.
 */
static int level_mechanisms_parse_liftable_stream(const uint8_t *bytes, size_t size,
                                                  uint32_t stream_offset,
                                                  LevelLiftable *out_liftables,
                                                  uint16_t max_liftables,
                                                  uint16_t *out_count,
                                                  char *error, size_t error_size)
{
    size_t cursor;
    uint16_t count;

    if (!bytes || !out_liftables || !out_count) {
        level_mechanisms_set_error(error, error_size, "liftable parser received null source data");
        return 0;
    }
    cursor = stream_offset;
    count = 0u;
    for (;;) {
        size_t wall_data_offset;
        uint32_t wall_count;
        int16_t marker;

        if (!level_mechanisms_range_is_valid(cursor, sizeof(uint16_t), size)) {
            level_mechanisms_set_error(error, error_size,
                                       "ZLiftable stream has no source 999 terminator");
            return 0;
        }
        marker = level_mechanisms_read_be16s(bytes + cursor);
        if (marker == LEVEL_MECHANISMS_LIST_END) {
            *out_count = count;
            return 1;
        }
        if (count >= max_liftables) {
            level_mechanisms_set_error(error, error_size,
                                       "ZLiftable stream exceeds the source liftable limit");
            return 0;
        }
        if (!level_mechanisms_range_is_valid(cursor, LEVEL_MECHANISMS_LIFTABLE_SIZE, size)) {
            level_mechanisms_set_error(error, error_size, "ZLiftable header is truncated");
            return 0;
        }

        wall_data_offset = cursor + LEVEL_MECHANISMS_LIFTABLE_SIZE;
        cursor = wall_data_offset;
        wall_count = 0u;
        for (;;) {
            int16_t edge_index;

            if (!level_mechanisms_range_is_valid(cursor, sizeof(uint16_t), size)) {
                level_mechanisms_set_error(error, error_size,
                                           "ZLiftable wall list has no negative source terminator");
                return 0;
            }
            edge_index = level_mechanisms_read_be16s(bytes + cursor);
            if (edge_index < 0) {
                if (wall_count > UINT16_MAX) {
                    level_mechanisms_set_error(error, error_size,
                                               "ZLiftable wall list exceeds the native view limit");
                    return 0;
                }
                level_mechanisms_read_liftable(bytes + wall_data_offset -
                                                    LEVEL_MECHANISMS_LIFTABLE_SIZE,
                                                wall_data_offset, (uint16_t)wall_count,
                                                &out_liftables[count]);
                ++count;
                cursor += sizeof(uint16_t);
                break;
            }
            if (!level_mechanisms_range_is_valid(cursor, LEVEL_MECHANISMS_WALL_SIZE, size)) {
                level_mechanisms_set_error(error, error_size, "ZLiftable wall record is truncated");
                return 0;
            }
            if (wall_count == UINT32_MAX) {
                level_mechanisms_set_error(error, error_size, "ZLiftable wall list is too long");
                return 0;
            }
            ++wall_count;
            cursor += LEVEL_MECHANISMS_WALL_SIZE;
        }
    }
}

static int level_mechanisms_get_liftable(const LevelMechanisms *mechanisms,
                                         const LevelLiftable *liftables,
                                         uint16_t liftable_count,
                                         uint16_t liftable_index,
                                         LevelLiftable *out_liftable,
                                         char *error, size_t error_size)
{
    if (!mechanisms || !mechanisms->graphics_bytes || !liftables || !out_liftable ||
        liftable_index >= liftable_count) {
        level_mechanisms_set_error(error, error_size,
                                   "requested source liftable is outside the runtime view");
        return 0;
    }
    *out_liftable = liftables[liftable_index];
    return 1;
}

static int level_mechanisms_get_liftable_wall(const LevelMechanisms *mechanisms,
                                              const LevelLiftable *liftables,
                                              uint16_t liftable_count,
                                              uint16_t liftable_index,
                                              uint16_t wall_index,
                                              LevelLiftableWall *out_wall,
                                              char *error, size_t error_size)
{
    const LevelLiftable *liftable;
    const uint8_t *source;
    size_t wall_offset;
    LevelLiftableWall wall;

    if (!mechanisms || !mechanisms->graphics_bytes || !liftables || !out_wall ||
        liftable_index >= liftable_count) {
        level_mechanisms_set_error(error, error_size,
                                   "requested source liftable wall is outside the runtime view");
        return 0;
    }
    liftable = &liftables[liftable_index];
    if (wall_index >= liftable->wall_count) {
        level_mechanisms_set_error(error, error_size,
                                   "requested wall is outside the source liftable list");
        return 0;
    }
    wall_offset = (size_t)liftable->wall_data_offset +
        (size_t)wall_index * LEVEL_MECHANISMS_WALL_SIZE;
    if (!level_mechanisms_range_is_valid(wall_offset, LEVEL_MECHANISMS_WALL_SIZE,
                                         mechanisms->graphics_size)) {
        level_mechanisms_set_error(error, error_size,
                                   "source liftable wall is outside the graphics data");
        return 0;
    }
    source = mechanisms->graphics_bytes + wall_offset;
    wall.edge_index = level_mechanisms_read_be16s(source + 0u);
    wall.graphics_offset = level_mechanisms_read_be32(source + 2u);
    wall.unknown_long = level_mechanisms_read_be32(source + 6u);
    *out_wall = wall;
    return 1;
}

int level_mechanisms_init(const AssetBlob *graphics_data,
                          const LevelGraphicsBootstrap *graphics_header,
                          LevelMechanisms *out_mechanisms,
                          char *error, size_t error_size)
{
    LevelMechanisms mechanisms;
    size_t switch_bytes;
    uint16_t switch_index;

    if (!graphics_data || !graphics_data->bytes || !graphics_header || !out_mechanisms) {
        level_mechanisms_set_error(error, error_size, "level mechanisms received null source data");
        return 0;
    }
    switch_bytes = LEVEL_MECHANISMS_SWITCH_COUNT * LEVEL_MECHANISMS_SWITCH_SIZE;
    if (!level_mechanisms_range_is_valid(graphics_header->door_data_offset,
                                         sizeof(uint16_t), graphics_data->size) ||
        !level_mechanisms_range_is_valid(graphics_header->lift_data_offset,
                                         sizeof(uint16_t), graphics_data->size) ||
        !level_mechanisms_range_is_valid(graphics_header->switch_data_offset,
                                         switch_bytes, graphics_data->size)) {
        level_mechanisms_set_error(error, error_size,
                                   "TLGT mechanism offset is outside the graphics data");
        return 0;
    }

    memset(&mechanisms, 0, sizeof(mechanisms));
    mechanisms.graphics_bytes = graphics_data->bytes;
    mechanisms.graphics_size = graphics_data->size;
    if (!level_mechanisms_parse_liftable_stream(graphics_data->bytes, graphics_data->size,
                                                graphics_header->door_data_offset,
                                                mechanisms.doors,
                                                LEVEL_MECHANISMS_MAX_DOORS,
                                                &mechanisms.door_count,
                                                error, error_size) ||
        !level_mechanisms_parse_liftable_stream(graphics_data->bytes, graphics_data->size,
                                                graphics_header->lift_data_offset,
                                                mechanisms.lifts,
                                                LEVEL_MECHANISMS_MAX_LIFTS,
                                                &mechanisms.lift_count,
                                                error, error_size)) {
        return 0;
    }
    for (switch_index = 0u; switch_index < LEVEL_MECHANISMS_SWITCH_COUNT; ++switch_index) {
        const uint8_t *source = graphics_data->bytes + graphics_header->switch_data_offset +
            (size_t)switch_index * LEVEL_MECHANISMS_SWITCH_SIZE;
        LevelSwitch *switch_record = &mechanisms.switches[switch_index];

        switch_record->word0 = level_mechanisms_read_be16s(source + 0u);
        switch_record->byte2 = source[2u];
        switch_record->byte3 = source[3u];
        switch_record->point_index = level_mechanisms_read_be16(source + 4u);
        switch_record->graphics_offset = level_mechanisms_read_be32(source + 6u);
        switch_record->byte10 = source[10u];
        memcpy(switch_record->bytes11_to_13, source + 11u,
               sizeof(switch_record->bytes11_to_13));
    }
    *out_mechanisms = mechanisms;
    return 1;
}

int level_mechanisms_get_door(const LevelMechanisms *mechanisms, uint16_t door_index,
                              LevelLiftable *out_door,
                              char *error, size_t error_size)
{
    return level_mechanisms_get_liftable(mechanisms, mechanisms ? mechanisms->doors : NULL,
                                         mechanisms ? mechanisms->door_count : 0u,
                                         door_index, out_door, error, error_size);
}

int level_mechanisms_get_lift(const LevelMechanisms *mechanisms, uint16_t lift_index,
                              LevelLiftable *out_lift,
                              char *error, size_t error_size)
{
    return level_mechanisms_get_liftable(mechanisms, mechanisms ? mechanisms->lifts : NULL,
                                         mechanisms ? mechanisms->lift_count : 0u,
                                         lift_index, out_lift, error, error_size);
}

int level_mechanisms_get_door_wall(const LevelMechanisms *mechanisms,
                                   uint16_t door_index, uint16_t wall_index,
                                   LevelLiftableWall *out_wall,
                                   char *error, size_t error_size)
{
    return level_mechanisms_get_liftable_wall(mechanisms,
                                              mechanisms ? mechanisms->doors : NULL,
                                              mechanisms ? mechanisms->door_count : 0u,
                                              door_index, wall_index, out_wall,
                                              error, error_size);
}

int level_mechanisms_get_lift_wall(const LevelMechanisms *mechanisms,
                                   uint16_t lift_index, uint16_t wall_index,
                                   LevelLiftableWall *out_wall,
                                   char *error, size_t error_size)
{
    return level_mechanisms_get_liftable_wall(mechanisms,
                                              mechanisms ? mechanisms->lifts : NULL,
                                              mechanisms ? mechanisms->lift_count : 0u,
                                              lift_index, wall_index, out_wall,
                                              error, error_size);
}

int level_mechanisms_get_switch(const LevelMechanisms *mechanisms,
                                uint16_t switch_index, LevelSwitch *out_switch,
                                char *error, size_t error_size)
{
    if (!mechanisms || !mechanisms->graphics_bytes || !out_switch ||
        switch_index >= LEVEL_MECHANISMS_SWITCH_COUNT) {
        level_mechanisms_set_error(error, error_size,
                                   "requested source switch is outside the runtime view");
        return 0;
    }
    *out_switch = mechanisms->switches[switch_index];
    return 1;
}
