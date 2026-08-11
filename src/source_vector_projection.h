#ifndef AB3D2_SOURCE_VECTOR_PROJECTION_H
#define AB3D2_SOURCE_VECTOR_PROJECTION_H

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

/*
 * Perspective matrix for the source-authored weapon projection.  The source
 * vertical scale is retained while the horizontal scale is fitted to the
 * actual desktop aspect ratio, avoiding widescreen stretching.
 */
int source_vector_make_view_weapon_matrix(
    const SceneViewWeaponProjection *projection, float out_matrix[16]);

#endif
