#include "object_blast.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "object_movement.h"

enum {
    /* defs.i:ObjT/EntT/ShotT fields used by newanims.s:ComputeBlast. */
    OBJECT_BLAST_POINT_INDEX = 0u,
    OBJECT_BLAST_VERTICAL_POSITION = 4u,
    OBJECT_BLAST_ZONE_ID = 12u,
    OBJECT_BLAST_TYPE_ID = 16u,
    OBJECT_BLAST_HIT_POINTS = 18u,
    OBJECT_BLAST_DAMAGE_TAKEN = 19u,
    OBJECT_BLAST_VELOCITY_X = 18u,
    OBJECT_BLAST_VELOCITY_Z = 22u,
    OBJECT_BLAST_VELOCITY_Y = 42u,
    OBJECT_BLAST_ACCUMULATED_Y = 44u,
    OBJECT_BLAST_IMPACT_X = 42u,
    OBJECT_BLAST_IMPACT_Z = 44u,
    OBJECT_BLAST_IMPACT_Y = 46u,
    OBJECT_BLAST_STATUS = 30u,
    OBJECT_BLAST_SIZE = 31u,
    OBJECT_BLAST_ANIMATION = 52u,
    OBJECT_BLAST_WORRY = 62u,
    OBJECT_BLAST_IN_UPPER_ZONE = 63u,
    OBJECT_BLAST_TYPE_OBJECT = 1u,
    OBJECT_BLAST_TYPE_PROJECTILE = 2u,
    OBJECT_BLAST_TYPE_AUXILIARY = 3u,
    OBJECT_BLAST_FLAME_RADIUS_PASSES = 3u,
    OBJECT_BLAST_FLAMES_PER_RADIUS = 2u,
    OBJECT_BLAST_MINIMUM_DIVISOR = 256,
    OBJECT_BLAST_MAXIMUM_SCALED_RANGE = 64,
    OBJECT_BLAST_PROJECTILE_VERTICAL_CLAMP = -8 * 256,
    OBJECT_BLAST_ENTITY_VERTICAL_CLAMP = -8
};

static void object_blast_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_blast_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_blast_read_be16s(const uint8_t *source)
{
    return (int16_t)object_blast_read_be16(source);
}

static uint32_t object_blast_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int32_t object_blast_read_be32s(const uint8_t *source)
{
    return (int32_t)object_blast_read_be32(source);
}

static void object_blast_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_blast_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int16_t object_blast_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_blast_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int16_t object_blast_neg16(int16_t value)
{
    return (int16_t)(UINT16_C(0) - (uint16_t)value);
}

static int32_t object_blast_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_blast_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t object_blast_muls16(int16_t left, int16_t right)
{
    return (int32_t)left * (int32_t)right;
}

/* 68020 MULS.L keeps the low longword in its single-register form. */
static int32_t object_blast_muls32(int32_t left, int32_t right)
{
    return (int32_t)(uint32_t)((int64_t)left * (int64_t)right);
}

static int32_t object_blast_asl32(int32_t value, unsigned int count)
{
    return (int32_t)((uint32_t)value << count);
}

static int16_t object_blast_asr16(int16_t value, unsigned int count)
{
    if (value >= 0) {
        return (int16_t)((uint16_t)value >> count);
    }
    return (int16_t)-(((int32_t)-value + ((INT32_C(1) << count) - 1)) >> count);
}

static int32_t object_blast_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -((-(int64_t)value + ((INT64_C(1) << count) - 1)) >> count);
}

static int object_blast_divs16(int32_t dividend, int16_t divisor,
                                int16_t *out_quotient,
                                char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        object_blast_set_error(error, error_size,
                               "ComputeBlast DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        object_blast_set_error(error, error_size,
                               "ComputeBlast DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

static int16_t object_blast_high_word(int32_t value)
{
    return (int16_t)((uint32_t)value >> 16u);
}

static int object_blast_refine_distance(int16_t *in_out_distance,
                                        int16_t multiplicand, int32_t squared_distance,
                                        char *error, size_t error_size)
{
    int16_t quotient;
    int16_t distance = *in_out_distance;
    int32_t difference = object_blast_sub32(
        object_blast_muls16(distance, multiplicand), squared_distance);

    if (!object_blast_divs16(object_blast_asr32(difference, 1u), distance,
                             &quotient, error, error_size)) {
        return 0;
    }
    distance = object_blast_sub16(distance, quotient);
    if (distance <= 0) {
        distance = 1;
    }
    *in_out_distance = distance;
    return 1;
}

/* newanims.s:ComputeBlast's three source refinement passes, including .stillnot0. */
static int object_blast_distance(int16_t x_difference, int16_t z_difference,
                                 int16_t *out_distance,
                                 char *error, size_t error_size)
{
    int32_t squared_distance = object_blast_add32(
        object_blast_muls16(x_difference, x_difference),
        object_blast_muls16(z_difference, z_difference));
    int16_t distance = 1;

    if (squared_distance != 0) {
        unsigned int highest_bit = 31u;

        while (((uint32_t)squared_distance & (UINT32_C(1) << highest_bit)) == 0u) {
            --highest_bit;
        }
        distance = (int16_t)(UINT16_C(1) << (highest_bit >> 1u));
        if (!object_blast_refine_distance(&distance, distance, squared_distance,
                                          error, error_size) ||
            /* Source .stillnot0 multiplies by retained d1 (Z delta), not d4. */
            !object_blast_refine_distance(&distance, z_difference, squared_distance,
                                          error, error_size) ||
            !object_blast_refine_distance(&distance, distance, squared_distance,
                                          error, error_size)) {
            return 0;
        }
    }
    *out_distance = distance;
    return 1;
}

static int16_t object_blast_random_byte_offset(GameRandom *random, int16_t radius)
{
    int16_t source_random_byte = (int16_t)(int8_t)(uint8_t)game_random_next(random);
    int16_t offset = object_blast_asr16(
        (int16_t)object_blast_muls16(source_random_byte, radius), 1u);

    return offset == 0 ? 2 : offset;
}

static int object_blast_spawn_flames(ObjectBlastRuntime *runtime, ObjectRuntime *objects,
                                     uint8_t *source_slot, LevelDynamicState *dynamic_level,
                                     GameRandom *random,
                                     ObjectMotionRuntime *motion_runtime,
                                     char *error, size_t error_size)
{
    uint8_t *source_point;
    uint16_t source_point_index;
    int16_t source_zone_index;
    int16_t middle_x;
    int16_t middle_z;
    int32_t old_y;
    uint32_t next_pool_index = 0u;
    int16_t source_remaining = (int16_t)(OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT - 1u);
    int16_t radius = 2;

    source_point_index = object_blast_read_be16(source_slot + OBJECT_BLAST_POINT_INDEX);
    source_zone_index = object_blast_read_be16s(source_slot + OBJECT_BLAST_ZONE_ID);
    if (source_zone_index < 0 || (uint16_t)source_zone_index >= dynamic_level->runtime.zone_count ||
        !object_runtime_get_point_bytes(objects, source_point_index, &source_point)) {
        object_blast_set_error(error, error_size,
                               "ComputeBlast flame source has invalid point or zone state");
        return 0;
    }
    middle_x = object_blast_high_word(object_blast_read_be32s(source_point));
    middle_z = object_blast_high_word(object_blast_read_be32s(source_point + 4u));
    old_y = (int32_t)object_blast_read_be16s(
        source_slot + OBJECT_BLAST_VERTICAL_POSITION) * 128;

    for (uint32_t radius_pass = 0u; radius_pass < OBJECT_BLAST_FLAME_RADIUS_PASSES;
         ++radius_pass, radius = object_blast_add16(radius, 2)) {
        for (uint32_t flame_attempt = 0u;
             flame_attempt < OBJECT_BLAST_FLAMES_PER_RADIUS; ++flame_attempt) {
            uint8_t *flame_slot;
            uint8_t *flame_point;
            int16_t source_search = source_remaining;
            int16_t new_x;
            int16_t new_z;
            int32_t new_y;
            ObjectMovementTrace trace = {0};
            LevelZone zone;
            uint16_t flame_point_index;

            for (;;) {
                if (!object_runtime_get_player_shot_slot_bytes(objects, next_pool_index,
                                                               &flame_slot)) {
                    object_blast_set_error(error, error_size,
                                           "ComputeBlast player-shot pool is outside source state");
                    return 0;
                }
                if (object_blast_read_be16s(flame_slot + OBJECT_BLAST_ZONE_ID) < 0) {
                    break;
                }
                ++next_pool_index;
                source_search = object_blast_sub16(source_search, 1);
                if (source_search == -1) {
                    return 1;
                }
            }
            source_remaining = source_search;
            ++runtime->completed_flame_count;
            new_x = object_blast_add16(middle_x,
                                       object_blast_random_byte_offset(random, radius));
            new_z = object_blast_add16(middle_z,
                                       object_blast_random_byte_offset(random, radius));
            new_y = object_blast_add32(
                old_y,
                object_blast_asr32(
                    object_blast_muls16((int16_t)game_random_next(random), radius), 3u));
            trace.zone_index = (uint16_t)source_zone_index;
            trace.old_x = middle_x;
            trace.old_z = middle_z;
            trace.new_x = new_x;
            trace.new_z = new_z;
            trace.old_y = old_y;
            trace.new_y = new_y;
            /* ItsABullet leaves these source MoveObject globals for ComputeBlast. */
            trace.thing_height = 10 * 128;
            trace.step_down = 0x1000000;
            trace.extension_length = 80;
            trace.wall_flags = 0x0400u;
            trace.away_from_wall = 1;
            trace.stood_in_top = source_slot[OBJECT_BLAST_IN_UPPER_ZONE];
            trace.wall_bounce = UINT8_MAX;
            trace.exit_first = 0u;
            object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);
            if (!object_movement_trace(dynamic_level, &trace, error, error_size) ||
                !level_runtime_get_zone(&dynamic_level->runtime, trace.zone_index, &zone,
                                        error, error_size)) {
                return 0;
            }
            new_y = trace.new_y;
            /* ComputeBlast's final flame trace leaves objectmove.s:newx/newz live. */
            object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);
            /* ComputeBlast clamps against the exploding slot's layer, not StoodInTop. */
            if (source_slot[OBJECT_BLAST_IN_UPPER_ZONE] != 0u) {
                if (zone.upper_floor > new_y) {
                    new_y = zone.upper_floor;
                }
                if (zone.upper_roof < new_y) {
                    new_y = zone.upper_roof;
                }
            } else {
                if (zone.floor > new_y) {
                    new_y = zone.floor;
                }
                if (zone.roof < new_y) {
                    new_y = zone.roof;
                }
            }
            flame_point_index = object_blast_read_be16(flame_slot + OBJECT_BLAST_POINT_INDEX);
            if (!object_runtime_get_point_bytes(objects, flame_point_index, &flame_point)) {
                object_blast_set_error(error, error_size,
                                       "ComputeBlast flame has an invalid source point");
                return 0;
            }
            flame_slot[OBJECT_BLAST_TYPE_ID] = OBJECT_BLAST_TYPE_PROJECTILE;
            object_blast_write_be16(flame_slot + OBJECT_BLAST_ZONE_ID, zone.id);
            object_blast_write_be32(flame_slot + OBJECT_BLAST_ACCUMULATED_Y,
                                    (uint32_t)new_y);
            object_blast_write_be16(
                flame_slot + OBJECT_BLAST_VERTICAL_POSITION,
                (uint16_t)object_blast_asr32(new_y, 7u));
            flame_slot[OBJECT_BLAST_ANIMATION] = 0u;
            flame_slot[OBJECT_BLAST_STATUS] = UINT8_MAX;
            flame_slot[OBJECT_BLAST_IN_UPPER_ZONE] = trace.stood_in_top;
            flame_slot[OBJECT_BLAST_SIZE] = runtime->flame_bullet_index;
            flame_slot[OBJECT_BLAST_WORRY] = UINT8_MAX;
            /* Source writes only the Vec2L high words, retaining the raw low tails. */
            object_blast_write_be16(flame_point, (uint16_t)new_x);
            object_blast_write_be16(flame_point + 4u, (uint16_t)new_z);
            ++next_pool_index;
            source_remaining = object_blast_sub16(source_remaining, 1);
            if (source_remaining < 0) {
                return 1;
            }
        }
    }
    return 1;
}

void object_blast_runtime_init(ObjectBlastRuntime *runtime)
{
    if (runtime) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

void object_blast_runtime_note_bullet(ObjectBlastRuntime *runtime, uint8_t bullet_index)
{
    if (runtime) {
        runtime->flame_bullet_index = bullet_index;
    }
}

int object_blast_compute(ObjectBlastRuntime *runtime, ObjectRuntime *objects,
                         uint32_t explosive_slot_index,
                         LevelDynamicState *dynamic_level, const AssetBlob *clips,
                         const GameLink *game_link, GameRandom *random,
                         ObjectMotionRuntime *motion_runtime,
                         const ObjectVisibilityRuntime *visibility,
                         int16_t explosive_force,
                         char *error, size_t error_size)
{
    uint8_t *source_slot;
    int16_t source_zone_index;

    if (!runtime || !objects || !dynamic_level || !clips || !game_link || !random ||
        !visibility || !dynamic_level->level_bytes ||
        dynamic_level->runtime.level_bytes != dynamic_level->level_bytes ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        explosive_slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, explosive_slot_index, &source_slot)) {
        object_blast_set_error(error, error_size, "ComputeBlast received invalid source state");
        return 0;
    }
    source_zone_index = object_blast_read_be16s(source_slot + OBJECT_BLAST_ZONE_ID);
    if (source_zone_index < 0 || (uint16_t)source_zone_index >= dynamic_level->runtime.zone_count) {
        object_blast_set_error(error, error_size,
                               "ComputeBlast exploding slot has an invalid source zone");
        return 0;
    }
    runtime->completed_flame_count = 0u;

    for (uint32_t candidate_index = 0u; candidate_index < objects->active_slot_count;
         ++candidate_index) {
        uint8_t *candidate_slot;
        uint8_t *candidate_point;
        uint8_t candidate_type;
        int16_t candidate_zone_index;
        int16_t target_x;
        int16_t target_z;
        int16_t x_difference;
        int16_t z_difference;
        int16_t distance;
        int16_t divisor;
        int16_t scaled_range;
        int16_t damage;
        ObjectVisibilityQuery query;
        uint8_t can_see;

        if (!object_runtime_get_slot_bytes(objects, candidate_index, &candidate_slot)) {
            object_blast_set_error(error, error_size,
                                   "ComputeBlast candidate is outside the source list");
            return 0;
        }
        if (object_blast_read_be16s(candidate_slot + OBJECT_BLAST_POINT_INDEX) < 0) {
            break;
        }
        candidate_zone_index = object_blast_read_be16s(candidate_slot + OBJECT_BLAST_ZONE_ID);
        if (candidate_zone_index < 0) {
            continue;
        }
        candidate_type = candidate_slot[OBJECT_BLAST_TYPE_ID];
        if (candidate_type == OBJECT_BLAST_TYPE_OBJECT ||
            candidate_type == OBJECT_BLAST_TYPE_AUXILIARY) {
            continue;
        }
        if (candidate_type == OBJECT_BLAST_TYPE_PROJECTILE) {
            GameBulletDefinition candidate_bullet;

            if (!game_link_get_bullet_definition(game_link,
                                                  candidate_slot[OBJECT_BLAST_SIZE],
                                                  &candidate_bullet, error, error_size)) {
                return 0;
            }
            if (candidate_bullet.gravity == 0u) {
                continue;
            }
        } else if (candidate_slot[OBJECT_BLAST_HIT_POINTS] == 0u) {
            continue;
        }
        if ((uint16_t)candidate_zone_index >= dynamic_level->runtime.zone_count ||
            !object_runtime_get_point_bytes(
                objects, object_blast_read_be16(candidate_slot + OBJECT_BLAST_POINT_INDEX),
                &candidate_point)) {
            object_blast_set_error(error, error_size,
                                   "ComputeBlast target has an invalid source point or zone");
            return 0;
        }
        query.viewer_zone_index = (uint16_t)source_zone_index;
        query.viewer_x = visibility->viewer_x;
        query.viewer_z = visibility->viewer_z;
        query.viewer_y = visibility->viewer_y;
        query.viewer_in_upper_zone = visibility->viewer_in_upper_zone;
        query.target_zone_index = (uint16_t)candidate_zone_index;
        target_x = object_blast_high_word(object_blast_read_be32s(candidate_point));
        target_z = object_blast_high_word(object_blast_read_be32s(candidate_point + 4u));
        query.target_x = target_x;
        query.target_z = target_z;
        query.target_y = object_blast_read_be16s(candidate_slot + OBJECT_BLAST_VERTICAL_POSITION);
        query.target_in_upper_zone = candidate_slot[OBJECT_BLAST_IN_UPPER_ZONE];
        if (!object_visibility_can_see(&dynamic_level->runtime, clips, &query, &can_see,
                                       error, error_size)) {
            return 0;
        }
        if (can_see == 0u) {
            continue;
        }
        x_difference = object_blast_sub16(target_x, visibility->viewer_x);
        z_difference = object_blast_sub16(target_z, visibility->viewer_z);
        if (!object_blast_distance(x_difference, z_difference, &distance,
                                   error, error_size)) {
            return 0;
        }
        divisor = distance >= OBJECT_BLAST_MINIMUM_DIVISOR ?
            distance : OBJECT_BLAST_MINIMUM_DIVISOR;
        scaled_range = object_blast_sub16(object_blast_asr16(distance, 3u), 4);
        if (scaled_range < 0) {
            scaled_range = 0;
        }
        if (scaled_range > OBJECT_BLAST_MAXIMUM_SCALED_RANGE) {
            continue;
        }
        damage = (int16_t)object_blast_asr32(
            object_blast_muls16(explosive_force,
                                object_blast_add16(object_blast_neg16(scaled_range), 64)),
            5u);
        if (damage >= explosive_force) {
            damage = explosive_force;
        }
        candidate_slot[OBJECT_BLAST_DAMAGE_TAKEN] =
            (uint8_t)(candidate_slot[OBJECT_BLAST_DAMAGE_TAKEN] + (uint8_t)damage);
        {
            int16_t impact_x;
            int16_t impact_z;

            if (!object_blast_divs16(
                    object_blast_muls32((int32_t)explosive_force, x_difference), divisor,
                    &impact_x, error, error_size) ||
                !object_blast_divs16(
                    object_blast_muls32((int32_t)explosive_force, z_difference), divisor,
                    &impact_z, error, error_size)) {
                return 0;
            }
            if (candidate_type == OBJECT_BLAST_TYPE_PROJECTILE) {
                int16_t impact_y;

                object_blast_write_be16(
                    candidate_slot + OBJECT_BLAST_VELOCITY_X,
                    (uint16_t)object_blast_add16(
                        object_blast_read_be16s(candidate_slot + OBJECT_BLAST_VELOCITY_X),
                        impact_x));
                object_blast_write_be16(
                    candidate_slot + OBJECT_BLAST_VELOCITY_Z,
                    (uint16_t)object_blast_add16(
                        object_blast_read_be16s(candidate_slot + OBJECT_BLAST_VELOCITY_Z),
                        impact_z));
                if (!object_blast_divs16(
                        object_blast_asl32((int32_t)explosive_force, 12u), divisor,
                        &impact_y, error, error_size)) {
                    return 0;
                }
                impact_y = object_blast_neg16(impact_y);
                if (impact_y < OBJECT_BLAST_PROJECTILE_VERTICAL_CLAMP) {
                    impact_y = OBJECT_BLAST_PROJECTILE_VERTICAL_CLAMP;
                }
                object_blast_write_be16(
                    candidate_slot + OBJECT_BLAST_VELOCITY_Y,
                    (uint16_t)object_blast_add16(
                        object_blast_read_be16s(candidate_slot + OBJECT_BLAST_VELOCITY_Y),
                        impact_y));
            } else {
                int16_t impact_y;

                object_blast_write_be16(candidate_slot + OBJECT_BLAST_IMPACT_X,
                                        (uint16_t)impact_x);
                object_blast_write_be16(candidate_slot + OBJECT_BLAST_IMPACT_Z,
                                        (uint16_t)impact_z);
                if (!object_blast_divs16(
                        object_blast_asl32((int32_t)explosive_force, 4u), divisor,
                        &impact_y, error, error_size)) {
                    return 0;
                }
                impact_y = object_blast_neg16(impact_y);
                if (impact_y < OBJECT_BLAST_ENTITY_VERTICAL_CLAMP) {
                    impact_y = OBJECT_BLAST_ENTITY_VERTICAL_CLAMP;
                }
                object_blast_write_be16(candidate_slot + OBJECT_BLAST_IMPACT_Y,
                                        (uint16_t)impact_y);
            }
        }
    }
    return object_blast_spawn_flames(runtime, objects, source_slot, dynamic_level,
                                     random, motion_runtime, error, error_size);
}
