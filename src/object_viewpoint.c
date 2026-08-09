#include "object_viewpoint.h"

#include <stdio.h>

static void object_viewpoint_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int object_viewpoint_select_frame(const GameMath *math, uint16_t current_angle,
                                  uint16_t viewer_angle, uint8_t *out_frame,
                                  char *error, size_t error_size)
{
    uint16_t relative_angle;
    int16_t sine;
    int16_t cosine;
    int32_t negated_cosine;

    if (!math || !out_frame) {
        object_viewpoint_set_error(error, error_size,
                                   "ViewpointToDraw received invalid source state");
        return 0;
    }
    relative_angle = game_math_wrap_angle_address((uint16_t)(current_angle - viewer_angle));
    if (!game_math_sine(math, relative_angle, &sine, error, error_size) ||
        !game_math_cosine(math, relative_angle, &cosine, error, error_size)) {
        return 0;
    }

    /* newaliencontrol.s:ViewpointToDraw's signed long comparisons. */
    negated_cosine = -(int32_t)cosine;
    if (negated_cosine > 0) {
        if (sine > 0) {
            *out_frame = sine > negated_cosine ? 1u : 0u;
        } else {
            *out_frame = -(int32_t)sine > negated_cosine ? 3u : 0u;
        }
    } else if (sine > 0) {
        *out_frame = sine > -negated_cosine ? 1u : 2u;
    } else {
        *out_frame = negated_cosine > sine ? 3u : 2u;
    }
    return 1;
}
