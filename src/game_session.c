#include "game_session.h"

#include <stdio.h>
#include <string.h>

static void game_session_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t game_session_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

int game_session_default(GameSession *session, const GameLink *game_link,
                         char *error, size_t error_size)
{
    const uint8_t *shoot_definitions;
    size_t shoot_definitions_size;
    uint16_t initial_ammunition_type;

    if (!session || !game_link ||
        !game_link_table(game_link, GAME_LINK_TABLE_SHOOT_DEFINITIONS,
                         &shoot_definitions, &shoot_definitions_size) ||
        shoot_definitions_size < 2) {
        game_session_set_error(error, error_size, "could not read the initial GLFT shoot definition");
        return 0;
    }

    /* controlloop.s:DEFAULTGAME clears these contiguous InvCT/InvIT fields. */
    memset(session, 0, sizeof(*session));
    initial_ammunition_type = game_session_read_be16(shoot_definitions);
    if (initial_ammunition_type >= sizeof(session->campaign_inventory.ammunition) /
                                     sizeof(session->campaign_inventory.ammunition[0])) {
        game_session_set_error(error, error_size,
                               "initial GLFT shoot definition names an invalid ammunition type");
        return 0;
    }

    /* controlloop.s:DEFAULTGAME. */
    session->menu_level_index = 0;
    session->campaign_inventory.health = 200;
    session->campaign_inventory.weapons[0] = 0x00ff;
    session->campaign_inventory.ammunition[initial_ammunition_type] = 20;
    return 1;
}

int game_session_select_level(GameSession *session, uint16_t level_index,
                              char *error, size_t error_size)
{
    if (!session || level_index >= GAME_LINK_LEVEL_COUNT) {
        game_session_set_error(error, error_size,
                               "single-player level selection is outside the 16-level campaign");
        return 0;
    }
    /* game_ReadMainMenu:playgame copies Game_LevelCounter_w to Game_LevelNumber_w. */
    session->menu_level_index = level_index;
    return 1;
}

void game_session_begin_single_player(GameSession *session)
{
    if (!session) {
        return;
    }
    /* game_DoneMenu copies Plr_ inventory into Plr1 before Game_Begin. */
    session->player1_inventory = session->campaign_inventory;
    session->active_level_index = session->menu_level_index;
    session->level_finished = 0;
}

void game_session_finish_single_player(GameSession *session, int level_finished)
{
    if (!session) {
        return;
    }
    session->level_finished = level_finished ? 1u : 0u;
    /* game_DoneMenu only copies Plr1 inventory back when Game_FinishedLevel_b is set. */
    if (session->level_finished) {
        session->campaign_inventory = session->player1_inventory;
    }
}
