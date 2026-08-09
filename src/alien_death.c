#include "alien_death.h"

#include <stdio.h>

#include "alien_spatial.h"

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by modules/ai.s:ai_DoDie. */
    ALIEN_DEATH_SLOT_ZONE_ID = 12u,
    ALIEN_DEATH_SLOT_TYPE_ID = 16u,
    ALIEN_DEATH_SLOT_HIT_POINTS = 18u,
    ALIEN_DEATH_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_DEATH_SLOT_WORRY = 62u,
    ALIEN_DEATH_TYPE_ALIEN = 0u
};

static void alien_death_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_death_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_death_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

int alien_death_update(ObjectRuntime *objects, uint32_t slot_index,
                       ObjectAnimationRuntime *animation_runtime,
                       const GameLink *game_link, const GameMath *math,
                       const LevelRuntime *level, const AlienSetup *setup,
                       uint16_t viewer_yaw, AlienDeathState *out_state,
                       char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *previous_slot;
    AlienDeathState state;

    if (!objects || !animation_runtime || !game_link || !math || !level || !setup ||
        !out_state || slot_index == 0u || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &previous_slot)) {
        alien_death_set_error(error, error_size, "ai_DoDie received invalid source state");
        return 0;
    }
    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, setup, viewer_yaw,
            &state.animation, error, error_size) ||
        !alien_spatial_store_room_stats_still(
            objects, slot_index, level, setup->zone_id, setup->thing_height,
            error, error_size)) {
        return 0;
    }

    state.got_out = 0u;
    if (state.animation.finished != 0u) {
        /* macros.i:FREE_ENT followed by ai_DoDie's type/worry cleanup. */
        alien_death_write_be16(slot + ALIEN_DEATH_SLOT_ZONE_ID, UINT16_MAX);
        alien_death_write_be16(slot + ALIEN_DEATH_SLOT_ENTITY_ZONE_ID, UINT16_MAX);
        slot[ALIEN_DEATH_SLOT_TYPE_ID] = ALIEN_DEATH_TYPE_ALIEN;
        slot[ALIEN_DEATH_SLOT_WORRY] = 0u;
        state.got_out = UINT8_MAX;
    } else {
        slot[ALIEN_DEATH_SLOT_HIT_POINTS] = 0u;
    }

    /* ai_DoDie copies the current source zone pair to a live preceding auxiliary. */
    if ((int16_t)alien_death_read_be16(previous_slot + ALIEN_DEATH_SLOT_ZONE_ID) >= 0) {
        alien_death_write_be16(previous_slot + ALIEN_DEATH_SLOT_ZONE_ID,
                               alien_death_read_be16(slot + ALIEN_DEATH_SLOT_ZONE_ID));
        alien_death_write_be16(previous_slot + ALIEN_DEATH_SLOT_ENTITY_ZONE_ID,
                               alien_death_read_be16(slot + ALIEN_DEATH_SLOT_ENTITY_ZONE_ID));
    }
    *out_state = state;
    return 1;
}
