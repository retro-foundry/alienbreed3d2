#define SDL_MAIN_HANDLED
#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <stdio.h>
#include <string.h>

#include "audio_sdl.h"
#include "desktop_settings.h"
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

/*
 * Native presentation owns this display choice. The source's 320-wide draw
 * state remains entirely in SceneFrame; renderer_opengl.c already renders to
 * the full SDL drawable each host frame.
 */
static int game_app_get_desktop_resolution(int *out_width, int *out_height,
                                           char *error, size_t error_size)
{
    SDL_DisplayMode mode;

    if (!out_width || !out_height ||
        SDL_GetDesktopDisplayMode(0, &mode) != 0 || mode.w <= 0 || mode.h <= 0) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size, "SDL desktop display mode is unavailable: %s",
                           SDL_GetError());
        }
        return 0;
    }
    *out_width = mode.w;
    *out_height = mode.h;
    return 1;
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
    uint8_t selected_level_from_command_line;
    DesktopSettings desktop_settings;
    GameBootstrap game;
    /* Completed source-frame endpoints retained for high-rate presentation. */
    SceneFrame source_frame;
    SceneFrame previous_source_frame;
    SceneFrame frame;
    Renderer *renderer;
    AudioSdl *audio;
    RenderView view;
    /* First-port presented-pixel to reference-view mouse conversion state. */
    int mouse_present_width;
    int mouse_present_height;
    int32_t mouse_remainder_x;
    int32_t mouse_remainder_y;
    /* Host display frames are not source VBlanks; keep source logic at 50 Hz. */
    GameVBlankClock vblank_clock;
    int sdl_initialized;
    int game_initialized;
    int source_frame_initialized;
    int previous_source_frame_initialized;
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
    audio_sdl_destroy(app->audio);
    app->audio = NULL;
    if (app->frame_initialized) {
        scene_frame_destroy(&app->frame);
        app->frame_initialized = 0;
    }
    if (app->previous_source_frame_initialized) {
        scene_frame_destroy(&app->previous_source_frame);
        app->previous_source_frame_initialized = 0;
    }
    if (app->source_frame_initialized) {
        scene_frame_destroy(&app->source_frame);
        app->source_frame_initialized = 0;
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
            app->selected_level_from_command_line = UINT8_MAX;
        } else if (strcmp(argv[argument_index], "--gpu-smoke") == 0 && !app->gpu_smoke) {
            if (strcmp(argv[argument_index + 1], "all") == 0) {
                app->selected_level_index = 0u;
                app->gpu_smoke_all_levels = 1;
            } else if (!level_index_from_argument(argv[argument_index + 1],
                                                  &app->selected_level_index)) {
                return 0;
            }
            app->gpu_smoke = 1;
            app->selected_level_from_command_line = UINT8_MAX;
        } else {
            return 0;
        }
    }
    return 1;
}

static int game_app_try_load_desktop_settings(GameApp *app, const char *path,
                                              char *error, size_t error_size,
                                              int *out_loaded)
{
    DesktopSettingsLoadResult result;
    char parse_error[256] = {0};

    result = desktop_settings_load_file(&app->desktop_settings, path, parse_error,
                                        sizeof(parse_error));
    if (result == DESKTOP_SETTINGS_LOAD_OK) {
        fprintf(stdout, "[SETTINGS] Loaded %s\n", path);
        *out_loaded = 1;
        return 1;
    }
    if (result == DESKTOP_SETTINGS_LOAD_NOT_FOUND) {
        return 1;
    }
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s: %s", path, parse_error);
    }
    return 0;
}

static int game_app_load_desktop_settings(GameApp *app, char *error, size_t error_size)
{
    char *base_path;
    char ini_path[1024];
    char template_path[1024];
    int loaded = 0;

    if (!app) {
        return 0;
    }
    desktop_settings_default(&app->desktop_settings);
    base_path = SDL_GetBasePath();
    if (base_path && *base_path) {
        int ini_written = snprintf(ini_path, sizeof(ini_path), "%sab3d2.ini", base_path);
        int template_written = snprintf(template_path, sizeof(template_path), "%sab3d2.ini.template",
                                        base_path);

        if (ini_written < 0 || (size_t)ini_written >= sizeof(ini_path) ||
            template_written < 0 || (size_t)template_written >= sizeof(template_path) ||
            !game_app_try_load_desktop_settings(app, ini_path, error, error_size, &loaded) ||
            (!loaded && !game_app_try_load_desktop_settings(app, template_path, error, error_size,
                                                            &loaded))) {
            SDL_free(base_path);
            return 0;
        }
    }
    if (base_path) {
        SDL_free(base_path);
    }
    if (!loaded &&
        (!game_app_try_load_desktop_settings(app, "ab3d2.ini", error, error_size, &loaded) ||
         (!loaded && !game_app_try_load_desktop_settings(app, "ab3d2.ini.template", error,
                                                          error_size, &loaded)))) {
        return 0;
    }
    if (!loaded) {
        fprintf(stdout, "[SETTINGS] No ab3d2.ini found; using documented defaults\n");
    }
    fprintf(stdout,
            "[SETTINGS] start_level=%u infinite_health=%u infinite_ammo=%u all_weapons=%u "
            "volume=%u always_run=%u\n",
            (unsigned)(app->desktop_settings.start_level_index + 1u),
            app->desktop_settings.infinite_health != 0u ? 1u : 0u,
            app->desktop_settings.infinite_ammo != 0u ? 1u : 0u,
            app->desktop_settings.all_weapons != 0u ? 1u : 0u,
            (unsigned)app->desktop_settings.volume,
            app->desktop_settings.always_run != 0u ? 1u : 0u);
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
    desktop_settings_default(&app->desktop_settings);
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
    if (!app->gpu_smoke &&
        !game_app_load_desktop_settings(app, error, sizeof(error))) {
        fprintf(stderr, "[SETTINGS] %s\n", error);
        return 0;
    }
    if (!app->gpu_smoke) {
        game_bootstrap_apply_desktop_settings(&app->game, &app->desktop_settings);
        if (app->selected_level_from_command_line == 0u) {
            app->selected_level_index = app->desktop_settings.start_level_index;
        }
    }
    app->audio = audio_sdl_create(app->data_root, error, sizeof(error));
    if (!app->audio) {
        fprintf(stderr, "[AUDIO] %s\n", error);
        return 0;
    }
    if (!audio_sdl_is_available(app->audio)) {
        fprintf(stderr, "[AUDIO] disabled: %s\n", error);
    }
    audio_sdl_set_volume(app->audio, app->desktop_settings.volume);
    if (!scene_frame_init(&app->source_frame, 1024u)) {
        fprintf(stderr, "[SCENE] unable to allocate frame command buffer\n");
        return 0;
    }
    app->source_frame_initialized = 1;
    if (!scene_frame_init(&app->previous_source_frame, 1024u)) {
        fprintf(stderr, "[SCENE] unable to allocate source snapshot buffer\n");
        return 0;
    }
    app->previous_source_frame_initialized = 1;
    if (!scene_frame_init(&app->frame, 1024u)) {
        fprintf(stderr, "[SCENE] unable to allocate presentation frame buffer\n");
        return 0;
    }
    app->frame_initialized = 1;
    renderer_config.backend = RENDERER_BACKEND_OPENGL;
    if (app->gpu_smoke) {
        /* Keep the opt-in hidden smoke bounded and independent of desktop layout. */
        renderer_config.window_width = 1280;
        renderer_config.window_height = 720;
    } else if (!game_app_get_desktop_resolution(&renderer_config.window_width,
                                                &renderer_config.window_height,
                                                error, sizeof(error))) {
        fprintf(stderr, "[PLATFORM] %s\n", error);
        return 0;
    }
    renderer_config.window_title = "Alien Breed 3D II: The Killing Grounds";
    /*
     * Keep direct play in the first port's desktop-sized startup mode.  The
     * hidden GPU validation path deliberately remains an ordinary bounded
     * window so it does not depend on the host display layout.
     */
    renderer_config.desktop_window = app->gpu_smoke ? 0 : 1;
    renderer_config.hidden_window = app->gpu_smoke;
    app->mouse_present_width = renderer_config.window_width;
    app->mouse_present_height = renderer_config.window_height;
    app->renderer = renderer_create(&renderer_config, error, sizeof(error));
    if (!app->renderer) {
        fprintf(stderr, "[RENDER] %s\n", error);
        return 0;
    }
    /* The normal desktop window is expanded past its frame after creation. */
    (void)renderer_get_presentation_size(app->renderer, &app->mouse_present_width,
                                         &app->mouse_present_height);
    /* Gameplay-first bootstrap: source session enters a selected A-P level directly. */
    if (!game_session_select_level(&app->game.session, app->selected_level_index,
                                   error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&app->game, app->data_root,
                                                     error, sizeof(error))) {
        fprintf(stderr, "[GAME] %s\n", error);
        return 0;
    }
    /* hires.s:Game_Begin's mt_init begins the source-selected packedtest module. */
    audio_sdl_set_music_enabled(app->audio, app->game.preferences.play_music);
    render_view_init(&app->view);
    render_view_set_source_look(&app->view, app->game.player.aim_speed,
                                app->game.player.look_offset);
    scene_frame_begin(&app->source_frame);
    if (!game_bootstrap_submit_scene_frame(&app->game, &app->source_frame) ||
        !scene_frame_clone(&app->previous_source_frame, &app->source_frame)) {
        fprintf(stderr, "[SCENE] unable to capture the initial source frame\n");
        return 0;
    }
    game_vblank_clock_reset(&app->vblank_clock, SDL_GetPerformanceCounter(),
                            SDL_GetPerformanceFrequency());
    if (app->vblank_clock.initialized == 0u) {
        fprintf(stderr, "[GAME] SDL performance counter has no usable frequency\n");
        return 0;
    }
    if (!app->gpu_smoke && SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
        fprintf(stderr, "[INPUT] relative mouse mode unavailable: %s\n", SDL_GetError());
    }
    fprintf(stdout,
            "[BOOTSTRAP] test.lnk=%zu bytes TEXT_FILE=%zu bytes Level %c active\n",
            app->game.game_link.size, app->game.story_text.size,
            (char)('A' + app->game.active_level_index));
    return 1;
}

/*
 * The first PC port retains the source state immediately before its next
 * 50 Hz update, then draws the blend to the completed source state using the
 * VBlank remainder.  Keep the same scheduler shape at this GPU-neutral scene
 * boundary: game logic continues to own every simulation value and renderer
 * input only owns immutable endpoint copies.
 */
static int game_app_capture_source_frame(GameApp *app)
{
    if (!app) {
        return 0;
    }
    scene_frame_begin(&app->source_frame);
    return game_bootstrap_submit_scene_frame(&app->game, &app->source_frame);
}

static int game_app_build_presentation_frame(GameApp *app)
{
    float interpolation_alpha;
    uint16_t yaw_offset;

    if (!app) {
        return 0;
    }
    interpolation_alpha = game_vblank_clock_interpolation_alpha(&app->vblank_clock);
    if (!scene_frame_interpolate(
                    &app->frame, &app->previous_source_frame, &app->source_frame,
                    interpolation_alpha) ||
        app->frame.count == 0u || app->frame.commands[0u].type != SCENE_COMMAND_CAMERA) {
        return 0;
    }
    /*
     * Keep source/keyboard yaw interpolated with player position. Only mouse
     * X runs ahead at host cadence, and Plr1_Use's ENT_NEXT_2 companion must
     * inherit that identical temporary offset so its source-relative angle
     * remains constant while the camera turns.
     */
    yaw_offset = render_view_yaw_offset(&app->view, interpolation_alpha);
    app->frame.commands[0u].data.camera.yaw = (uint16_t)(
        (app->frame.commands[0u].data.camera.yaw + yaw_offset) & UINT16_C(8190));
    for (size_t index = 1u; index < app->frame.count; ++index) {
        SceneCommand *command = &app->frame.commands[index];

        if (command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
            command->data.sprite_instance.sprite.presentation ==
                SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON) {
            command->data.sprite_instance.sprite.yaw = (uint16_t)(
                (command->data.sprite_instance.sprite.yaw + yaw_offset) & UINT16_C(8190));
        }
    }
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
            enum {
                /* Alien-Breed-3D-I renderer reference view: 96x80 at 8x. */
                MOUSE_REFERENCE_WIDTH = 768,
                MOUSE_REFERENCE_HEIGHT = 640
            };
            /*
             * Match Alien-Breed-3D-I's live g_renderer.present_* conversion.
             * The desktop client may be larger than the requested mode after
             * its border is shifted off-screen, and resizable windows can
             * change again while running.
             */
            (void)renderer_get_presentation_size(
                app->renderer, &app->mouse_present_width, &app->mouse_present_height);
            int16_t mouse_x = game_input_scale_present_mouse_delta(
                event.motion.xrel, MOUSE_REFERENCE_WIDTH, app->mouse_present_width,
                &app->mouse_remainder_x);
            int16_t mouse_y = game_input_scale_present_mouse_delta(
                event.motion.yrel, MOUSE_REFERENCE_HEIGHT, app->mouse_present_height,
                &app->mouse_remainder_y);

            game_input_add_mouse_motion(&app->game.input, mouse_x, mouse_y);
            /* Native real look is presentation state; source mouse input stays intact. */
            if (app->game.player.mouse_active != 0u) {
                render_view_add_mouse_yaw(&app->view, mouse_x);
                render_view_add_mouse_motion(&app->view, mouse_y,
                                             app->game.player.invert_mouse);
            }
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
    source_vblanks = game_vblank_clock_advance(&app->vblank_clock, SDL_GetPerformanceCounter());
    for (uint32_t vblank_index = 0u; vblank_index < source_vblanks; ++vblank_index) {
        int16_t consumed_mouse_x = app->game.player.mouse_active != 0u ?
            game_input_peek_mouse_x(&app->game.input) : 0;

        if (!scene_frame_clone(&app->previous_source_frame, &app->source_frame)) {
            fprintf(stderr, "[SCENE] unable to snapshot the previous source frame\n");
            app->exit_code = 1;
            renderer_request_quit(app->renderer);
            return;
        }
        if (!game_bootstrap_update_single_player(&app->game, error, sizeof(error))) {
            fprintf(stderr, "[GAME] %s\n", error);
            app->exit_code = 1;
            renderer_request_quit(app->renderer);
            return;
        }
        render_view_commit_mouse_yaw(&app->view, consumed_mouse_x);
        render_view_set_source_look(&app->view, app->game.player.aim_speed,
                                    app->game.player.look_offset);
        audio_sdl_consume_events(app->audio, &app->game.audio_events, &app->game.player,
                                 app->game.player.yaw);
        if (!game_app_capture_source_frame(app)) {
            fprintf(stderr, "[SCENE] unable to capture the completed source frame\n");
            app->exit_code = 1;
            renderer_request_quit(app->renderer);
            return;
        }
        if (app->game.session.level_ended != 0u) {
            break;
        }
    }
    if (app->game.session.level_ended != 0u) {
        fprintf(stdout, "[GAME] Level %c %s; direct session is ending\n",
                (char)('A' + app->game.active_level_index),
                app->game.session.level_finished != 0u ? "complete" : "failed");
        /* The source returns to its menu after endlevel; direct mode exits instead. */
        renderer_request_quit(app->renderer);
        return;
    }
    if (!game_app_build_presentation_frame(app)) {
        fprintf(stderr, "[SCENE] source scene interpolation failed\n");
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

/*
 * Draw one source-authored additive or glare effect in the hidden GPU test.
 * This deliberately uses ItsABullet's exact selected asset/frame/effect mode
 * where that mode exists; the shipped bullet table has no glare descriptor,
 * so its glare fixture uses an authored fixed ObjT frame. Neither mutates
 * campaign simulation simply to make an effect visible.
 */
static int game_app_append_source_effect_smoke(GameApp *app, int glare,
                                               char *error, size_t error_size)
{
    const SceneCamera *camera;
    int16_t sine;
    int16_t cosine;

    if (!app || app->frame.count == 0u ||
        app->frame.commands[0u].type != SCENE_COMMAND_CAMERA) {
        return 0;
    }
    camera = &app->frame.commands[0u].data.camera;
    if (!game_math_sine(&app->game.math, camera->yaw, &sine, error, error_size) ||
        !game_math_cosine(&app->game.math, camera->yaw, &cosine, error, error_size)) {
        return 0;
    }
    for (uint16_t bullet_index = 0u; bullet_index < GAME_LINK_BULLET_COUNT; ++bullet_index) {
        GameBulletDefinition definition;

        if (!game_link_get_bullet_definition(&app->game.game_link_catalog, bullet_index,
                                             &definition, error, error_size)) {
            continue;
        }
        for (GameBulletAnimationKind animation_kind = GAME_LINK_BULLET_ANIMATION_FLIGHT;
             animation_kind <= GAME_LINK_BULLET_ANIMATION_POP; ++animation_kind) {
            GameBulletAnimationFrame animation;
            GameObjectFrameData frame_data;
            uint32_t graphics_type = animation_kind == GAME_LINK_BULLET_ANIMATION_FLIGHT ?
                definition.graphics_type : definition.impact_graphics_type;
            uint16_t asset_index;
            uint16_t frame_index;
            SceneCommand command = {0};

            if ((glare != 0 ? graphics_type != 1u : (int32_t)graphics_type <= 1) ||
                !game_link_get_bullet_animation_frame(
                    &app->game.game_link_catalog, animation_kind, bullet_index, 0u,
                    &animation, error, error_size)) {
                continue;
            }
            asset_index = animation.byte_0;
            frame_index = animation.byte_1;
            if (asset_index >= app->game.shared_resources.object_count ||
                !game_link_get_object_frame_data(&app->game.game_link_catalog, asset_index,
                                                  frame_index, &frame_data, error, error_size) ||
                animation.word_2 >> 8u == 0u || (uint8_t)animation.word_2 == 0u) {
                continue;
            }
            command.type = SCENE_COMMAND_SPRITE_INSTANCE;
            command.data.sprite_instance.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
            command.data.sprite_instance.sprite.position = camera->position;
            /*
             * Keep the exact ItsABullet descriptor comfortably beyond the
             * presentation-only contact bias.  This drives the same
             * projectile path that draws a live wall impact rather than
             * testing an unflagged synthetic billboard.
             */
            command.data.sprite_instance.sprite.position.x += sine / 256;
            command.data.sprite_instance.sprite.position.z += cosine / 256;
            command.data.sprite_instance.sprite.source = glare != 0 ? SCENE_SPRITE_SOURCE_GLARE_BITMAP :
                                                       SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
            command.data.sprite_instance.sprite.presentation = SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
            command.data.sprite_instance.sprite.source_asset_id = asset_index;
            command.data.sprite_instance.sprite.source_record_id = UINT32_MAX - (uint32_t)glare;
            command.data.sprite_instance.sprite.frame_index = frame_index;
            command.data.sprite_instance.sprite.source_clip_top_y = camera->position.y - 65536;
            command.data.sprite_instance.sprite.source_clip_bottom_y = camera->position.y + 65536;
            command.data.sprite_instance.sprite.source_width = (uint8_t)(animation.word_2 >> 8u);
            command.data.sprite_instance.sprite.source_height = (uint8_t)animation.word_2;
            command.data.sprite_instance.sprite.flags = (uint8_t)(SCENE_SPRITE_FLAG_PROJECTILE |
                (glare != 0 ? 0u : SCENE_SPRITE_FLAG_ADDITIVE));
            command.data.sprite_instance.sprite.source_effect = glare != 0 ? 0u : 6u;
            command.data.sprite_instance.sprite.frame_metrics.pointer_table_index = frame_data.pointer_table_index;
            command.data.sprite_instance.sprite.frame_metrics.down_strip = frame_data.down_strip;
            command.data.sprite_instance.sprite.frame_metrics.strip_count = frame_data.strip_count;
            command.data.sprite_instance.sprite.frame_metrics.line_count = frame_data.line_count;
            command.data.sprite_instance.sprite.source_bytes =
                app->game.shared_resources.object_wads[asset_index].bytes;
            command.data.sprite_instance.sprite.source_byte_count =
                app->game.shared_resources.object_wads[asset_index].size;
            command.data.sprite_instance.sprite.source_aux_bytes =
                app->game.shared_resources.object_ptrs[asset_index].bytes;
            command.data.sprite_instance.sprite.source_aux_byte_count =
                app->game.shared_resources.object_ptrs[asset_index].size;
            command.data.sprite_instance.sprite.source_palette_bytes = glare != 0 ?
                app->game.shared_resources.texture_palette.bytes :
                app->game.shared_resources.object_palettes[asset_index].bytes;
            command.data.sprite_instance.sprite.source_palette_byte_count = glare != 0 ?
                app->game.shared_resources.texture_palette.size :
                app->game.shared_resources.object_palettes[asset_index].size;
            command.data.sprite_instance.sprite.source_display_palette_bytes =
                app->game.shared_resources.main_palette.bytes;
            command.data.sprite_instance.sprite.source_display_palette_byte_count =
                app->game.shared_resources.main_palette.size;
            command.data.sprite_instance.source_mesh_id =
                ((uint32_t)command.data.sprite_instance.sprite.source << 30u) | asset_index;
            if (!command.data.sprite_instance.sprite.source_bytes ||
                !command.data.sprite_instance.sprite.source_aux_bytes ||
                !command.data.sprite_instance.sprite.source_palette_bytes ||
                !command.data.sprite_instance.sprite.source_display_palette_bytes ||
                !scene_frame_reserve(&app->frame, app->frame.count + 1u) ||
                !scene_frame_submit(&app->frame, &command)) {
                return 0;
            }
            return 1;
        }
    }
    /* The shipped projectile table has no glare flight/pop descriptor, but
     * ObjT definitions do: keep this pass covered with an authored fixed
     * glare object and its normal DEFANIMOBJ frame. */
    if (glare != 0) {
        for (uint16_t object_index = 0u; object_index < GAME_LINK_OBJECT_COUNT; ++object_index) {
            GameObjectDefinition definition;
            GameObjectAnimationFrame animation;
            GameObjectFrameData frame_data;
            uint16_t asset_index;
            uint16_t frame_index;
            SceneCommand command = {0};

            if (!game_link_get_object_definition(&app->game.game_link_catalog, object_index,
                                                 &definition, error, error_size) ||
                definition.graphics_type <= 1u ||
                !game_link_get_object_animation_frame(
                    &app->game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                    object_index, 0u, &animation, error, error_size)) {
                continue;
            }
            asset_index = animation.byte_0;
            frame_index = animation.byte_1;
            if (asset_index >= app->game.shared_resources.object_count ||
                !game_link_get_object_frame_data(&app->game.game_link_catalog, asset_index,
                                                  frame_index, &frame_data, error, error_size) ||
                animation.word_2 >> 8u == 0u || (uint8_t)animation.word_2 == 0u) {
                continue;
            }
            command.type = SCENE_COMMAND_SPRITE_INSTANCE;
            command.data.sprite_instance.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
            command.data.sprite_instance.sprite.position = camera->position;
            command.data.sprite_instance.sprite.position.x += sine / 256;
            command.data.sprite_instance.sprite.position.z += cosine / 256;
            command.data.sprite_instance.sprite.source = SCENE_SPRITE_SOURCE_GLARE_BITMAP;
            command.data.sprite_instance.sprite.presentation = SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
            command.data.sprite_instance.sprite.source_asset_id = asset_index;
            command.data.sprite_instance.sprite.source_record_id = UINT32_MAX - 1u;
            command.data.sprite_instance.sprite.frame_index = frame_index;
            command.data.sprite_instance.sprite.source_clip_top_y = camera->position.y - 65536;
            command.data.sprite_instance.sprite.source_clip_bottom_y = camera->position.y + 65536;
            command.data.sprite_instance.sprite.source_width = (uint8_t)(animation.word_2 >> 8u);
            command.data.sprite_instance.sprite.source_height = (uint8_t)animation.word_2;
            command.data.sprite_instance.sprite.flags = SCENE_SPRITE_FLAG_PROJECTILE;
            command.data.sprite_instance.sprite.frame_metrics.pointer_table_index = frame_data.pointer_table_index;
            command.data.sprite_instance.sprite.frame_metrics.down_strip = frame_data.down_strip;
            command.data.sprite_instance.sprite.frame_metrics.strip_count = frame_data.strip_count;
            command.data.sprite_instance.sprite.frame_metrics.line_count = frame_data.line_count;
            command.data.sprite_instance.sprite.source_bytes =
                app->game.shared_resources.object_wads[asset_index].bytes;
            command.data.sprite_instance.sprite.source_byte_count =
                app->game.shared_resources.object_wads[asset_index].size;
            command.data.sprite_instance.sprite.source_aux_bytes =
                app->game.shared_resources.object_ptrs[asset_index].bytes;
            command.data.sprite_instance.sprite.source_aux_byte_count =
                app->game.shared_resources.object_ptrs[asset_index].size;
            command.data.sprite_instance.sprite.source_palette_bytes =
                app->game.shared_resources.texture_palette.bytes;
            command.data.sprite_instance.sprite.source_palette_byte_count =
                app->game.shared_resources.texture_palette.size;
            command.data.sprite_instance.sprite.source_display_palette_bytes =
                app->game.shared_resources.main_palette.bytes;
            command.data.sprite_instance.sprite.source_display_palette_byte_count =
                app->game.shared_resources.main_palette.size;
            command.data.sprite_instance.source_mesh_id =
                ((uint32_t)SCENE_SPRITE_SOURCE_GLARE_BITMAP << 30u) | asset_index;
            if (!command.data.sprite_instance.sprite.source_bytes ||
                !command.data.sprite_instance.sprite.source_aux_bytes ||
                !command.data.sprite_instance.sprite.source_palette_bytes ||
                !command.data.sprite_instance.sprite.source_display_palette_bytes ||
                !scene_frame_reserve(&app->frame, app->frame.count + 1u) ||
                !scene_frame_submit(&app->frame, &command)) {
                return 0;
            }
            return 1;
        }
    }
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "source %s projectile effect fixture is unavailable",
                       glare != 0 ? "glare" : "additive");
    }
    return 0;
}

static int game_app_run_gpu_smoke(GameApp *app)
{
    enum {
        /* test.lnk: GLFT gun entry six -> object 24 -> vectobj/rocketlauncher. */
        GAME_APP_ROCKET_LAUNCHER_GUN_INDEX = 5u,
        GAME_APP_ROCKET_LAUNCHER_VECTOR_ASSET = 17u
    };
    char error[256];
    uint16_t first_level = app->selected_level_index;
    uint16_t last_level = app->gpu_smoke_all_levels != 0 ? 15u : first_level;

    for (uint16_t level_index = first_level; level_index <= last_level; ++level_index) {
        SceneCommand source_effect_camera;
        uint64_t source_effect_background_checksum;
        uint64_t source_lighting_checksum;
        uint64_t source_weapon_lighting_checksum;
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
        /*
         * Drive Plr1_Shot and ObjectHandler against the loaded room.  This
         * is intentionally separate from the isolated asset conversion
         * checks below: a live player-shot descriptor must alter the complete
         * depth-tested world frame, exactly as a player firing into a wall.
         */
        if (!game_input_set_raw_key(
                &app->game.input,
                app->game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                error, sizeof(error)) ||
            !game_bootstrap_update_single_player_at_time(
                &app->game, (uint64_t)level_index * 20u + 21u, error, sizeof(error)) ||
            !game_input_set_raw_key(
                &app->game.input,
                app->game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 0,
                error, sizeof(error))) {
            fprintf(stderr, "[GAME] GPU smoke could not fire in Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        scene_frame_begin(&app->frame);
        if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame) ||
            !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU live-shot smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        if (renderer_last_projectile_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] GPU live ItsABullet changed no visible pixels in Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        /* Exercise the effect conversion in an isolated camera frame so a
         * valid near-wall spawn cannot hide it behind source geometry. */
        source_effect_camera = app->frame.commands[0u];
        scene_frame_begin(&app->frame);
        if (!scene_frame_submit(&app->frame, &source_effect_camera) ||
            !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU effect background smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        source_effect_background_checksum = renderer_last_frame_rgb_checksum(app->renderer);
        for (int glare = 0; glare <= 1; ++glare) {
            scene_frame_begin(&app->frame);
            if (!scene_frame_submit(&app->frame, &source_effect_camera) ||
                !game_app_append_source_effect_smoke(app, glare, error, sizeof(error)) ||
                !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
                fprintf(stderr, "[RENDER] GPU %s effect smoke failed for Level %c: %s\n",
                        glare != 0 ? "glare" : "additive", (char)('A' + level_index), error);
                app->exit_code = 1;
                return 0;
            }
            if (renderer_last_frame_rgb_checksum(app->renderer) ==
                source_effect_background_checksum) {
                fprintf(stderr,
                        "[RENDER] GPU %s effect had no visible source blend output for Level %c\n",
                        glare != 0 ? "glare" : "additive", (char)('A' + level_index));
                app->exit_code = 1;
                return 0;
            }
        }
        /*
         * modules/player.s selects entry five for RAWKEY_6. Exercise the
         * complete input -> Plr1_Use -> Collectable:GUNHELD -> renderer path:
         * the Rocket Launcher is a multi-part vector model, so malformed
         * later parts cannot be hidden by a successful first draw.
         */
        app->game.session.player1_inventory.weapons[GAME_APP_ROCKET_LAUNCHER_GUN_INDEX] =
            UINT8_MAX;
        if (!game_input_set_raw_key(&app->game.input, 6u, 1, error, sizeof(error)) ||
            !game_bootstrap_update_single_player_at_time(
                &app->game, (uint64_t)level_index * 20u + 41u, error, sizeof(error)) ||
            !game_input_set_raw_key(&app->game.input, 6u, 0, error, sizeof(error)) ||
            !game_bootstrap_update_single_player_at_time(
                &app->game, (uint64_t)level_index * 20u + 61u, error, sizeof(error))) {
            fprintf(stderr, "[GAME] GPU smoke could not select the Rocket Launcher in Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        for (uint16_t zone_index = 0u;
             zone_index < app->game.dynamic_level.runtime.zone_count; ++zone_index) {
            for (uint16_t point_index = 0u;
                 point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
                app->game.lighting_runtime.current_point_brightness[zone_index][point_index] = 200;
            }
        }
        scene_frame_begin(&app->frame);
        if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame)) {
            fprintf(stderr, "[SCENE] GPU Rocket Launcher scene submission failed for Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        {
            uint8_t saw_rocket_launcher = 0u;

            for (size_t command_index = 0u; command_index < app->frame.count; ++command_index) {
                const SceneCommand *command = &app->frame.commands[command_index];

                if (command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                    command->data.sprite_instance.sprite.source_record_id ==
                        app->game.object_runtime.player1_slot + 2u &&
                    command->data.sprite_instance.sprite.presentation ==
                        SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON &&
                    command->data.sprite_instance.sprite.source ==
                        SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                    command->data.sprite_instance.sprite.source_asset_id ==
                        GAME_APP_ROCKET_LAUNCHER_VECTOR_ASSET) {
                    saw_rocket_launcher = UINT8_MAX;
                    break;
                }
            }
            if (saw_rocket_launcher == 0u) {
                fprintf(stderr,
                        "[SCENE] GPU smoke selected the wrong key-six companion in Level %c\n",
                        (char)('A' + level_index));
                app->exit_code = 1;
                return 0;
            }
        }
        if (!renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU Rocket Launcher smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        if (renderer_last_view_weapon_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] GPU Rocket Launcher smoke changed no visible pixels in Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        /*
         * Compare two explicit source states.  A level can legitimately
         * start at the same palette-light clamp used by the bright probe, so
         * comparing against its incidental live state is not a valid all-level
         * assertion.  renderer_present above has already exercised that live
         * scene; these controlled values prove its material handoff responds
         * to `CurrentPointBrights`.
         */
        for (uint16_t zone_index = 0u;
             zone_index < app->game.dynamic_level.runtime.zone_count; ++zone_index) {
            for (uint16_t point_index = 0u;
                 point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
                app->game.lighting_runtime.current_point_brightness[zone_index][point_index] = -345;
            }
        }
        scene_frame_begin(&app->frame);
        if (!game_bootstrap_submit_scene_frame(&app->game, &app->frame) ||
            !renderer_present(app->renderer, &app->frame, &app->view, error, sizeof(error))) {
            fprintf(stderr, "[RENDER] GPU dark-light smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        source_lighting_checksum = renderer_last_frame_rgb_checksum(app->renderer);
        source_weapon_lighting_checksum = renderer_last_view_weapon_rgb_checksum(app->renderer);
        /*
         * A complete-scene frame must react to the live `CurrentPointBrights`
         * words, including geometry outside the source PVS. Use a bright
         * source value opposite the prior dark probe, so this hidden smoke
         * detects a missing wall, floor, ceiling, or vector palette-light
         * pass without relying on a screenshot.
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
        /* The forced bright state makes the live companion's vector faces
         * observable.  This catches a reversed doapoly winding test or a
         * weapon pass that accidentally drops every textured polygon. */
        if (renderer_last_view_weapon_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] GPU smoke view weapon has no visible vector coverage "
                    "for Level %c\n", (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        if (renderer_last_view_weapon_rgb_checksum(app->renderer) ==
            source_weapon_lighting_checksum) {
            fprintf(stderr,
                    "[RENDER] GPU smoke source Gouraud lighting did not change companion output "
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
    /*
     * renderer_opengl_present() swaps the OpenGL back buffer.  Its requested
     * swap interval is the presentation boundary on native builds, just as
     * display_draw_display() is in the first port's game loop.  Do not add a
     * fixed host delay here: it would cap source-frame interpolation at about
     * 60 Hz even on a 120 Hz or higher-refresh display.
     */
    while (renderer_is_running(app->renderer)) {
        game_app_tick(app);
    }
    int exit_code = app->exit_code;
    game_app_shutdown(app);
    return exit_code;
#endif
}
