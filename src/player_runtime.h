#ifndef AB3D2_PLAYER_RUNTIME_H
#define AB3D2_PLAYER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"

/* Single-player subset of modules/player.s:Plr_Initialise. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t height;
    uint16_t zone_index;
    uint16_t yaw;
    uint32_t default_enemy_flags;
} PlayerRuntime;

int player_runtime_init_single_player(const LevelBootstrap *level,
                                      const LevelRuntime *runtime,
                                      PlayerRuntime *out_player,
                                      char *error, size_t error_size);

#endif
