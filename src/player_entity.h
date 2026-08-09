#ifndef AB3D2_PLAYER_ENTITY_H
#define AB3D2_PLAYER_ENTITY_H

#include <stddef.h>

#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * hires.s:Plr1_Use source-owned ObjT/object-point publication needed by the
 * object, projectile, and sprite paths. Damage response and sprite selection
 * stay with their original owning routines.
 */
int player_entity_sync_single_player(ObjectRuntime *objects, const LevelRuntime *level,
                                     const PlayerRuntime *player,
                                     char *error, size_t error_size);

#endif
