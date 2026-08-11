#ifndef AB3D2_OBJECT_PASSIVES_H
#define AB3D2_OBJECT_PASSIVES_H

#include <stddef.h>
#include <stdint.h>

#include "alien_runtime.h"
#include "asset_io.h"
#include "game_link.h"
#include "level_runtime.h"
#include "message_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* One source ItsAnObject destructible or decoration branch. */
int object_passives_update_slot(ObjectRuntime *objects, uint32_t slot_index,
                                AlienRuntime *alien_runtime,
                                const LevelRuntime *level, const GameLink *game_link,
                                const AssetBlob *clips, const PlayerRuntime *player,
                                const GameObjectDefinition *definition,
                                MessageRuntime *messages, uint8_t messages_enabled,
                                char *error, size_t error_size);

#endif
