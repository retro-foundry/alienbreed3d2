#ifndef AB3D2_SCENE_GEOMETRY_COMPILE_H
#define AB3D2_SCENE_GEOMETRY_COMPILE_H

#include "scene_frame.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Renderer-neutral native world space. Source X/Z are signed low words;
 * source Y is the down-positive 8.8 domain scaled into the same projection
 * space and reflected to up-positive coordinates.
 */
#define SCENE_RENDER_SOURCE_Y_UNIT (1.0f / 128.0f)

typedef struct {
    float x;
    float y;
    float z;
} SceneRenderPoint;

SceneRenderPoint scene_render_world_point(SceneWorldPoint point);
SceneRenderPoint scene_render_camera_point(const SceneCamera *camera);

/*
 * Produce deterministic triangle indices into geometry->vertices. Triangle
 * lists retain their submitted order; polygon boundaries use X/Z ear clipping
 * so concave authored flats do not acquire a renderer-specific fan.
 */
int scene_geometry_triangle_indices(const SceneGeometry *geometry,
                                    uint32_t **out_indices,
                                    uint32_t *out_index_count,
                                    char *error, size_t error_size);
void scene_geometry_triangle_indices_release(uint32_t *indices);

#ifdef __cplusplus
}
#endif

#endif
