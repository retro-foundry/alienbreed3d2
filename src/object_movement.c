#include "object_movement.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* objectmove.s:MoveObject source workspace values. */
    OBJECT_MOVEMENT_MAX_ZONE_TRANSITIONS = 50u,
    OBJECT_MOVEMENT_CONTACT_MARGIN = 4,
    OBJECT_MOVEMENT_EDGE_CONTACT_DISTANCE = 32,
    OBJECT_MOVEMENT_SOLID_HEIGHT = -65536 * 256
};

static void object_movement_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int32_t object_movement_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_movement_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int16_t object_movement_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_movement_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int32_t object_movement_muls16(int16_t left, int16_t right)
{
    return (int32_t)((int64_t)left * right);
}

static int object_movement_divs16(int32_t dividend, int16_t divisor,
                                  int16_t *out_quotient,
                                  char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        object_movement_set_error(error, error_size,
                                  "MoveObject DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        object_movement_set_error(error, error_size,
                                  "MoveObject DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

static int object_movement_or_edge_flags(LevelDynamicState *dynamic_level,
                                         uint32_t edge_index, uint16_t flags,
                                         char *error, size_t error_size)
{
    if (!level_dynamic_state_or_edge_flags(dynamic_level, edge_index, flags)) {
        object_movement_set_error(error, error_size,
                                  "MoveObject could not update source EdgeT flags");
        return 0;
    }
    return 1;
}

static void object_movement_shifted_edge(const LevelEdge *edge, int8_t away_from_wall,
                                         int16_t *out_shift_x, int16_t *out_shift_z,
                                         int16_t *out_delta_x, int16_t *out_delta_z)
{
    int16_t shift_x = 0;
    int16_t shift_z = 0;

    if (away_from_wall >= 0) {
        shift_x = edge->unknown_byte_12;
        shift_z = edge->unknown_byte_13;
        if (away_from_wall != 0) {
            uint8_t shift_count = (uint8_t)away_from_wall & 63u;

            if (shift_count >= 16u) {
                /* 68000 ASL.W shifts every source bit out at count 16 or above. */
                shift_x = 0;
                shift_z = 0;
            } else {
                shift_x = (int16_t)((uint16_t)shift_x << shift_count);
                shift_z = (int16_t)((uint16_t)shift_z << shift_count);
            }
        }
    }
    *out_shift_x = shift_x;
    *out_shift_z = shift_z;
    *out_delta_x = object_movement_sub16(object_movement_sub16(edge->x_length, shift_x),
                                         shift_z);
    *out_delta_z = object_movement_sub16(object_movement_add16(edge->z_length, shift_x),
                                         shift_z);
}

static int object_movement_point_is_on_source_edge(int16_t point_x, int16_t point_z,
                                                    const LevelEdge *edge,
                                                    int16_t shift_x, int16_t shift_z,
                                                    int16_t delta_x, int16_t delta_z)
{
    int16_t local_x = object_movement_sub16(
        object_movement_sub16(point_x, edge->x), shift_x);
    int16_t local_z = object_movement_sub16(
        object_movement_sub16(point_z, edge->z), shift_z);

    /*
     * objectmove.s:othercheck uses the Z-side sign to orient both edge
     * components, then selects an X or Z range check. Preserve that exact
     * sequence rather than replacing it with a geometric distance test.
     */
    if (local_z < 0) {
        delta_x = (int16_t)(0u - (uint16_t)delta_x);
        delta_z = (int16_t)(0u - (uint16_t)delta_z);
    }
    if (delta_z > delta_x) {
        if (local_z <= 0) {
            if (delta_z > OBJECT_MOVEMENT_CONTACT_MARGIN ||
                local_z < object_movement_sub16(delta_z, OBJECT_MOVEMENT_CONTACT_MARGIN)) {
                return 0;
            }
        } else if (delta_z < -OBJECT_MOVEMENT_CONTACT_MARGIN ||
                   local_z > object_movement_add16(delta_z,
                                                    OBJECT_MOVEMENT_CONTACT_MARGIN)) {
            return 0;
        }
        return 1;
    }
    if (local_x <= 0) {
        if (delta_x > OBJECT_MOVEMENT_CONTACT_MARGIN ||
            local_x < object_movement_sub16(delta_x, OBJECT_MOVEMENT_CONTACT_MARGIN)) {
            return 0;
        }
    } else if (delta_x < -OBJECT_MOVEMENT_CONTACT_MARGIN ||
               local_x > object_movement_add16(delta_x, OBJECT_MOVEMENT_CONTACT_MARGIN)) {
        return 0;
    }
    return 1;
}

/* objectmove.s:checkwalls through hitthewall, with Obj_ExtLen_w fixed at zero. */
static int object_movement_check_primary_edge(LevelDynamicState *dynamic_level,
                                              ObjectMovementTrace *trace,
                                              uint32_t edge_index,
                                              const LevelEdge *edge,
                                              int *out_stop,
                                              char *error, size_t error_size)
{
    const LevelRuntime *runtime = &dynamic_level->runtime;
    int32_t lower_floor = OBJECT_MOVEMENT_SOLID_HEIGHT;
    int32_t lower_roof = OBJECT_MOVEMENT_SOLID_HEIGHT;
    int32_t upper_floor = OBJECT_MOVEMENT_SOLID_HEIGHT;
    int32_t upper_roof = OBJECT_MOVEMENT_SOLID_HEIGHT;
    int16_t shift_x;
    int16_t shift_z;
    int16_t delta_x;
    int16_t delta_z;
    int16_t denominator;
    int16_t local_x;
    int16_t local_z;
    int32_t cross;
    int16_t distance;
    int16_t travel_distance;
    int32_t crossing_height;
    int32_t lower_extent;
    int16_t hit_x;
    int16_t hit_z;

    *out_stop = 0;
    if (edge->join_zone_id >= 0) {
        LevelZone joined_zone;

        if (!level_runtime_get_zone(runtime, (uint16_t)edge->join_zone_id, &joined_zone,
                                    error, error_size)) {
            return 0;
        }
        lower_floor = joined_zone.floor;
        lower_roof = joined_zone.roof;
        upper_floor = joined_zone.upper_floor;
        upper_roof = joined_zone.upper_roof;
    }
    object_movement_shifted_edge(edge, trace->away_from_wall, &shift_x, &shift_z,
                                 &delta_x, &delta_z);
    denominator = edge->unknown_word;
    local_x = object_movement_sub16(
        object_movement_sub16(trace->new_x, edge->x), shift_x);
    local_z = object_movement_sub16(
        object_movement_sub16(trace->new_z, edge->z), shift_z);
    cross = object_movement_sub32(object_movement_muls16(delta_z, local_x),
                                  object_movement_muls16(delta_x, local_z));
    if (cross > 0) {
        if (!object_movement_divs16(cross, denominator, &distance, error, error_size)) {
            return 0;
        }
        if (distance < OBJECT_MOVEMENT_EDGE_CONTACT_DISTANCE &&
            !object_movement_or_edge_flags(dynamic_level, edge_index, trace->wall_flags,
                                           error, error_size)) {
            return 0;
        }
        return 1;
    }
    if (!object_movement_divs16(cross, denominator, &distance, error, error_size)) {
        return 0;
    }
    local_x = object_movement_sub16(
        object_movement_sub16(trace->old_x, edge->x), shift_x);
    local_z = object_movement_sub16(
        object_movement_sub16(trace->old_z, edge->z), shift_z);
    cross = object_movement_sub32(object_movement_muls16(delta_z, local_x),
                                  object_movement_muls16(delta_x, local_z));
    if (!object_movement_divs16(cross, denominator, &travel_distance, error, error_size)) {
        return 0;
    }
    travel_distance = object_movement_sub16(travel_distance, distance);
    if (travel_distance <= 0) {
        travel_distance = 1;
    }
    crossing_height = trace->new_y;
    if (trace->new_y != trace->old_y) {
        int16_t interpolation;

        if (!object_movement_divs16(
                object_movement_sub32(trace->new_y, trace->old_y), travel_distance,
                &interpolation, error, error_size)) {
            return 0;
        }
        crossing_height = object_movement_add32(
            trace->new_y, object_movement_muls16(interpolation, distance));
    }
    lower_extent = object_movement_sub32(
        object_movement_add32(crossing_height, trace->thing_height), trace->step_up);
    /* objectmove.s:chkhttt evaluates lower and upper openings in this order. */
    if (lower_extent < lower_floor && crossing_height > lower_roof) {
        return 1;
    }
    if (lower_extent < lower_floor && crossing_height >= upper_roof &&
        lower_extent < upper_floor) {
        return 1;
    }
    trace->wall_hit_height = crossing_height;
    if (trace->wall_bounce != 0u || trace->exit_first != 0u) {
        int16_t movement_x = object_movement_sub16(trace->new_x, trace->old_x);
        int16_t movement_z = object_movement_sub16(trace->new_z, trace->old_z);
        int16_t offset_x;
        int16_t offset_z;
        int32_t start_cross;
        int32_t end_cross;

        if (!object_movement_divs16(object_movement_muls16(movement_x, distance),
                                    travel_distance, &offset_x, error, error_size) ||
            !object_movement_divs16(object_movement_muls16(movement_z, distance),
                                    travel_distance, &offset_z, error, error_size)) {
            return 0;
        }
        hit_x = object_movement_add16(trace->new_x, offset_x);
        hit_z = object_movement_add16(trace->new_z, offset_z);
        start_cross = object_movement_sub32(
            object_movement_muls16(object_movement_sub16(object_movement_add16(edge->x, shift_x),
                                                          trace->old_x), movement_z),
            object_movement_muls16(object_movement_sub16(object_movement_add16(edge->z, shift_z),
                                                          trace->old_z), movement_x));
        if (start_cross > 0) {
            return 1;
        }
        end_cross = object_movement_sub32(
            object_movement_muls16(
                object_movement_sub16(object_movement_add16(
                    object_movement_add16(edge->x, shift_x), delta_x), trace->old_x),
                movement_z),
            object_movement_muls16(
                object_movement_sub16(object_movement_add16(
                    object_movement_add16(edge->z, shift_z), delta_z), trace->old_z),
                movement_x));
        if (end_cross < 0) {
            return 1;
        }
    } else {
        int16_t offset_x;
        int16_t offset_z;

        if (!object_movement_divs16(object_movement_muls16(distance, delta_z), denominator,
                                    &offset_x, error, error_size) ||
            !object_movement_divs16(object_movement_muls16(distance, delta_x), denominator,
                                    &offset_z, error, error_size)) {
            return 0;
        }
        hit_x = object_movement_sub16(trace->new_x, offset_x);
        hit_z = object_movement_add16(trace->new_z, offset_z);
        if (!object_movement_point_is_on_source_edge(hit_x, hit_z, edge, shift_x, shift_z,
                                                     delta_x, delta_z)) {
            return 1;
        }
    }
    trace->new_x = hit_x;
    trace->new_z = hit_z;
    if (trace->wall_bounce != 0u) {
        /* objectmove.s:.calcbounce supplies these only to bounce callers. */
        trace->wall_x_size = delta_x;
        trace->wall_z_size = delta_z;
        trace->wall_length = denominator;
    }
    trace->hit_wall = UINT8_MAX;
    if (!object_movement_or_edge_flags(dynamic_level, edge_index, trace->wall_flags,
                                       error, error_size)) {
        return 0;
    }
    if (trace->exit_first != 0u) {
        *out_stop = 1;
    }
    return 1;
}

/* objectmove.s:CheckMoreFloorLines, reached after the no-extension wall pass. */
static int object_movement_cross_joined_zone(const LevelRuntime *runtime,
                                             ObjectMovementTrace *trace,
                                             uint32_t edge_index,
                                             int *out_changed_zone,
                                             char *error, size_t error_size)
{
    LevelEdge edge;
    LevelZone joined_zone;
    int16_t local_x;
    int16_t local_z;
    int32_t new_side;
    int16_t movement_x;
    int16_t movement_z;
    int32_t start_cross;
    int32_t end_cross;
    int16_t new_distance;
    int16_t old_distance;
    int16_t travel_distance;
    int32_t crossing_height;

    *out_changed_zone = 0;
    if (!level_runtime_get_edge(runtime, edge_index, &edge, error, error_size)) {
        return 0;
    }
    if (edge.join_zone_id < 0) {
        return 1;
    }
    if (!level_runtime_get_zone(runtime, (uint16_t)edge.join_zone_id, &joined_zone,
                                error, error_size)) {
        return 0;
    }
    local_x = object_movement_sub16(trace->new_x, edge.x);
    local_z = object_movement_sub16(trace->new_z, edge.z);
    new_side = object_movement_sub32(object_movement_muls16(local_x, edge.z_length),
                                     object_movement_muls16(local_z, edge.x_length));
    if (new_side >= 0) {
        return 1;
    }
    movement_x = object_movement_sub16(trace->new_x, trace->old_x);
    movement_z = object_movement_sub16(trace->new_z, trace->old_z);
    start_cross = object_movement_sub32(
        object_movement_muls16(object_movement_sub16(edge.x, trace->old_x), movement_z),
        object_movement_muls16(object_movement_sub16(edge.z, trace->old_z), movement_x));
    if (start_cross > 0) {
        return 1;
    }
    end_cross = object_movement_sub32(
        object_movement_muls16(
            object_movement_sub16(object_movement_add16(edge.x, edge.x_length), trace->old_x),
            movement_z),
        object_movement_muls16(
            object_movement_sub16(object_movement_add16(edge.z, edge.z_length), trace->old_z),
            movement_x));
    if (end_cross < 0 ||
        !object_movement_divs16(new_side, edge.unknown_word, &new_distance,
                                error, error_size)) {
        return end_cross < 0 ? 1 : 0;
    }
    local_x = object_movement_sub16(trace->old_x, edge.x);
    local_z = object_movement_sub16(trace->old_z, edge.z);
    if (!object_movement_divs16(
            object_movement_sub32(object_movement_muls16(local_x, edge.z_length),
                                  object_movement_muls16(local_z, edge.x_length)),
            edge.unknown_word, &old_distance, error, error_size)) {
        return 0;
    }
    travel_distance = object_movement_sub16(old_distance, new_distance);
    if (travel_distance <= 0) {
        travel_distance = 1;
    }
    if (trace->new_y != trace->old_y) {
        int16_t interpolation;

        if (!object_movement_divs16(
                object_movement_sub32(trace->new_y, trace->old_y), travel_distance,
                &interpolation, error, error_size)) {
            return 0;
        }
        crossing_height = object_movement_add32(
            trace->new_y, object_movement_muls16(interpolation, new_distance));
    } else {
        crossing_height = trace->new_y;
    }
    trace->stood_in_top = crossing_height < joined_zone.roof ? UINT8_MAX : 0u;
    trace->zone_index = (uint16_t)edge.join_zone_id;
    *out_changed_zone = 1;
    return 1;
}

int object_movement_trace_zero_extension(LevelDynamicState *dynamic_level,
                                         ObjectMovementTrace *trace,
                                         char *error, size_t error_size)
{
    const LevelRuntime *runtime;
    uint16_t backup_zone;
    uint8_t backup_top;

    if (!dynamic_level || !trace || !dynamic_level->level_bytes ||
        dynamic_level->runtime.level_bytes != dynamic_level->level_bytes ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        trace->zone_index >= dynamic_level->runtime.zone_count) {
        object_movement_set_error(error, error_size,
                                  "MoveObject received an invalid mutable source level");
        return 0;
    }
    runtime = &dynamic_level->runtime;
    backup_zone = trace->zone_index;
    backup_top = trace->stood_in_top;
    trace->hit_wall = 0u;
    trace->wall_hit_height = trace->new_y;
    if (trace->new_x == trace->old_x && trace->new_z == trace->old_z) {
        return 1;
    }
    for (uint32_t transition_count = 0u;
         transition_count < OBJECT_MOVEMENT_MAX_ZONE_TRANSITIONS; ++transition_count) {
        uint32_t edge_count;
        int changed_zone = 0;

        if (!level_runtime_get_zone_edge_count(runtime, trace->zone_index, &edge_count,
                                               error, error_size)) {
            return 0;
        }
        for (uint32_t edge_list_index = 0u; edge_list_index < edge_count; ++edge_list_index) {
            uint32_t edge_index;
            LevelEdge edge;
            int stop;

            if (!level_runtime_get_zone_edge_index(runtime, trace->zone_index, edge_list_index,
                                                   &edge_index, error, error_size) ||
                !level_runtime_get_edge(runtime, edge_index, &edge, error, error_size) ||
                !object_movement_check_primary_edge(dynamic_level, trace, edge_index, &edge,
                                                    &stop, error, error_size)) {
                return 0;
            }
            if (stop != 0) {
                return 1;
            }
        }
        for (uint32_t edge_list_index = 0u; edge_list_index < edge_count; ++edge_list_index) {
            uint32_t edge_index;

            if (!level_runtime_get_zone_edge_index(runtime, trace->zone_index, edge_list_index,
                                                   &edge_index, error, error_size) ||
                !object_movement_cross_joined_zone(runtime, trace, edge_index, &changed_zone,
                                                    error, error_size)) {
                return 0;
            }
            if (changed_zone != 0) {
                break;
            }
        }
        if (changed_zone == 0) {
            return 1;
        }
    }
    /* objectmove.s:ERRORINMOVEMENT restores this source workspace and marks a hit. */
    trace->zone_index = backup_zone;
    trace->stood_in_top = backup_top;
    trace->new_x = trace->old_x;
    trace->new_z = trace->old_z;
    trace->new_y = trace->old_y;
    trace->hit_wall = UINT8_MAX;
    return 1;
}
