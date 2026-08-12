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

/* Signed 16-bit source map address assembled by objdrawhires.s:drawpol. */
int16_t source_vector_texture_coordinate(uint8_t source_u, uint8_t source_v);

/* Exact fixed-point point transform used by the source view-weapon path. */
int source_vector_transform_view_weapon_point(
    const SceneViewWeaponProjection *projection,
    int16_t source_x, int16_t source_y, int16_t source_z,
    SourceVectorEyePoint *out_point);

/*
 * Presentation-only counterpart used between two valid source vector frames.
 * Endpoint rendering continues to use source_vector_transform_view_weapon_point
 * so the original 68000 fixed-point result remains authoritative at every
 * 50 Hz VBlank snapshot.
 */
int source_vector_transform_view_weapon_interpolated_point(
    const SceneViewWeaponProjection *projection,
    float source_x, float source_y, float source_z,
    SourceVectorEyePoint *out_point);

/*
 * Perspective matrix for the source-authored weapon projection.  The source
 * vertical scale is retained while the horizontal scale is fitted to the
 * actual desktop aspect ratio, avoiding widescreen stretching. `look_offset`
 * is the source STOPOFFSET: fullscreen `draw_PolygonModel` centres the weapon
 * at Vid_CentreY, which is the native source centre minus that offset.
 */
int source_vector_make_view_weapon_matrix(
    const SceneViewWeaponProjection *projection, float drawable_aspect, int16_t look_offset,
    float out_matrix[16]);

#endif
