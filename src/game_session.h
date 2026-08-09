#ifndef AB3D2_GAME_SESSION_H
#define AB3D2_GAME_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"

/* defs.i:InvCT and InvIT, represented as native values rather than disk bytes. */
typedef struct {
    uint16_t health;
    uint16_t jetpack_fuel;
    uint16_t ammunition[20];
    uint16_t shield;
    uint16_t jetpack;
    uint16_t weapons[10];
} GameInventory;

/* Single-player state from controlloop.s:DEFAULTGAME and game_DoneMenu. */
typedef struct {
    uint16_t menu_level_index;
    uint16_t active_level_index;
    uint8_t level_finished;
    GameInventory campaign_inventory;
    GameInventory player1_inventory;
} GameSession;

int game_session_default(GameSession *session, const GameLink *game_link,
                         char *error, size_t error_size);
int game_session_select_level(GameSession *session, uint16_t level_index,
                              char *error, size_t error_size);
void game_session_begin_single_player(GameSession *session);
void game_session_finish_single_player(GameSession *session, int level_finished);

#endif
