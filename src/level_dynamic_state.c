#include "level_dynamic_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void level_dynamic_state_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int level_dynamic_state_range_is_valid(uint32_t offset, size_t length, size_t total_size)
{
    return (size_t)offset <= total_size && length <= total_size - (size_t)offset;
}

void level_dynamic_state_destroy(LevelDynamicState *state)
{
    if (!state) {
        return;
    }
    free(state->level_bytes);
    free(state->graphics_bytes);
    memset(state, 0, sizeof(*state));
}

int level_dynamic_state_init(LevelDynamicState *out_state, const LevelRuntime *source,
                             char *error, size_t error_size)
{
    LevelDynamicState state = {0};

    if (!out_state || !source || !source->level_bytes || !source->graphics_bytes ||
        source->level_size == 0u || source->graphics_size == 0u) {
        level_dynamic_state_set_error(error, error_size,
                                      "dynamic level state received invalid source data");
        return 0;
    }
    state.level_bytes = malloc(source->level_size);
    state.graphics_bytes = malloc(source->graphics_size);
    if (!state.level_bytes || !state.graphics_bytes) {
        level_dynamic_state_destroy(&state);
        level_dynamic_state_set_error(error, error_size,
                                      "out of memory for mutable source level state");
        return 0;
    }
    memcpy(state.level_bytes, source->level_bytes, source->level_size);
    memcpy(state.graphics_bytes, source->graphics_bytes, source->graphics_size);
    state.runtime = *source;
    state.runtime.level_bytes = state.level_bytes;
    state.runtime.graphics_bytes = state.graphics_bytes;
    level_dynamic_state_destroy(out_state);
    *out_state = state;
    return 1;
}

int level_dynamic_state_get_level_range(LevelDynamicState *state, uint32_t offset,
                                        size_t length, uint8_t **out_bytes)
{
    if (!state || !state->level_bytes || !out_bytes ||
        !level_dynamic_state_range_is_valid(offset, length, state->runtime.level_size)) {
        return 0;
    }
    *out_bytes = state->level_bytes + offset;
    return 1;
}

int level_dynamic_state_get_graphics_range(LevelDynamicState *state, uint32_t offset,
                                           size_t length, uint8_t **out_bytes)
{
    if (!state || !state->graphics_bytes || !out_bytes ||
        !level_dynamic_state_range_is_valid(offset, length, state->runtime.graphics_size)) {
        return 0;
    }
    *out_bytes = state->graphics_bytes + offset;
    return 1;
}
