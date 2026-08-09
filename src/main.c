#define SDL_MAIN_HANDLED
#include <SDL.h>

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

int main(int argc, char **argv)
{
    char data_root[1024];
    char error[256];
    const char *configured_data_root = NULL;
    GameBootstrap game;
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

    fprintf(stdout,
            "[BOOTSTRAP] test.lnk=%zu bytes TEXT_FILE=%zu bytes LEVEL_A zones=%u points=%u\n",
            game.game_link.size, game.story_text.size, game.level.zone_count,
            game.level.point_count);
    while (renderer_stub_handle_events(renderer)) {
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
