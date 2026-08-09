#include "level_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    LEVEL_RUNTIME_ZONE_SIZE = 50,
    LEVEL_RUNTIME_POINT_SIZE = 4,
    LEVEL_RUNTIME_POINT_BRIGHTNESS_TRAILER = 4,
    LEVEL_RUNTIME_ZONE_BORDER_BYTES = 80,
    /* defs.i:ObjT_SizeOf_l and the two 32-bit object-point coordinates. */
    LEVEL_RUNTIME_OBJECT_SLOT_SIZE = 64,
    LEVEL_RUNTIME_OBJECT_POINT_SIZE = 8,
    /* defs.i:NUM_PLR_SHOT_DATA and NUM_ALIEN_SHOT_DATA. */
    LEVEL_RUNTIME_PROJECTILE_SLOT_COUNT = 20
};

static uint16_t level_runtime_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t level_runtime_read_be16s(const uint8_t *source)
{
    return (int16_t)level_runtime_read_be16(source);
}

static uint32_t level_runtime_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int32_t level_runtime_read_be32s(const uint8_t *source)
{
    return (int32_t)level_runtime_read_be32(source);
}

static void level_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int level_runtime_range_is_valid(uint32_t offset, size_t length, size_t total_size)
{
    return (size_t)offset <= total_size && length <= total_size - (size_t)offset;
}

int level_runtime_init(const AssetBlob *level_data, const AssetBlob *graphics_data,
                       const LevelBootstrap *level,
                       const LevelGraphicsBootstrap *graphics_header,
                       LevelRuntime *out_runtime,
                       char *error, size_t error_size)
{
    LevelRuntime runtime;
    uint64_t point_brightness_offset;
    uint64_t zone_border_points_offset;
    uint64_t zone_offsets_table_bytes;
    uint64_t object_point_count;
    uint64_t object_point_bytes;
    uint32_t object_record_count;
    size_t object_list_end;
    uint16_t zone_index;

    if (!level_data || !level_data->bytes || !graphics_data || !graphics_data->bytes ||
        !level || !graphics_header || !out_runtime) {
        level_runtime_set_error(error, error_size, "level runtime received null source data");
        return 0;
    }
    if (level->floor_line_offset < 2u) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "TLBT floor-line offset is invalid (floor=%u)",
                           level->floor_line_offset);
        }
        return 0;
    }

    /* hires.s:Game_Begin computes these pointer bases directly from TLBT. */
    point_brightness_offset = (uint64_t)level->points_offset +
        (uint64_t)level->point_count * LEVEL_RUNTIME_POINT_SIZE +
        LEVEL_RUNTIME_POINT_BRIGHTNESS_TRAILER;
    zone_border_points_offset = point_brightness_offset +
        (uint64_t)level->zone_count * LEVEL_RUNTIME_ZONE_BORDER_BYTES;
    zone_offsets_table_bytes = (uint64_t)level->zone_count * sizeof(uint32_t);
    /*
     * hires.s:413 copies TLBT_NumObjects into Lvl_NumObjectPoints_w. Every
     * transform loop uses DBRA, so the stored value is the final valid index.
     */
    object_point_count = (uint64_t)level->object_count + 1u;
    object_point_bytes = object_point_count * LEVEL_RUNTIME_OBJECT_POINT_SIZE;
    if (point_brightness_offset > UINT32_MAX || zone_border_points_offset > UINT32_MAX ||
        !level_runtime_range_is_valid(level->points_offset,
                                      (size_t)level->point_count * LEVEL_RUNTIME_POINT_SIZE,
                                      level_data->size) ||
        !level_runtime_range_is_valid((uint32_t)point_brightness_offset,
                                      LEVEL_RUNTIME_POINT_BRIGHTNESS_TRAILER,
                                      level_data->size) ||
        !level_runtime_range_is_valid((uint32_t)zone_border_points_offset,
                                      (size_t)level->zone_count * LEVEL_RUNTIME_ZONE_BORDER_BYTES,
                                      level_data->size) ||
        zone_offsets_table_bytes > SIZE_MAX ||
        !level_runtime_range_is_valid(graphics_header->zone_adds_table_offset,
                                      (size_t)zone_offsets_table_bytes,
                                      graphics_data->size) ||
        !level_runtime_range_is_valid(graphics_header->zone_graph_adds_offset,
                                      (size_t)level->zone_count * 2u * sizeof(uint32_t),
                                      graphics_data->size) ||
        object_point_bytes > SIZE_MAX ||
        !level_runtime_range_is_valid(level->object_data_offset, sizeof(uint16_t),
                                      level_data->size) ||
        !level_runtime_range_is_valid(level->object_points_offset,
                                      (size_t)object_point_bytes, level_data->size) ||
        !level_runtime_range_is_valid(level->player_shot_offset,
                                      LEVEL_RUNTIME_PROJECTILE_SLOT_COUNT *
                                          LEVEL_RUNTIME_OBJECT_SLOT_SIZE,
                                      level_data->size) ||
        !level_runtime_range_is_valid(level->alien_shot_offset,
                                      LEVEL_RUNTIME_PROJECTILE_SLOT_COUNT *
                                          LEVEL_RUNTIME_OBJECT_SLOT_SIZE,
                                      level_data->size) ||
        !level_runtime_range_is_valid(level->player1_object_offset,
                                      LEVEL_RUNTIME_OBJECT_SLOT_SIZE, level_data->size) ||
        !level_runtime_range_is_valid(level->player2_object_offset,
                                      LEVEL_RUNTIME_OBJECT_SLOT_SIZE, level_data->size)) {
        level_runtime_set_error(error, error_size, "Game_Begin level table range is outside its source file");
        return 0;
    }
    /*
     * The source uses TLBT_NumObjects only for the object-point transform
     * loop. newanims.s:ObjectHandler independently walks ObjT records until
     * word zero is negative, so use the file boundary only as a native guard.
     */
    object_record_count = 0;
    object_list_end = (size_t)level->object_data_offset;
    while (object_list_end <= level_data->size &&
           sizeof(uint16_t) <= level_data->size - object_list_end &&
           level_runtime_read_be16s(level_data->bytes + object_list_end) >= 0) {
        if (LEVEL_RUNTIME_OBJECT_SLOT_SIZE > level_data->size - object_list_end) {
            level_runtime_set_error(error, error_size,
                                    "ObjT record is truncated before its source -1 terminator");
            return 0;
        }
        ++object_record_count;
        object_list_end += LEVEL_RUNTIME_OBJECT_SLOT_SIZE;
    }
    if (object_list_end > level_data->size ||
        sizeof(uint16_t) > level_data->size - object_list_end) {
        level_runtime_set_error(error, error_size,
                                "ObjT static list has no source -1 terminator");
        return 0;
    }
    if (level->player_shot_offset < level->object_data_offset ||
        level->alien_shot_offset < level->object_data_offset ||
        level->player1_object_offset < level->object_data_offset ||
        level->player2_object_offset < level->object_data_offset ||
        (uint64_t)level->player_shot_offset +
                LEVEL_RUNTIME_PROJECTILE_SLOT_COUNT * LEVEL_RUNTIME_OBJECT_SLOT_SIZE >
            object_list_end ||
        (uint64_t)level->alien_shot_offset +
                LEVEL_RUNTIME_PROJECTILE_SLOT_COUNT * LEVEL_RUNTIME_OBJECT_SLOT_SIZE >
            object_list_end ||
        (uint64_t)level->player1_object_offset + LEVEL_RUNTIME_OBJECT_SLOT_SIZE > object_list_end ||
        (uint64_t)level->player2_object_offset + LEVEL_RUNTIME_OBJECT_SLOT_SIZE > object_list_end ||
        (level->player_shot_offset - level->object_data_offset) % LEVEL_RUNTIME_OBJECT_SLOT_SIZE != 0u ||
        (level->alien_shot_offset - level->object_data_offset) % LEVEL_RUNTIME_OBJECT_SLOT_SIZE != 0u ||
        (level->player1_object_offset - level->object_data_offset) % LEVEL_RUNTIME_OBJECT_SLOT_SIZE != 0u ||
        (level->player2_object_offset - level->object_data_offset) % LEVEL_RUNTIME_OBJECT_SLOT_SIZE != 0u) {
        level_runtime_set_error(error, error_size,
                                "Game_Begin object pointers are outside the ObjT record list");
        return 0;
    }

    for (zone_index = 0; zone_index < level->zone_count; ++zone_index) {
        uint32_t zone_offset = level_runtime_read_be32(
            graphics_data->bytes + graphics_header->zone_adds_table_offset +
                                                        (size_t)zone_index * sizeof(uint32_t));
        if (!level_runtime_range_is_valid(zone_offset, LEVEL_RUNTIME_ZONE_SIZE, level_data->size)) {
            level_runtime_set_error(error, error_size,
                                    "TLGT zone offset is outside the twolev.bin zone data");
            return 0;
        }
    }

    memset(&runtime, 0, sizeof(runtime));
    runtime.level_bytes = level_data->bytes;
    runtime.level_size = level_data->size;
    runtime.graphics_bytes = graphics_data->bytes;
    runtime.graphics_size = graphics_data->size;
    runtime.control_point_coordinates_offset = AB3D2_LEVEL_MESSAGE_BYTES + AB3D2_TLBT_SIZE;
    runtime.point_brightness_offset = (uint32_t)point_brightness_offset;
    runtime.zone_border_points_offset = (uint32_t)zone_border_points_offset;
    /* hires.s:Game_Begin takes this base from TLGT_ZoneAddsOffset_l (byte 16). */
    runtime.zone_graph_adds_offset = graphics_header->zone_graph_adds_offset;
    runtime.zone_offsets_table_offset = graphics_header->zone_adds_table_offset;
    runtime.object_data_offset = level->object_data_offset;
    runtime.player_shot_offset = level->player_shot_offset;
    runtime.alien_shot_offset = level->alien_shot_offset;
    runtime.object_points_offset = level->object_points_offset;
    runtime.player1_object_offset = level->player1_object_offset;
    runtime.player2_object_offset = level->player2_object_offset;
    runtime.object_point_count = (uint32_t)object_point_count;
    runtime.object_record_count = object_record_count;
    runtime.edge_data_span = (int32_t)level->object_data_offset -
                             (int32_t)level->floor_line_offset;
    runtime.exit_zone_id = level_runtime_read_be16s(level_data->bytes + level->floor_line_offset - 2u);
    runtime.zone_count = level->zone_count;
    *out_runtime = runtime;
    return 1;
}

int level_runtime_get_zone(const LevelRuntime *runtime, uint16_t zone_index,
                           LevelZone *out_zone, char *error, size_t error_size)
{
    const uint8_t *source;
    uint32_t zone_offset;
    LevelZone zone;

    if (!runtime || !runtime->level_bytes || !runtime->graphics_bytes || !out_zone ||
        zone_index >= runtime->zone_count) {
        level_runtime_set_error(error, error_size, "requested level zone is outside the runtime view");
        return 0;
    }
    zone_offset = level_runtime_read_be32(runtime->graphics_bytes + runtime->zone_offsets_table_offset +
                                           (size_t)zone_index * sizeof(uint32_t));
    if (!level_runtime_range_is_valid(zone_offset, LEVEL_RUNTIME_ZONE_SIZE, runtime->level_size)) {
        level_runtime_set_error(error, error_size, "requested level zone is malformed");
        return 0;
    }
    source = runtime->level_bytes + zone_offset;
    memset(&zone, 0, sizeof(zone));
    zone.id = level_runtime_read_be16(source + 0u);
    zone.floor = level_runtime_read_be32s(source + 2u);
    zone.roof = level_runtime_read_be32s(source + 6u);
    zone.upper_floor = level_runtime_read_be32s(source + 10u);
    zone.upper_roof = level_runtime_read_be32s(source + 14u);
    zone.water = level_runtime_read_be32s(source + 18u);
    zone.brightness = level_runtime_read_be16(source + 22u);
    zone.upper_brightness = level_runtime_read_be16(source + 24u);
    zone.control_point = level_runtime_read_be16(source + 26u);
    zone.background_sfx_mask = level_runtime_read_be16(source + 28u);
    zone.edge_list_relative_offset = level_runtime_read_be16s(source + 32u);
    zone.points_relative_offset = level_runtime_read_be16s(source + 34u);
    zone.draw_backdrop = source[36u];
    zone.echo = source[37u];
    zone.teleport_zone = level_runtime_read_be16s(source + 38u);
    zone.teleport_x = level_runtime_read_be16s(source + 40u);
    zone.teleport_z = level_runtime_read_be16s(source + 42u);
    zone.floor_noise = level_runtime_read_be16(source + 44u);
    zone.upper_floor_noise = level_runtime_read_be16(source + 46u);
    *out_zone = zone;
    return 1;
}

int level_runtime_get_object_record(const LevelRuntime *runtime, uint32_t record_index,
                                    LevelObjectSlot *out_object,
                                    char *error, size_t error_size)
{
    const uint8_t *source;
    LevelObjectSlot object;
    size_t slot_offset;

    if (!runtime || !runtime->level_bytes || !out_object ||
        record_index >= runtime->object_record_count) {
        level_runtime_set_error(error, error_size,
                                "requested ObjT slot is outside the runtime view");
        return 0;
    }
    slot_offset = (size_t)runtime->object_data_offset +
        (size_t)record_index * LEVEL_RUNTIME_OBJECT_SLOT_SIZE;
    if (slot_offset > runtime->level_size ||
        LEVEL_RUNTIME_OBJECT_SLOT_SIZE > runtime->level_size - slot_offset) {
        level_runtime_set_error(error, error_size,
                                "requested ObjT slot is outside the runtime view");
        return 0;
    }
    source = runtime->level_bytes + slot_offset;
    object.point_index = level_runtime_read_be16(source + 0u);
    object.zone_id = level_runtime_read_be16s(source + 12u);
    object.type_id = source[16u];
    object.sees_player = source[17u];
    *out_object = object;
    return 1;
}

int level_runtime_get_object_point(const LevelRuntime *runtime, uint32_t point_index,
                                   LevelObjectPoint *out_point,
                                   char *error, size_t error_size)
{
    const uint8_t *source;
    LevelObjectPoint point;
    size_t point_offset;

    if (!runtime || !runtime->level_bytes || !out_point ||
        point_index >= runtime->object_point_count) {
        level_runtime_set_error(error, error_size,
                                "requested object point is outside the runtime view");
        return 0;
    }
    point_offset = (size_t)runtime->object_points_offset +
        (size_t)point_index * LEVEL_RUNTIME_OBJECT_POINT_SIZE;
    if (point_offset > runtime->level_size ||
        LEVEL_RUNTIME_OBJECT_POINT_SIZE > runtime->level_size - point_offset) {
        level_runtime_set_error(error, error_size,
                                "requested object point is outside the runtime view");
        return 0;
    }
    source = runtime->level_bytes + point_offset;
    point.x = level_runtime_read_be32s(source + 0u);
    point.z = level_runtime_read_be32s(source + 4u);
    *out_point = point;
    return 1;
}
