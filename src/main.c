#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "game_bootstrap.h"
#include "game_menu.h"
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

static int menu_input_from_key(SDL_Keycode key, GameMenuInput *out_input)
{
    if (!out_input) {
        return 0;
    }
    switch (key) {
    case SDLK_UP:
        *out_input = GAME_MENU_INPUT_UP;
        return 1;
    case SDLK_DOWN:
        *out_input = GAME_MENU_INPUT_DOWN;
        return 1;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        *out_input = GAME_MENU_INPUT_ACTIVATE;
        return 1;
    case SDLK_ESCAPE:
        *out_input = GAME_MENU_INPUT_BACK;
        return 1;
    default:
        return 0;
    }
}

int main(int argc, char **argv)
{
    char data_root[1024];
    char error[256];
    const char *configured_data_root = NULL;
    GameBootstrap game;
    GameMenu menu;
    SceneFrame frame;
    RendererStub *renderer = NULL;

    if (argc == 3 && strcmp(argv[1], "--data-root") == 0) {
        configured_data_root = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--data-root <directory>]\n", argv[0]);
        return 2;
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

    game_menu_init(&menu, &game);
    renderer_stub_set_status(renderer, game_menu_status(&menu));

    fprintf(stdout,
            "[BOOTSTRAP] test.lnk=%zu bytes TEXT_FILE=%zu bytes single-player menu ready\n",
            game.game_link.size, game.story_text.size);
    while (renderer_stub_is_running(renderer)) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            GameMenuInput input;
            int should_quit;

            if (event.type == SDL_QUIT) {
                renderer_stub_request_quit(renderer);
                break;
            }
            if (event.type != SDL_KEYDOWN || event.key.repeat ||
                !menu_input_from_key(event.key.keysym.sym, &input)) {
                continue;
            }
            if (!game_menu_handle_input(&menu, &game, configured_data_root, input,
                                        &should_quit, error, sizeof(error))) {
                fprintf(stderr, "[MENU] %s\n", error);
                renderer_stub_set_status(renderer, error);
                continue;
            }
            renderer_stub_set_status(renderer, game_menu_status(&menu));
            if (should_quit) {
                renderer_stub_request_quit(renderer);
                break;
            }
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
