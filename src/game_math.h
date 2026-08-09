#ifndef AB3D2_GAME_MATH_H
#define AB3D2_GAME_MATH_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"

/* data/tables_data.s: the 16-bit address-domain sine table constants. */
enum {
    GAME_MATH_SINE_CYCLE_BYTES = 4096 * 2,
    GAME_MATH_COSINE_OFFSET_BYTES = 2048,
    GAME_MATH_SINE_TABLE_BYTES = 2 * GAME_MATH_SINE_CYCLE_BYTES
};

typedef struct {
    const uint8_t *bytes;
    size_t size;
} GameMath;

int game_math_init(const AssetBlob *sine_table, GameMath *out_math,
                   char *error, size_t error_size);
/* data/tables_data.s:AMOD_A applied before indexed 16-bit table reads. */
uint16_t game_math_wrap_angle_address(uint16_t angle_address);
int game_math_sine(const GameMath *math, uint16_t angle_address,
                   int16_t *out_value, char *error, size_t error_size);
int game_math_cosine(const GameMath *math, uint16_t angle_address,
                     int16_t *out_value, char *error, size_t error_size);

#endif
