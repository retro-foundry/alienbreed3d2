#include "alien_death.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "alien_spawn.h"
#include "alien_spatial.h"

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by modules/ai.s:ai_DoDie. */
    ALIEN_DEATH_SLOT_POINT_INDEX = 0u,
    ALIEN_DEATH_SLOT_ZONE_ID = 12u,
    ALIEN_DEATH_SLOT_TYPE_ID = 16u,
    ALIEN_DEATH_SLOT_HIT_POINTS = 18u,
    ALIEN_DEATH_SLOT_CURRENT_MODE = 20u,
    ALIEN_DEATH_SLOT_DISPLAY_TEXT = 24u,
    ALIEN_DEATH_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_DEATH_SLOT_TIMER2 = 40u,
    ALIEN_DEATH_SLOT_ENTITY_TYPE = 54u,
    ALIEN_DEATH_SLOT_WHICH_ANIMATION = 55u,
    ALIEN_DEATH_SLOT_WORRY = 62u,
    ALIEN_DEATH_TYPE_ALIEN = 0u,
    /* modules/ai.s:ai_JustDied's move.w #8,d2 before Anim_ExplodeIntoBits. */
    ALIEN_DEATH_EXPLOSION_REQUESTED_COUNT = 8u,
    /* c/message.h:MSG_TAG_NARRATIVE is zero in the upper two bits. */
    ALIEN_DEATH_NARRATIVE_LENGTH_AND_TAG = AB3D2_LEVEL_MESSAGE_LENGTH,
    /* ObjectWorkspace_vl byte one is the source special-frame/action byte. */
    ALIEN_DEATH_WORKSPACE_SPECIAL_FRAME = 1u
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

static int16_t alien_death_read_be16s(const uint8_t *source)
{
    uint16_t value = alien_death_read_be16(source);

    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
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

int alien_death_just_died(ObjectRuntime *objects, uint32_t slot_index,
                          const LevelRuntime *level, const GameLink *game_link,
                          GameProgression *progression,
                          ObjectAnimationRuntime *animation_runtime,
                          ObjectExplosionRuntime *explosion_runtime,
                          const GameMath *math, GameRandom *random,
                          AlienJustDiedState *out_state,
                          char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint16_t point_index;
    uint8_t alien_type;
    uint8_t splat_type;
    GameAlienDefinition definition;
    AlienJustDiedState state;

    if (!objects || !level || !game_link || !progression || !animation_runtime ||
        !explosion_runtime || !math || !random || !out_state ||
        slot_index >= objects->active_slot_count ||
        slot_index >= OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_death_set_error(error, error_size, "ai_JustDied received invalid source state");
        return 0;
    }
    memset(&state, 0, sizeof(state));

    /* ai_JustDied clears HP before making its narrative/progression side effects. */
    slot[ALIEN_DEATH_SLOT_HIT_POINTS] = 0u;
    if (alien_death_read_be16s(slot + ALIEN_DEATH_SLOT_DISPLAY_TEXT) >= 0) {
        LevelNarrativeMessage message;

        if (!level_runtime_get_narrative_message(
                level, alien_death_read_be16(slot + ALIEN_DEATH_SLOT_DISPLAY_TEXT),
                &message, error, error_size)) {
            return 0;
        }
        state.narrative.bytes = message.bytes;
        state.narrative.byte_count = (uint16_t)message.byte_count;
        state.narrative.length_and_tag = ALIEN_DEATH_NARRATIVE_LENGTH_AND_TAG;
    }

    point_index = alien_death_read_be16(slot + ALIEN_DEATH_SLOT_POINT_INDEX);
    if (!object_runtime_get_point_bytes(objects, point_index, &point)) {
        alien_death_set_error(error, error_size, "ai_JustDied alien point is outside source state");
        return 0;
    }
    alien_type = slot[ALIEN_DEATH_SLOT_ENTITY_TYPE];
    if (!game_progression_record_alien_kill(progression, alien_type, error, error_size) ||
        !game_link_get_alien_definition(game_link, alien_type, &definition, error, error_size)) {
        return 0;
    }
    /* AlienT_SplatType_w+1 is the source's low byte, even when the word is wider. */
    splat_type = (uint8_t)definition.splat_type;
    state.splat_type = splat_type;
    if (splat_type < GAME_LINK_BULLET_COUNT) {
        if (!object_explosion_into_bits(
                explosion_runtime, objects, slot_index, math, random,
                (int16_t)alien_death_read_be16(point),
                (int16_t)alien_death_read_be16(point + 4u), splat_type,
                ALIEN_DEATH_EXPLOSION_REQUESTED_COUNT, (int16_t)point_index,
                &state.fragment_count, error, error_size)) {
            return 0;
        }
    } else {
        uint8_t child_type = (uint8_t)(splat_type - GAME_LINK_BULLET_COUNT);
        GameAlienDefinition child_definition;

        if (!game_link_get_alien_definition(game_link, child_type, &child_definition,
                                             error, error_size) ||
            !alien_spawn_smaller(objects, slot_index, &child_definition, child_type,
                                 &state.child_count, error, error_size)) {
            return 0;
        }
    }

    slot[ALIEN_DEATH_SLOT_CURRENT_MODE] = 5u;
    slot[ALIEN_DEATH_SLOT_WHICH_ANIMATION] = 3u;
    alien_death_write_be16(slot + ALIEN_DEATH_SLOT_TIMER2, 0u);
    animation_runtime->workspace[slot_index][ALIEN_DEATH_WORKSPACE_SPECIAL_FRAME] = UINT8_MAX;
    state.got_out = UINT8_MAX;
    *out_state = state;
    return 1;
}
