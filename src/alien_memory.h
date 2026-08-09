#ifndef AB3D2_ALIEN_MEMORY_H
#define AB3D2_ALIEN_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "alien_runtime.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * modules/ai.s:ai_StorePlayerPosition.  This is a source AI helper only; the
 * later AI mode paths remain responsible for deciding when an alien sees the
 * player and therefore invokes it.
 */
int alien_memory_store_player_position(AlienRuntime *alien_runtime,
                                       ObjectRuntime *objects, uint32_t slot_index,
                                       const LevelRuntime *level,
                                       const PlayerRuntime *player,
                                       char *error, size_t error_size);

#endif
