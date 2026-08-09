#ifndef AB3D2_GAME_CONTROLS_H
#define AB3D2_GAME_CONTROLS_H

#include <stddef.h>
#include <stdint.h>

/* controlloop.s:AssignableKeys_vb, including its trailing spare byte. */
enum {
    GAME_CONTROL_BINDING_COUNT = 17,
    GAME_CONTROL_PERSISTED_BYTE_COUNT = 18
};

typedef enum {
    GAME_CONTROL_TURN_LEFT = 0,
    GAME_CONTROL_TURN_RIGHT,
    GAME_CONTROL_FORWARDS,
    GAME_CONTROL_BACKWARDS,
    GAME_CONTROL_FIRE,
    GAME_CONTROL_OPERATE,
    GAME_CONTROL_RUN,
    GAME_CONTROL_FORCE_SIDESTEP,
    GAME_CONTROL_SIDESTEP_LEFT,
    GAME_CONTROL_SIDESTEP_RIGHT,
    GAME_CONTROL_CROUCH,
    GAME_CONTROL_LOOK_BEHIND,
    GAME_CONTROL_JUMP,
    GAME_CONTROL_LOOK_UP,
    GAME_CONTROL_LOOK_DOWN,
    GAME_CONTROL_CENTRE_VIEW,
    GAME_CONTROL_NEXT_WEAPON
} GameControlBinding;

typedef struct {
    uint8_t assigned_raw_keys[GAME_CONTROL_PERSISTED_BYTE_COUNT];
} GameControls;

void game_controls_default(GameControls *controls);
int game_controls_assign_raw_key(GameControls *controls, uint16_t binding_index,
                                 uint8_t raw_key, char *error, size_t error_size);
const char *game_controls_binding_name(uint16_t binding_index);

#endif
