#include "level_draw_graph.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    LEVEL_DRAW_GRAPH_TAG_SIZE = 2,
    /* hireswall.s:Draw_Wall consumes 28 payload bytes after the tag. */
    LEVEL_DRAW_GRAPH_WALL_SIZE = 30,
    /* draw_zone_graph.s:Draw_Objects consumes a one-word selector. */
    LEVEL_DRAW_GRAPH_OBJECTS_SIZE = 4,
    /* Draw_Flats uses `lea 10(a0,d6.w*2),a0` after floor height/count. */
    LEVEL_DRAW_GRAPH_FLAT_FIXED_SIZE = 16,
    /* Draw_Flats masks its source point words with this before point lookup. */
    LEVEL_DRAW_GRAPH_FLAT_POINT_INDEX_MASK = 0x0fff
};

static uint16_t level_draw_graph_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t level_draw_graph_read_be16s(const uint8_t *source)
{
    return (int16_t)level_draw_graph_read_be16(source);
}

static uint32_t level_draw_graph_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int32_t level_draw_graph_read_be32s(const uint8_t *source)
{
    return (int32_t)level_draw_graph_read_be32(source);
}

static void level_draw_graph_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int level_draw_graph_range_is_valid(size_t offset, size_t length, size_t total_size)
{
    return offset <= total_size && length <= total_size - offset;
}

static int level_draw_graph_stream_start(const LevelRuntime *runtime, uint16_t zone_index,
                                         int upper_stream, size_t *out_cursor,
                                         char *error, size_t error_size)
{
    LevelDrawGraphStreams streams;
    uint32_t stream_offset;

    if (!out_cursor ||
        !level_runtime_get_zone_draw_graph_streams(runtime, zone_index, &streams,
                                                   error, error_size)) {
        return 0;
    }
    if (upper_stream && streams.has_upper_stream == 0u) {
        level_draw_graph_set_error(error, error_size,
                                   "requested zone has no upper draw-graph stream");
        return 0;
    }
    stream_offset = upper_stream ? streams.upper_stream_offset : streams.lower_stream_offset;
    if (!level_draw_graph_range_is_valid(stream_offset, LEVEL_DRAW_GRAPH_TAG_SIZE,
                                         runtime->graphics_size) ||
        level_draw_graph_read_be16s(runtime->graphics_bytes + stream_offset) !=
            (int16_t)zone_index) {
        level_draw_graph_set_error(error, error_size,
                                   "draw-graph stream does not begin with its source zone id");
        return 0;
    }
    *out_cursor = (size_t)stream_offset + LEVEL_DRAW_GRAPH_TAG_SIZE;
    return 1;
}

/*
 * Active draw_zone_graph.s cursor semantics. Types not dispatched there,
 * including its legacy type-3 set-clip tag, deliberately consume only their
 * tag word. This is a source-data parser, not an emulation of PVS clipping.
 */
static int level_draw_graph_next_record(const LevelRuntime *runtime, size_t cursor,
                                        LevelDrawGraphRecord *out_record, int *out_ended,
                                        char *error, size_t error_size)
{
    uint16_t raw_tag;
    uint8_t type;
    uint64_t record_size;

    if (!runtime || !runtime->graphics_bytes || !out_record || !out_ended ||
        !level_draw_graph_range_is_valid(cursor, LEVEL_DRAW_GRAPH_TAG_SIZE,
                                         runtime->graphics_size)) {
        level_draw_graph_set_error(error, error_size,
                                   "draw-graph stream is missing its source terminator");
        return 0;
    }
    raw_tag = level_draw_graph_read_be16(runtime->graphics_bytes + cursor);
    type = (uint8_t)raw_tag;
    if ((int8_t)type < 0) {
        *out_ended = 1;
        return 1;
    }

    record_size = LEVEL_DRAW_GRAPH_TAG_SIZE;
    if (type == LEVEL_DRAW_GRAPH_TYPE_WALL) {
        record_size = LEVEL_DRAW_GRAPH_WALL_SIZE;
    } else if (type == LEVEL_DRAW_GRAPH_TYPE_FLOOR ||
               type == LEVEL_DRAW_GRAPH_TYPE_CEILING ||
               type == LEVEL_DRAW_GRAPH_TYPE_WATER) {
        uint16_t sides_minus_one;

        if (!level_draw_graph_range_is_valid(cursor + LEVEL_DRAW_GRAPH_TAG_SIZE,
                                             2u * sizeof(uint16_t), runtime->graphics_size)) {
            level_draw_graph_set_error(error, error_size, "flat draw-graph record is truncated");
            return 0;
        }
        sides_minus_one = level_draw_graph_read_be16(
            runtime->graphics_bytes + cursor + LEVEL_DRAW_GRAPH_TAG_SIZE + sizeof(uint16_t));
        record_size = LEVEL_DRAW_GRAPH_FLAT_FIXED_SIZE +
            (uint64_t)sides_minus_one * sizeof(uint16_t);
    } else if (type == LEVEL_DRAW_GRAPH_TYPE_OBJECTS) {
        record_size = LEVEL_DRAW_GRAPH_OBJECTS_SIZE;
    }
    if (record_size > UINT32_MAX || record_size > SIZE_MAX ||
        !level_draw_graph_range_is_valid(cursor, (size_t)record_size, runtime->graphics_size)) {
        level_draw_graph_set_error(error, error_size, "draw-graph record extends past its source file");
        return 0;
    }
    memset(out_record, 0, sizeof(*out_record));
    out_record->raw_tag = raw_tag;
    out_record->type = type;
    out_record->source_offset = (uint32_t)cursor;
    out_record->byte_count = (uint32_t)record_size;
    *out_ended = 0;
    return 1;
}

static int level_draw_graph_scan(const LevelRuntime *runtime, uint16_t zone_index,
                                 int upper_stream, uint32_t wanted_index,
                                 LevelDrawGraphRecord *out_record, uint32_t *out_count,
                                 char *error, size_t error_size)
{
    size_t cursor;
    uint32_t count;

    if (!level_draw_graph_stream_start(runtime, zone_index, upper_stream, &cursor,
                                       error, error_size)) {
        return 0;
    }
    count = 0u;
    for (;;) {
        LevelDrawGraphRecord record;
        int ended;

        if (!level_draw_graph_next_record(runtime, cursor, &record, &ended, error, error_size)) {
            return 0;
        }
        if (ended) {
            if (out_count) {
                *out_count = count;
            }
            if (out_record) {
                level_draw_graph_set_error(error, error_size,
                                           "draw-graph record index is outside the source stream");
                return 0;
            }
            return 1;
        }
        if (out_record && count == wanted_index) {
            *out_record = record;
            return 1;
        }
        if (count == UINT32_MAX) {
            level_draw_graph_set_error(error, error_size,
                                       "draw-graph stream has too many source records");
            return 0;
        }
        cursor += record.byte_count;
        ++count;
    }
}

int level_draw_graph_record_count(const LevelRuntime *runtime, uint16_t zone_index,
                                  int upper_stream, uint32_t *out_count,
                                  char *error, size_t error_size)
{
    if (!out_count) {
        level_draw_graph_set_error(error, error_size, "draw-graph record count output is null");
        return 0;
    }
    return level_draw_graph_scan(runtime, zone_index, upper_stream, UINT32_MAX, NULL,
                                 out_count, error, error_size);
}

int level_draw_graph_get_record(const LevelRuntime *runtime, uint16_t zone_index,
                                int upper_stream, uint32_t record_index,
                                LevelDrawGraphRecord *out_record,
                                char *error, size_t error_size)
{
    if (!out_record) {
        level_draw_graph_set_error(error, error_size, "draw-graph record output is null");
        return 0;
    }
    return level_draw_graph_scan(runtime, zone_index, upper_stream, record_index, out_record,
                                 NULL, error, error_size);
}

int level_draw_graph_read_wall(const LevelRuntime *runtime,
                               const LevelDrawGraphRecord *record,
                               LevelDrawWall *out_wall,
                               char *error, size_t error_size)
{
    const uint8_t *source;
    LevelDrawWall wall;

    if (!runtime || !runtime->graphics_bytes || !record || !out_wall ||
        record->type != LEVEL_DRAW_GRAPH_TYPE_WALL ||
        record->byte_count != LEVEL_DRAW_GRAPH_WALL_SIZE ||
        !level_draw_graph_range_is_valid(record->source_offset, LEVEL_DRAW_GRAPH_WALL_SIZE,
                                         runtime->graphics_size)) {
        level_draw_graph_set_error(error, error_size, "draw-graph wall record is malformed");
        return 0;
    }
    source = runtime->graphics_bytes + record->source_offset;
    memset(&wall, 0, sizeof(wall));
    wall.left_point_index = level_draw_graph_read_be16(source + 2u);
    wall.right_point_index = level_draw_graph_read_be16(source + 4u);
    wall.left_point_brightness = source[6u];
    wall.right_point_brightness = source[7u];
    wall.texture_u_end = level_draw_graph_read_be16(source + 8u);
    wall.texture_u_tile = level_draw_graph_read_be16(source + 10u);
    wall.texture_y_offset = level_draw_graph_read_be16(source + 12u);
    wall.texture_id = level_draw_graph_read_be16(source + 14u);
    wall.texture_height_mask = source[16u];
    wall.texture_height_shift = source[17u];
    wall.texture_width_mask = source[18u];
    wall.point_brightness_selector = source[19u];
    wall.top = level_draw_graph_read_be32s(source + 20u);
    wall.bottom = level_draw_graph_read_be32s(source + 24u);
    wall.brightness_offset = (int8_t)source[28u];
    wall.other_zone = source[29u];
    *out_wall = wall;
    return 1;
}

int level_draw_graph_read_flat(const LevelRuntime *runtime,
                               const LevelDrawGraphRecord *record,
                               LevelDrawFlat *out_flat,
                               char *error, size_t error_size)
{
    const uint8_t *source;
    uint16_t sides_minus_one;
    uint64_t expected_size;
    LevelDrawFlat flat;

    if (!runtime || !runtime->graphics_bytes || !record || !out_flat ||
        (record->type != LEVEL_DRAW_GRAPH_TYPE_FLOOR &&
         record->type != LEVEL_DRAW_GRAPH_TYPE_CEILING &&
         record->type != LEVEL_DRAW_GRAPH_TYPE_WATER) ||
        !level_draw_graph_range_is_valid(record->source_offset, record->byte_count,
                                         runtime->graphics_size)) {
        level_draw_graph_set_error(error, error_size, "draw-graph flat record is malformed");
        return 0;
    }
    source = runtime->graphics_bytes + record->source_offset;
    sides_minus_one = level_draw_graph_read_be16(source + 4u);
    expected_size = LEVEL_DRAW_GRAPH_FLAT_FIXED_SIZE +
        (uint64_t)sides_minus_one * sizeof(uint16_t);
    if (expected_size != record->byte_count || sides_minus_one == UINT16_MAX) {
        level_draw_graph_set_error(error, error_size, "draw-graph flat record has an invalid side count");
        return 0;
    }
    flat.height = level_draw_graph_read_be16s(source + 2u);
    /* Draw_Flats uses DBRA, hence its stored value is the final point index. */
    flat.point_count = (uint16_t)(sides_minus_one + 1u);
    flat.points_offset = record->source_offset + 6u;
    /* hires.s:pastsides advances one word, then reads scale, tile, and light. */
    flat.skipped_word = level_draw_graph_read_be16(
        source + 6u + (size_t)flat.point_count * sizeof(uint16_t));
    flat.texture_scale = level_draw_graph_read_be16s(
        source + 8u + (size_t)flat.point_count * sizeof(uint16_t));
    flat.texture_offset = level_draw_graph_read_be16(
        source + 10u + (size_t)flat.point_count * sizeof(uint16_t));
    flat.brightness_offset = level_draw_graph_read_be16s(
        source + 12u + (size_t)flat.point_count * sizeof(uint16_t));
    *out_flat = flat;
    return 1;
}

int level_draw_graph_get_flat_point(const LevelRuntime *runtime,
                                    const LevelDrawFlat *flat, uint16_t point_index,
                                    uint16_t *out_raw_point_word,
                                    uint16_t *out_world_point_index,
                                    char *error, size_t error_size)
{
    size_t point_offset;
    uint16_t raw_point_word;
    uint16_t world_point_index;

    if (!runtime || !runtime->graphics_bytes || !flat || !out_raw_point_word ||
        !out_world_point_index || point_index >= flat->point_count) {
        level_draw_graph_set_error(error, error_size,
                                   "draw-graph flat point is outside the runtime view");
        return 0;
    }
    point_offset = (size_t)flat->points_offset + (size_t)point_index * sizeof(uint16_t);
    if (!level_draw_graph_range_is_valid(point_offset, sizeof(uint16_t), runtime->graphics_size)) {
        level_draw_graph_set_error(error, error_size,
                                   "draw-graph flat point is outside the source file");
        return 0;
    }
    raw_point_word = level_draw_graph_read_be16(runtime->graphics_bytes + point_offset);
    world_point_index = raw_point_word & LEVEL_DRAW_GRAPH_FLAT_POINT_INDEX_MASK;
    if (world_point_index >= runtime->world_point_count) {
        level_draw_graph_set_error(error, error_size,
                                   "draw-graph flat point references an invalid world point");
        return 0;
    }
    *out_raw_point_word = raw_point_word;
    *out_world_point_index = world_point_index;
    return 1;
}
