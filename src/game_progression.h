#ifndef AB3D2_GAME_PROGRESSION_H
#define AB3D2_GAME_PROGRESSION_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"

/*
 * Host-endian mutable subset of defs.i:GStatT and Game_ProgressSignal_l used
 * by macros.i:STATS_KILL. The remaining source persistence and achievement
 * records retain their owning c/game_progress.c routines.
 */
typedef struct {
    uint16_t alien_kills[GAME_LINK_ALIEN_COUNT];
    uint32_t signal;
} GameProgression;

void game_progression_init(GameProgression *progression);

/* macros.i:STATS_KILL, whose d0 input is EntT_Type_b. */
int game_progression_record_alien_kill(GameProgression *progression, uint8_t alien_type,
                                       char *error, size_t error_size);

#endif
