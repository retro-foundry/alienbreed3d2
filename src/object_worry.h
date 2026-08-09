#ifndef AB3D2_OBJECT_WORRY_H
#define AB3D2_OBJECT_WORRY_H

#include <stddef.h>

#include "alien_runtime.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * hires.s:.doallrooms2/.doallobs single-player gameplay activation pass.
 * This marks ObjT ShotT_Worry_b for the following frame; it does not provide
 * renderer visibility, PVS traversal, or portal culling.
 */
int object_worry_update_single_player(ObjectRuntime *objects, const LevelRuntime *level,
                                      const PlayerRuntime *player,
                                      const AlienRuntime *alien_runtime,
                                      char *error, size_t error_size);

#endif
