#ifndef AB3D2_OBJECT_SCENE_H
#define AB3D2_OBJECT_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "game_preferences.h"
#include "game_resources.h"
#include "lighting_runtime.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "scene_frame.h"

/*
 * objdrawhires.s:Draw_Objects source-object handoff without its zone sorting,
 * clipping, PVS, or software rasterization.  It excludes Player 1's own
 * world entity, which draw_Bitmap rejects at the source near plane because
 * Plr1_Use places it at the active first-person camera.
 */
int object_scene_count_active(const ObjectRuntime *objects, uint32_t *out_count,
                              char *error, size_t error_size);
int object_scene_submit_active(const ObjectRuntime *objects, const GameLink *game_link,
                               const GameSharedResources *resources,
                               const LevelRuntime *level,
                               const LightingRuntime *lighting,
                               const GameMath *math,
                               const GamePreferences *preferences, SceneFrame *frame,
                               char *error, size_t error_size);

#endif
