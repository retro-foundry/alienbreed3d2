#include "player_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    /* hires.s:55, consumed by modules/player.s:Plr_Initialise. */
    PLAYER_STANDING_HEIGHT = 12 * 1024
};

static void player_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int player_runtime_init_single_player(const LevelBootstrap *level,
                                      const LevelRuntime *runtime,
                                      PlayerRuntime *out_player,
                                      char *error, size_t error_size)
{
    LevelZone start_zone;
    PlayerRuntime player;

    if (!level || !runtime || !out_player || level->player1_start_zone >= runtime->zone_count) {
        player_runtime_set_error(error, error_size, "single-player start zone is invalid");
        return 0;
    }
    if (!level_runtime_get_zone(runtime, level->player1_start_zone, &start_zone,
                                error, error_size)) {
        return 0;
    }

    /* modules/player.s:Plr_Initialise player-one branch. */
    memset(&player, 0, sizeof(player));
    player.zone_index = level->player1_start_zone;
    player.x = level->player1_start_x;
    player.z = level->player1_start_z;
    player.height = PLAYER_STANDING_HEIGHT;
    player.y = start_zone.floor - player.height;
    player.default_enemy_flags = 0x23u; /* %100011 in Plr_Initialise. */
    *out_player = player;
    return 1;
}
