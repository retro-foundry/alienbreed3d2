#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "game_bootstrap.h"
#include "renderer_stub.h"

static int make_default_data_root(char *out_root, size_t out_root_size)
{
    char *base_path = SDL_GetBasePath();
    int written;

    if (!base_path) {
        return 0;
    }
    written = snprintf(out_root, out_root_size, "%sdata", base_path);
    SDL_free(base_path);
    return written >= 0 && (size_t)written < out_root_size;
}

/*
 * modules/rawkey_macros.i uses these Amiga raw-key values. SDL scancodes are
 * physical keys too, so this boundary keeps source bindings out of simulation
 * and maps the host only while the source-style controls menu is capturing.
 */
static int raw_key_from_scancode(SDL_Scancode scancode, uint8_t *out_raw_key)
{
    uint8_t raw_key;

    if (!out_raw_key) {
        return 0;
    }
    switch (scancode) {
    case SDL_SCANCODE_GRAVE: raw_key = 0x00u; break;
    case SDL_SCANCODE_1: raw_key = 0x01u; break;
    case SDL_SCANCODE_2: raw_key = 0x02u; break;
    case SDL_SCANCODE_3: raw_key = 0x03u; break;
    case SDL_SCANCODE_4: raw_key = 0x04u; break;
    case SDL_SCANCODE_5: raw_key = 0x05u; break;
    case SDL_SCANCODE_6: raw_key = 0x06u; break;
    case SDL_SCANCODE_7: raw_key = 0x07u; break;
    case SDL_SCANCODE_8: raw_key = 0x08u; break;
    case SDL_SCANCODE_9: raw_key = 0x09u; break;
    case SDL_SCANCODE_0: raw_key = 0x0au; break;
    case SDL_SCANCODE_MINUS: raw_key = 0x0bu; break;
    case SDL_SCANCODE_EQUALS: raw_key = 0x0cu; break;
    case SDL_SCANCODE_BACKSLASH: raw_key = 0x0du; break;
    case SDL_SCANCODE_Q: raw_key = 0x10u; break;
    case SDL_SCANCODE_W: raw_key = 0x11u; break;
    case SDL_SCANCODE_E: raw_key = 0x12u; break;
    case SDL_SCANCODE_R: raw_key = 0x13u; break;
    case SDL_SCANCODE_T: raw_key = 0x14u; break;
    case SDL_SCANCODE_Y: raw_key = 0x15u; break;
    case SDL_SCANCODE_U: raw_key = 0x16u; break;
    case SDL_SCANCODE_I: raw_key = 0x17u; break;
    case SDL_SCANCODE_O: raw_key = 0x18u; break;
    case SDL_SCANCODE_P: raw_key = 0x19u; break;
    case SDL_SCANCODE_LEFTBRACKET: raw_key = 0x1au; break;
    case SDL_SCANCODE_RIGHTBRACKET: raw_key = 0x1bu; break;
    case SDL_SCANCODE_A: raw_key = 0x20u; break;
    case SDL_SCANCODE_S: raw_key = 0x21u; break;
    case SDL_SCANCODE_D: raw_key = 0x22u; break;
    case SDL_SCANCODE_F: raw_key = 0x23u; break;
    case SDL_SCANCODE_G: raw_key = 0x24u; break;
    case SDL_SCANCODE_H: raw_key = 0x25u; break;
    case SDL_SCANCODE_J: raw_key = 0x26u; break;
    case SDL_SCANCODE_K: raw_key = 0x27u; break;
    case SDL_SCANCODE_L: raw_key = 0x28u; break;
    case SDL_SCANCODE_SEMICOLON: raw_key = 0x29u; break;
    case SDL_SCANCODE_NONUSHASH: raw_key = 0x2au; break;
    case SDL_SCANCODE_NONUSBACKSLASH: raw_key = 0x30u; break;
    case SDL_SCANCODE_Z: raw_key = 0x31u; break;
    case SDL_SCANCODE_X: raw_key = 0x32u; break;
    case SDL_SCANCODE_C: raw_key = 0x33u; break;
    case SDL_SCANCODE_V: raw_key = 0x34u; break;
    case SDL_SCANCODE_B: raw_key = 0x35u; break;
    case SDL_SCANCODE_N: raw_key = 0x36u; break;
    case SDL_SCANCODE_M: raw_key = 0x37u; break;
    case SDL_SCANCODE_COMMA: raw_key = 0x38u; break;
    case SDL_SCANCODE_PERIOD: raw_key = 0x39u; break;
    case SDL_SCANCODE_SLASH: raw_key = 0x3au; break;
    case SDL_SCANCODE_KP_1: raw_key = 0x1du; break;
    case SDL_SCANCODE_KP_2: raw_key = 0x1eu; break;
    case SDL_SCANCODE_KP_3: raw_key = 0x1fu; break;
    case SDL_SCANCODE_KP_4: raw_key = 0x2du; break;
    case SDL_SCANCODE_KP_5: raw_key = 0x2eu; break;
    case SDL_SCANCODE_KP_6: raw_key = 0x2fu; break;
    case SDL_SCANCODE_KP_7: raw_key = 0x3du; break;
    case SDL_SCANCODE_KP_8: raw_key = 0x3eu; break;
    case SDL_SCANCODE_KP_9: raw_key = 0x3fu; break;
    case SDL_SCANCODE_KP_0: raw_key = 0x0fu; break;
    case SDL_SCANCODE_SPACE: raw_key = 0x40u; break;
    case SDL_SCANCODE_BACKSPACE: raw_key = 0x41u; break;
    case SDL_SCANCODE_TAB: raw_key = 0x42u; break;
    case SDL_SCANCODE_KP_ENTER: raw_key = 0x43u; break;
    case SDL_SCANCODE_RETURN: raw_key = 0x44u; break;
    case SDL_SCANCODE_ESCAPE: raw_key = 0x45u; break;
    case SDL_SCANCODE_DELETE: raw_key = 0x46u; break;
    case SDL_SCANCODE_KP_MINUS: raw_key = 0x4au; break;
    case SDL_SCANCODE_KP_PERIOD: raw_key = 0x3cu; break;
    case SDL_SCANCODE_UP: raw_key = 0x4cu; break;
    case SDL_SCANCODE_DOWN: raw_key = 0x4du; break;
    case SDL_SCANCODE_RIGHT: raw_key = 0x4eu; break;
    case SDL_SCANCODE_LEFT: raw_key = 0x4fu; break;
    case SDL_SCANCODE_F1: raw_key = 0x50u; break;
    case SDL_SCANCODE_F2: raw_key = 0x51u; break;
    case SDL_SCANCODE_F3: raw_key = 0x52u; break;
    case SDL_SCANCODE_F4: raw_key = 0x53u; break;
    case SDL_SCANCODE_F5: raw_key = 0x54u; break;
    case SDL_SCANCODE_F6: raw_key = 0x55u; break;
    case SDL_SCANCODE_F7: raw_key = 0x56u; break;
    case SDL_SCANCODE_F8: raw_key = 0x57u; break;
    case SDL_SCANCODE_F9: raw_key = 0x58u; break;
    case SDL_SCANCODE_F10: raw_key = 0x59u; break;
    case SDL_SCANCODE_KP_DIVIDE: raw_key = 0x5cu; break;
    case SDL_SCANCODE_KP_MULTIPLY: raw_key = 0x5du; break;
    case SDL_SCANCODE_KP_PLUS: raw_key = 0x5eu; break;
    case SDL_SCANCODE_LSHIFT: raw_key = 0x60u; break;
    case SDL_SCANCODE_RSHIFT: raw_key = 0x61u; break;
    case SDL_SCANCODE_CAPSLOCK: raw_key = 0x62u; break;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL: raw_key = 0x63u; break;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT: raw_key = 0x64u; break;
    case SDL_SCANCODE_LGUI: raw_key = 0x66u; break;
    case SDL_SCANCODE_RGUI: raw_key = 0x67u; break;
    default:
        return 0;
    }
    *out_raw_key = raw_key;
    return 1;
}

int main(int argc, char **argv)
{
    char data_root[1024];
    char error[256];
    char status[160];
    const char *configured_data_root = NULL;
    GameBootstrap game;
    SceneFrame frame;
    RendererStub *renderer = NULL;

    for (int argument_index = 1; argument_index < argc; argument_index += 2) {
        if (argument_index + 1 >= argc) {
            fprintf(stderr, "usage: %s [--data-root <directory>]\n",
                    argv[0]);
            return 2;
        }
        if (strcmp(argv[argument_index], "--data-root") == 0 && !configured_data_root) {
            configured_data_root = argv[argument_index + 1];
        } else {
            fprintf(stderr, "usage: %s [--data-root <directory>]\n",
                    argv[0]);
            return 2;
        }
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[PLATFORM] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!configured_data_root) {
        if (!make_default_data_root(data_root, sizeof(data_root))) {
            fprintf(stderr, "[PLATFORM] SDL_GetBasePath failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        configured_data_root = data_root;
    }
    if (!game_bootstrap_init(&game, configured_data_root, error, sizeof(error))) {
        fprintf(stderr, "[ASSET] %s\n", error);
        SDL_Quit();
        return 1;
    }
    if (!scene_frame_init(&frame, 1024)) {
        fprintf(stderr, "[SCENE] unable to allocate frame command buffer\n");
        game_bootstrap_destroy(&game);
        SDL_Quit();
        return 1;
    }
    renderer = renderer_stub_create();
    if (!renderer) {
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        SDL_Quit();
        return 1;
    }

    /* Gameplay-first bootstrap: source default session enters Level A directly. */
    if (!game_bootstrap_start_selected_single_player(&game, configured_data_root,
                                                     error, sizeof(error))) {
        fprintf(stderr, "[GAME] %s\n", error);
        renderer_stub_destroy(renderer);
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        SDL_Quit();
        return 1;
    }
    renderer_stub_set_status(renderer, "Level A active; static collision; GPU renderer pending");

    fprintf(stdout,
            "[BOOTSTRAP] test.lnk=%zu bytes TEXT_FILE=%zu bytes Level A active\n",
            game.game_link.size, game.story_text.size);
    while (renderer_stub_is_running(renderer)) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            uint8_t raw_key;

            if (event.type == SDL_QUIT) {
                renderer_stub_request_quit(renderer);
                break;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                if (raw_key_from_scancode(event.key.keysym.scancode, &raw_key) &&
                    !game_input_set_raw_key(&game.input, raw_key,
                                            event.type == SDL_KEYDOWN,
                                            error, sizeof(error))) {
                    fprintf(stderr, "[INPUT] %s\n", error);
                    renderer_stub_set_status(renderer, error);
                    continue;
                }
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                renderer_stub_request_quit(renderer);
                break;
            }
        }
        if (!player_runtime_update_discrete_controls(&game.player, &game.input,
                                                     &game.controls, &game.level_runtime,
                                                     error, sizeof(error)) ||
            !player_runtime_update_spatial(&game.player, &game.input, &game.controls,
                                           &game.preferences, &game.math, &game.level_runtime,
                                           error, sizeof(error))) {
            fprintf(stderr, "[GAME] %s\n", error);
            renderer_stub_set_status(renderer, error);
        } else {
            (void)snprintf(status, sizeof(status),
                           "Level %c | zone %u | x=%" PRId32 " y=%" PRId32
                           " z=%" PRId32 " | GPU renderer pending",
                           (char)('A' + game.active_level_index), game.player.zone_index,
                           game.player.x, game.player.y, game.player.z);
            renderer_stub_set_status(renderer, status);
        }
        scene_frame_begin(&frame);
        if (!game_bootstrap_submit_diagnostic_frame(&game, &frame)) {
            fprintf(stderr, "[SCENE] diagnostic command submission failed\n");
            break;
        }
        renderer_stub_present(renderer, &frame);
        SDL_Delay(16);
    }

    renderer_stub_destroy(renderer);
    scene_frame_destroy(&frame);
    game_bootstrap_destroy(&game);
    SDL_Quit();
    return 0;
}
