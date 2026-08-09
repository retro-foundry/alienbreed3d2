#include "object_observation.h"

#include <stdio.h>
#include <string.h>

enum {
    /* defs.i:ObjT fields consumed by modules/transform.s:CalcPLR1InLine. */
    OBJECT_OBSERVATION_SLOT_ZONE_ID = 12u,
    OBJECT_OBSERVATION_SLOT_TYPE_ID = 16u,
    OBJECT_OBSERVATION_TYPE_AUX = 3u,
    /* CalcPLR1InLine accepts a horizontal half-width of 80 source units. */
    OBJECT_OBSERVATION_IN_LINE_HALF_WIDTH = 80u
};

static void object_observation_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_observation_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

/* 68000 ADD.L, SUB.L, ASL.L, and NEG.L retain 32-bit wrapping. */
static int32_t object_observation_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_observation_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t object_observation_asl32_1(int32_t value)
{
    return (int32_t)((uint32_t)value << 1);
}

static int32_t object_observation_asl32_2(int32_t value)
{
    return (int32_t)((uint32_t)value << 2);
}

static int32_t object_observation_neg32(int32_t value)
{
    return (int32_t)(0u - (uint32_t)value);
}

/* The word visible after SWAP is the original longword's high word. */
static int16_t object_observation_swapped_low_word(int32_t value)
{
    return (int16_t)(uint16_t)((uint32_t)value >> 16);
}

static int16_t object_observation_asr16_1(int16_t value)
{
    if (value >= 0) {
        return (int16_t)(value >> 1);
    }
    return (int16_t)-(((-(int32_t)value) + 1) >> 1);
}

void object_observation_init(ObjectObservation *observation)
{
    if (observation) {
        memset(observation, 0, sizeof(*observation));
    }
}

int object_observation_update_single_player(ObjectObservation *observation,
                                             const ObjectRuntime *objects,
                                             const PlayerRuntime *player,
                                             const GameMath *math,
                                             char *error, size_t error_size)
{
    uint32_t slot_index = 0u;
    uint32_t point_index = 0u;
    uint32_t output_index = 0u;
    int16_t sine;
    int16_t cosine;

    if (!observation || !objects || !objects->slot_bytes || !objects->point_bytes || !player ||
        !math || objects->point_count > OBJECT_OBSERVATION_DISTANCE_COUNT ||
        objects->point_count > OBJECT_OBSERVATION_IN_LINE_COUNT ||
        objects->slot_count < objects->point_count) {
        object_observation_set_error(error, error_size,
                                     "CalcPLR1InLine received invalid source object state");
        return 0;
    }
    if (!game_math_sine(math, player->yaw, &sine, error, error_size) ||
        !game_math_cosine(math, player->yaw, &cosine, error, error_size)) {
        return 0;
    }

    /*
     * CalcPLR1InLine uses Lvl_NumObjectPoints as its DBRA final index. AUX
     * slots consume only ObjT state, precisely matching its .itaux branch.
     */
    while (point_index < objects->point_count) {
        const uint8_t *slot;
        const uint8_t *point;
        int16_t offset_x;
        int16_t offset_z;
        int32_t horizontal;
        int32_t depth;
        int16_t horizontal_word;
        int16_t depth_word;

        if (slot_index >= objects->slot_count || output_index >= OBJECT_OBSERVATION_IN_LINE_COUNT ||
            output_index >= OBJECT_OBSERVATION_DISTANCE_COUNT) {
            object_observation_set_error(error, error_size,
                                         "CalcPLR1InLine exceeded its source object workspace");
            return 0;
        }
        slot = objects->slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        if (slot[OBJECT_OBSERVATION_SLOT_TYPE_ID] == OBJECT_OBSERVATION_TYPE_AUX) {
            ++slot_index;
            continue;
        }
        point = objects->point_bytes + (size_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
        offset_x = (int16_t)((int32_t)(int16_t)object_observation_read_be16(point + 0u) -
                             (int16_t)(uint16_t)player->x);
        offset_z = (int16_t)((int32_t)(int16_t)object_observation_read_be16(point + 4u) -
                             (int16_t)(uint16_t)player->z);
        if ((int16_t)object_observation_read_be16(slot + OBJECT_OBSERVATION_SLOT_ZONE_ID) < 0) {
            observation->in_line[output_index] = 0u;
            observation->distances[output_index] = 0u;
        } else {
            /* modules/transform.s:CalcPLR1InLine from .objpointrotlop. */
            horizontal = object_observation_sub32((int32_t)offset_x * cosine,
                                                   (int32_t)offset_z * sine);
            horizontal = object_observation_asl32_1(horizontal);
            if (horizontal <= 0) {
                horizontal = object_observation_neg32(horizontal);
            }
            horizontal_word = object_observation_swapped_low_word(horizontal);
            depth = object_observation_add32((int32_t)offset_x * sine,
                                              (int32_t)offset_z * cosine);
            depth_word = object_observation_swapped_low_word(
                object_observation_asl32_2(depth));
            observation->in_line[output_index] =
                depth_word > 0 && object_observation_asr16_1(horizontal_word) <=
                    OBJECT_OBSERVATION_IN_LINE_HALF_WIDTH ?
                UINT8_MAX : 0u;
            observation->distances[output_index] = (uint16_t)depth_word;
        }
        ++slot_index;
        ++point_index;
        ++output_index;
    }
    return 1;
}
