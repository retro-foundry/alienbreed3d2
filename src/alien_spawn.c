#include "alien_spawn.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i: ObjT/EntT/ShotT fields written by modules/ai.s:ai_JustDied. */
    ALIEN_SPAWN_SLOT_POINT_INDEX = 0u,
    ALIEN_SPAWN_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_SPAWN_SLOT_ZONE_ID = 12u,
    ALIEN_SPAWN_SLOT_TYPE_ID = 16u,
    ALIEN_SPAWN_SLOT_HIT_POINTS = 18u,
    ALIEN_SPAWN_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_SPAWN_SLOT_CURRENT_MODE = 20u,
    ALIEN_SPAWN_SLOT_TEAM_NUMBER = 21u,
    ALIEN_SPAWN_SLOT_DISPLAY_TEXT = 24u,
    ALIEN_SPAWN_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_SPAWN_SLOT_CURRENT_CONTROL_POINT = 28u,
    ALIEN_SPAWN_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_SPAWN_SLOT_TARGET_CONTROL_POINT = 32u,
    ALIEN_SPAWN_SLOT_TIMER1 = 34u,
    ALIEN_SPAWN_SLOT_TIMER2 = 40u,
    ALIEN_SPAWN_SLOT_IMPACT_X = 42u,
    ALIEN_SPAWN_SLOT_IMPACT_Z = 44u,
    ALIEN_SPAWN_SLOT_IMPACT_Y = 46u,
    ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD = 50u,
    ALIEN_SPAWN_SLOT_ENTITY_TYPE = 54u,
    ALIEN_SPAWN_SLOT_WHICH_ANIMATION = 55u,
    ALIEN_SPAWN_SLOT_IN_UPPER_ZONE = 63u,
    /* ai_JustDied: move.w #2,d7 followed by dbra. */
    ALIEN_SPAWN_REQUESTED_COUNT = 3u,
    /* ai_JustDied: move.w #9,d3 followed by dbra. */
    ALIEN_SPAWN_PROBE_COUNT = 10u,
    ALIEN_SPAWN_OBJECT_TYPE_ALIEN = 0u,
    ALIEN_SPAWN_OBJECT_TYPE_AUXILIARY = 3u
};

static void alien_spawn_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_spawn_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_spawn_read_be16s(const uint8_t *source)
{
    uint16_t value = alien_spawn_read_be16(source);

    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static void alien_spawn_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void alien_spawn_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

int alien_spawn_smaller(ObjectRuntime *objects, uint32_t parent_slot_index,
                        const GameAlienDefinition *child_definition,
                        uint8_t child_alien_type, uint32_t *out_spawned_count,
                        char *error, size_t error_size)
{
    uint8_t *parent_slot;
    uint8_t *parent_point;
    uint32_t candidate_slot_index;
    int16_t source_d3 = (int16_t)(ALIEN_SPAWN_PROBE_COUNT - 1u);
    int16_t source_d7 = (int16_t)(ALIEN_SPAWN_REQUESTED_COUNT - 1u);
    uint32_t spawned_count = 0u;

    if (out_spawned_count) {
        *out_spawned_count = 0u;
    }
    if (!objects || !child_definition || parent_slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, parent_slot_index, &parent_slot) ||
        objects->alien_shot_first_slot > UINT32_MAX -
            (OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 1u)) {
        alien_spawn_set_error(error, error_size, "ai_JustDied spawn received invalid source state");
        return 0;
    }
    if (!object_runtime_get_point_bytes(
            objects, alien_spawn_read_be16(parent_slot + ALIEN_SPAWN_SLOT_POINT_INDEX),
            &parent_point)) {
        alien_spawn_set_error(error, error_size,
                              "ai_JustDied parent has an invalid source point");
        return 0;
    }

    /* hires.s derives AI_OtherAlienDataPtrs after 20 alien-shot records, then NEXT_OBJ. */
    candidate_slot_index = objects->alien_shot_first_slot +
        OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 1u;
    for (;;) {
        uint8_t *candidate_slot;
        uint8_t *candidate_point;
        uint8_t *preceding_slot;
        uint16_t candidate_point_index;

        if (!object_runtime_get_slot_bytes(objects, candidate_slot_index, &candidate_slot)) {
            alien_spawn_set_error(error, error_size,
                                  "ai_JustDied spawned-alien records are outside source state");
            return 0;
        }
        if (alien_spawn_read_be16s(candidate_slot + ALIEN_SPAWN_SLOT_ZONE_ID) >= 0 &&
            candidate_slot[ALIEN_SPAWN_SLOT_HIT_POINTS] != 0u) {
            if (candidate_slot_index > UINT32_MAX - 2u) {
                alien_spawn_set_error(error, error_size,
                                      "ai_JustDied spawned-alien probe index overflows");
                return 0;
            }
            candidate_slot_index += 2u;
            source_d3 = (int16_t)((uint16_t)source_d3 - 1u);
            if (source_d3 != -1) {
                continue;
            }
            break;
        }

        candidate_point_index = alien_spawn_read_be16(
            candidate_slot + ALIEN_SPAWN_SLOT_POINT_INDEX);
        if (candidate_slot_index == 0u ||
            !object_runtime_get_point_bytes(objects, candidate_point_index, &candidate_point) ||
            !object_runtime_get_slot_bytes(objects, candidate_slot_index - 1u, &preceding_slot)) {
            alien_spawn_set_error(error, error_size,
                                  "ai_JustDied child has an invalid source slot or point");
            return 0;
        }

        /* Each write matches ai_JustDied's found_one_free path and its byte widths. */
        candidate_slot[ALIEN_SPAWN_SLOT_HIT_POINTS] = (uint8_t)child_definition->hit_points;
        candidate_slot[ALIEN_SPAWN_SLOT_ENTITY_TYPE] = child_alien_type;
        candidate_slot[ALIEN_SPAWN_SLOT_DISPLAY_TEXT] = UINT8_MAX;
        candidate_slot[ALIEN_SPAWN_SLOT_TYPE_ID] = ALIEN_SPAWN_OBJECT_TYPE_ALIEN;
        memcpy(candidate_point, parent_point, OBJECT_RUNTIME_POINT_BYTE_COUNT);
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_VERTICAL_POSITION,
                               alien_spawn_read_be16(parent_slot +
                                                     ALIEN_SPAWN_SLOT_VERTICAL_POSITION));
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_ZONE_ID,
                               alien_spawn_read_be16(parent_slot + ALIEN_SPAWN_SLOT_ZONE_ID));
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_ENTITY_ZONE_ID,
                               alien_spawn_read_be16(parent_slot + ALIEN_SPAWN_SLOT_ZONE_ID));
        alien_spawn_write_be16(preceding_slot + ALIEN_SPAWN_SLOT_ZONE_ID, UINT16_MAX);
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_CURRENT_CONTROL_POINT,
                               alien_spawn_read_be16(parent_slot +
                                                     ALIEN_SPAWN_SLOT_CURRENT_CONTROL_POINT));
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_TARGET_CONTROL_POINT,
                               alien_spawn_read_be16(parent_slot +
                                                     ALIEN_SPAWN_SLOT_CURRENT_CONTROL_POINT));
        candidate_slot[ALIEN_SPAWN_SLOT_TEAM_NUMBER] = UINT8_MAX;
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_CURRENT_ANGLE,
                               alien_spawn_read_be16(parent_slot +
                                                     ALIEN_SPAWN_SLOT_CURRENT_ANGLE));
        candidate_slot[ALIEN_SPAWN_SLOT_CURRENT_MODE] = 0u;
        candidate_slot[ALIEN_SPAWN_SLOT_WHICH_ANIMATION] = 0u;
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_TIMER2, 0u);
        candidate_slot[ALIEN_SPAWN_SLOT_DAMAGE_TAKEN] = 0u;
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_TIMER1, 0u);
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_IMPACT_X, 0u);
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_IMPACT_Z, 0u);
        alien_spawn_write_be16(candidate_slot + ALIEN_SPAWN_SLOT_IMPACT_Y, 0u);
        candidate_slot[ALIEN_SPAWN_SLOT_HIT_POINTS] = (uint8_t)child_definition->hit_points;
        candidate_slot[ALIEN_SPAWN_SLOT_DAMAGE_TAKEN] = 0u;
        alien_spawn_write_be32(candidate_slot + ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD,
                               ((uint32_t)parent_slot[ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD] << 24) |
                               ((uint32_t)parent_slot[ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD + 1u] << 16) |
                               ((uint32_t)parent_slot[ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD + 2u] << 8) |
                               parent_slot[ALIEN_SPAWN_SLOT_DOORS_AND_LIFTS_HELD + 3u]);
        candidate_slot[ALIEN_SPAWN_SLOT_IN_UPPER_ZONE] =
            parent_slot[ALIEN_SPAWN_SLOT_IN_UPPER_ZONE];
        preceding_slot[ALIEN_SPAWN_SLOT_TYPE_ID] = ALIEN_SPAWN_OBJECT_TYPE_AUXILIARY;
        ++spawned_count;

        source_d7 = (int16_t)((uint16_t)source_d7 - 1u);
        if (source_d7 < 0) {
            break;
        }
    }
    if (out_spawned_count) {
        *out_spawned_count = spawned_count;
    }
    return 1;
}
