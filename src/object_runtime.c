#include "object_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void object_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int object_runtime_source_offset_to_slot_index(const LevelRuntime *level_runtime,
                                                      uint32_t source_offset,
                                                      uint32_t *out_slot_index)
{
    uint32_t relative_offset;
    uint32_t slot_index;

    if (!level_runtime || !out_slot_index || source_offset < level_runtime->object_data_offset) {
        return 0;
    }
    relative_offset = source_offset - level_runtime->object_data_offset;
    if (relative_offset % OBJECT_RUNTIME_SLOT_BYTE_COUNT != 0u) {
        return 0;
    }
    slot_index = relative_offset / OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    if (slot_index >= level_runtime->object_record_count) {
        return 0;
    }
    *out_slot_index = slot_index;
    return 1;
}

void object_runtime_destroy(ObjectRuntime *runtime)
{
    if (!runtime) {
        return;
    }
    free(runtime->slot_bytes);
    free(runtime->point_bytes);
    memset(runtime, 0, sizeof(*runtime));
}

int object_runtime_init(ObjectRuntime *out_runtime, const LevelRuntime *level_runtime,
                        char *error, size_t error_size)
{
    ObjectRuntime runtime = {0};
    uint64_t slot_count;
    uint64_t slot_byte_count;
    uint64_t point_byte_count;
    uint32_t index;

    if (!out_runtime || !level_runtime || !level_runtime->level_bytes ||
        level_runtime->object_record_count == UINT32_MAX) {
        object_runtime_set_error(error, error_size, "object runtime received invalid level state");
        return 0;
    }
    slot_count = (uint64_t)level_runtime->object_record_count + 1u;
    slot_byte_count = slot_count * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    point_byte_count = (uint64_t)level_runtime->object_point_count *
        OBJECT_RUNTIME_POINT_BYTE_COUNT;
    if (slot_count > UINT32_MAX || slot_byte_count > SIZE_MAX ||
        point_byte_count > SIZE_MAX) {
        object_runtime_set_error(error, error_size, "source object runtime data is too large");
        return 0;
    }
    if (!object_runtime_source_offset_to_slot_index(level_runtime, level_runtime->player_shot_offset,
                                                    &runtime.player_shot_first_slot) ||
        runtime.player_shot_first_slot > level_runtime->object_record_count ||
        level_runtime->object_record_count - runtime.player_shot_first_slot <
            OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT ||
        !object_runtime_source_offset_to_slot_index(level_runtime, level_runtime->alien_shot_offset,
                                                    &runtime.alien_shot_first_slot) ||
        runtime.alien_shot_first_slot > level_runtime->object_record_count ||
        level_runtime->object_record_count - runtime.alien_shot_first_slot <
            OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT ||
        !object_runtime_source_offset_to_slot_index(level_runtime, level_runtime->player1_object_offset,
                                                    &runtime.player1_slot) ||
        !object_runtime_source_offset_to_slot_index(level_runtime, level_runtime->player2_object_offset,
                                                    &runtime.player2_slot)) {
        object_runtime_set_error(error, error_size,
                                 "Game_Begin object pool offset is outside the owned ObjT list");
        return 0;
    }
    runtime.slot_bytes = malloc((size_t)slot_byte_count);
    runtime.point_bytes = malloc((size_t)(point_byte_count == 0u ? 1u : point_byte_count));
    if (!runtime.slot_bytes || !runtime.point_bytes) {
        object_runtime_destroy(&runtime);
        object_runtime_set_error(error, error_size, "out of memory for source object runtime state");
        return 0;
    }
    for (index = 0u; index < (uint32_t)slot_count; ++index) {
        const uint8_t *source;

        if (!level_runtime_get_object_slot_bytes(level_runtime, index, &source,
                                                 error, error_size)) {
            object_runtime_destroy(&runtime);
            return 0;
        }
        memcpy(runtime.slot_bytes + (size_t)index * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
               source, OBJECT_RUNTIME_SLOT_BYTE_COUNT);
    }
    for (index = 0u; index < level_runtime->object_point_count; ++index) {
        const uint8_t *source;

        if (!level_runtime_get_object_point_bytes(level_runtime, index, &source,
                                                  error, error_size)) {
            object_runtime_destroy(&runtime);
            return 0;
        }
        memcpy(runtime.point_bytes + (size_t)index * OBJECT_RUNTIME_POINT_BYTE_COUNT,
               source, OBJECT_RUNTIME_POINT_BYTE_COUNT);
    }
    runtime.slot_count = (uint32_t)slot_count;
    runtime.active_slot_count = level_runtime->object_record_count;
    runtime.point_count = level_runtime->object_point_count;
    object_runtime_destroy(out_runtime);
    *out_runtime = runtime;
    return 1;
}

int object_runtime_get_slot_bytes(ObjectRuntime *runtime, uint32_t slot_index,
                                  uint8_t **out_bytes)
{
    if (!runtime || !runtime->slot_bytes || !out_bytes || slot_index >= runtime->slot_count) {
        return 0;
    }
    *out_bytes = runtime->slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    return 1;
}

int object_runtime_get_point_bytes(ObjectRuntime *runtime, uint32_t point_index,
                                   uint8_t **out_bytes)
{
    if (!runtime || !runtime->point_bytes || !out_bytes || point_index >= runtime->point_count) {
        return 0;
    }
    *out_bytes = runtime->point_bytes + (size_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
    return 1;
}

int object_runtime_get_player_shot_slot_bytes(ObjectRuntime *runtime, uint32_t shot_index,
                                              uint8_t **out_bytes)
{
    if (!runtime || shot_index >= OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT ||
        runtime->player_shot_first_slot > runtime->slot_count ||
        runtime->slot_count - runtime->player_shot_first_slot <= shot_index) {
        return 0;
    }
    return object_runtime_get_slot_bytes(runtime, runtime->player_shot_first_slot + shot_index,
                                         out_bytes);
}

int object_runtime_get_alien_shot_slot_bytes(ObjectRuntime *runtime, uint32_t shot_index,
                                             uint8_t **out_bytes)
{
    if (!runtime || shot_index >= OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT ||
        runtime->alien_shot_first_slot > runtime->slot_count ||
        runtime->slot_count - runtime->alien_shot_first_slot <= shot_index) {
        return 0;
    }
    return object_runtime_get_slot_bytes(runtime, runtime->alien_shot_first_slot + shot_index,
                                         out_bytes);
}

int object_runtime_get_player1_slot_bytes(ObjectRuntime *runtime, uint8_t **out_bytes)
{
    if (!runtime) {
        return 0;
    }
    return object_runtime_get_slot_bytes(runtime, runtime->player1_slot, out_bytes);
}

int object_runtime_get_player2_slot_bytes(ObjectRuntime *runtime, uint8_t **out_bytes)
{
    if (!runtime) {
        return 0;
    }
    return object_runtime_get_slot_bytes(runtime, runtime->player2_slot, out_bytes);
}
