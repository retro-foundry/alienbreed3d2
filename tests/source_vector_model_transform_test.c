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
    return 0;
}
