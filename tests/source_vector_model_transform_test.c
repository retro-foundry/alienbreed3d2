#include <math.h>
#include <stdio.h>

#include "source_vector_model_transform.h"

static int close_enough(float actual, float expected)
{
    return fabsf(actual - expected) <= 0.0001f;
}

static int expect_offset(float source_x, float source_y, float source_z,
                         uint16_t yaw, float expected_x, float expected_y,
                         float expected_z, const char *description)
{
    SourceVectorModelWorldOffset offset;

    source_vector_model_world_offset(source_x, source_y, source_z, yaw, &offset);
    if (!close_enough(offset.x, expected_x) ||
        !close_enough(offset.y, expected_y) ||
        !close_enough(offset.z, expected_z)) {
        fprintf(stderr,
                "%s is inconsistent: got (%f, %f, %f), expected (%f, %f, %f)\n",
                description, offset.x, offset.y, offset.z,
                expected_x, expected_y, expected_z);
        return 0;
    }
    return 1;
}

int main(void)
{
    SourceVectorModelWorldOffset forward;
    static const uint8_t bounds_model[] = {
        0x00u, 0x00u, /* unsorted */
        0x00u, 0x02u, /* two points */
        0x00u, 0x01u, /* one frame */
        0x00u, 0x08u, 0x00u, 0x00u, /* frame/angle pointers relative to +2 */
        0x00u, 0x00u, 0x00u, 0x01u, /* on/off mask */
        0x00u, 0x00u,             /* two point-angle bytes */
        0x00u, 0x01u, 0xffu, 0xf4u, 0x00u, 0x02u, /* y=-12 */
        0x00u, 0x03u, 0x00u, 0x22u, 0x00u, 0x04u  /* y=34 */
    };
    int16_t minimum_y;
    int16_t maximum_y;
    int32_t adjustment;

    /* One authored unit has the same quarter-unit scale on every world axis. */
    if (!expect_offset(4.0f, 0.0f, 0.0f, 4096u, 1.0f, 0.0f, 0.0f,
                       "full-screen vector X scale") ||
        !expect_offset(0.0f, 4.0f, 0.0f, 0u, 0.0f, -1.0f, 0.0f,
                       "full-screen vector Y scale") ||
        !expect_offset(0.0f, 0.0f, 4.0f, 4096u, 0.0f, 0.0f, 1.0f,
                       "full-screen vector Z scale")) {
        return 1;
    }

    /* The authored -Z face must follow HeadTowardsAng's entity heading. */
    if (!expect_offset(0.0f, 0.0f, -4.0f, 0u, 0.0f, 0.0f, 1.0f,
                       "yaw-zero model forward") ||
        !expect_offset(0.0f, 0.0f, -4.0f, 2048u, 1.0f, 0.0f, 0.0f,
                       "quarter-turn model forward") ||
        !expect_offset(0.0f, 0.0f, -4.0f, 4096u, 0.0f, 0.0f, -1.0f,
                       "half-turn model forward") ||
        !expect_offset(0.0f, 0.0f, -4.0f, 6144u, -1.0f, 0.0f, 0.0f,
                       "three-quarter-turn model forward")) {
        return 1;
    }

    /* Non-cardinal source angles remain smooth for presentation interpolation. */
    source_vector_model_world_offset(0.0f, 0.0f, -4.0f, 1024u, &forward);
    if (!close_enough(forward.x, 0.7071068f) ||
        !close_enough(forward.z, 0.7071068f)) {
        fprintf(stderr, "intermediate vector facing is inconsistent: (%f, %f)\n",
                forward.x, forward.z);
        return 1;
    }

    if (!source_vector_model_frame_y_bounds(
            bounds_model, sizeof(bounds_model), 0u, &minimum_y, &maximum_y) ||
        minimum_y != -12 || maximum_y != 34 ||
        !source_vector_model_projectile_y_adjustment(
            bounds_model, sizeof(bounds_model), 0u, -1024, &adjustment) ||
        adjustment != 640 ||
        !source_vector_model_projectile_y_adjustment(
            bounds_model, sizeof(bounds_model), 0u, -128, &adjustment) ||
        adjustment != 0 ||
        source_vector_model_frame_y_bounds(
            bounds_model, sizeof(bounds_model), 1u, &minimum_y, &maximum_y)) {
        fprintf(stderr, "vector frame bounds/projectile attachment is inconsistent\n");
        return 1;
    }
    return 0;
}
