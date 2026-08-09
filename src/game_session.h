#ifndef AB3D2_GAME_SESSION_H
#define AB3D2_GAME_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"

#define GAME_SESSION_AMMUNITION_COUNT GAME_INVENTORY_AMMUNITION_COUNT
#define GAME_SESSION_WEAPON_COUNT GAME_INVENTORY_WEAPON_COUNT
/* defs.i:InvCT_SizeOf_l + InvIT_SizeOf_l (44 + 24 bytes). */
#define GAME_SESSION_INVENTORY_DISK_SIZE 68u
/* controlloop.s:DEFGAME first writes Game_LevelCounter_w, then the inventory. */
#define GAME_SESSION_RECORD_SIZE (2u + GAME_SESSION_INVENTORY_DISK_SIZE)

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

/*
 * controlloop.s:DEFGAME and game_LoadPosition use the same 70-byte Amiga
 * record: level counter, 11 longs of InvCT, then 6 longs of InvIT. These
 * helpers retain that big-endian disk layout for source definitions and save
 * slots without coupling session state to a host file path.
 */
int game_session_decode_campaign_record(GameSession *session, const uint8_t *bytes,
                                        size_t size, char *error, size_t error_size);
int game_session_encode_campaign_record(const GameSession *session, uint8_t *out_bytes,
                                        size_t out_size, char *error, size_t error_size);
/* DEFGAME loads the definition when present; an absent one runs DEFAULTGAME. */
int game_session_load_level_definition(GameSession *session, const GameLink *game_link,
                                       const uint8_t *definition_bytes, size_t definition_size,
                                       int definition_found, char *error, size_t error_size);

#endif
