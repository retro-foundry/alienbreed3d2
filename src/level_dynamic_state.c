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

static uint16_t level_dynamic_state_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void level_dynamic_state_write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static int level_dynamic_state_get_edge_flag_bytes(const LevelDynamicState *state,
                                                   uint32_t edge_index,
                                                   uint8_t **out_bytes)
{
    uint64_t offset;

    if (!state || !out_bytes) {
        return 0;
    }
    /* EdgeT is 16 bytes and EdgeT_Flags_w is its final source word. */
    offset = (uint64_t)state->runtime.edge_table_offset + (uint64_t)edge_index * 16u + 14u;
    if (offset > UINT32_MAX) {
        return 0;
    }
    return level_dynamic_state_get_level_range((LevelDynamicState *)state, (uint32_t)offset,
                                               2u, out_bytes);
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

int level_dynamic_state_get_edge_flags(const LevelDynamicState *state, uint32_t edge_index,
                                       uint16_t *out_flags)
{
    uint8_t *flag_bytes;

    if (!out_flags || !level_dynamic_state_get_edge_flag_bytes(state, edge_index, &flag_bytes)) {
        return 0;
    }
    *out_flags = level_dynamic_state_read_be16(flag_bytes);
    return 1;
}

int level_dynamic_state_or_edge_flags(LevelDynamicState *state, uint32_t edge_index,
                                      uint16_t flags)
{
    uint8_t *flag_bytes;
    uint16_t current_flags;

    if (!level_dynamic_state_get_edge_flag_bytes(state, edge_index, &flag_bytes)) {
        return 0;
    }
    current_flags = level_dynamic_state_read_be16(flag_bytes);
    level_dynamic_state_write_be16(flag_bytes, (uint16_t)(current_flags | flags));
    return 1;
}

int level_dynamic_state_set_edge_flags(LevelDynamicState *state, uint32_t edge_index,
                                       uint16_t flags)
{
    uint8_t *flag_bytes;

    if (!level_dynamic_state_get_edge_flag_bytes(state, edge_index, &flag_bytes)) {
        return 0;
    }
    level_dynamic_state_write_be16(flag_bytes, flags);
    return 1;
}
