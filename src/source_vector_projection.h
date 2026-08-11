#ifndef AB3D2_SOURCE_VECTOR_PROJECTION_H
#define AB3D2_SOURCE_VECTOR_PROJECTION_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

typedef struct {
    float x;
    float y;
    float z;
} SourceVectorEyePoint;

/* Exact fixed-point point transform used by the source view-weapon path. */
int source_vector_transform_view_weapon_point(
    const SceneViewWeaponProjection *projection,
    int16_t source_x, int16_t source_y, int16_t source_z,
    SourceVectorEyePoint *out_point);

int source_vector_configure_view_weapon_axis_correction(
    SceneViewWeaponProjection *projection, const uint8_t *source_bytes,
    size_t source_byte_count, uint16_t frame_index,
    char *error, size_t error_size);

/*
 * Perspective matrix for the source-authored weapon projection.  The source
 * vertical scale is retained while the horizontal scale is fitted to the
 * actual desktop aspect ratio, avoiding widescreen stretching.
 */
int source_vector_make_view_weapon_matrix(
    const SceneViewWeaponProjection *projection, float drawable_aspect,
    float out_matrix[16]);

#endif
