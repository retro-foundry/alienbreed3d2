#include "source_vector_model_transform.h"

#include <limits.h>
#include <math.h>

static const float source_vector_model_pi = 3.14159265358979323846f;

static uint16_t source_vector_model_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8u) | source[1]);
}

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

int source_vector_model_frame_y_bounds(const uint8_t *bytes, size_t size,
                                       uint16_t frame_index,
                                       int16_t *out_minimum_y,
                                       int16_t *out_maximum_y)
{
    const size_t pointer_table_offset = 6u;
    uint16_t point_count;
    uint16_t frame_count;
    size_t frame_pointer_offset;
    size_t frame_offset;
    size_t point_data_offset;
    int16_t minimum_y = INT16_MAX;
    int16_t maximum_y = INT16_MIN;

    if (!bytes || !out_minimum_y || !out_maximum_y || size < pointer_table_offset) {
        return 0;
    }
    point_count = source_vector_model_read_be16(bytes + 2u);
    frame_count = source_vector_model_read_be16(bytes + 4u);
    if (point_count == 0u || frame_count == 0u || frame_index >= frame_count ||
        (size_t)frame_count > (size - pointer_table_offset) / 4u) {
        return 0;
    }
    frame_pointer_offset = pointer_table_offset + (size_t)frame_index * 4u;
    frame_offset = 2u + source_vector_model_read_be16(bytes + frame_pointer_offset);
    if (frame_offset > size || 4u > size - frame_offset) {
        return 0;
    }
    point_data_offset = frame_offset + 4u + (size_t)point_count + (point_count & 1u);
    if (point_data_offset > size ||
        (size_t)point_count > (size - point_data_offset) / 6u) {
        return 0;
    }
    for (uint16_t point_index = 0u; point_index < point_count; ++point_index) {
        int16_t y = (int16_t)source_vector_model_read_be16(
            bytes + point_data_offset + (size_t)point_index * 6u + 2u);

        if (y < minimum_y) {
            minimum_y = y;
        }
        if (y > maximum_y) {
            maximum_y = y;
        }
    }
    *out_minimum_y = minimum_y;
    *out_maximum_y = maximum_y;
    return 1;
}

int source_vector_model_projectile_y_adjustment(const uint8_t *bytes, size_t size,
                                                uint16_t frame_index,
                                                int32_t source_y_offset,
                                                int32_t *out_adjustment)
{
    int16_t minimum_y;
    int16_t maximum_y;
    int32_t model_top_y;

    if (!out_adjustment ||
        !source_vector_model_frame_y_bounds(bytes, size, frame_index,
                                            &minimum_y, &maximum_y)) {
        return 0;
    }
    (void)maximum_y;
    /* One source model Y unit is 1/4 level unit, or 32 in world 8.8. */
    model_top_y = (int32_t)minimum_y * 32;
    *out_adjustment = source_y_offset < model_top_y ?
        model_top_y - source_y_offset : 0;
    return 1;
}
