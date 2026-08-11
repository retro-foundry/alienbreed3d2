#include "source_vector_projection.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    SOURCE_VECTOR_START_OFFSET = 2u,
    SOURCE_VECTOR_POINTER_TABLE_OFFSET = 6u
};

static void source_vector_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t source_vector_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static int32_t source_vector_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t source_vector_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t source_vector_asr32(int32_t value, unsigned int shift)
{
    if (shift == 0u) {
        return value;
    }
    if (value >= 0) {
        return value >> shift;
    }
    return (int32_t)-((-(int64_t)value + (((int64_t)1 << shift) - 1)) >> shift);
}

int source_vector_transform_view_weapon_point(
    const SceneViewWeaponProjection *projection,
    int16_t source_x, int16_t source_y, int16_t source_z,
    SourceVectorEyePoint *out_point)
{
    int32_t x_product;
    int32_t z_product;
    int32_t source_view_x;
    int32_t source_view_y;
    int16_t rotated_source_view_z;
    int32_t source_view_z;

    if (!projection || !out_point || projection->depth_bias <= 0) {
        return 0;
    }

    /*
     * objdrawhires.s:rotate_object:
     *   x' = ASR.L((x * sin) - (z * cos), 9)
     *   y' = y << 6
     *   z' = high word of ((z * sin) + (x * cos))
     * Preserve the 68000 wrap and arithmetic-shift semantics before handing
     * the resulting eye-space point to the GPU.
     */
    x_product = source_vector_sub32(
        (int32_t)source_x * projection->sine,
        (int32_t)source_z * projection->cosine);
    source_view_x = source_vector_asr32(x_product, 9u);
    source_view_y = source_vector_add32(
        (int32_t)((uint32_t)(int32_t)source_y << 6u), projection->y_offset);
    z_product = source_vector_add32(
        (int32_t)source_z * projection->sine,
        (int32_t)source_x * projection->cosine);
    rotated_source_view_z = (int16_t)((uint32_t)z_product >> 16u);
    rotated_source_view_z = (int16_t)((uint16_t)rotated_source_view_z +
                                      (uint16_t)projection->depth_bias);
    source_view_z = rotated_source_view_z;
    if (projection->reverse_longitudinal_axis != 0u) {
        source_view_x = source_vector_sub32(0, source_view_x);
        source_view_z = source_vector_sub32(projection->depth_reflection_sum,
                                            source_view_z);
    }

    out_point->x = (float)source_view_x;
    /* Source screen Y grows down; OpenGL eye-space Y grows up. */
    out_point->y = -(float)source_view_y;
    /* The shared shader uses the conventional camera looking down -Z. */
    out_point->z = -(float)source_view_z;
    return 1;
}

int source_vector_configure_view_weapon_axis_correction(
    SceneViewWeaponProjection *projection, const uint8_t *source_bytes,
    size_t source_byte_count, uint16_t frame_index,
    char *error, size_t error_size)
{
    uint16_t point_count;
    uint16_t frame_count;
    size_t frame_pointer_offset;
    size_t frame_offset;
    size_t point_data_offset;
    int32_t minimum_depth = INT32_MAX;
    int32_t maximum_depth = INT32_MIN;

    if (!projection || !source_bytes || source_byte_count < 6u) {
        source_vector_set_error(error, error_size,
                                "view weapon axis correction has no vector model header");
        return 0;
    }
    point_count = source_vector_read_be16(source_bytes + 2u);
    frame_count = source_vector_read_be16(source_bytes + 4u);
    if (point_count == 0u || frame_count == 0u || frame_index >= frame_count ||
        (size_t)frame_count >
            (source_byte_count - SOURCE_VECTOR_POINTER_TABLE_OFFSET) / 4u) {
        source_vector_set_error(error, error_size,
                                "view weapon axis correction has an invalid vector header or frame");
        return 0;
    }
    frame_pointer_offset = SOURCE_VECTOR_POINTER_TABLE_OFFSET + (size_t)frame_index * 4u;
    frame_offset = SOURCE_VECTOR_START_OFFSET +
        source_vector_read_be16(source_bytes + frame_pointer_offset);
    if (frame_offset > source_byte_count || 4u > source_byte_count - frame_offset) {
        source_vector_set_error(error, error_size,
                                "view weapon axis correction frame is outside the vector asset");
        return 0;
    }
    point_data_offset = frame_offset + 4u + (size_t)point_count + (point_count & 1u);
    if (point_data_offset > source_byte_count ||
        (size_t)point_count > (source_byte_count - point_data_offset) / 6u) {
        source_vector_set_error(error, error_size,
                                "view weapon axis correction point table is malformed");
        return 0;
    }

    projection->reverse_longitudinal_axis = 0u;
    projection->depth_reflection_sum = 0;
    for (uint16_t point_index = 0u; point_index < point_count; ++point_index) {
        const uint8_t *point_bytes = source_bytes + point_data_offset +
            (size_t)point_index * 6u;
        SourceVectorEyePoint point;
        int32_t depth;

        if (!source_vector_transform_view_weapon_point(
                projection,
                (int16_t)source_vector_read_be16(point_bytes),
                (int16_t)source_vector_read_be16(point_bytes + 2u),
                (int16_t)source_vector_read_be16(point_bytes + 4u),
                &point)) {
            source_vector_set_error(error, error_size,
                                    "view weapon axis correction projection is invalid");
            return 0;
        }
        depth = (int32_t)-point.z;
        if (depth < minimum_depth) {
            minimum_depth = depth;
        }
        if (depth > maximum_depth) {
            maximum_depth = depth;
        }
    }
    projection->depth_reflection_sum = source_vector_add32(minimum_depth, maximum_depth);
    projection->reverse_longitudinal_axis = UINT8_MAX;
    return 1;
}

int source_vector_make_view_weapon_matrix(
    const SceneViewWeaponProjection *projection, float drawable_aspect,
    float out_matrix[16])
{
    const float near_plane = 0.5f;
    const float far_plane = 32767.0f;
    float vertical_scale;

    if (!projection || !out_matrix || drawable_aspect <= 0.0f ||
        projection->centre_x == 0u ||
        projection->centre_y == 0u || projection->scale_numerator == 0u ||
        projection->scale_denominator == 0u) {
        return 0;
    }
    vertical_scale = (float)projection->scale_numerator /
        ((float)projection->scale_denominator * (float)projection->centre_y);
    memset(out_matrix, 0, 16u * sizeof(*out_matrix));
    /*
     * fullscreen_conv's 160x120 centres describe a 4:3 source viewport.
     * Fitting that projection directly to wider NDC stretches X by the ratio
     * between 4:3 and the drawable.  Derive horizontal scale from the source
     * vertical scale and the real drawable aspect: at 4:3 this is exactly the
     * original 5/(3*160), while widescreen retains square weapon geometry.
     */
    out_matrix[0] = vertical_scale / drawable_aspect;
    out_matrix[5] = vertical_scale;
    out_matrix[10] = (far_plane + near_plane) / (near_plane - far_plane);
    out_matrix[11] = -1.0f;
    out_matrix[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    return 1;
}
