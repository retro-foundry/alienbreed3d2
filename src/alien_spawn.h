#ifndef AB3D2_ALIEN_SPAWN_H
#define AB3D2_ALIEN_SPAWN_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "object_runtime.h"

/*
 * modules/ai.s:ai_JustDied's spawned-alien branch.  The caller has already
 * decoded AlienT_SplatType_w and selected the corresponding child definition.
 */
int alien_spawn_smaller(ObjectRuntime *objects, uint32_t parent_slot_index,
                        const GameAlienDefinition *child_definition,
                        uint8_t child_alien_type, uint32_t *out_spawned_count,
                        char *error, size_t error_size);

#endif
