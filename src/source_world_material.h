#ifndef AB3D2_SOURCE_WORLD_MATERIAL_H
#define AB3D2_SOURCE_WORLD_MATERIAL_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *rgba;
    uint16_t width;
    uint16_t height;
} SourceWorldMaterialImage;

/* Decode a wall window or 64x64 flat to its brightest authored true-colour
 * row. The returned pixels are renderer-independent RGBA8 sRGB data. */
int source_world_material_decode(const SceneMaterial *material,
                                 const SceneGeometry *geometry,
                                 SourceWorldMaterialImage *out_image,
                                 char *error, size_t error_size);
void source_world_material_image_destroy(SourceWorldMaterialImage *image);

#ifdef __cplusplus
}
#endif

#endif
