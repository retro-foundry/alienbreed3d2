#include "object_explosion.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by newanims.s:Anim_ExplodeIntoBits. */
    OBJECT_EXPLOSION_SLOT_POINT_INDEX = 0u,
    OBJECT_EXPLOSION_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_EXPLOSION_SLOT_ZONE_ID = 12u,
    OBJECT_EXPLOSION_SLOT_TYPE_ID = 16u,
    OBJECT_EXPLOSION_SLOT_VELOCITY_X = 18u,
    OBJECT_EXPLOSION_SLOT_VELOCITY_Z = 22u,
    OBJECT_EXPLOSION_SLOT_POWER = 28u,
    OBJECT_EXPLOSION_SLOT_STATUS = 30u,
    OBJECT_EXPLOSION_SLOT_SIZE = 31u,
    OBJECT_EXPLOSION_SLOT_ENEMY_FLAGS = 36u,
    OBJECT_EXPLOSION_SLOT_VELOCITY_Y = 42u,
    OBJECT_EXPLOSION_SLOT_ACCUMULATED_Y = 44u,
    OBJECT_EXPLOSION_SLOT_LIFETIME = 58u,
    OBJECT_EXPLOSION_SLOT_FLAGS = 60u,
    OBJECT_EXPLOSION_SLOT_WORRY = 62u,
    OBJECT_EXPLOSION_SLOT_IN_UPPER_ZONE = 63u,
    /* The source clamps d2 to seven, then spawns once before its decrement. */
    OBJECT_EXPLOSION_MAX_REQUESTED_COUNT = 7u
};

static void object_explosion_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_explosion_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_explosion_read_be16s(const uint8_t *source)
{
    uint16_t value = object_explosion_read_be16(source);

    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static uint32_t object_explosion_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static void object_explosion_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_explosion_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int16_t object_explosion_asr16(int16_t value, unsigned int count)
{
    if (value >= 0) {
        return (int16_t)((uint16_t)value >> count);
    }
    return (int16_t)-(((int32_t)-value + ((INT32_C(1) << count) - 1)) >> count);
}

static int16_t object_explosion_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_explosion_shifted_high_word(int16_t source, uint16_t shift_count)
{
    uint32_t value = (uint32_t)(int32_t)source;

    value <<= shift_count;
    return (int16_t)(uint16_t)(value >> 16u);
}

void object_explosion_runtime_init(ObjectExplosionRuntime *runtime)
{
    if (runtime) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

int object_explosion_into_bits(ObjectExplosionRuntime *runtime,
                               ObjectRuntime *objects, uint32_t source_slot_index,
                               const GameMath *math, GameRandom *random,
                               int16_t new_x, int16_t new_z, uint8_t splat_type,
                               int16_t requested_count, int16_t radius,
                               uint32_t *out_spawned_count,
                               char *error, size_t error_size)
{
    uint8_t *source_slot;
    uint32_t pool_index = 0u;
    int16_t source_d1 = (int16_t)(OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT - 1u);
    int16_t source_d2 = requested_count;
    uint32_t spawned_count = 0u;

    if (out_spawned_count) {
        *out_spawned_count = 0u;
    }
    if (!runtime || !objects || !math || !random ||
        source_slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, source_slot_index, &source_slot)) {
        object_explosion_set_error(error, error_size,
                                   "Anim_ExplodeIntoBits received invalid source state");
        return 0;
    }
    runtime->radius = radius;
    if (source_d2 > (int16_t)OBJECT_EXPLOSION_MAX_REQUESTED_COUNT) {
        source_d2 = OBJECT_EXPLOSION_MAX_REQUESTED_COUNT;
    }

    for (;;) {
        uint8_t *fragment_slot;
        uint8_t *fragment_point;
        uint16_t fragment_point_index;
        uint16_t angle_address;
        int16_t sine;
        int16_t cosine;
        uint16_t shift_count;
        uint32_t impact_pair;
        int16_t impact_x;
        int16_t impact_z;
        int16_t vertical_position;
        int16_t vertical_speed;

        if (!object_runtime_get_alien_shot_slot_bytes(objects, pool_index, &fragment_slot)) {
            object_explosion_set_error(error, error_size,
                                       "Anim_ExplodeIntoBits alien-shot pool is outside source state");
            return 0;
        }
        if (object_explosion_read_be16s(fragment_slot + OBJECT_EXPLOSION_SLOT_ZONE_ID) >= 0) {
            ++pool_index;
            source_d1 = (int16_t)((uint16_t)source_d1 - 1u);
            if (source_d1 != -1) {
                continue;
            }
            break;
        }
        fragment_point_index = object_explosion_read_be16(fragment_slot +
                                                           OBJECT_EXPLOSION_SLOT_POINT_INDEX);
        if ((uint32_t)fragment_point_index + 2u >= objects->point_count ||
            !object_runtime_get_point_bytes(objects, fragment_point_index, &fragment_point)) {
            object_explosion_set_error(error, error_size,
                                       "Anim_ExplodeIntoBits fragment has an invalid source point");
            return 0;
        }
        angle_address = game_math_wrap_angle_address(game_random_next(random));
        if (!game_math_sine(math, angle_address, &sine, error, error_size) ||
            !game_math_cosine(math, angle_address, &cosine, error, error_size)) {
            return 0;
        }
        shift_count = (uint16_t)((game_random_next(random) & 3u) + 1u);
        impact_pair = object_explosion_read_be32(source_slot +
                                                  OBJECT_EXPLOSION_SLOT_VELOCITY_Y);
        impact_x = object_explosion_asr16((int16_t)(impact_pair >> 16u), 1u);
        impact_z = object_explosion_asr16((int16_t)(uint16_t)impact_pair, 1u);
        vertical_speed = (int16_t)(UINT16_C(0) -
            (uint16_t)((game_random_next(random) & 1023u) + 2u * 128u));
        vertical_position = object_explosion_read_be16s(
            source_slot + OBJECT_EXPLOSION_SLOT_VERTICAL_POSITION);

        /* The source writes only these Vec2L high words, retaining their raw tails. */
        object_explosion_write_be16(fragment_point, (uint16_t)new_x);
        object_explosion_write_be16(fragment_point + 4u, (uint16_t)new_z);
        /* Anim_ExplodeIntoBits' surprising `move.b #2,16(a2)`. */
        fragment_point[16u] = 2u;
        fragment_slot[OBJECT_EXPLOSION_SLOT_POWER] = 0u;
        object_explosion_write_be16(
            fragment_slot + OBJECT_EXPLOSION_SLOT_VELOCITY_Z,
            (uint16_t)object_explosion_add16(
                object_explosion_shifted_high_word(cosine, shift_count), impact_z));
        object_explosion_write_be16(
            fragment_slot + OBJECT_EXPLOSION_SLOT_VELOCITY_X,
            (uint16_t)object_explosion_add16(
                object_explosion_shifted_high_word(sine, shift_count), impact_x));
        object_explosion_write_be32(fragment_slot + OBJECT_EXPLOSION_SLOT_ENEMY_FLAGS, 0u);
        object_explosion_write_be16(
            fragment_slot + OBJECT_EXPLOSION_SLOT_ZONE_ID,
            object_explosion_read_be16(source_slot + OBJECT_EXPLOSION_SLOT_ZONE_ID));
        object_explosion_write_be16(fragment_slot + OBJECT_EXPLOSION_SLOT_VERTICAL_POSITION,
                                    (uint16_t)vertical_position);
        object_explosion_write_be32(fragment_slot + OBJECT_EXPLOSION_SLOT_ACCUMULATED_Y,
                                    (uint32_t)(int32_t)object_explosion_add16(vertical_position, 6) *
                                        128u);
        fragment_slot[OBJECT_EXPLOSION_SLOT_SIZE] = splat_type;
        object_explosion_write_be16(fragment_slot + OBJECT_EXPLOSION_SLOT_FLAGS, 0u);
        object_explosion_write_be16(fragment_slot + OBJECT_EXPLOSION_SLOT_LIFETIME, 0u);
        fragment_slot[OBJECT_EXPLOSION_SLOT_STATUS] = 0u;
        fragment_slot[OBJECT_EXPLOSION_SLOT_IN_UPPER_ZONE] =
            source_slot[OBJECT_EXPLOSION_SLOT_IN_UPPER_ZONE];
        fragment_slot[OBJECT_EXPLOSION_SLOT_WORRY] = UINT8_MAX;
        object_explosion_write_be16(fragment_slot + OBJECT_EXPLOSION_SLOT_VELOCITY_Y,
                                    (uint16_t)vertical_speed);
        ++spawned_count;

        ++pool_index;
        source_d2 = (int16_t)((uint16_t)source_d2 - 1u);
        if (source_d2 < 0) {
            break;
        }
        source_d1 = (int16_t)((uint16_t)source_d1 - 1u);
        if (source_d1 == -1) {
            break;
        }
    }
    if (out_spawned_count) {
        *out_spawned_count = spawned_count;
    }
    return 1;
}
