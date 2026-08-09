#ifndef AB3D2_LEVEL_DYNAMIC_STATE_H
#define AB3D2_LEVEL_DYNAMIC_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"

/*
 * Source-owned mutable level state. newanims.s changes ZoneT, EdgeT, and
 * twolev.graph.bin records in place while doors and lifts run; this clone
 * keeps that mutation out of staged media and the immutable scene bootstrap.
 * It intentionally performs no mechanism, PVS, rendering, or collision work.
 */
typedef struct {
    uint8_t *level_bytes;
    uint8_t *graphics_bytes;
    LevelRuntime runtime;
} LevelDynamicState;

int level_dynamic_state_init(LevelDynamicState *out_state, const LevelRuntime *source,
                             char *error, size_t error_size);
void level_dynamic_state_destroy(LevelDynamicState *state);

/* Bounds-checked mutable source-layout ranges for later routine translations. */
int level_dynamic_state_get_level_range(LevelDynamicState *state, uint32_t offset,
                                        size_t length, uint8_t **out_bytes);
int level_dynamic_state_get_graphics_range(LevelDynamicState *state, uint32_t offset,
                                           size_t length, uint8_t **out_bytes);

/* EdgeT_Flags_w is the source's per-edge collision/event signal word. */
int level_dynamic_state_get_edge_flags(const LevelDynamicState *state, uint32_t edge_index,
                                       uint16_t *out_flags);
int level_dynamic_state_set_edge_flags(LevelDynamicState *state, uint32_t edge_index,
                                       uint16_t flags);
int level_dynamic_state_or_edge_flags(LevelDynamicState *state, uint32_t edge_index,
                                      uint16_t flags);

#endif
