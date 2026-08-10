#include "alien_perception.h"

#include <stdio.h>

#include "object_visibility.h"

enum {
    /* defs.i:ObjT/ShotT fields used by modules/ai.s:AI_LookForPlayer1. */
    ALIEN_PERCEPTION_SLOT_SEES_PLAYER = 17u,
    ALIEN_PERCEPTION_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_PERCEPTION_SLOT_IN_UPPER_ZONE = 63u
};

static void alien_perception_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_perception_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int32_t alien_perception_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -(((-(int64_t)value) + 127) >> 7);
}

int alien_perception_look_for_player_one(AlienRuntime *alien_runtime,
                                         ObjectRuntime *objects, uint32_t slot_index,
                                         const LevelRuntime *level, const AssetBlob *clips,
                                         const PlayerRuntime *player,
                                         uint16_t viewer_zone_index,
                                         int16_t viewer_x, int16_t viewer_z,
                                         char *error, size_t error_size)
{
    uint8_t *slot;
    ObjectVisibilityQuery query;
    uint8_t can_see;

    if (!alien_runtime || !objects || !level || !clips || !player ||
        slot_index >= objects->active_slot_count ||
        viewer_zone_index >= level->zone_count || player->zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_perception_set_error(error, error_size,
                                   "AI_LookForPlayer1 received invalid source state");
        return 0;
    }
    /* AI_LookForPlayer1 clears ObjT_SeePlayer_b before calling CanItBeSeen. */
    slot[ALIEN_PERCEPTION_SLOT_SEES_PLAYER] = 0u;
    query.viewer_zone_index = viewer_zone_index;
    query.viewer_x = viewer_x;
    query.viewer_z = viewer_z;
    query.viewer_y = (int16_t)alien_perception_read_be16(
        slot + ALIEN_PERCEPTION_SLOT_VERTICAL_POSITION);
    query.viewer_in_upper_zone = slot[ALIEN_PERCEPTION_SLOT_IN_UPPER_ZONE];
    /* AI_LookForPlayer1 writes these source globals before CanItBeSeen. */
    object_motion_runtime_set_new_words(&alien_runtime->motion, query.viewer_x, query.viewer_z);
    object_visibility_runtime_set_viewer(&alien_runtime->visibility,
                                         query.viewer_x, query.viewer_z, query.viewer_y,
                                         query.viewer_in_upper_zone);
    query.target_zone_index = player->zone_index;
    query.target_x = (int16_t)(uint16_t)player->x;
    query.target_z = (int16_t)(uint16_t)player->z;
    query.target_y = (int16_t)alien_perception_asr32_7(player->y);
    query.target_in_upper_zone = player->stood_in_top;
    if (!object_visibility_can_see(level, clips, &query, &can_see, error, error_size)) {
        return 0;
    }
    if (can_see != 0u) {
        /* The source writes immediate one, rather than ST's 0xff. */
        slot[ALIEN_PERCEPTION_SLOT_SEES_PLAYER] = 1u;
    }
    return 1;
}
