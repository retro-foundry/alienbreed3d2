#include "game_random.h"

enum {
    /* objectmove.s:Rand1 dc.w 234 and GetRand's add.w immediate. */
    GAME_RANDOM_INITIAL_STATE = 234u,
    GAME_RANDOM_INCREMENT = 0x2343u
};

void game_random_init(GameRandom *random)
{
    if (random) {
        random->state = GAME_RANDOM_INITIAL_STATE;
    }
}

uint16_t game_random_next(GameRandom *random)
{
    uint16_t value;

    if (!random) {
        return 0u;
    }
    /* move.w Rand1,d0 / rol.w #3,d0 / add.w #$2343,d0 / move.w d0,Rand1. */
    value = random->state;
    value = (uint16_t)((uint16_t)(value << 3) | (value >> 13));
    value = (uint16_t)(value + GAME_RANDOM_INCREMENT);
    random->state = value;
    return value;
}
