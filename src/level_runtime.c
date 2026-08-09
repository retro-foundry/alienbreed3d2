#include "level_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    LEVEL_RUNTIME_ZONE_SIZE = 50,
    LEVEL_RUNTIME_POINT_SIZE = 4,
    LEVEL_RUNTIME_ZONE_BORDER_BYTES = 80,
    /* defs.i:EdgeT_SizeOf_l. */
    LEVEL_RUNTIME_EDGE_SIZE = 16,
    /* modules/ai.s indexes Lvl_ControlPointCoordsPtr_l by eight bytes. */
    LEVEL_RUNTIME_CONTROL_POINT_SIZE = 8,
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

/*
 * objectmove.s starts at ZoneT_EdgeListOffset_w and stops its normal collision
 * pass at the first negative word. Some source paths then inspect an extended
 * segment through a -2 marker, so this deliberately exposes only the primary
 * sequence rather than guessing a single flattened list format.
 */
static int level_runtime_get_primary_zone_edge_list(const LevelRuntime *runtime,
                                                    uint16_t zone_index,
                                                    uint32_t *out_list_offset,
                                                    uint32_t *out_count,
                                                    uint32_t *out_highest_edge_index,
                                                    char *error, size_t error_size)
{
    uint32_t zone_offset;
    int64_t list_offset;
    uint32_t list_start;
    uint32_t edge_count;
    uint32_t highest_edge_index;

    if (!runtime || !runtime->level_bytes || !runtime->graphics_bytes ||
        zone_index >= runtime->zone_count) {
        level_runtime_set_error(error, error_size,
                                "requested zone edge list is outside the runtime view");
        return 0;
    }
    zone_offset = level_runtime_read_be32(runtime->graphics_bytes +
                                           runtime->zone_offsets_table_offset +
                                           (size_t)zone_index * sizeof(uint32_t));
    if (!level_runtime_range_is_valid(zone_offset, LEVEL_RUNTIME_ZONE_SIZE, runtime->level_size)) {
        level_runtime_set_error(error, error_size, "requested zone edge list has a malformed zone");
        return 0;
    }
    list_offset = (int64_t)zone_offset +
        (int64_t)level_runtime_read_be16s(runtime->level_bytes + zone_offset + 32u);
    if (list_offset < 0 || (uint64_t)list_offset > UINT32_MAX ||
        !level_runtime_range_is_valid((uint32_t)list_offset, sizeof(uint16_t),
                                      runtime->level_size)) {
        level_runtime_set_error(error, error_size,
                                "ZoneT edge-list offset is outside the level data");
        return 0;
    }

    list_start = (uint32_t)list_offset;
    edge_count = 0;
    highest_edge_index = 0;
    while (level_runtime_range_is_valid((uint32_t)list_offset,
                                        sizeof(uint16_t), runtime->level_size)) {
        int16_t edge_index = level_runtime_read_be16s(runtime->level_bytes + list_offset);
        if (edge_index < 0) {
            if (out_list_offset) {
                *out_list_offset = list_start;
            }
            if (out_count) {
                *out_count = edge_count;
            }
            if (out_highest_edge_index) {
                *out_highest_edge_index = highest_edge_index;
            }
            return 1;
        }
        if (!level_runtime_range_is_valid(runtime->edge_table_offset +
                                          (size_t)(uint16_t)edge_index * LEVEL_RUNTIME_EDGE_SIZE,
                                          LEVEL_RUNTIME_EDGE_SIZE, runtime->level_size)) {
            level_runtime_set_error(error, error_size,
                                    "ZoneT edge list references an EdgeT outside its table");
            return 0;
        }
        if (runtime->edge_count != 0u && (uint32_t)edge_index >= runtime->edge_count) {
            level_runtime_set_error(error, error_size,
                                    "ZoneT edge list exceeds its validated EdgeT prefix");
            return 0;
        }
        if (edge_count == 0u || (uint32_t)edge_index > highest_edge_index) {
            highest_edge_index = (uint32_t)edge_index;
        }
        if (edge_count == UINT32_MAX) {
            level_runtime_set_error(error, error_size, "ZoneT edge list is too long");
            return 0;
        }
        ++edge_count;
        list_offset += sizeof(uint16_t);
    }
    level_runtime_set_error(error, error_size,
                            "ZoneT edge list has no source negative terminator");
    return 0;
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
    uint64_t control_point_bytes;
    uint64_t world_point_count;
    uint64_t object_point_count;
    uint64_t object_point_bytes;
    int64_t edge_data_span;
    uint32_t object_record_count;
    uint32_t highest_primary_edge_index;
    int has_primary_edge;
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
    edge_data_span = (int64_t)level->object_data_offset - (int64_t)level->floor_line_offset;
    if (edge_data_span < INT32_MIN || edge_data_span > INT32_MAX) {
        level_runtime_set_error(error, error_size,
                                "TLBT floor/object offset difference is outside the native range");
        return 0;
    }

    /*
     * transform.s uses DBRA with Lvl_NumPoints_w, so TLBT stores the final
     * valid Vec2W index. Game_Begin's `lea 4(a2,d0.w*4)` reaches the first
     * point-brightness word immediately after that inclusive point array.
     */
    world_point_count = (uint64_t)level->point_count + 1u;
    point_brightness_offset = (uint64_t)level->points_offset +
        world_point_count * LEVEL_RUNTIME_POINT_SIZE;
    zone_border_points_offset = point_brightness_offset +
        (uint64_t)level->zone_count * LEVEL_RUNTIME_ZONE_BORDER_BYTES;
    zone_offsets_table_bytes = (uint64_t)level->zone_count * sizeof(uint32_t);
    control_point_bytes = (uint64_t)level->control_point_count *
        LEVEL_RUNTIME_CONTROL_POINT_SIZE;
    /*
     * hires.s:413 copies TLBT_NumObjects into Lvl_NumObjectPoints_w. Every
     * transform loop uses DBRA, so the stored value is the final valid index.
     */
    object_point_count = (uint64_t)level->object_count + 1u;
    object_point_bytes = object_point_count * LEVEL_RUNTIME_OBJECT_POINT_SIZE;
    if (world_point_count > UINT32_MAX || point_brightness_offset > UINT32_MAX ||
        zone_border_points_offset > UINT32_MAX ||
        world_point_count > SIZE_MAX / LEVEL_RUNTIME_POINT_SIZE ||
        !level_runtime_range_is_valid(level->points_offset,
                                      (size_t)world_point_count * LEVEL_RUNTIME_POINT_SIZE,
                                      level_data->size) ||
        control_point_bytes > SIZE_MAX ||
        !level_runtime_range_is_valid(AB3D2_LEVEL_MESSAGE_BYTES + AB3D2_TLBT_SIZE,
                                      (size_t)control_point_bytes, level_data->size) ||
        !level_runtime_range_is_valid((uint32_t)point_brightness_offset,
                                      sizeof(uint32_t),
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
    runtime.control_point_count = level->control_point_count;
    runtime.world_point_count = (uint32_t)world_point_count;
    runtime.world_points_offset = level->points_offset;
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
    runtime.edge_data_span = (int32_t)edge_data_span;
    runtime.edge_table_offset = level->floor_line_offset;
    runtime.edge_count = 0;
    runtime.exit_zone_id = level_runtime_read_be16s(level_data->bytes + level->floor_line_offset - 2u);
    runtime.zone_count = level->zone_count;

    highest_primary_edge_index = 0;
    has_primary_edge = 0;
    for (zone_index = 0; zone_index < runtime.zone_count; ++zone_index) {
        uint32_t primary_edge_count;
        uint32_t zone_highest_edge_index;
        if (!level_runtime_get_primary_zone_edge_list(&runtime, zone_index, NULL,
                                                      &primary_edge_count,
                                                      &zone_highest_edge_index,
                                                      error, error_size)) {
            return 0;
        }
        if (primary_edge_count != 0u &&
            (!has_primary_edge || zone_highest_edge_index > highest_primary_edge_index)) {
            highest_primary_edge_index = zone_highest_edge_index;
            has_primary_edge = 1;
        }
    }
    if (has_primary_edge) {
        runtime.edge_count = highest_primary_edge_index + 1u;
    }
    for (uint32_t edge_index = 0; edge_index < runtime.edge_count; ++edge_index) {
        const uint8_t *edge_source = level_data->bytes + runtime.edge_table_offset +
            (size_t)edge_index * LEVEL_RUNTIME_EDGE_SIZE;
        int16_t join_zone_id = level_runtime_read_be16s(edge_source + 8u);
        if (join_zone_id >= 0 && (uint16_t)join_zone_id >= runtime.zone_count) {
            level_runtime_set_error(error, error_size,
                                    "EdgeT join-zone index is outside the source zone table");
            return 0;
        }
    }
    *out_runtime = runtime;
    return 1;
}

int level_runtime_get_zone_edge_count(const LevelRuntime *runtime, uint16_t zone_index,
                                      uint32_t *out_count,
                                      char *error, size_t error_size)
{
    if (!out_count) {
        level_runtime_set_error(error, error_size, "zone edge-count output is null");
        return 0;
    }
    return level_runtime_get_primary_zone_edge_list(runtime, zone_index, NULL, out_count, NULL,
                                                     error, error_size);
}

int level_runtime_get_zone_edge_index(const LevelRuntime *runtime, uint16_t zone_index,
                                      uint32_t list_index, uint32_t *out_edge_index,
                                      char *error, size_t error_size)
{
    uint32_t list_offset;
    uint32_t edge_count;

    if (!out_edge_index) {
        level_runtime_set_error(error, error_size, "zone edge-index output is null");
        return 0;
    }
    if (!level_runtime_get_primary_zone_edge_list(runtime, zone_index, &list_offset, &edge_count,
                                                   NULL, error, error_size)) {
        return 0;
    }
    if (list_index >= edge_count) {
        level_runtime_set_error(error, error_size,
                                "requested zone edge is outside the primary source list");
        return 0;
    }
    *out_edge_index = level_runtime_read_be16(runtime->level_bytes + list_offset +
                                               (size_t)list_index * sizeof(uint16_t));
    return 1;
}

int level_runtime_get_edge(const LevelRuntime *runtime, uint32_t edge_index,
                           LevelEdge *out_edge, char *error, size_t error_size)
{
    const uint8_t *source;
    LevelEdge edge;
    size_t edge_offset;

    if (!runtime || !runtime->level_bytes || !out_edge || edge_index >= runtime->edge_count) {
        level_runtime_set_error(error, error_size, "requested EdgeT is outside the runtime view");
        return 0;
    }
    edge_offset = (size_t)runtime->edge_table_offset +
        (size_t)edge_index * LEVEL_RUNTIME_EDGE_SIZE;
    if (edge_offset > runtime->level_size ||
        LEVEL_RUNTIME_EDGE_SIZE > runtime->level_size - edge_offset) {
        level_runtime_set_error(error, error_size, "requested EdgeT is outside the runtime view");
        return 0;
    }
    source = runtime->level_bytes + edge_offset;
    edge.x = level_runtime_read_be16s(source + 0u);
    edge.z = level_runtime_read_be16s(source + 2u);
    edge.x_length = level_runtime_read_be16s(source + 4u);
    edge.z_length = level_runtime_read_be16s(source + 6u);
    edge.join_zone_id = level_runtime_read_be16s(source + 8u);
    edge.unknown_word = level_runtime_read_be16s(source + 10u);
    edge.unknown_byte_12 = (int8_t)source[12u];
    edge.unknown_byte_13 = (int8_t)source[13u];
    edge.flags = level_runtime_read_be16(source + 14u);
    *out_edge = edge;
    return 1;
}

int level_runtime_get_control_point(const LevelRuntime *runtime, uint16_t control_point_index,
                                    LevelControlPoint *out_control_point,
                                    char *error, size_t error_size)
{
    const uint8_t *source;
    LevelControlPoint control_point;
    size_t control_point_offset;

    if (!runtime || !runtime->level_bytes || !out_control_point ||
        control_point_index >= runtime->control_point_count) {
        level_runtime_set_error(error, error_size,
                                "requested control point is outside the runtime view");
        return 0;
    }
    control_point_offset = (size_t)runtime->control_point_coordinates_offset +
        (size_t)control_point_index * LEVEL_RUNTIME_CONTROL_POINT_SIZE;
    if (control_point_offset > runtime->level_size ||
        LEVEL_RUNTIME_CONTROL_POINT_SIZE > runtime->level_size - control_point_offset) {
        level_runtime_set_error(error, error_size,
                                "requested control point is outside the runtime view");
        return 0;
    }
    source = runtime->level_bytes + control_point_offset;
    control_point.x = level_runtime_read_be16s(source + 0u);
    control_point.z = level_runtime_read_be16s(source + 2u);
    control_point.height = level_runtime_read_be16s(source + 4u);
    control_point.unknown_word = level_runtime_read_be16s(source + 6u);
    *out_control_point = control_point;
    return 1;
}

int level_runtime_get_world_point(const LevelRuntime *runtime, uint32_t point_index,
                                  LevelWorldPoint *out_point,
                                  char *error, size_t error_size)
{
    const uint8_t *source;
    LevelWorldPoint point;
    size_t point_offset;

    if (!runtime || !runtime->level_bytes || !out_point ||
        point_index >= runtime->world_point_count) {
        level_runtime_set_error(error, error_size,
                                "requested world point is outside the runtime view");
        return 0;
    }
    point_offset = (size_t)runtime->world_points_offset +
        (size_t)point_index * LEVEL_RUNTIME_POINT_SIZE;
    if (point_offset > runtime->level_size ||
        LEVEL_RUNTIME_POINT_SIZE > runtime->level_size - point_offset) {
        level_runtime_set_error(error, error_size,
                                "requested world point is outside the runtime view");
        return 0;
    }
    source = runtime->level_bytes + point_offset;
    point.x = level_runtime_read_be16s(source + 0u);
    point.z = level_runtime_read_be16s(source + 2u);
    *out_point = point;
    return 1;
}

int level_runtime_get_narrative_message(const LevelRuntime *runtime, uint16_t message_index,
                                        LevelNarrativeMessage *out_message,
                                        char *error, size_t error_size)
{
    uint32_t message_offset;
    LevelNarrativeMessage message;

    if (!runtime || !runtime->level_bytes || !out_message ||
        message_index >= AB3D2_LEVEL_MESSAGE_COUNT) {
        level_runtime_set_error(error, error_size,
                                "requested narrative message is outside the source message table");
        return 0;
    }
    message_offset = (uint32_t)message_index * AB3D2_LEVEL_MESSAGE_LENGTH;
    if (!level_runtime_range_is_valid(message_offset, AB3D2_LEVEL_MESSAGE_LENGTH,
                                      runtime->level_size)) {
        level_runtime_set_error(error, error_size,
                                "requested narrative message is outside the level data");
        return 0;
    }
    message.bytes = runtime->level_bytes + message_offset;
    message.byte_count = AB3D2_LEVEL_MESSAGE_LENGTH;
    *out_message = message;
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
