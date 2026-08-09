#include "game_controls.h"

#include <stdio.h>
#include <string.h>

static void game_controls_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

void game_controls_default(GameControls *controls)
{
    static const uint8_t source_defaults[GAME_CONTROL_PERSISTED_BYTE_COUNT] = {
        /* controlloop.s:AssignableKeys_vb; modules/rawkey_macros.i. */
        0x4fu, /* RAWKEY_LEFT */
        0x4eu, /* RAWKEY_RIGHT */
        0x11u, /* RAWKEY_W */
        0x21u, /* RAWKEY_S */
        0x63u, /* RAWKEY_CTRL */
        0x23u, /* RAWKEY_F */
        0x60u, /* RAWKEY_LSHIFT */
        0x64u, /* RAWKEY_LALT */
        0x20u, /* RAWKEY_A */
        0x22u, /* RAWKEY_D */
        0x33u, /* RAWKEY_C */
        0x28u, /* RAWKEY_L */
        0x40u, /* RAWKEY_SPACEBAR */
        0x0cu, /* RAWKEY_EQUAL */
        0x0bu, /* RAWKEY_UNDERSCORE */
        0x29u, /* RAWKEY_SEMICOLON */
        0x0du, /* RAWKEY_BSLASH */
        0x00u  /* spare_key */
    };

    if (controls) {
        memcpy(controls->assigned_raw_keys, source_defaults, sizeof(source_defaults));
    }
}

int game_controls_assign_raw_key(GameControls *controls, uint16_t binding_index,
                                 uint8_t raw_key, char *error, size_t error_size)
{
    if (!controls || binding_index >= GAME_CONTROL_BINDING_COUNT) {
        game_controls_set_error(error, error_size,
                                "control binding is outside the source menu range");
        return 0;
    }
    /* controlloop.s:CHANGECONTROLS copies mnu_getrawvalue directly into this byte. */
    controls->assigned_raw_keys[binding_index] = raw_key;
    return 1;
}

const char *game_controls_binding_name(uint16_t binding_index)
{
    static const char *const names[GAME_CONTROL_BINDING_COUNT] = {
        "TURN LEFT",
        "TURN RIGHT",
        "FORWARDS",
        "BACKWARDS",
        "FIRE",
        "OPERATE",
        "RUN",
        "FORCE S/S",
        "S/S LEFT",
        "S/S RIGHT",
        "CROUCH",
        "LOOK BEHIND",
        "JUMP",
        "LOOK UP",
        "LOOK DOWN",
        "CENTRE VIEW",
        "NEXT WEAPON"
    };

    return binding_index < GAME_CONTROL_BINDING_COUNT ? names[binding_index] : NULL;
}
