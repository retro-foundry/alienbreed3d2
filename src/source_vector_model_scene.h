#ifndef AB3D2_SOURCE_VECTOR_MODEL_SCENE_H
#define AB3D2_SOURCE_VECTOR_MODEL_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"
#include "render_view.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
    float source_light;
} SourceVectorSceneVertex;

typedef struct {
    SourceVectorSceneVertex vertices[3];
    uint32_t material_index;
    uint8_t additive;
} SourceVectorSceneTriangle;

typedef struct {
    uint8_t *rgba;
    uint16_t width;
    uint16_t height;
    /* objdrawhires.s:doapoly's exact texture-region identity.  Keeping this
     * beside the decoded source image lets DXR bind the corresponding
     * preconverted artist PBR PNGs without matching pixels heuristically. */
    uint32_t source_map_offset;
    uint8_t minimum_u;
    uint8_t maximum_u;
    uint8_t minimum_v;
    uint8_t maximum_v;
    uint8_t glare;
} SourceVectorSceneMaterial;

typedef struct {
    SourceVectorSceneTriangle *triangles;
    size_t triangle_count;
    SourceVectorSceneMaterial *materials;
    size_t material_count;
} SourceVectorSceneMesh;

/* Compile the exact active ENT_NEXT_2 source model into projected NDC faces. */
int source_vector_scene_compile_view_weapon(
    const SceneSprite *sprite, float drawable_aspect,
    SourceVectorSceneMesh *out_mesh, char *error, size_t error_size);
/* Compile the same source pose into camera-local level units.  +X is camera
 * right, +Y is camera up, and +Z is camera forward.  Authored model geometry
 * retains the original renderer's one-quarter level-unit scale. Source-culled
 * faces remain zero-area slots so animation never changes a DXR BLAS layout.
 * Vertex source_light is neutral 1: the PBR/DXR consumer traces illumination
 * instead of applying the source directional flat/Gouraud response. */
int source_vector_scene_compile_view_weapon_camera(
    const SceneSprite *sprite, SourceVectorSceneMesh *out_mesh,
    char *error, size_t error_size);
int source_vector_scene_compile_world(
    const SceneSprite *sprite, const SceneCamera *camera,
    const RenderView *view, float drawable_aspect,
    SourceVectorSceneMesh *out_mesh, char *error, size_t error_size);
void source_vector_scene_mesh_destroy(SourceVectorSceneMesh *mesh);

#if defined(__cplusplus)
}
#endif

#endif
