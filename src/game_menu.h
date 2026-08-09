#ifndef AB3D2_GAME_MENU_H
#define AB3D2_GAME_MENU_H

#include <stddef.h>
#include <stdint.h>

#include "game_bootstrap.h"

typedef enum {
    GAME_MENU_SCREEN_MAIN,
    GAME_MENU_SCREEN_LEVEL_PAGE_ONE,
    GAME_MENU_SCREEN_LEVEL_PAGE_TWO,
    GAME_MENU_SCREEN_CUSTOM_OPTIONS,
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
    char status[160];
} GameMenu;

void game_menu_init(GameMenu *menu, const GameBootstrap *game);

/*
 * Executes one menu key action. The caller owns presentation and uses
 * out_should_quit to close the native process after source option 8 (EXIT).
 */
int game_menu_handle_input(GameMenu *menu, GameBootstrap *game, const char *data_root,
                           GameMenuInput input, int *out_should_quit,
                           char *error, size_t error_size);
const char *game_menu_status(const GameMenu *menu);

#endif
