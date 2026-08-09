#ifndef AB3D2_GAME_MENU_H
#define AB3D2_GAME_MENU_H

#include <stddef.h>
#include <stdint.h>

#include "game_bootstrap.h"
#include "game_save.h"

#define GAME_MENU_SAVE_PATH_MAX 1024u

typedef enum {
    GAME_MENU_SCREEN_MAIN,
    GAME_MENU_SCREEN_LEVEL_PAGE_ONE,
    GAME_MENU_SCREEN_LEVEL_PAGE_TWO,
    GAME_MENU_SCREEN_CUSTOM_OPTIONS,
    GAME_MENU_SCREEN_CONTROLS_PAGE_ONE,
    GAME_MENU_SCREEN_CONTROLS_PAGE_TWO,
    GAME_MENU_SCREEN_CAPTURE_CONTROL,
    GAME_MENU_SCREEN_LOAD_POSITION,
    GAME_MENU_SCREEN_SAVE_POSITION,
    GAME_MENU_SCREEN_NOTICE,
    GAME_MENU_SCREEN_LEVEL_ACTIVE
} GameMenuScreen;

typedef enum {
    GAME_MENU_INPUT_UP,
    GAME_MENU_INPUT_DOWN,
    GAME_MENU_INPUT_ACTIVATE,
    GAME_MENU_INPUT_BACK
} GameMenuInput;

/* Native state equivalent to the single-player menu flow in controlloop.s. */
typedef struct {
    GameMenuScreen screen;
    uint16_t selection;
    uint16_t capture_binding_index;
    GameSaveSlots saved_games;
    char save_path[GAME_MENU_SAVE_PATH_MAX];
    char status[160];
} GameMenu;

/* save_path is an external, source-format boot.dat; staged media stays immutable. */
int game_menu_init(GameMenu *menu, const GameBootstrap *game, const char *save_path,
                   char *error, size_t error_size);

/*
 * Executes one menu key action. The caller owns presentation and uses
 * out_should_quit to close the native process after source option 8 (EXIT).
 */
int game_menu_handle_input(GameMenu *menu, GameBootstrap *game, const char *data_root,
                           GameMenuInput input, int *out_should_quit,
                           char *error, size_t error_size);
/* controlloop.s:CHANGECONTROLS receives an Amiga raw-key byte after selecting a row. */
int game_menu_capture_control_key(GameMenu *menu, GameBootstrap *game, uint8_t raw_key,
                                  char *error, size_t error_size);
const char *game_menu_status(const GameMenu *menu);

#endif
