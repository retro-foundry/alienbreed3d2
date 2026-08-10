#define SDL_MAIN_HANDLED
#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <stdio.h>
#include <string.h>

#include "game_bootstrap.h"
#include "game_vblank_clock.h"
#include "render_view.h"
#include "renderer.h"

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

/* Source campaign identifiers are SETPLAYERS' contiguous lowercase a..p. */
static int level_index_from_argument(const char *text, uint16_t *out_level_index)
{
    char level;

    if (!text || !out_level_index || text[0] == '\0' || text[1] != '\0') {
        return 0;
    }
    level = text[0];
    if (level >= 'A' && level <= 'P') {
        level = (char)(level - 'A' + 'a');
    }
    if (level < 'a' || level > 'p') {
        return 0;
    }
    *out_level_index = (uint16_t)(level - 'a');
    return 1;
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

/* modules/player.s:plr_MouseControl writes button state into these bindings. */
static int set_mouse_button_source_key(GameBootstrap *game, uint8_t button, int is_pressed,
                                       char *error, size_t error_size)
{
    uint16_t binding;

    if (!game) {
        return 0;
    }
    if (button == SDL_BUTTON_LEFT) {
        binding = GAME_CONTROL_FIRE;
    } else if (button == SDL_BUTTON_RIGHT) {
        binding = game->preferences.original_mouse != 0u ?
            GAME_CONTROL_FORWARDS : GAME_CONTROL_NEXT_WEAPON;
    } else {
        return 1;
    }
    return game_input_set_raw_key(&game->input,
                                  game->controls.assigned_raw_keys[binding], is_pressed,
                                  error, error_size);
}

typedef struct {
    char data_root[1024];
    uint16_t selected_level_index;
    GameBootstrap game;
    SceneFrame frame;
    Renderer *renderer;
    RenderView view;
    /* Host display frames are not source VBlanks; keep source logic at 50 Hz. */
    GameVBlankClock vblank_clock;
    int sdl_initialized;
    int game_initialized;
    int frame_initialized;
    int gpu_smoke;
    int gpu_smoke_all_levels;
    int exit_code;
} GameApp;

static void game_app_shutdown(GameApp *app)
{
    if (!app) {
        return;
    }
    renderer_destroy(app->renderer);
    app->renderer = NULL;
    if (app->frame_initialized) {
        scene_frame_destroy(&app->frame);
        app->frame_initialized = 0;
    }
    if (app->game_initialized) {
        game_bootstrap_destroy(&app->game);
        app->game_initialized = 0;
    }
    if (app->sdl_initialized) {
        SDL_Quit();
        app->sdl_initialized = 0;
    }
}

static int game_app_parse_arguments(GameApp *app, int argc, char **argv)
{
    int has_data_root = 0;

    for (int argument_index = 1; argument_index < argc; argument_index += 2) {
        if (argument_index + 1 >= argc) {
            return 0;
        }
        if (strcmp(argv[argument_index], "--data-root") == 0 && !has_data_root) {
            int written = snprintf(app->data_root, sizeof(app->data_root), "%s",
                                   argv[argument_index + 1]);

            if (written < 0 || (size_t)written >= sizeof(app->data_root)) {
                return 0;
            }
            has_data_root = 1;
        } else if (strcmp(argv[argument_index], "--level") == 0) {
            if (!level_index_from_argument(argv[argument_index + 1],
                                           &app->selected_level_index)) {
                return 0;
            }
        } else if (strcmp(argv[argument_index], "--gpu-smoke") == 0 && !app->gpu_smoke) {
            if (strcmp(argv[argument_index + 1], "all") == 0) {
                app->selected_level_index = 0u;
                app->gpu_smoke_all_levels = 1;
            } else if (!level_index_from_argument(argv[argument_index + 1],
                                                  &app->selected_level_index)) {
                return 0;
            }
            app->gpu_smoke = 1;
        } else {
            return 0;
        }
    }
    return 1;
}

static int game_app_init(GameApp *app, int argc, char **argv)
{
    char error[256];
    RendererConfig renderer_config;

    if (!app || !game_app_parse_arguments(app, argc, argv)) {
        fprintf(stderr,
                "usage: %s [--data-root <directory>] [--level <A-P>] [--gpu-smoke <A-P|all>]\n",
                argv[0]);
        return 0;
    }
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[PLATFORM] SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    app->sdl_initialized = 1;
    if (!app->data_root[0] && !make_default_data_root(app->data_root, sizeof(app->data_root))) {
        fprintf(stderr, "[PLATFORM] SDL_GetBasePath failed: %s\n", SDL_GetError());
        return 0;
    }
    if (!game_bootstrap_init(&app->game, app->data_root, error, sizeof(error))) {
        fprintf(stderr, "[ASSET] %s\n", error);
        return 0;
    }
    app->game_initialized = 1;
    if (!scene_frame_init(&app->frame, 1024u)) {
        fprintf(stderr, "[SCENE] unable to allocate frame command buffer\n");
        return 0;
    }
    app->frame_initialized = 1;
    renderer_config.backend = RENDERER_BACKEND_OPENGL;
    renderer_config.window_width = 1280;
    renderer_config.window_height = 720;
    renderer_config.window_title = "Alien Breed 3D II: The Killing Grounds";
    renderer_config.hidden_window = app->gpu_smoke;
    app->renderer = renderer_create(&renderer_config, error, sizeof(error));
    if (!app->renderer) {
        fprintf(stderr, "[RENDER] %s\n", error);
        return 0;
    }
    /* Gameplay-first bootstrap: source session enters a selected A-P level directly. */
    if (!game_session_select_level(&app->game.session, app->selected_level_index,
                                   error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&app->game, app->data_root,
                                                     error, sizeof(error))) {
        fprintf(stderr, "[GAME] %s\n", error);
        return 0;
    }
    render_view_init(&app->view);
    game_vblank_clock_reset(&app->vblank_clock, SDL_GetTicks64());
    if (!app->gpu_smoke && SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
        fprintf(stderr, "[INPUT] relative mouse mode unavailable: %s\n", SDL_GetError());
    }
    fprintf(stdout,
            "[BOOTSTRAP] test.lnk=%zu bytes TEXT_FILE=%zu bytes Level %c active\n",
            app->game.game_link.size, app->game.story_text.size,
            (char)('A' + app->game.active_level_index));
    return 1;
}

static void game_app_tick(GameApp *app)
{
    char error[256];
    SDL_Event event;
    uint32_t source_vblanks;

    if (!app || !renderer_is_running(app->renderer)) {
        return;
    }
    while (SDL_PollEvent(&event)) {
        uint8_t raw_key;

        if (event.type == SDL_QUIT) {
            renderer_request_quit(app->renderer);
            break;
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            if (raw_key_from_scancode(event.key.keysym.scancode, &raw_key) &&
                !game_input_set_raw_key(&app->game.input, raw_key,
                                        event.type == SDL_KEYDOWN,
                                        error, sizeof(error))) {
                fprintf(stderr, "[INPUT] %s\n", error);
                app->exit_code = 1;
                renderer_request_quit(app->renderer);
                break;
            }
        }
        if (event.type == SDL_MOUSEMOTION) {
            game_input_add_mouse_motion(&app->game.input, event.motion.xrel, event.motion.yrel);
            /* Native real pitch is presentation state; source mouse input stays intact. */
            render_view_add_mouse_motion(&app->view, event.motion.yrel,
                                         app->game.player.invert_mouse);
        }
        if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
            if (!set_mouse_button_source_key(&app->game, event.button.button,
                                             event.type == SDL_MOUSEBUTTONDOWN,
                                             error, sizeof(error))) {
                fprintf(stderr, "[INPUT] %s\n", error);
                app->exit_code = 1;
                renderer_request_quit(app->renderer);
                break;
            }
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            renderer_request_quit(app->renderer);
            break;
        }
    }
    if (!renderer_is_running(app->renderer)) {
        return;
    }
    /*
     * hires.s:VBlankInterrupt produces one source frame at PAL 50 Hz.  Do not
     * advance Plr1_Control once per host present: its source X/Z velocity is
     * expressed per VBlank, so doing that makes movement display-rate dependent.
     */
    source_vblanks = game_vblank_clock_advance(&app->vblank_clock, SDL_GetTicks64());
    for (uint32_t vblank_index = 0u; vblank_index < source_vblanks; ++vblank_index) {
        if (!game_bootstrap_update_single_player(&app->game, error, sizeof(error))) {
            fprintf(stderr, "[GAME] %s\n", error);
            app->exit_code = 1;
            renderer_request_quit(app->renderer);
            return;
        }
        if (app->game.session.level_finished != 0u) {
            break;
        }
    }
    if (app->game.session.level_finished != 0u) {
        fprintf(stdout, "[GAME] Level %c complete; direct session is ending\n",
                (char)('A' + app->game.active_level_index));
        /* The source returns to its menu after endlevel; direct mode exits instead. */
        renderer_request_quit(app->renderer);
        return;
    }
    scene_frame_begin(&app->frame);
    if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame)) {
        fprintf(stderr, "[SCENE] source scene command submission failed\n");
        app->exit_code = 1;
        renderer_request_quit(app->renderer);
        return;
    }
    if (!renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
        fprintf(stderr, "[RENDER] %s\n", error);
        app->exit_code = 1;
        renderer_request_quit(app->renderer);
    }
}

static int game_app_run_gpu_smoke(GameApp *app)
{
    char error[256];
    uint16_t first_level = app->selected_level_index;
    uint16_t last_level = app->gpu_smoke_all_levels != 0 ? 15u : first_level;

    for (uint16_t level_index = first_level; level_index <= last_level; ++level_index) {
        uint64_t source_lighting_checksum;
        if (level_index != first_level &&
            (!game_session_select_level(&app->game.session, level_index, error, sizeof(error)) ||
             !game_bootstrap_start_selected_single_player(&app->game, app->data_root,
                                                          error, sizeof(error)))) {
            fprintf(stderr, "[GAME] GPU smoke could not load Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        /* Exercise Plr1_Use followed by ObjectHandler's live companion draw state. */
        if (!game_bootstrap_update_single_player_at_time(
                &app->game, (uint64_t)level_index * 20u + 1u, error, sizeof(error))) {
            fprintf(stderr, "[GAME] GPU smoke could not update Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        scene_frame_begin(&app->frame);
        if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame) ||
            !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        /* A valid source directional-light field can black out a view weapon
         * completely. renderer_present has still executed the vector weapon
         * pass (and reports any decode, shader, or draw failure), so framebuffer
         * coverage is not a valid all-level smoke assertion. */
        source_lighting_checksum = renderer_last_frame_rgb_checksum(app->renderer);
        /*
         * A complete-scene frame must react to the live `CurrentPointBrights`
         * words, including geometry outside the source PVS. Use a bright
         * source value so this hidden smoke detects a missing wall, floor, or
         * ceiling palette-light pass without relying on a screenshot. A more
         * negative value can map to the same fully-dark source palette row.
         */
        for (uint16_t zone_index = 0u;
             zone_index < app->game.dynamic_level.runtime.zone_count; ++zone_index) {
            for (uint16_t point_index = 0u;
                 point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
                app->game.lighting_runtime.current_point_brightness[zone_index][point_index] = 200;
            }
        }
        scene_frame_begin(&app->frame);
        if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame) ||
            !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU light-response smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        if (renderer_last_frame_rgb_checksum(app->renderer) == source_lighting_checksum) {
            fprintf(stderr,
                    "[RENDER] GPU smoke source Gouraud lighting did not change world output "
                    "for Level %c\n", (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
    }
    return 1;
}

#if defined(__EMSCRIPTEN__)
static GameApp game_app_web;

static void game_app_web_tick(void *argument)
{
    GameApp *app = argument;

    game_app_tick(app);
    if (!renderer_is_running(app->renderer)) {
        game_app_shutdown(app);
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char **argv)
{
#if defined(__EMSCRIPTEN__)
    GameApp *app = &game_app_web;
#else
    GameApp native_app = {0};
    GameApp *app = &native_app;
#endif

    if (!game_app_init(app, argc, argv)) {
        game_app_shutdown(app);
        return 1;
    }
    if (app->gpu_smoke) {
        if (game_app_run_gpu_smoke(app) && app->exit_code == 0) {
            fprintf(stdout, "[RENDER] hidden GPU smoke passed for %s\n",
                    app->gpu_smoke_all_levels != 0 ? "Levels A-P" : "the selected level");
        }
        int exit_code = app->exit_code;
        game_app_shutdown(app);
        return exit_code;
    }
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(game_app_web_tick, app, 0, 1);
    return 0;
#else
    while (renderer_is_running(app->renderer)) {
        game_app_tick(app);
        SDL_Delay(16u);
    }
    int exit_code = app->exit_code;
    game_app_shutdown(app);
    return exit_code;
#endif
}
