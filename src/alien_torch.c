#include "alien_torch.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* defs.i: ObjT/EntT fields read by modules/ai.s:ai_DoTorch. */
    ALIEN_TORCH_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_TORCH_SLOT_ZONE_ID = 12u,
    ALIEN_TORCH_SLOT_CURRENT_ANGLE = 30u
};

static void alien_torch_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_torch_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_torch_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

int alien_torch_apply(LightingRuntime *lighting, const LevelRuntime *level,
                      const GameMath *math, const ObjectRuntime *objects,
                      uint32_t slot_index, const AlienSetup *setup,
                      int16_t new_x, int16_t new_z,
                      char *error, size_t error_size)
{
    uint8_t *slot;
    int16_t zone_index;
    int16_t vertical_position;

    if (!setup) {
        alien_torch_set_error(error, error_size, "ai_DoTorch has no ItsAnAlien setup");
        return 0;
    }
    /* modules/ai.s:ai_DoTorch returns before touching a0 when ALIENBRIGHT is non-negative. */
    if (setup->brightness >= 0) {
        return 1;
    }
    if (!lighting || !level || !math || !objects || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot)) {
        alien_torch_set_error(error, error_size, "ai_DoTorch received invalid source state");
        return 0;
    }
    zone_index = alien_torch_word_from_u16(
        alien_torch_read_be16(slot + ALIEN_TORCH_SLOT_ZONE_ID));
    if (zone_index < 0 || (uint16_t)zone_index >= level->zone_count) {
        alien_torch_set_error(error, error_size, "ai_DoTorch object zone is outside the source level");
        return 0;
    }
    vertical_position = alien_torch_word_from_u16(
        alien_torch_read_be16(slot + ALIEN_TORCH_SLOT_VERTICAL_POSITION));

    /* `move.w 4(a0),d3` / `ext.l d3` / `asl.l #7,d3` then Anim_BrightenPointsAngle. */
    return lighting_runtime_brighten_points_angle(
        lighting, level, math, setup->brightness, new_x, new_z,
        (int32_t)vertical_position * 128, (uint16_t)zone_index,
        alien_torch_read_be16(slot + ALIEN_TORCH_SLOT_CURRENT_ANGLE), error, error_size);
}
