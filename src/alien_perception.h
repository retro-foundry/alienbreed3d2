#ifndef AB3D2_ALIEN_PERCEPTION_H
#define AB3D2_ALIEN_PERCEPTION_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * modules/ai.s:AI_LookForPlayer1. `viewer_x`/`viewer_z` are the caller's
 * source `newx`/`newz` words; the later AI movement routines own their exact
 * production and must pass them instead of this helper guessing a position.
 */
int alien_perception_look_for_player_one(ObjectRuntime *objects, uint32_t slot_index,
                                         const LevelRuntime *level, const AssetBlob *clips,
                                         const PlayerRuntime *player,
                                         uint16_t viewer_zone_index,
                                         int16_t viewer_x, int16_t viewer_z,
                                         char *error, size_t error_size);

#endif
