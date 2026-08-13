#ifndef AB3D2_SOURCE_VECTOR_MODEL_TRANSFORM_H
#define AB3D2_SOURCE_VECTOR_MODEL_TRANSFORM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Renderer-neutral world offset for one point from
 * objdrawhires.s:draw_PolygonModel's full-screen vector path.  The source
 * model Y axis is down-positive; render worlds use up-positive Y.
 */
typedef struct {
    float x;
    float y;
    float z;
} SourceVectorModelWorldOffset;

/*
 * Reconstruct the authored model-to-world transform before camera rotation.
 * `source_yaw` is EntT_CurrentAngle_w in the source's 8192-byte angle domain.
 * Floating point inputs allow presentation-only interpolation between two
 * completed 50 Hz vector frames without changing source simulation state.
 */
void source_vector_model_world_offset(float source_x, float source_y, float source_z,
                                      uint16_t source_yaw,
                                      SourceVectorModelWorldOffset *out_offset);

/*
 * Resolve the authored vertical bounds of one compiled vector frame.  This
 * shares draw_PolygonModel's point-table layout but performs no rendering.
 */
int source_vector_model_frame_y_bounds(const uint8_t *bytes, size_t size,
                                       uint16_t frame_index,
                                       int16_t *out_minimum_y,
                                       int16_t *out_maximum_y);

/*
 * Return the down-positive 8.8 scene adjustment which brings an authored
 * alien SHOTYOFF no lower than the firing vector frame's visible top.
 */
int source_vector_model_projectile_y_adjustment(const uint8_t *bytes, size_t size,
                                                uint16_t frame_index,
                                                int32_t source_y_offset,
                                                int32_t *out_adjustment);

#endif
