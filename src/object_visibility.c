#include "object_visibility.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

void object_visibility_runtime_init(ObjectVisibilityRuntime *runtime)
{
    if (runtime) {
        /* objectmove.s declares Viewerx/Viewerz/Viewery/ViewerTop in zeroed BSS. */
        memset(runtime, 0, sizeof(*runtime));
    }
}

void object_visibility_runtime_set_viewer(ObjectVisibilityRuntime *runtime,
                                          int16_t viewer_x, int16_t viewer_z,
                                          int16_t viewer_y,
                                          uint8_t viewer_in_upper_zone)
{
    if (runtime) {
        runtime->viewer_x = viewer_x;
        runtime->viewer_z = viewer_z;
        runtime->viewer_y = viewer_y;
        runtime->viewer_in_upper_zone = viewer_in_upper_zone;
    }
}

void object_visibility_runtime_set_viewer_position(ObjectVisibilityRuntime *runtime,
                                                   int16_t viewer_x, int16_t viewer_z,
                                                   int16_t viewer_y)
{
    if (runtime) {
        runtime->viewer_x = viewer_x;
        runtime->viewer_z = viewer_z;
        runtime->viewer_y = viewer_y;
    }
}

static void object_visibility_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_visibility_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_visibility_read_be16s(const uint8_t *source)
{
    return (int16_t)object_visibility_read_be16(source);
}

/* 68000 MULS.W produces the signed 32-bit product of its two source words. */
static int32_t object_visibility_muls16(int16_t left, int16_t right)
{
    return (int32_t)((int64_t)left * right);
}

/* The original arithmetic is 32-bit register arithmetic and wraps on subtract. */
static int32_t object_visibility_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

/*
 * The source uses DIVS.W. Valid authored data must yield a signed-word
 * quotient; surface invalid data instead of silently changing that result.
 */
static int object_visibility_divs16(int32_t dividend, int16_t divisor,
                                    int16_t *out_quotient,
                                    char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        object_visibility_set_error(error, error_size,
                                    "CanItBeSeen DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        object_visibility_set_error(error, error_size,
                                    "CanItBeSeen DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

/*
 * Matches both clip loops and FindWayOut: (point_z - viewer_z) * dx -
 * (point_x - viewer_x) * dz, using source word inputs and longword products.
 */
static int32_t object_visibility_side_of_line(int16_t point_x, int16_t point_z,
                                               const ObjectVisibilityQuery *query,
                                               int16_t delta_x, int16_t delta_z)
{
    int16_t offset_x = (int16_t)((uint16_t)point_x - (uint16_t)query->viewer_x);
    int16_t offset_z = (int16_t)((uint16_t)point_z - (uint16_t)query->viewer_z);

    return object_visibility_sub32(object_visibility_muls16(offset_z, delta_x),
                                   object_visibility_muls16(offset_x, delta_z));
}

static int object_visibility_check_clips(const LevelRuntime *level, const AssetBlob *clips,
                                         uint16_t clip_id,
                                         const ObjectVisibilityQuery *query,
                                         int16_t delta_x, int16_t delta_z,
                                         uint8_t *out_can_see,
                                         char *error, size_t error_size)
{
    size_t cursor;
    uint32_t list_count;

    if (!clips || !clips->bytes || (clips->size & 1u) != 0u) {
        object_visibility_set_error(error, error_size,
                                    "CanItBeSeen requires an even-sized source clip stream");
        return 0;
    }
    cursor = (size_t)clip_id * sizeof(uint16_t);
    if (cursor > clips->size || sizeof(uint16_t) > clips->size - cursor) {
        object_visibility_set_error(error, error_size,
                                    "CanItBeSeen clip offset is outside the source clip stream");
        return 0;
    }
    list_count = 0u;
    for (;;) {
        int16_t point_index;
        LevelWorldPoint point;

        if (cursor > clips->size || sizeof(uint16_t) > clips->size - cursor) {
            object_visibility_set_error(error, error_size,
                                        "CanItBeSeen left clip list has no negative terminator");
            return 0;
        }
        point_index = object_visibility_read_be16s(clips->bytes + cursor);
        cursor += sizeof(uint16_t);
        if (point_index < 0) {
            break;
        }
        if (!level_runtime_get_world_point(level, (uint16_t)point_index, &point,
                                           error, error_size)) {
            return 0;
        }
        if (object_visibility_side_of_line(point.x, point.z, query, delta_x, delta_z) <= 0) {
            *out_can_see = 0u;
            return 1;
        }
        if (++list_count > level->world_point_count) {
            object_visibility_set_error(error, error_size,
                                        "CanItBeSeen left clip list exceeds source point count");
            return 0;
        }
    }
    list_count = 0u;
    for (;;) {
        int16_t point_index;
        LevelWorldPoint point;

        if (cursor > clips->size || sizeof(uint16_t) > clips->size - cursor) {
            object_visibility_set_error(error, error_size,
                                        "CanItBeSeen right clip list has no negative terminator");
            return 0;
        }
        point_index = object_visibility_read_be16s(clips->bytes + cursor);
        cursor += sizeof(uint16_t);
        if (point_index < 0) {
            break;
        }
        if (!level_runtime_get_world_point(level, (uint16_t)point_index, &point,
                                           error, error_size)) {
            return 0;
        }
        if (object_visibility_side_of_line(point.x, point.z, query, delta_x, delta_z) >= 0) {
            *out_can_see = 0u;
            return 1;
        }
        if (++list_count > level->world_point_count) {
            object_visibility_set_error(error, error_size,
                                        "CanItBeSeen right clip list exceeds source point count");
            return 0;
        }
    }
    return 1;
}

static int object_visibility_find_potential_zone(const LevelRuntime *level,
                                                  const ObjectVisibilityQuery *query,
                                                  int16_t *out_clip_id,
                                                  uint8_t *out_found,
                                                  char *error, size_t error_size)
{
    LevelZone target_zone;

    *out_found = 0u;
    if (!level_runtime_get_zone(level, query->target_zone_index, &target_zone,
                                error, error_size)) {
        return 0;
    }
    for (uint32_t entry_index = 0u; ; ++entry_index) {
        LevelPotentialVisibility entry;
        LevelDrawGraphStreams streams;

        if (!level_runtime_get_zone_potential_visibility(
                level, query->viewer_zone_index, entry_index, &entry, error, error_size)) {
            return 0;
        }
        if (entry.zone_index < 0) {
            return 1;
        }
        if (!level_runtime_get_zone_draw_graph_streams(level, (uint16_t)entry.zone_index,
                                                       &streams, error, error_size)) {
            return 0;
        }
        /* `CanItBeSeen` compares the first lower graphics-stream word to ZoneT_ID_w. */
        if (streams.lower_zone_id == (int16_t)target_zone.id) {
            *out_clip_id = entry.clip_id;
            *out_found = UINT8_MAX;
            return 1;
        }
        if (entry_index >= level->zone_count) {
            object_visibility_set_error(error, error_size,
                                        "CanItBeSeen PVST list has no negative terminator");
            return 0;
        }
    }
}

/* Direct translation of CanItBeSeen's GoThroughZones / FindWayOut loop. */
static int object_visibility_cross_joined_zones(const LevelRuntime *level,
                                                const ObjectVisibilityQuery *query,
                                                uint8_t *out_can_see,
                                                char *error, size_t error_size)
{
    uint16_t current_zone_index = query->viewer_zone_index;
    uint8_t current_in_upper_zone = query->viewer_in_upper_zone;
    int16_t delta_x = (int16_t)((uint16_t)query->target_x - (uint16_t)query->viewer_x);
    int16_t delta_z = (int16_t)((uint16_t)query->target_z - (uint16_t)query->viewer_z);
    int16_t delta_y = (int16_t)((uint16_t)query->target_y - (uint16_t)query->viewer_y);

    for (uint32_t transition_count = 0u; transition_count < level->zone_count;
         ++transition_count) {
        uint32_t edge_count;
        uint8_t found_exit = 0u;

        if (!level_runtime_get_zone_edge_count(level, current_zone_index, &edge_count,
                                               error, error_size)) {
            return 0;
        }
        for (uint32_t edge_list_index = 0u; edge_list_index < edge_count;
             ++edge_list_index) {
            uint32_t edge_index;
            LevelEdge edge;
            int16_t target_cross;
            int16_t viewer_cross;
            int16_t crossing_sum;
            int16_t height_delta;
            int16_t crossing_height_word;
            int32_t crossing_height;
            LevelZone current_zone;
            LevelZone joined_zone;

            if (!level_runtime_get_zone_edge_index(level, current_zone_index, edge_list_index,
                                                   &edge_index, error, error_size) ||
                !level_runtime_get_edge(level, edge_index, &edge, error, error_size)) {
                return 0;
            }
            if (object_visibility_side_of_line(edge.x, edge.z, query, delta_x, delta_z) <= 0 ||
                object_visibility_side_of_line(
                    (int16_t)((uint16_t)edge.x + (uint16_t)edge.x_length),
                    (int16_t)((uint16_t)edge.z + (uint16_t)edge.z_length),
                    query, delta_x, delta_z) >= 0) {
                continue;
            }
            if (edge.join_zone_id < 0) {
                *out_can_see = 0u;
                return 1;
            }
            if ((uint16_t)edge.join_zone_id >= level->zone_count) {
                object_visibility_set_error(error, error_size,
                                            "CanItBeSeen edge joins an invalid source zone");
                return 0;
            }
            if (!object_visibility_divs16(
                    object_visibility_sub32(
                        object_visibility_muls16(
                            (int16_t)((uint16_t)query->target_z - (uint16_t)edge.z),
                            edge.x_length),
                        object_visibility_muls16(
                            (int16_t)((uint16_t)query->target_x - (uint16_t)edge.x),
                            edge.z_length)),
                    edge.unknown_word, &target_cross, error, error_size) ||
                !object_visibility_divs16(
                    object_visibility_sub32(
                        object_visibility_muls16(
                            (int16_t)((uint16_t)query->viewer_x - (uint16_t)edge.x),
                            edge.z_length),
                        object_visibility_muls16(
                            (int16_t)((uint16_t)query->viewer_z - (uint16_t)edge.z),
                            edge.x_length)),
                    edge.unknown_word, &viewer_cross, error, error_size)) {
                return 0;
            }
            crossing_sum = (int16_t)((uint16_t)target_cross + (uint16_t)viewer_cross);
            height_delta = viewer_cross;
            if (crossing_sum != 0 &&
                !object_visibility_divs16(object_visibility_muls16(delta_y, viewer_cross),
                                           crossing_sum, &height_delta, error, error_size)) {
                return 0;
            }
            crossing_height_word =
                (int16_t)((uint16_t)query->viewer_y + (uint16_t)height_delta);
            crossing_height = (int32_t)crossing_height_word * 128;
            if (!level_runtime_get_zone(level, current_zone_index, &current_zone,
                                        error, error_size) ||
                !level_runtime_get_zone(level, (uint16_t)edge.join_zone_id, &joined_zone,
                                        error, error_size)) {
                return 0;
            }
            if (current_in_upper_zone != 0u) {
                if (crossing_height < current_zone.upper_roof ||
                    crossing_height > current_zone.upper_floor) {
                    *out_can_see = 0u;
                    return 1;
                }
            } else if (crossing_height < current_zone.roof ||
                       crossing_height > current_zone.floor) {
                *out_can_see = 0u;
                return 1;
            }
            current_zone_index = (uint16_t)edge.join_zone_id;
            current_in_upper_zone = 0u;
            if (crossing_height > joined_zone.floor) {
                *out_can_see = 0u;
                return 1;
            }
            if (crossing_height <= joined_zone.roof) {
                current_in_upper_zone = UINT8_MAX;
                if (crossing_height > joined_zone.upper_floor ||
                    crossing_height < joined_zone.upper_roof) {
                    *out_can_see = 0u;
                    return 1;
                }
            }
            if (current_zone_index == query->target_zone_index) {
                *out_can_see = current_in_upper_zone == query->target_in_upper_zone ?
                    UINT8_MAX : 0u;
                return 1;
            }
            found_exit = UINT8_MAX;
            break;
        }
        if (found_exit == 0u) {
            *out_can_see = 0u;
            return 1;
        }
    }
    object_visibility_set_error(error, error_size,
                                "CanItBeSeen joined-zone traversal exceeds source zone count");
    return 0;
}

int object_visibility_can_see(const LevelRuntime *level, const AssetBlob *clips,
                              const ObjectVisibilityQuery *query,
                              uint8_t *out_can_see,
                              char *error, size_t error_size)
{
    int16_t clip_id;
    int16_t delta_x;
    int16_t delta_z;
    uint8_t potential_zone_found;

    if (!level || !query || !out_can_see || query->viewer_zone_index >= level->zone_count ||
        query->target_zone_index >= level->zone_count) {
        object_visibility_set_error(error, error_size,
                                    "CanItBeSeen received invalid source zone state");
        return 0;
    }
    *out_can_see = 0u;
    if (query->viewer_zone_index == query->target_zone_index) {
        *out_can_see = query->viewer_in_upper_zone == query->target_in_upper_zone ?
            UINT8_MAX : 0u;
        return 1;
    }
    if (!object_visibility_find_potential_zone(level, query, &clip_id,
                                               &potential_zone_found, error, error_size)) {
        return 0;
    }
    if (potential_zone_found == 0u) {
        /* The source's negative PVST terminator takes the outlist branch. */
        return 1;
    }
    delta_x = (int16_t)((uint16_t)query->target_x - (uint16_t)query->viewer_x);
    delta_z = (int16_t)((uint16_t)query->target_z - (uint16_t)query->viewer_z);
    if (clip_id >= 0) {
        /* `isinlist` sets CanSee before entering either source clip loop. */
        *out_can_see = UINT8_MAX;
        if (!object_visibility_check_clips(level, clips, (uint16_t)clip_id, query,
                                           delta_x, delta_z, out_can_see,
                                           error, error_size)) {
            return 0;
        }
        if (*out_can_see == 0u) {
            /* A clip test can take source outlist before the vertical traversal. */
            return 1;
        }
    }
    return object_visibility_cross_joined_zones(level, query, out_can_see, error, error_size);
}
