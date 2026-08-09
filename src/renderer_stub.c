#include "renderer_stub.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

struct RendererStub {
    SDL_Window *window;
    int running;
};

RendererStub *renderer_stub_create(void)
{
    RendererStub *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) {
        return NULL;
    }
    renderer->window = SDL_CreateWindow(
        "Alien Breed 3D II PC - GPU renderer pending",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!renderer->window) {
        fprintf(stderr, "[RENDERER] SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(renderer);
        return NULL;
    }
    renderer->running = 1;
    return renderer;
}

void renderer_stub_destroy(RendererStub *renderer)
{
    if (!renderer) {
        return;
    }
    SDL_DestroyWindow(renderer->window);
    free(renderer);
}

int renderer_stub_handle_events(RendererStub *renderer)
{
    SDL_Event event;

    if (!renderer) {
        return 0;
    }
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
            renderer->running = 0;
        }
    }
    return renderer->running;
}

void renderer_stub_present(RendererStub *renderer, const SceneFrame *frame)
{
    char title[160];
    size_t command_count = frame ? frame->count : 0;

    if (!renderer || !renderer->window) {
        return;
    }
    (void)snprintf(title, sizeof(title),
                   "Alien Breed 3D II PC - GPU renderer pending (%zu scene command%s)",
                   command_count, command_count == 1 ? "" : "s");
    SDL_SetWindowTitle(renderer->window, title);
}
