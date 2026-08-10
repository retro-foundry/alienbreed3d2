#include "alien_run_around.h"

#include <stdio.h>

static void alien_run_around_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int16_t alien_run_around_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int16_t alien_run_around_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t alien_run_around_neg16(int16_t value)
{
    return (int16_t)(UINT16_C(0) - (uint16_t)value);
}

static int16_t alien_run_around_asr16_1(int16_t value)
{
    if (value >= 0) {
        return (int16_t)((uint16_t)value >> 1u);
    }
    return (int16_t)-(((int32_t)-value + 1) >> 1u);
}

static int32_t alien_run_around_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

int alien_run_around_apply(AlienRunAroundState *state, char *error, size_t error_size)
{
    int16_t x_delta;
    int16_t z_delta;
    int32_t player_relative_x;

    if (!state) {
        alien_run_around_set_error(error, error_size,
                                   "RunAround received no source workspace");
        return 0;
    }
    x_delta = alien_run_around_asr16_1(
        alien_run_around_sub16(state->old_x, state->new_x));
    z_delta = alien_run_around_asr16_1(
        alien_run_around_sub16(state->old_z, state->new_z));
    player_relative_x = alien_run_around_sub32(
        (int32_t)alien_run_around_sub16(state->object_x, state->player_temporary_x) *
            state->player_cosine,
        (int32_t)alien_run_around_sub16(state->object_z, state->player_temporary_z) *
            state->player_sine);
    if (player_relative_x >= 0) {
        x_delta = alien_run_around_neg16(x_delta);
        z_delta = alien_run_around_neg16(z_delta);
    }
    state->new_x = alien_run_around_sub16(state->new_x, z_delta);
    state->new_z = alien_run_around_add16(state->new_z, x_delta);
    return 1;
}
