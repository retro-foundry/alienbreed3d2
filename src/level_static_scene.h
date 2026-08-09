#ifndef AB3D2_LEVEL_STATIC_SCENE_H
#define AB3D2_LEVEL_STATIC_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"
#include "scene_frame.h"

/* One source Draw_Wall quad expanded to a GPU-neutral triangle list. */
typedef struct {
    SceneVertex vertices[6];
    uint32_t material_id;
    uint32_t source_record_offset;
} LevelStaticWallScene;

typedef struct {
    LevelStaticWallScene *walls;
    uint32_t wall_count;
} LevelStaticScene;

/*
 * Builds all static wall geometry from every lower and upper draw-graph
 * stream. It does not use PVS, portals, or source screen clipping.
 */
int level_static_scene_build(const LevelRuntime *runtime, uint32_t material_count,
                             LevelStaticScene *out_scene,
                             char *error, size_t error_size);
void level_static_scene_destroy(LevelStaticScene *scene);

#endif
