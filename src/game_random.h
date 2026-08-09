#ifndef AB3D2_GAME_RANDOM_H
#define AB3D2_GAME_RANDOM_H

#include <stdint.h>

/* objectmove.s:GetRand / Rand1. */
typedef struct {
    uint16_t state;
} GameRandom;

void game_random_init(GameRandom *random);
uint16_t game_random_next(GameRandom *random);

#endif
