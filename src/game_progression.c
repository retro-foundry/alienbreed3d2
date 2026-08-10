#include "game_progression.h"

#include <stdio.h>
#include <string.h>

enum {
    /* defs.i:GAME_EVENTBIT_KILL. */
    GAME_PROGRESSION_EVENTBIT_KILL = 0u
};

static void game_progression_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

void game_progression_init(GameProgression *progression)
{
    if (progression) {
        memset(progression, 0, sizeof(*progression));
    }
}

int game_progression_record_alien_kill(GameProgression *progression, uint8_t alien_type,
                                       char *error, size_t error_size)
{
    if (!progression || alien_type >= GAME_LINK_ALIEN_COUNT) {
        game_progression_set_error(error, error_size,
                                  "STATS_KILL alien type is outside the source catalog");
        return 0;
    }

    /* add.w #1,(game_PlayerProgression+GStatT_AlienKills_vw,d0.w*2). */
    progression->alien_kills[alien_type] =
        (uint16_t)(progression->alien_kills[alien_type] + 1u);
    /* move.l #1,Game_ProgressSignal_l; SET_MEM_BIT GAME_EVENTBIT_KILL,... */
    progression->signal = UINT32_C(1) | (UINT32_C(1) << GAME_PROGRESSION_EVENTBIT_KILL);
    return 1;
}
