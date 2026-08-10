#include "game_menu.h"

#include <stdio.h>
#include <string.h>

enum {
    /* menu/menunb.s:mnu_MYMAINMENU and both level menus have nine rows. */
    GAME_MENU_MAIN_ITEM_COUNT = 9,
    GAME_MENU_LEVEL_ITEM_COUNT = 9,
    GAME_MENU_LEVELS_PER_PAGE = 8,
    /* controlloop.s:CHANGECONTROLS loops 0..10, then row 11 is MORE. */
    GAME_MENU_CONTROLS_PAGE_ONE_ITEM_COUNT = 12,
    /* CHANGECONTROLS2 loops 0..5, then row 6 returns to the main menu. */
    GAME_MENU_CONTROLS_PAGE_TWO_ITEM_COUNT = 7,
    GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT = 11
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
    if (menu->screen == GAME_MENU_SCREEN_CUSTOM_OPTIONS) {
        return GAME_MENU_MAIN_ITEM_COUNT;
    }
    if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_ONE) {
        return GAME_MENU_CONTROLS_PAGE_ONE_ITEM_COUNT;
    }
    if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_TWO) {
        return GAME_MENU_CONTROLS_PAGE_TWO_ITEM_COUNT;
    }
    if (menu->screen == GAME_MENU_SCREEN_LOAD_POSITION) {
        /* mnu_MYLOADMENU: NEW GAME, five stored positions, CANCEL. */
        return GAME_SAVE_SLOT_COUNT + 1u;
    }
    if (menu->screen == GAME_MENU_SCREEN_SAVE_POSITION) {
        /* mnu_MYSAVEMENU: five stored positions, CANCEL. */
        return GAME_SAVE_USER_SLOT_COUNT + 1u;
    }
    return 0;
}

static void game_menu_update_saved_slot_status(GameMenu *menu, const GameBootstrap *game,
                                               uint16_t source_slot_index,
                                               const char *operation)
{
    char level_name[41];
    uint16_t level_index;
    char save_error[96];

    if (!game_save_slot_level_index(&menu->saved_games, source_slot_index, &level_index,
                                    save_error, sizeof(save_error))) {
        game_menu_set_status(menu, save_error);
        return;
    }
    if (game && level_index < GAME_LINK_LEVEL_COUNT &&
        game_link_copy_level_name(&game->game_link_catalog, level_index, level_name,
                                  sizeof(level_name), NULL, 0)) {
        (void)snprintf(menu->status, sizeof(menu->status),
                       "%s position %u/%u: %.20s", operation,
                       (unsigned int)source_slot_index,
                       GAME_SAVE_USER_SLOT_COUNT, level_name);
        return;
    }
    (void)snprintf(menu->status, sizeof(menu->status),
                   "%s position %u/%u: source level %u is outside A-P",
                   operation, (unsigned int)source_slot_index,
                   GAME_SAVE_USER_SLOT_COUNT, level_index);
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
    static const char *const custom_option_names[GAME_PREFERENCES_CUSTOM_TOGGLE_COUNT] = {
        "ORIGINAL MOUSE",
        "ALWAYS RUN",
        "SHOW MESSAGES",
        "NO AUTO AIM",
        "SHOW FPS",
        "HIDE WEAPON",
        "PLAY MUSIC"
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
    case GAME_MENU_SCREEN_CUSTOM_OPTIONS:
        if (menu->selection < GAME_PREFERENCES_CUSTOM_TOGGLE_COUNT) {
            (void)snprintf(menu->status, sizeof(menu->status),
                           "Custom options %u/9: %s %c",
                           (unsigned int)menu->selection + 1u,
                           custom_option_names[menu->selection],
                           game && game_preferences_custom_option_enabled(
                                       &game->preferences, menu->selection) ? 'Y' : 'N');
        } else if (menu->selection == 7u) {
            game_menu_set_status(menu, "Custom options 8/9: OPTION 8 (inactive in source)");
        } else {
            game_menu_set_status(menu, "Custom options 9/9: MAIN MENU");
        }
        break;
    case GAME_MENU_SCREEN_CONTROLS_PAGE_ONE:
    case GAME_MENU_SCREEN_CONTROLS_PAGE_TWO:
        if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_ONE &&
            menu->selection == GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT) {
            game_menu_set_status(menu, "Control options 12/12: MORE");
            break;
        }
        if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_TWO && menu->selection == 6u) {
            game_menu_set_status(menu, "Control options 7/7: MAIN MENU");
            break;
        }
        level_index = (uint16_t)(menu->selection +
            (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_TWO ?
                GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT : 0u));
        (void)snprintf(menu->status, sizeof(menu->status),
                       "Control options %u/%u: %s raw $%02X",
                       (unsigned int)menu->selection + 1u,
                       menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_ONE ?
                           GAME_MENU_CONTROLS_PAGE_ONE_ITEM_COUNT :
                           GAME_MENU_CONTROLS_PAGE_TWO_ITEM_COUNT,
                       game_controls_binding_name(level_index),
                       game ? game->controls.assigned_raw_keys[level_index] : 0u);
        break;
    case GAME_MENU_SCREEN_CAPTURE_CONTROL:
        (void)snprintf(menu->status, sizeof(menu->status),
                       "Press a key for %s", game_controls_binding_name(menu->capture_binding_index));
        break;
    case GAME_MENU_SCREEN_LOAD_POSITION:
        if (menu->selection == 0u) {
            game_menu_set_status(menu, "Load position 0/5: NEW GAME");
        } else if (menu->selection == GAME_SAVE_SLOT_COUNT) {
            game_menu_set_status(menu, "Load position: CANCEL");
        } else {
            game_menu_update_saved_slot_status(menu, game, menu->selection, "Load");
        }
        break;
    case GAME_MENU_SCREEN_SAVE_POSITION:
        if (menu->selection == GAME_SAVE_USER_SLOT_COUNT) {
            game_menu_set_status(menu, "Save position: CANCEL");
        } else {
            game_menu_update_saved_slot_status(menu, game,
                                               (uint16_t)(menu->selection + 1u), "Save");
        }
        break;
    case GAME_MENU_SCREEN_NOTICE:
        break;
    case GAME_MENU_SCREEN_LEVEL_ACTIVE:
        game_menu_set_status(menu, "Source single-player level is loaded");
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

int game_menu_init(GameMenu *menu, const GameBootstrap *game, const char *save_path,
                   char *error, size_t error_size)
{
    int written;

    if (!menu || !save_path || !save_path[0]) {
        game_menu_set_error(error, error_size, "menu initialization received null state or save path");
        return 0;
    }
    memset(menu, 0, sizeof(*menu));
    written = snprintf(menu->save_path, sizeof(menu->save_path), "%s", save_path);
    if (written < 0 || (size_t)written >= sizeof(menu->save_path)) {
        game_menu_set_error(error, error_size, "source boot.dat path is too long");
        return 0;
    }
    menu->screen = GAME_MENU_SCREEN_MAIN;
    game_menu_update_status(menu, game);
    return 1;
}

static int game_menu_open_saved_positions(GameMenu *menu, GameBootstrap *game,
                                          GameMenuScreen screen,
                                          char *error, size_t error_size)
{
    if (!game_save_load_file(&menu->saved_games, menu->save_path, error, error_size)) {
        return 0;
    }
    menu->screen = screen;
    menu->selection = 0;
    game_menu_update_status(menu, game);
    return 1;
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
            menu->screen = GAME_MENU_SCREEN_CONTROLS_PAGE_ONE;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        case 4:
            /* controlloop.s leaves the mnu_viewcredz call commented out. */
            game_menu_set_status(menu,
                                 "Game credits are inactive in the maintained source menu");
            return 1;
        case 5:
            /* controlloop.s:game_LoadPosition loads all six source records first. */
            return game_menu_open_saved_positions(menu, game, GAME_MENU_SCREEN_LOAD_POSITION,
                                                  error, error_size);
        case 6:
            /* controlloop.s:game_SavePosition loads, edits, then rewrites the same payload. */
            return game_menu_open_saved_positions(menu, game, GAME_MENU_SCREEN_SAVE_POSITION,
                                                  error, error_size);
        case 7:
            menu->screen = GAME_MENU_SCREEN_CUSTOM_OPTIONS;
            menu->selection = 0;
            game_menu_update_status(menu, game);
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
    if (menu->screen == GAME_MENU_SCREEN_CUSTOM_OPTIONS) {
        if (menu->selection < GAME_PREFERENCES_CUSTOM_TOGGLE_COUNT) {
            if (!game_preferences_toggle_custom_option(&game->preferences, menu->selection,
                                                       error, error_size)) {
                return 0;
            }
            game_menu_update_status(menu, game);
            return 1;
        }
        if (menu->selection == 8u) {
            /* controlloop.s:customOptionsDone returns to game_ReadMainMenu. */
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
        }
        /* Source menu option 7 has no action. */
        return 1;
    }
    if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_ONE) {
        if (menu->selection == GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT) {
            menu->screen = GAME_MENU_SCREEN_CONTROLS_PAGE_TWO;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        menu->capture_binding_index = menu->selection;
        menu->screen = GAME_MENU_SCREEN_CAPTURE_CONTROL;
        game_menu_update_status(menu, game);
        return 1;
    }
    if (menu->screen == GAME_MENU_SCREEN_CONTROLS_PAGE_TWO) {
        if (menu->selection == 6u) {
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        menu->capture_binding_index = (uint16_t)(menu->selection +
            GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT);
        menu->screen = GAME_MENU_SCREEN_CAPTURE_CONTROL;
        game_menu_update_status(menu, game);
        return 1;
    }
    if (menu->screen == GAME_MENU_SCREEN_LOAD_POSITION) {
        if (menu->selection == GAME_SAVE_SLOT_COUNT) {
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        /* game_LoadPosition selects record zero for NEW GAME and one through five otherwise. */
        if (!game_save_load_campaign_slot(&menu->saved_games, menu->selection,
                                          &game->session, error, error_size)) {
            return 0;
        }
        menu->screen = GAME_MENU_SCREEN_MAIN;
        menu->selection = 0;
        game_menu_update_status(menu, game);
        return 1;
    }
    if (menu->screen == GAME_MENU_SCREEN_SAVE_POSITION) {
        if (menu->selection == GAME_SAVE_USER_SLOT_COUNT) {
            menu->screen = GAME_MENU_SCREEN_MAIN;
            menu->selection = 0;
            game_menu_update_status(menu, game);
            return 1;
        }
        if (!game_save_store_campaign_slot(&menu->saved_games, menu->selection,
                                           &game->session, error, error_size) ||
            !game_save_write_file(&menu->saved_games, menu->save_path,
                                  error, error_size)) {
            return 0;
        }
        menu->screen = GAME_MENU_SCREEN_MAIN;
        menu->selection = 0;
        game_menu_update_status(menu, game);
        return 1;
    }
    return 1;
}

int game_menu_capture_control_key(GameMenu *menu, GameBootstrap *game, uint8_t raw_key,
                                  char *error, size_t error_size)
{
    uint16_t binding_index;

    if (!menu || !game || menu->screen != GAME_MENU_SCREEN_CAPTURE_CONTROL) {
        game_menu_set_error(error, error_size, "control key capture is not active");
        return 0;
    }
    binding_index = menu->capture_binding_index;
    if (!game_controls_assign_raw_key(&game->controls, binding_index, raw_key,
                                      error, error_size)) {
        return 0;
    }
    if (binding_index < GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT) {
        menu->screen = GAME_MENU_SCREEN_CONTROLS_PAGE_ONE;
        menu->selection = binding_index;
    } else {
        menu->screen = GAME_MENU_SCREEN_CONTROLS_PAGE_TWO;
        menu->selection = (uint16_t)(binding_index - GAME_MENU_CONTROLS_PAGE_ONE_BINDING_COUNT);
    }
    game_menu_update_status(menu, game);
    return 1;
}

const char *game_menu_status(const GameMenu *menu)
{
    return menu ? menu->status : "";
}
