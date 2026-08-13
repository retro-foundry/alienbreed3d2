#ifndef AB3D2_SOURCE_VECTOR_MODEL_TRANSFORM_H
#define AB3D2_SOURCE_VECTOR_MODEL_TRANSFORM_H

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

#endif
