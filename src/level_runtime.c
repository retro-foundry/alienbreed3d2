#include "level_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    LEVEL_RUNTIME_ZONE_SIZE = 50,
    LEVEL_RUNTIME_POINT_SIZE = 4,
    LEVEL_RUNTIME_POINT_BRIGHTNESS_TRAILER = 4,
    LEVEL_RUNTIME_ZONE_BORDER_BYTES = 80
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
                                      graphics_data->size)) {
        level_runtime_set_error(error, error_size, "Game_Begin level table range is outside its source file");
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
