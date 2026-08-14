#ifndef AB3D2_SOURCE_BITMAP_SPRITE_H
#define AB3D2_SOURCE_BITMAP_SPRITE_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

typedef struct {
    uint8_t *rgba;
    uint16_t width;
    uint16_t height;
    uint8_t additive;
} SourceBitmapSpriteImage;

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

#endif
