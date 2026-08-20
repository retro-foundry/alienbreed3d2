#ifndef AB3D2_SOURCE_BITMAP_SPRITE_H
#define AB3D2_SOURCE_BITMAP_SPRITE_H

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
    uint8_t additive;
} SourceBitmapSpriteImage;

typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
} SourceBitmapSceneVertex;

/*
 * Renderer-neutral world billboard compiled from objdrawhires.s:draw_Bitmap.
 * The six-vertex list is retained even when sector clipping hides the frame;
 * hidden sprites collapse to a degenerate quad so a dynamic BLAS keeps the
 * same topology while the source object moves or animates.
 */
typedef struct {
    SourceBitmapSceneVertex vertices[6];
    uint32_t material_mode;
    uint8_t visible;
    uint8_t additive;
} SourceBitmapSceneMesh;

/*
 * Decode objdrawhires.s's bitmap, lighted-bitmap, additive, and glare paths
 * without depending on a graphics API.  The returned pixels retain the
 * source display palette's sRGB encoding and transparent texel zero.
 */
int source_bitmap_sprite_decode(const SceneSprite *sprite,
                                const SceneCamera *camera,
                                SourceBitmapSpriteImage *out_image,
                                char *error, size_t error_size);
void source_bitmap_sprite_image_destroy(SourceBitmapSpriteImage *image);

int source_bitmap_scene_compile_world(const SceneSprite *sprite,
                                      const SceneCamera *camera,
                                      SourceBitmapSceneMesh *out_mesh,
                                      char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
