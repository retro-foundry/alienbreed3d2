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

/*
 * One source Draw_Flats boundary. Its vertices deliberately remain a polygon
 * boundary because the source record does not establish that a triangle fan is
 * safe for every floor, ceiling, or water polygon.
 */
typedef struct {
    SceneVertex *vertices;
    uint32_t vertex_count;
    uint32_t material_id;
    uint32_t source_record_offset;
    SceneGeometryPrimitive primitive;
    uint16_t texture_scale;
    int16_t brightness_offset;
} LevelStaticFlatScene;

typedef struct {
    LevelStaticWallScene *walls;
    uint32_t wall_count;
    LevelStaticFlatScene *flats;
    uint32_t flat_count;
} LevelStaticScene;

/*
 * Builds all static wall and flat geometry from every lower and upper
 * draw-graph stream. `floor_texture_size` selects the currently active global
 * or level-override floortile byte range. It does not use PVS, portals, or
 * source screen clipping.
 */
int level_static_scene_build(const LevelRuntime *runtime, uint32_t wall_material_count,
                             size_t floor_texture_size,
                             LevelStaticScene *out_scene,
                             char *error, size_t error_size);
void level_static_scene_destroy(LevelStaticScene *scene);

#endif
