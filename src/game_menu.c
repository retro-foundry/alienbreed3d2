#include "game_menu.h"

#include <stdio.h>
#include <string.h>

enum {
    /* menu/menunb.s:mnu_MYMAINMENU and both level menus have nine rows. */
    GAME_MENU_MAIN_ITEM_COUNT = 9,
    GAME_MENU_LEVEL_ITEM_COUNT = 9,
    GAME_MENU_LEVELS_PER_PAGE = 8
};

static void game_menu_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void game_menu_set_status(GameMenu *menu, const char *status)
{
    (void)snprintf(menu->status, sizeof(menu->status), "%s", status);
}

static uint16_t game_menu_item_count(const GameMenu *menu)
{
    if (menu->screen == GAME_MENU_SCREEN_MAIN) {
        return GAME_MENU_MAIN_ITEM_COUNT;
    }
    if (menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_ONE ||
        menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_TWO) {
        return GAME_MENU_LEVEL_ITEM_COUNT;
    }
    return 0;
}

static void game_menu_update_status(GameMenu *menu, const GameBootstrap *game)
{
    static const char *const main_option_names[GAME_MENU_MAIN_ITEM_COUNT] = {
        "PLAY GAME",
        "master/slave multiplayer",
        "SELECT LEVEL",
        "CONTROL OPTIONS",
        "GAME CREDITS",
        "LOAD POSITION",
        "SAVE POSITION",
        "CUSTOM OPTIONS",
        "EXIT"
    };
    /* defs.i:GLFT_LevelNames contains fixed 40-byte labels. */
    char level_name[41];
    uint16_t level_index;

    if (!menu) {
        return;
    }
    switch (menu->screen) {
    case GAME_MENU_SCREEN_MAIN:
        if (game && game_link_copy_level_name(&game->game_link_catalog,
                                              game->session.menu_level_index,
                                              level_name, sizeof(level_name),
                                              NULL, 0)) {
            (void)snprintf(menu->status, sizeof(menu->status),
                           "Main menu %u/9: %s | current %.20s",
                           (unsigned int)menu->selection + 1u,
                           main_option_names[menu->selection], level_name);
        } else {
            (void)snprintf(menu->status, sizeof(menu->status),
                           "Main menu %u/9: %s", (unsigned int)menu->selection + 1u,
                           main_option_names[menu->selection]);
        }
        break;
    case GAME_MENU_SCREEN_LEVEL_PAGE_ONE:
    case GAME_MENU_SCREEN_LEVEL_PAGE_TWO:
        if (menu->selection == GAME_MENU_LEVELS_PER_PAGE) {
            game_menu_set_status(menu,
                                 menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_ONE ?
                                     "Level selection: NEXT PAGE" :
                                     "Level selection: MAIN MENU");
            break;
        }
        level_index = (uint16_t)(menu->selection +
            (menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_TWO ?
                 GAME_MENU_LEVELS_PER_PAGE : 0u));
        if (game && game_link_copy_level_name(&game->game_link_catalog, level_index,
                                              level_name, sizeof(level_name), NULL, 0)) {
            (void)snprintf(menu->status, sizeof(menu->status),
                           "Level selection %u/9: %.20s", (unsigned int)menu->selection + 1u,
                           level_name);
        } else {
            (void)snprintf(menu->status, sizeof(menu->status),
                           "Level selection %u/9", (unsigned int)menu->selection + 1u);
        }
        break;
    case GAME_MENU_SCREEN_NOTICE:
        break;
    case GAME_MENU_SCREEN_LEVEL_ACTIVE:
        game_menu_set_status(menu,
                             "Source single-player level is loaded; GPU renderer pending");
        break;
    }
}

static void game_menu_move_selection(GameMenu *menu, int direction, const GameBootstrap *game)
{
    uint16_t item_count = game_menu_item_count(menu);

    if (item_count == 0) {
        return;
    }
    if (direction < 0) {
        menu->selection = menu->selection == 0 ? (uint16_t)(item_count - 1u) :
            (uint16_t)(menu->selection - 1u);
    } else {
        menu->selection = (uint16_t)((menu->selection + 1u) % item_count);
    }
    /* menu/menunb.s:mnu_waitmenu derives the row modulo mnu_items. */
    game_menu_update_status(menu, game);
}

void game_menu_init(GameMenu *menu, const GameBootstrap *game)
{
    if (!menu) {
        return;
    }
    memset(menu, 0, sizeof(*menu));
    menu->screen = GAME_MENU_SCREEN_MAIN;
    game_menu_update_status(menu, game);
}

static int game_menu_activate_level_definition(GameMenu *menu, GameBootstrap *game,
                                               const char *data_root, uint16_t level_index,
                                               char *error, size_t error_size)
{
    /* controlloop.s:levelMenu/levelMenu2 call DEFGAME before reopening main. */
    if (!game_bootstrap_load_level_definition(game, data_root, level_index,
                                              error, error_size)) {
        return 0;
    }
    /*
     * levelMenu/levelMenu2 wrap DEFGAME in SAVEREGS/GETREGS, then write the
     * original selected d0 back to Game_LevelCounter_w (page two adds eight).
     * DEFGAME therefore supplies inventory, while the selected menu index
     * remains the level the user chose even when deflev.dat is absent.
     */
    if (!game_session_select_level(&game->session, level_index, error, error_size)) {
        return 0;
    }
    menu->screen = GAME_MENU_SCREEN_MAIN;
    menu->selection = 0;
    game_menu_update_status(menu, game);
    return 1;
}

int game_menu_handle_input(GameMenu *menu, GameBootstrap *game, const char *data_root,
                           GameMenuInput input, int *out_should_quit,
                           char *error, size_t error_size)
{
    if (out_should_quit) {
        *out_should_quit = 0;
    }
    if (!menu || !game || !data_root || !out_should_quit) {
        game_menu_set_error(error, error_size, "menu input received null state or data root");
        return 0;
    }
    if (input == GAME_MENU_INPUT_UP) {
        game_menu_move_selection(menu, -1, game);
        return 1;
    }
    if (input == GAME_MENU_INPUT_DOWN) {
        game_menu_move_selection(menu, 1, game);
        return 1;
    }
    if (input == GAME_MENU_INPUT_BACK) {
        if (menu->screen == GAME_MENU_SCREEN_NOTICE) {
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
        }
        /* game_ReadMainMenu ignores mnu_waitmenu's -1 return on the main page. */
        return 1;
    }
    if (input != GAME_MENU_INPUT_ACTIVATE) {
        game_menu_set_error(error, error_size, "menu received an unknown input");
        return 0;
    }

    if (menu->screen == GAME_MENU_SCREEN_MAIN) {
        switch (menu->selection) {
        case 0:
            /* game_ReadMainMenu:playgame followed by game_DoneMenu. */
            if (!game_bootstrap_start_selected_single_player(game, data_root,
                                                             error, error_size)) {
                return 0;
            }
            menu->screen = GAME_MENU_SCREEN_LEVEL_ACTIVE;
            game_menu_update_status(menu, game);
            return 1;
        case 1:
            /* User-specified scope excludes controlloop.s:game_MasterMenu. */
            menu->screen = GAME_MENU_SCREEN_NOTICE;
            game_menu_set_status(menu,
                                 "Master/slave multiplayer is intentionally not part of this PC port");
            return 1;
        case 2:
            menu->screen = GAME_MENU_SCREEN_LEVEL_PAGE_ONE;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        case 3:
            menu->screen = GAME_MENU_SCREEN_NOTICE;
            game_menu_set_status(menu,
                                 "Control options await source preference/input porting");
            return 1;
        case 4:
            /* controlloop.s leaves the mnu_viewcredz call commented out. */
            game_menu_set_status(menu,
                                 "Game credits are inactive in the maintained source menu");
            return 1;
        case 5:
        case 6:
            menu->screen = GAME_MENU_SCREEN_NOTICE;
            game_menu_set_status(menu,
                                 "Load/save position awaits source-compatible host boot.dat storage");
            return 1;
        case 7:
            menu->screen = GAME_MENU_SCREEN_NOTICE;
            game_menu_set_status(menu,
                                 "Custom options await source preference porting");
            return 1;
        case 8:
            /* controlloop.s:game_ReadMainMenu option 8 sets Game_ShouldQuit_b. */
            *out_should_quit = 1;
            return 1;
        }
    }
    if (menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_ONE) {
        if (menu->selection == GAME_MENU_LEVELS_PER_PAGE) {
            menu->screen = GAME_MENU_SCREEN_LEVEL_PAGE_TWO;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        return game_menu_activate_level_definition(menu, game, data_root, menu->selection,
                                                   error, error_size);
    }
    if (menu->screen == GAME_MENU_SCREEN_LEVEL_PAGE_TWO) {
        if (menu->selection == GAME_MENU_LEVELS_PER_PAGE) {
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        return game_menu_activate_level_definition(
            menu, game, data_root,
            (uint16_t)(menu->selection + GAME_MENU_LEVELS_PER_PAGE), error, error_size);
    }
    return 1;
}

const char *game_menu_status(const GameMenu *menu)
{
    return menu ? menu->status : "";
}
