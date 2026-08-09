#include "object_collision.h"

#include <stdio.h>

enum {
    /* defs.i: ObjT/EntT fields consumed by objectmove.s:Obj_DoCollision. */
    OBJECT_COLLISION_SLOT_POINT_INDEX = 0u,
    OBJECT_COLLISION_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_COLLISION_SLOT_ZONE_ID = 12u,
    OBJECT_COLLISION_SLOT_TYPE_ID = 16u,
    OBJECT_COLLISION_SLOT_HIT_POINTS = 18u,
    OBJECT_COLLISION_SLOT_ENTITY_TYPE = 54u,
    OBJECT_COLLISION_SLOT_IN_UPPER_ZONE = 63u,
    OBJECT_COLLISION_TYPE_ALIEN = 0u,
    OBJECT_COLLISION_TYPE_OBJECT = 1u,
    OBJECT_COLLISION_HORIZONTAL_RADIUS = 80
};

static void object_collision_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_collision_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_collision_read_be16s(const uint8_t *source)
{
    return (int16_t)object_collision_read_be16(source);
}

static int16_t object_collision_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_collision_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int16_t object_collision_neg16(int16_t value)
{
    return (int16_t)(UINT16_C(0) - (uint16_t)value);
}

static int32_t object_collision_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_collision_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7u;
    }
    return (int32_t)-(((int64_t)-value + 127) >> 7u);
}

static int16_t object_collision_absolute16(int16_t value)
{
    /* objectmove.s uses BGE before NEG.W in both horizontal axes. */
    return value >= 0 ? value : object_collision_neg16(value);
}

static int object_collision_candidate_is_solid_object(const uint8_t *slot,
                                                       const GameLink *game_link,
                                                       int *out_is_solid,
                                                       char *error, size_t error_size)
{
    GameObjectDefinition definition;
    int16_t behaviour;

    *out_is_solid = 0;
    if (!game_link_get_object_definition(game_link, slot[OBJECT_COLLISION_SLOT_ENTITY_TYPE],
                                         &definition, error, error_size)) {
        return 0;
    }
    behaviour = (int16_t)definition.behaviour;
    if (behaviour < 2) {
        return 1;
    }
    if (behaviour > 2) {
        *out_is_solid = 1;
        return 1;
    }
    /* The source's second HP gate is TST.B/BLE, not its preceding BEQ gate. */
    *out_is_solid = (int8_t)slot[OBJECT_COLLISION_SLOT_HIT_POINTS] > 0;
    return 1;
}

int object_collision_check(const ObjectRuntime *objects, const GameLink *game_link,
                           const int16_t *source_a2_words, size_t source_a2_word_count,
                           const ObjectCollisionTrace *trace, uint8_t *out_hit_wall,
                           char *error, size_t error_size)
{
    uint8_t *collider_slot;
    int16_t collider_zone;
    int16_t movement_bottom;
    int16_t movement_top;

    if (!objects || !objects->slot_bytes || !objects->point_bytes || !game_link ||
        !source_a2_words || !trace || !out_hit_wall ||
        objects->active_slot_count >= objects->slot_count ||
        trace->collision_id >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, trace->collision_id,
                                       &collider_slot)) {
        object_collision_set_error(error, error_size,
                                   "Obj_DoCollision received invalid source object state");
        return 0;
    }
    collider_zone = object_collision_read_be16s(collider_slot + OBJECT_COLLISION_SLOT_ZONE_ID);
    movement_bottom = (int16_t)object_collision_asr32_7(trace->new_y);
    movement_top = (int16_t)object_collision_asr32_7(
        object_collision_add32(trace->new_y, trace->thing_height));
    *out_hit_wall = 0u;

    /* PREV_OBJ then NEXT_OBJ starts the source list at record zero. */
    for (uint32_t slot_index = 0u; slot_index < objects->slot_count; ++slot_index) {
        uint8_t *candidate_slot;
        int16_t candidate_point_index;
        uint8_t candidate_type;
        int candidate_is_solid = 0;
        size_t extent_word_index;
        int16_t candidate_bottom;
        int16_t candidate_top;
        uint8_t *candidate_point;
        int16_t horizontal_x;
        int16_t horizontal_z;

        if (!object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index,
                                           &candidate_slot)) {
            object_collision_set_error(error, error_size,
                                       "Obj_DoCollision source ObjT list is unavailable");
            return 0;
        }
        candidate_point_index = object_collision_read_be16s(
            candidate_slot + OBJECT_COLLISION_SLOT_POINT_INDEX);
        if (candidate_point_index < 0) {
            return 1;
        }
        if ((uint16_t)candidate_point_index == trace->collision_id ||
            object_collision_read_be16s(candidate_slot + OBJECT_COLLISION_SLOT_ZONE_ID) < 0 ||
            object_collision_read_be16s(candidate_slot + OBJECT_COLLISION_SLOT_ZONE_ID) !=
                collider_zone ||
            candidate_slot[OBJECT_COLLISION_SLOT_HIT_POINTS] == 0u ||
            (candidate_slot[OBJECT_COLLISION_SLOT_IN_UPPER_ZONE] ^ trace->stood_in_top) != 0u) {
            continue;
        }

        candidate_type = candidate_slot[OBJECT_COLLISION_SLOT_TYPE_ID];
        if (candidate_type > OBJECT_COLLISION_TYPE_OBJECT) {
            continue;
        }
        if (candidate_type == OBJECT_COLLISION_TYPE_OBJECT &&
            !object_collision_candidate_is_solid_object(candidate_slot, game_link,
                                                        &candidate_is_solid,
                                                        error, error_size)) {
            return 0;
        }
        if (candidate_type == OBJECT_COLLISION_TYPE_OBJECT && candidate_is_solid == 0) {
            continue;
        }

        extent_word_index = (size_t)candidate_type * 4u;
        if (extent_word_index + 2u >= source_a2_word_count) {
            object_collision_set_error(error, error_size,
                                       "Obj_DoCollision caller a2 table is shorter than its type index");
            return 0;
        }
        candidate_bottom = object_collision_sub16(
            object_collision_read_be16s(candidate_slot + OBJECT_COLLISION_SLOT_VERTICAL_POSITION),
            source_a2_words[extent_word_index + 1u]);
        if (movement_top < candidate_bottom) {
            continue;
        }
        candidate_top = object_collision_add16(candidate_bottom,
                                                source_a2_words[extent_word_index + 2u]);
        if (movement_bottom > candidate_top) {
            continue;
        }
        if ((uint32_t)candidate_point_index >= objects->point_count ||
            !object_runtime_get_point_bytes((ObjectRuntime *)objects,
                                            (uint32_t)candidate_point_index,
                                            &candidate_point)) {
            object_collision_set_error(error, error_size,
                                       "Obj_DoCollision candidate point is outside Lvl_ObjectPointsPtr_l");
            return 0;
        }
        horizontal_x = object_collision_absolute16(object_collision_sub16(
            object_collision_read_be16s(candidate_point), trace->new_x));
        horizontal_z = object_collision_absolute16(object_collision_sub16(
            object_collision_read_be16s(candidate_point + 4u), trace->new_z));
        if (horizontal_z > horizontal_x) {
            horizontal_z = object_collision_sub16(horizontal_z,
                                                  OBJECT_COLLISION_HORIZONTAL_RADIUS);
            if (horizontal_z > OBJECT_COLLISION_HORIZONTAL_RADIUS) {
                continue;
            }
            *out_hit_wall = UINT8_MAX;
            return 1;
        }
        horizontal_x = object_collision_sub16(horizontal_x, OBJECT_COLLISION_HORIZONTAL_RADIUS);
        if (horizontal_x > OBJECT_COLLISION_HORIZONTAL_RADIUS) {
            continue;
        }
        {
            int16_t new_x_delta = object_collision_sub16(
                object_collision_read_be16s(candidate_point), trace->new_x);
            int16_t new_z_delta = object_collision_sub16(
                object_collision_read_be16s(candidate_point + 4u), trace->new_z);
            int16_t old_x_delta = object_collision_sub16(
                object_collision_read_be16s(candidate_point), trace->old_x);
            int16_t old_z_delta = object_collision_sub16(
                object_collision_read_be16s(candidate_point + 4u), trace->old_z);
            int32_t new_distance_squared = object_collision_add32(
                (int32_t)new_x_delta * new_x_delta, (int32_t)new_z_delta * new_z_delta);
            int32_t old_distance_squared = object_collision_add32(
                (int32_t)old_x_delta * old_x_delta, (int32_t)old_z_delta * old_z_delta);

            if (new_distance_squared > old_distance_squared) {
                continue;
            }
        }
        /* The X-dominant hit falls through to keep scanning in the source. */
        *out_hit_wall = UINT8_MAX;
    }
    object_collision_set_error(error, error_size,
                               "Obj_DoCollision source ObjT list has no negative terminator");
    return 0;
}
