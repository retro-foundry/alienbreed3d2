#include "object_animation.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    /* defs.i:ObjT/EntT/ShotT offsets used by hires.s:DOALLANIMS. */
    OBJECT_ANIMATION_SLOT_POINT_INDEX = 0u,
    OBJECT_ANIMATION_SLOT_ZONE_ID = 12u,
    OBJECT_ANIMATION_SLOT_TYPE_ID = 16u,
    OBJECT_ANIMATION_SLOT_ENTITY_ZONE_ID = 26u,
    OBJECT_ANIMATION_SLOT_TIMER2 = 40u,
    OBJECT_ANIMATION_SLOT_ENTITY_TYPE = 54u,
    OBJECT_ANIMATION_SLOT_WHICH_ANIMATION = 55u,
    OBJECT_ANIMATION_SLOT_WORRY = 62u,
    OBJECT_ANIMATION_TYPE_OBJECT = 1u,
    OBJECT_ANIMATION_UPDATE_INTERVAL = 5u
};

static void object_animation_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_animation_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int32_t object_animation_read_be32s(const uint8_t *source)
{
    return (int32_t)(((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
                     ((uint32_t)source[2] << 8) | source[3]);
}

static void object_animation_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static uint8_t object_animation_option_for_which_animation(uint8_t which_animation,
                                                             int *out_is_alien_animation)
{
    *out_is_alien_animation = 1;
    if (which_animation == 0u) {
        return 0u;
    }
    if (which_animation == 1u) {
        return 8u;
    }
    if (which_animation == 2u) {
        return 9u;
    }
    if (which_animation == 3u) {
        return 10u;
    }
    *out_is_alien_animation = 0;
    return 0u;
}

void object_animation_runtime_init(ObjectAnimationRuntime *runtime)
{
    if (runtime) {
        /* tables_bss.s:ObjectWorkspace_vl and hires.s:thistime start cleared once. */
        memset(runtime, 0, sizeof(*runtime));
    }
}

void object_animation_runtime_destroy(ObjectAnimationRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    free(runtime->extended_workspace);
    memset(runtime, 0, sizeof(*runtime));
}

int object_animation_runtime_reserve(ObjectAnimationRuntime *runtime, uint32_t slot_count,
                                     char *error, size_t error_size)
{
    uint32_t required_extended_slots;
    size_t old_byte_count;
    size_t new_byte_count;
    uint8_t *extended_workspace;

    if (!runtime) {
        object_animation_set_error(error, error_size,
                                   "ObjectWorkspace reserve received null runtime state");
        return 0;
    }
    if (slot_count <= OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT) {
        return 1;
    }
    required_extended_slots = slot_count - OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT;
    if (required_extended_slots <= runtime->extended_workspace_slot_count) {
        return 1;
    }
    if (required_extended_slots > SIZE_MAX / OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT) {
        object_animation_set_error(error, error_size,
                                   "ObjectWorkspace source list exceeds host allocation limits");
        return 0;
    }
    old_byte_count = (size_t)runtime->extended_workspace_slot_count *
        OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;
    new_byte_count = (size_t)required_extended_slots *
        OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;
    extended_workspace = realloc(runtime->extended_workspace, new_byte_count);
    if (!extended_workspace) {
        object_animation_set_error(error, error_size,
                                   "ObjectWorkspace host tail allocation failed");
        return 0;
    }
    memset(extended_workspace + old_byte_count, 0, new_byte_count - old_byte_count);
    runtime->extended_workspace = extended_workspace;
    runtime->extended_workspace_slot_count = required_extended_slots;
    return 1;
}

uint8_t *object_animation_runtime_workspace(ObjectAnimationRuntime *runtime,
                                            uint32_t slot_index)
{
    uint32_t extended_slot_index;

    if (!runtime) {
        return NULL;
    }
    if (slot_index < OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT) {
        return runtime->workspace[slot_index];
    }
    extended_slot_index = slot_index - OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT;
    if (!runtime->extended_workspace ||
        extended_slot_index >= runtime->extended_workspace_slot_count) {
        return NULL;
    }
    return runtime->extended_workspace +
        (size_t)extended_slot_index * OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;
}

int object_animation_update_single_player_with_audio(ObjectAnimationRuntime *runtime,
                                                     ObjectRuntime *objects,
                                                     const GameLink *game_link,
                                                     GameRandom *random,
                                                     GameAudioEvents *audio_events,
                                                     char *error, size_t error_size)
{
    if (!runtime || !objects || !game_link || !random || !objects->slot_bytes ||
        objects->active_slot_count > objects->slot_count) {
        object_animation_set_error(error, error_size,
                                   "DOALLANIMS received invalid source animation state");
        return 0;
    }
    if (!object_animation_runtime_reserve(runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }

    /* hires.s:DOALLANIMS uses subq.b then signed BLE against thistime. */
    runtime->thistime = (uint8_t)(runtime->thistime - 1u);
    if ((int8_t)runtime->thistime > 0) {
        return 1;
    }
    runtime->thistime = OBJECT_ANIMATION_UPDATE_INTERVAL;

    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        uint8_t *slot;
        uint8_t *workspace = object_animation_runtime_workspace(runtime, slot_index);
        uint16_t timer2;
        uint16_t next_timer2;
        uint8_t option;
        uint8_t special;
        uint8_t special_value;
        uint8_t special_mode;
        int is_alien_animation;
        GameAlienAnimationFrame current_frame;
        GameAlienAnimationFrame next_frame;

        if (!workspace || !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
            object_animation_set_error(error, error_size,
                                       "DOALLANIMS slot is outside the owned source list");
            return 0;
        }
        /* Objectloop2 stops at the signed ObjT point-index terminator. */
        if ((int16_t)object_animation_read_be16(slot + OBJECT_ANIMATION_SLOT_POINT_INDEX) < 0) {
            break;
        }
        if ((int16_t)object_animation_read_be16(slot + OBJECT_ANIMATION_SLOT_ZONE_ID) < 0) {
            continue;
        }
        object_animation_write_be16(slot + OBJECT_ANIMATION_SLOT_ENTITY_ZONE_ID,
                                    object_animation_read_be16(
                                        slot + OBJECT_ANIMATION_SLOT_ZONE_ID));
        if (slot[OBJECT_ANIMATION_SLOT_WORRY] == 0u ||
            (int8_t)slot[OBJECT_ANIMATION_SLOT_TYPE_ID] >=
                (int8_t)OBJECT_ANIMATION_TYPE_OBJECT) {
            continue;
        }

        option = object_animation_option_for_which_animation(
            slot[OBJECT_ANIMATION_SLOT_WHICH_ANIMATION], &is_alien_animation);
        if (is_alien_animation == 0) {
            continue;
        }
        timer2 = object_animation_read_be16(slot + OBJECT_ANIMATION_SLOT_TIMER2);
        if (timer2 >= GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT ||
            slot[OBJECT_ANIMATION_SLOT_ENTITY_TYPE] >= GAME_LINK_ALIEN_COUNT) {
            object_animation_set_error(error, error_size,
                                       "DOALLANIMS encountered an alien frame outside GLFT bounds");
            return 0;
        }
        if (!game_link_get_alien_animation_frame(
                game_link, slot[OBJECT_ANIMATION_SLOT_ENTITY_TYPE], option, timer2,
                &current_frame, error, error_size)) {
            return 0;
        }

        /* hires.s:DOALLANIMS byte five is a one-based SFX index. */
        if (current_frame.bytes[5u] != 0u) {
            uint8_t *point;
            uint16_t point_index = object_animation_read_be16(
                slot + OBJECT_ANIMATION_SLOT_POINT_INDEX);

            if (!object_runtime_get_point_bytes(objects, point_index, &point)) {
                object_animation_set_error(error, error_size,
                                           "DOALLANIMS sound frame has an invalid source point");
                return 0;
            }
            game_audio_events_emit(audio_events, (int16_t)current_frame.bytes[5u] - 1,
                                   80, (int16_t)(object_animation_read_be32s(point) >> 16),
                                   (int16_t)(object_animation_read_be32s(point + 4u) >> 16),
                                   point_index, GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
        }
        if (current_frame.bytes[6u] != 0u) {
            workspace[0u] = (uint8_t)(workspace[0u] + 1u);
            workspace[1u] = (uint8_t)timer2;
        }
        next_timer2 = (uint16_t)(timer2 + 1u);
        special = current_frame.bytes[7u];
        if (special != 0u) {
            special_value = (uint8_t)(special & 0x3fu);
            special_mode = (uint8_t)(special >> 6u);
            if (special_mode < 2u) {
                workspace[4u] = special_value;
            } else if (special_mode == 2u) {
                if (special_value == 0u) {
                    object_animation_set_error(error, error_size,
                                               "DOALLANIMS source animation divides by zero");
                    return 0;
                }
                workspace[4u] = (uint8_t)(game_random_next(random) % special_value);
            } else {
                workspace[4u] = (uint8_t)(workspace[4u] - 1u);
                if (workspace[4u] != 0u) {
                    next_timer2 = special_value;
                }
            }
        }
        if (next_timer2 >= GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT ||
            !game_link_get_alien_animation_frame(
                game_link, slot[OBJECT_ANIMATION_SLOT_ENTITY_TYPE], option, next_timer2,
                &next_frame, error, error_size)) {
            object_animation_set_error(error, error_size,
                                       "DOALLANIMS next alien frame is outside GLFT bounds");
            return 0;
        }
        if ((int8_t)next_frame.bytes[0u] < 0) {
            workspace[3u] = UINT8_MAX;
            next_timer2 = 0u;
        }
        workspace[2u] = option;
        object_animation_write_be16(slot + OBJECT_ANIMATION_SLOT_TIMER2, next_timer2);
    }
    return 1;
}

int object_animation_update_single_player(ObjectAnimationRuntime *runtime,
                                          ObjectRuntime *objects,
                                          const GameLink *game_link,
                                          GameRandom *random,
                                          char *error, size_t error_size)
{
    return object_animation_update_single_player_with_audio(runtime, objects, game_link, random,
                                                            NULL, error, error_size);
}
