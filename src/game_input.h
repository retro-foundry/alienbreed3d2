#ifndef AB3D2_GAME_INPUT_H
#define AB3D2_GAME_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "game_controls.h"

/* bss/tables_bss.s:KeyMap_vb; key_interrupt only indexes raw codes 0..127. */
enum {
    GAME_INPUT_KEY_MAP_SIZE = 256,
    GAME_INPUT_RAW_KEY_LIMIT = 128
};

typedef struct {
    uint8_t key_map[GAME_INPUT_KEY_MAP_SIZE];
    uint8_t last_pressed_raw_key;
    /* c/system.c:Sys_ReadMouse's signed-word Sys_MouseY accumulator. */
    int16_t mouse_y;
    /* c/system.c:Sys_ReadMouse's horizontal counter difference for one tick. */
    int16_t pending_mouse_x;
    /* modules/player.s:plr_MouseControl's global Sys_OldMouseY word. */
    int16_t old_mouse_y;
} GameInput;

void game_input_init(GameInput *input);
int game_input_set_raw_key(GameInput *input, uint8_t raw_key, int is_pressed,
                           char *error, size_t error_size);
int game_input_is_raw_key_down(const GameInput *input, uint8_t raw_key);
int game_input_is_control_down(const GameInput *input, const GameControls *controls,
                               uint16_t binding_index);
uint8_t game_input_take_last_pressed(GameInput *input);

/*
 * SDL supplies relative host motion; retain c/system.c:Sys_ReadMouse's
 * word-wide accumulation at the platform boundary.  Horizontal motion is
 * consumed once by the next source player-control tick; vertical Sys_MouseY
 * persists until modules/player.s derives its delta from old_mouse_y.
 */
void game_input_add_mouse_motion(GameInput *input, int32_t delta_x, int32_t delta_y);
int16_t game_input_take_mouse_x(GameInput *input);

#endif
