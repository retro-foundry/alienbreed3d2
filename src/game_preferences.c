#include "game_preferences.h"

#include <stdio.h>
#include <string.h>

static void game_preferences_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint8_t *game_preferences_custom_option(GamePreferences *preferences,
                                                uint16_t option_index)
{
    if (!preferences) {
        return NULL;
    }
    switch (option_index) {
    case 0:
        return &preferences->original_mouse;
    case 1:
        return &preferences->always_run;
    case 2:
        return &preferences->show_messages;
    case 3:
        return &preferences->no_auto_aim;
    case 4:
        return &preferences->display_fps;
    case 5:
        return &preferences->show_weapon;
    case 6:
        return &preferences->play_music;
    default:
        return NULL;
    }
}

void game_preferences_default(GamePreferences *preferences)
{
    if (!preferences) {
        return;
    }
    /* controlloop.s:271-279 (0 is off; $ff is the source's true value). */
    memset(preferences, 0, sizeof(*preferences));
    preferences->show_messages = UINT8_MAX;
    preferences->play_music = UINT8_MAX;
    preferences->crosshair_colour = 1u;
}

int game_preferences_toggle_custom_option(GamePreferences *preferences, uint16_t option_index,
                                          char *error, size_t error_size)
{
    uint8_t *option = game_preferences_custom_option(preferences, option_index);

    if (!option) {
        game_preferences_set_error(error, error_size,
                                   "custom preference option is outside the source menu range");
        return 0;
    }
    /* controlloop.s:customOptions uses not.b on each implemented option. */
    *option = (uint8_t)~*option;
    return 1;
}

int game_preferences_custom_option_enabled(const GamePreferences *preferences,
                                           uint16_t option_index)
{
    if (!preferences) {
        return 0;
    }
    switch (option_index) {
    case 0:
        return preferences->original_mouse != 0u;
    case 1:
        return preferences->always_run != 0u;
    case 2:
        return preferences->show_messages != 0u;
    case 3:
        return preferences->no_auto_aim != 0u;
    case 4:
        return preferences->display_fps != 0u;
    case 5:
        return preferences->show_weapon != 0u;
    case 6:
        return preferences->play_music != 0u;
    default:
        return 0;
    }
}
