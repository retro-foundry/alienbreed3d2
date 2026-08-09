#include "player_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    /* hires.s:55, consumed by modules/player.s:Plr_Initialise. */
    PLAYER_STANDING_HEIGHT = 12 * 1024,
    /* hires.s:56, consumed by modules/player.s:plr_KeyboardControl. */
    PLAYER_CROUCH_HEIGHT = 8 * 1024,
    PLAYER_STANDING_CLEARANCE = PLAYER_STANDING_HEIGHT + 3 * 1024
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
    player.snap_x = level->player1_start_x;
    player.snap_z = level->player1_start_z;
    player.height = PLAYER_STANDING_HEIGHT;
    player.snap_height = PLAYER_STANDING_HEIGHT;
    player.snap_target_height = PLAYER_STANDING_HEIGHT;
    player.snap_squished_height = PLAYER_STANDING_HEIGHT;
    player.y = start_zone.floor - player.height;
    player.snap_y = player.y;
    player.snap_target_y = player.y;
    player.default_enemy_flags = 0x23u; /* %100011 in Plr_Initialise. */
    *out_player = player;
    return 1;
}

int player_runtime_update_discrete_controls(PlayerRuntime *player, GameInput *input,
                                            const GameControls *controls,
                                            const LevelRuntime *runtime,
                                            char *error, size_t error_size)
{
    LevelZone zone;
    int fire_down;
    int32_t available_height;
    int32_t target_height;

    if (!player || !input || !controls || !runtime ||
        player->zone_index >= runtime->zone_count) {
        player_runtime_set_error(error, error_size,
                                 "discrete player control received invalid source state");
        return 0;
    }

    /* modules/player.s: plr_PrevUseKeyState_b gates one Used_b pulse per press. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_OPERATE) &&
        player->previous_use_key_state == 0u) {
        player->used = UINT8_MAX;
    }
    player->previous_use_key_state =
        game_input_is_control_down(input, controls, GAME_CONTROL_OPERATE) ? UINT8_MAX : 0u;

    /* The source consumes duck_key itself before toggling Ducked_b with not.b. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_CROUCH)) {
        uint8_t crouch_raw_key = controls->assigned_raw_keys[GAME_CONTROL_CROUCH];

        if (!game_input_set_raw_key(input, crouch_raw_key, 0, error, error_size)) {
            return 0;
        }
        player->snap_target_height = PLAYER_STANDING_HEIGHT;
        player->ducked = (uint8_t)~player->ducked;
        if (player->ducked != 0u) {
            player->snap_target_height = PLAYER_CROUCH_HEIGHT;
        }
    }

    if (!level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }
    /* modules/player.s selects the current lower or upper zone vertical span. */
    available_height = player->stood_in_top != 0u ?
        (int32_t)((uint32_t)zone.upper_floor - (uint32_t)zone.upper_roof) :
        (int32_t)((uint32_t)zone.floor - (uint32_t)zone.roof);
    player->squished = 0u;
    player->snap_squished_height = PLAYER_STANDING_HEIGHT;
    if (available_height <= PLAYER_STANDING_CLEARANCE) {
        player->squished = UINT8_MAX;
        player->snap_squished_height = PLAYER_CROUCH_HEIGHT;
    }
    target_height = player->snap_target_height;
    if (target_height > player->snap_squished_height) {
        target_height = player->snap_squished_height;
    }
    if (player->snap_height < target_height) {
        player->snap_height += 1024;
    } else if (player->snap_height > target_height) {
        player->snap_height -= 1024;
    }

    /* The final plr_KeyboardControl branch latches Fire_b and Clicked_b. */
    fire_down = game_input_is_control_down(input, controls, GAME_CONTROL_FIRE);
    if (player->fire != 0u) {
        player->fire = fire_down ? UINT8_MAX : 0u;
    } else if (fire_down) {
        player->clicked = UINT8_MAX;
        player->fire = UINT8_MAX;
    }
    return 1;
}
