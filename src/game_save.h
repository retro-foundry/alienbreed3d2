#ifndef AB3D2_GAME_SAVE_H
#define AB3D2_GAME_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "game_session.h"

/* controlloop.s:game_LoadPosition/game_SavePosition use six fixed records. */
#define GAME_SAVE_SLOT_COUNT 6u
/* mnu_MYLOADMENU record zero is NEW GAME; save slots are records one through five. */
#define GAME_SAVE_USER_SLOT_COUNT 5u
#define GAME_SAVE_FILE_SIZE (GAME_SAVE_SLOT_COUNT * GAME_SESSION_RECORD_SIZE)

/* Raw source-format boot.dat payload. Unselected records are retained verbatim. */
typedef struct {
    uint8_t bytes[GAME_SAVE_FILE_SIZE];
} GameSaveSlots;

/*
 * Host persistence boundary for data/game_data.s:Game_SavedGamesName_vb.
 * This accepts only the complete, unversioned source payload; it neither
 * generates defaults nor migrates a native replacement format.
 */
int game_save_load_file(GameSaveSlots *slots, const char *path,
                        char *error, size_t error_size);
int game_save_write_file(const GameSaveSlots *slots, const char *path,
                         char *error, size_t error_size);

/* game_LoadPosition indexes all six records, including record zero (NEW GAME). */
int game_save_load_campaign_slot(const GameSaveSlots *slots, uint16_t slot_index,
                                 GameSession *session,
                                 char *error, size_t error_size);
/* game_SavePosition maps its five menu entries to source records one through five. */
int game_save_store_campaign_slot(GameSaveSlots *slots, uint16_t user_slot_index,
                                  const GameSession *session,
                                  char *error, size_t error_size);
/* Returns the raw source level word used by source menu-label construction. */
int game_save_slot_level_index(const GameSaveSlots *slots, uint16_t slot_index,
                               uint16_t *out_level_index,
                               char *error, size_t error_size);

#endif
