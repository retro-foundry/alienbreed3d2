#ifndef AB3D2_OBJECT_PASSIVES_H
#define AB3D2_OBJECT_PASSIVES_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "level_runtime.h"
#include "message_runtime.h"
#include "object_runtime.h"

/* One source ItsAnObject destructible or decoration branch. */
int object_passives_update_slot(ObjectRuntime *objects, uint32_t slot_index,
                                const LevelRuntime *level, const GameLink *game_link,
                                const GameObjectDefinition *definition,
                                MessageRuntime *messages, uint8_t messages_enabled,
                                char *error, size_t error_size);

#endif
