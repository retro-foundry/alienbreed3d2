#include "game_input.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static void game_input_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

void game_input_init(GameInput *input)
{
    if (input) {
        memset(input, 0, sizeof(*input));
    }
}

int game_input_set_raw_key(GameInput *input, uint8_t raw_key, int is_pressed,
                           char *error, size_t error_size)
{
    if (!input || raw_key >= GAME_INPUT_RAW_KEY_LIMIT) {
        game_input_set_error(error, error_size,
                             "source raw key is outside key_interrupt's 0..127 range");
        return 0;
    }
    /* hires.s:key_interrupt writes $ff on key-down and clears on key-up. */
    input->key_map[raw_key] = is_pressed ? UINT8_MAX : 0u;
    if (is_pressed) {
        /* hires.s:key_readkey returns this byte once, then clears it. */
        input->last_pressed_raw_key = raw_key;
    }
    return 1;
}

int game_input_is_raw_key_down(const GameInput *input, uint8_t raw_key)
{
    return input && raw_key < GAME_INPUT_RAW_KEY_LIMIT && input->key_map[raw_key] != 0u;
}

int game_input_is_control_down(const GameInput *input, const GameControls *controls,
                               uint16_t binding_index)
{
    if (!controls || binding_index >= GAME_CONTROL_BINDING_COUNT) {
        return 0;
    }
    return game_input_is_raw_key_down(input, controls->assigned_raw_keys[binding_index]);
}

uint8_t game_input_take_last_pressed(GameInput *input)
{
    uint8_t raw_key;

    if (!input) {
        return 0u;
    }
    raw_key = input->last_pressed_raw_key;
    input->last_pressed_raw_key = 0u;
    return raw_key;
}

void game_input_add_mouse_motion(GameInput *input, int32_t delta_x, int32_t delta_y)
{
    if (!input) {
        return;
    }
    /* Sys_ReadMouse's counters and Sys_MouseY are both consumed as WORDs. */
    input->pending_mouse_x = (int16_t)((uint16_t)input->pending_mouse_x +
                                       (uint16_t)delta_x);
    input->mouse_y = (int16_t)((uint16_t)input->mouse_y + (uint16_t)delta_y);
}

int16_t game_input_scale_present_mouse_delta(int32_t delta, int32_t reference_extent,
                                             int32_t present_extent, int32_t *remainder)
{
    int64_t scaled;
    int64_t reference_delta;

    if (!remainder || reference_extent <= 0 || present_extent <= 0) {
        return 0;
    }
    /*
     * Alien-Breed-3D-I:player_mouse_delta_to_reference converts SDL relative
     * motion back into its reference render extent. Keep the signed remainder
     * so high-resolution one-pixel events are not discarded.
     */
    scaled = (int64_t)delta * reference_extent + *remainder;
    reference_delta = scaled / present_extent;
    *remainder = (int32_t)(scaled % present_extent);
    if (reference_delta > INT16_MAX) {
        return INT16_MAX;
    }
    if (reference_delta < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)reference_delta;
}

int16_t game_input_peek_mouse_x(const GameInput *input)
{
    return input ? input->pending_mouse_x : 0;
}

int16_t game_input_take_mouse_x(GameInput *input)
{
    int16_t delta_x;

    if (!input) {
        return 0;
    }
    delta_x = input->pending_mouse_x;
    input->pending_mouse_x = 0;
    return delta_x;
}
