#include "object_worry.h"

#include <stdio.h>
#include <string.h>

enum {
    /* hires.s:Sys_Workspace_vl uses eight longwords for source zone bits. */
    OBJECT_WORRY_VISIBLE_ZONE_BIT_COUNT = 8u * 32u,
    OBJECT_WORRY_VISIBLE_ZONE_BYTE_COUNT = OBJECT_WORRY_VISIBLE_ZONE_BIT_COUNT / 8u,
    /* defs.i:ObjT/EntT/ShotT fields used by hires.s:.doallobs. */
    OBJECT_WORRY_POINT_INDEX = 0u,
    OBJECT_WORRY_ZONE_ID = 12u,
    OBJECT_WORRY_TYPE_ID = 16u,
    OBJECT_WORRY_TEAM_NUMBER = 21u,
    OBJECT_WORRY_WORRY = 62u
};

static void object_worry_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_worry_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int object_worry_build_visible_zone_bits(const LevelRuntime *level,
                                                uint16_t viewer_zone_index,
                                                uint8_t *visible_zone_bits,
                                                char *error, size_t error_size)
{
    uint32_t entry_index;
    uint8_t terminated = 0u;

    memset(visible_zone_bits, 0, OBJECT_WORRY_VISIBLE_ZONE_BYTE_COUNT);
    for (entry_index = 0u; entry_index <= level->level_size / 8u; ++entry_index) {
        LevelPotentialVisibility entry;
        uint16_t visible_zone_index;

        if (!level_runtime_get_zone_potential_visibility(level, viewer_zone_index, entry_index,
                                                         &entry, error, error_size)) {
            return 0;
        }
        if (entry.zone_index < 0) {
            terminated = UINT8_MAX;
            break;
        }
        visible_zone_index = (uint16_t)entry.zone_index;
        if (visible_zone_index >= OBJECT_WORRY_VISIBLE_ZONE_BIT_COUNT) {
            object_worry_set_error(error, error_size,
                                   "source PVST zone exceeds Sys_Workspace bit capacity");
            return 0;
        }
        /* bset d1,(a1,d0.w): bit numbering is within the addressed source byte. */
        visible_zone_bits[visible_zone_index >> 3u] |=
            (uint8_t)(UINT8_C(1) << (visible_zone_index & 7u));
    }
    if (terminated == 0u) {
        object_worry_set_error(error, error_size,
                               "source PVST list has no negative terminator");
        return 0;
    }
    return 1;
}

int object_worry_update_single_player(ObjectRuntime *objects, const LevelRuntime *level,
                                      const PlayerRuntime *player,
                                      const AlienRuntime *alien_runtime,
                                      char *error, size_t error_size)
{
    uint8_t visible_zone_bits[OBJECT_WORRY_VISIBLE_ZONE_BYTE_COUNT];
    uint32_t slot_index;

    if (!objects || !level || !player || !alien_runtime || !objects->slot_bytes ||
        player->zone_index >= level->zone_count ||
        objects->active_slot_count > objects->slot_count) {
        object_worry_set_error(error, error_size,
                               "source worry update received invalid single-player state");
        return 0;
    }
    if (!object_worry_build_visible_zone_bits(level, player->zone_index, visible_zone_bits,
                                              error, error_size)) {
        return 0;
    }
    for (slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        uint8_t *slot;
        int16_t zone_id;

        if (!object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
            object_worry_set_error(error, error_size,
                                   "source worry slot is outside the owned ObjT list");
            return 0;
        }
        if ((int16_t)object_worry_read_be16(slot + OBJECT_WORRY_POINT_INDEX) < 0) {
            break;
        }
        zone_id = (int16_t)object_worry_read_be16(slot + OBJECT_WORRY_ZONE_ID);
        if (zone_id < 0) {
            continue;
        }
        if ((uint16_t)zone_id >= OBJECT_WORRY_VISIBLE_ZONE_BIT_COUNT) {
            object_worry_set_error(error, error_size,
                                   "source object zone exceeds Sys_Workspace bit capacity");
            return 0;
        }
        if ((visible_zone_bits[(uint16_t)zone_id >> 3u] &
             (uint8_t)(UINT8_C(1) << ((uint16_t)zone_id & 7u))) != 0u) {
            slot[OBJECT_WORRY_WORRY] |= 0x7fu;
            continue;
        }
        /* d7 is %000001, so only a normal alien type reaches the team path. */
        if ((slot[OBJECT_WORRY_TYPE_ID] & 31u) != 0u) {
            continue;
        }
        if ((int8_t)slot[OBJECT_WORRY_TEAM_NUMBER] < 0) {
            continue;
        }
        if (slot[OBJECT_WORRY_TEAM_NUMBER] >= ALIEN_RUNTIME_TEAM_COUNT) {
            object_worry_set_error(error, error_size,
                                   "source alien team index exceeds AI workspace");
            return 0;
        }
        if (alien_runtime->team_workspace[slot[OBJECT_WORRY_TEAM_NUMBER]][4u] < 0) {
            continue;
        }
        slot[OBJECT_WORRY_WORRY] |= 0x7fu;
    }
    return 1;
}
