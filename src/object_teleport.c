#include "object_teleport.h"

#include <stdio.h>
#include <string.h>

static void object_teleport_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int32_t object_teleport_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_teleport_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

int object_teleport_check(const LevelRuntime *level, const ObjectRuntime *objects,
                          const GameLink *game_link, uint16_t from_zone_index,
                          const int16_t *source_a2_words, size_t source_a2_word_count,
                          ObjectCollisionTrace *trace, ObjectTeleportState *out_state,
                          char *error, size_t error_size)
{
    LevelZone source_zone;
    LevelZone destination_zone;
    ObjectTeleportState state;
    uint8_t hit_wall;

    if (!level || !trace || !out_state || from_zone_index >= level->zone_count ||
        !level_runtime_get_zone(level, from_zone_index, &source_zone, error, error_size)) {
        object_teleport_set_error(error, error_size,
                                  "CheckTeleport received an invalid source zone state");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    state.zone_index = from_zone_index;
    if (source_zone.teleport_zone < 0) {
        *out_state = state;
        return 1;
    }
    if ((uint16_t)source_zone.teleport_zone >= level->zone_count || !objects || !game_link ||
        !source_a2_words ||
        !level_runtime_get_zone(level, (uint16_t)source_zone.teleport_zone,
                                &destination_zone, error, error_size)) {
        object_teleport_set_error(error, error_size,
                                  "CheckTeleport teleport destination is outside source state");
        return 0;
    }

    /* ZoneT_Floor_l destination-minus-source, then the source newx/newz writes. */
    state.floor_delta = object_teleport_sub32(destination_zone.floor, source_zone.floor);
    trace->new_y = object_teleport_add32(trace->new_y, state.floor_delta);
    trace->new_x = source_zone.teleport_x;
    trace->new_z = source_zone.teleport_z;
    if (!object_collision_check(objects, game_link, source_a2_words, source_a2_word_count,
                                trace, &hit_wall, error, error_size)) {
        /* The source restores newy immediately after its Obj_DoCollision call. */
        trace->new_y = object_teleport_sub32(trace->new_y, state.floor_delta);
        return 0;
    }
    trace->new_y = object_teleport_sub32(trace->new_y, state.floor_delta);
    if (hit_wall == 0u) {
        /* SEQ writes $ff and only this branch replaces Obj_ZonePtr_l. */
        state.teleported = UINT8_MAX;
        state.zone_index = (uint16_t)source_zone.teleport_zone;
    }
    *out_state = state;
    return 1;
}
