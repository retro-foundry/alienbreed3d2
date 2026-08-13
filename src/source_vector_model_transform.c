#include "source_vector_model_transform.h"

#include <math.h>

static const float source_vector_model_pi = 3.14159265358979323846f;

void source_vector_model_world_offset(float source_x, float source_y, float source_z,
                                      uint16_t source_yaw,
                                      SourceVectorModelWorldOffset *out_offset)
{
    float yaw;
    float sine;
    float cosine;

    if (!out_offset) {
        return;
    }

    /*
     * Full-screen source scale:
     *
     * - RotateObjectPtsFullScreen supplies approximately x and 2*z/3,
     *   then draw_PolygonModel doubles its X fixed-point long and triples Z.
     * - A model point supplies x/y << 6 and (x/z product) >> 16.
     * - The common 5/3 projection therefore makes one authored model unit
     *   exactly one quarter of one level X/Y/Z unit.
     *
     * The source first evaluates `object - 2048 - view`, but its model
     * equations are x*sin-z*cos and z*sin+x*cos.  Removing the later camera
     * rotation yields a world rotation of pi-object, not the ordinary raw
     * object angle used by the old GPU path.  In particular, an authored -Z
     * facing points along the entity's `(sin(angle), cos(angle))` heading.
     */
    yaw = (float)((uint16_t)(UINT16_C(4096) - source_yaw) & UINT16_C(8191)) *
        (2.0f * source_vector_model_pi / 8192.0f);
    sine = sinf(yaw);
    cosine = cosf(yaw);
    source_x *= 0.25f;
    source_y *= -0.25f;
    source_z *= 0.25f;
    out_offset->x = cosine * source_x - sine * source_z;
    out_offset->y = source_y;
    out_offset->z = sine * source_x + cosine * source_z;
}
