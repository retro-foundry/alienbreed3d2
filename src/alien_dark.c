#include "alien_dark.h"

#include <stdio.h>

enum {
    /* defs.i:ObjT_XPos_l's first word, used as d0 by ai_CheckForDark. */
    ALIEN_DARK_SLOT_POINT_INDEX = 0u
};

static void alien_dark_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_dark_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

int alien_dark_check(const ObjectRuntime *objects, uint32_t slot_index,
                     uint16_t player_zone_id, int16_t player_room_brightness,
                     GameRandom *random, int16_t *out_result,
                     char *error, size_t error_size)
{
    uint8_t *slot;
    int16_t random_threshold;

    if (!objects || !random || !out_result || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot)) {
        alien_dark_set_error(error, error_size,
                             "ai_CheckForDark received invalid source state");
        return 0;
    }
    if (alien_dark_read_be16(slot + ALIEN_DARK_SLOT_POINT_INDEX) == player_zone_id) {
        *out_result = -1;
        return 1;
    }
    random_threshold = (int16_t)(game_random_next(random) & 31u);
    *out_result = random_threshold >= player_room_brightness ? -1 : 0;
    return 1;
}
