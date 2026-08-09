#ifndef AB3D2_GAME_PREFERENCES_H
#define AB3D2_GAME_PREFERENCES_H

#include <stddef.h>
#include <stdint.h>

/* The custom-options bytes from controlloop.s:Prefs_CustomOptionsBuffer_vb. */
typedef struct {
    uint8_t original_mouse;
    uint8_t always_run;
    uint8_t show_messages;
    uint8_t no_auto_aim;
    uint8_t display_fps;
    uint8_t show_weapon;
    uint8_t play_music;
    uint8_t crosshair_colour;
} GamePreferences;

enum {
    /* customOptions has seven implemented toggle entries (0 through 6). */
    GAME_PREFERENCES_CUSTOM_TOGGLE_COUNT = 7
};

void game_preferences_default(GamePreferences *preferences);
int game_preferences_toggle_custom_option(GamePreferences *preferences, uint16_t option_index,
                                          char *error, size_t error_size);
int game_preferences_custom_option_enabled(const GamePreferences *preferences,
                                           uint16_t option_index);

#endif
