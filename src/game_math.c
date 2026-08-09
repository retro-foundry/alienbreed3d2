#include "game_math.h"

#include <stdio.h>
#include <string.h>

static int16_t game_math_read_be16s(const uint8_t *source)
{
    return (int16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void game_math_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int game_math_init(const AssetBlob *sine_table, GameMath *out_math,
                   char *error, size_t error_size)
{
    GameMath math;

    if (!sine_table || !sine_table->bytes || !out_math ||
        sine_table->size != GAME_MATH_SINE_TABLE_BYTES) {
        game_math_set_error(error, error_size,
                            "bigsine is not the source 8192-word table");
        return 0;
    }
    memset(&math, 0, sizeof(math));
    math.bytes = sine_table->bytes;
    math.size = sine_table->size;
    *out_math = math;
    return 1;
}

uint16_t game_math_wrap_angle_address(uint16_t angle_address)
{
    /* data/tables_data.s:SINTAB_MASK_ADR. */
    return (uint16_t)(angle_address & (GAME_MATH_SINE_CYCLE_BYTES - 2u));
}

int game_math_sine(const GameMath *math, uint16_t angle_address,
                   int16_t *out_value, char *error, size_t error_size)
{
    uint16_t offset;

    if (!math || !math->bytes || !out_value || math->size != GAME_MATH_SINE_TABLE_BYTES) {
        game_math_set_error(error, error_size, "source bigsine table is unavailable");
        return 0;
    }
    offset = game_math_wrap_angle_address(angle_address);
    *out_value = game_math_read_be16s(math->bytes + offset);
    return 1;
}

int game_math_cosine(const GameMath *math, uint16_t angle_address,
                     int16_t *out_value, char *error, size_t error_size)
{
    uint16_t offset;

    if (!math || !math->bytes || !out_value || math->size != GAME_MATH_SINE_TABLE_BYTES) {
        game_math_set_error(error, error_size, "source bigsine table is unavailable");
        return 0;
    }
    offset = (uint16_t)(game_math_wrap_angle_address(angle_address) +
                        GAME_MATH_COSINE_OFFSET_BYTES);
    *out_value = game_math_read_be16s(math->bytes + offset);
    return 1;
}
