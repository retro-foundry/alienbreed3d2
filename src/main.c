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
#include "game_quicksave.h"
#include "game_vblank_clock.h"
#include "level_transition.h"
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

static int make_quicksave_path(char *out_path, size_t out_path_size)
{
    char *base_path = SDL_GetBasePath();
    int written;

    if (!out_path || out_path_size == 0u) {
        return 0;
    }
    if (!base_path) {
        written = snprintf(out_path, out_path_size, "%s", GAME_QUICKSAVE_FILE_NAME);
        return written >= 0 && (size_t)written < out_path_size;
    }
    written = snprintf(out_path, out_path_size, "%s%s", base_path,
                       GAME_QUICKSAVE_FILE_NAME);
    SDL_free(base_path);
    return written >= 0 && (size_t)written < out_path_size;
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
    char quicksave_path[1024];
    uint16_t selected_level_index;
    uint8_t selected_level_from_command_line;
    uint8_t world_light_tessellation_from_command_line;
    uint8_t has_world_light_tessellation_from_command_line;
    RendererBackend renderer_backend_from_command_line;
    uint8_t has_renderer_backend_from_command_line;
    DesktopSettings desktop_settings;
    GameBootstrap game;
    /* Completed source-frame endpoints retained for high-rate presentation. */
    SceneFrame source_frame;
    SceneFrame previous_source_frame;
    SceneFrame frame;
    SceneVectorPoseHistory vector_pose_history;
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
    enum {
        GAME_APP_PHASE_GAMEPLAY,
        GAME_APP_PHASE_LEVEL_TEXT_FADE_IN,
        GAME_APP_PHASE_LEVEL_TEXT_WAIT,
        GAME_APP_PHASE_LEVEL_TEXT_FADE_OUT
    } phase;
    uint16_t transition_level_index;
    Uint32 transition_phase_started_ms;
    uint8_t transition_level_needs_load;
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
    scene_vector_pose_history_destroy(&app->vector_pose_history);
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

static int game_app_prepare_renderer_resources(GameApp *app,
                                               char *error, size_t error_size)
{
    RendererVectorResource vector_resources[GAME_LINK_OBJECT_COUNT];
    RendererResourceCatalog catalog = {0};
    size_t prepared_material_count = 0u;
    Uint64 start_counter;
    Uint64 end_counter;
    Uint64 frequency;

    if (!app || !app->renderer ||
        app->game.shared_resources.vector_count > GAME_LINK_OBJECT_COUNT) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "renderer source-resource catalog is invalid");
        }
        return 0;
    }
    memset(vector_resources, 0, sizeof(vector_resources));
    for (uint16_t resource_index = 0u;
         resource_index < app->game.shared_resources.vector_count; ++resource_index) {
        vector_resources[resource_index].source_asset_id = resource_index;
        vector_resources[resource_index].source_bytes =
            app->game.shared_resources.vector_models[resource_index].bytes;
        vector_resources[resource_index].source_byte_count =
            app->game.shared_resources.vector_models[resource_index].size;
    }
    catalog.vector_resources = vector_resources;
    catalog.vector_resource_count = app->game.shared_resources.vector_count;
    catalog.vector_texture_bytes = app->game.shared_resources.texture_maps.bytes;
    catalog.vector_texture_byte_count = app->game.shared_resources.texture_maps.size;
    catalog.vector_light_palette_bytes = app->game.shared_resources.texture_palette.bytes;
    catalog.vector_light_palette_byte_count = app->game.shared_resources.texture_palette.size;
    catalog.source_display_palette_bytes = app->game.shared_resources.main_palette.bytes;
    catalog.source_display_palette_byte_count = app->game.shared_resources.main_palette.size;
    start_counter = SDL_GetPerformanceCounter();
    if (!renderer_prepare_resources(
            app->renderer, &catalog, &prepared_material_count, error, error_size)) {
        return 0;
    }
    end_counter = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    fprintf(stdout, "[RENDER] prepared %zu vector materials before gameplay",
            prepared_material_count);
    if (frequency != 0u && end_counter >= start_counter) {
        double elapsed_milliseconds =
            (double)(end_counter - start_counter) * 1000.0 / (double)frequency;

        fprintf(stdout, " in %.1f ms", elapsed_milliseconds);
    }
    fputc('\n', stdout);
    return 1;
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
        } else if (strcmp(argv[argument_index], "--world-light-tessellation") == 0 &&
                   !app->has_world_light_tessellation_from_command_line) {
            const char *value = argv[argument_index + 1];

            if (strcmp(value, "1") == 0) {
                app->world_light_tessellation_from_command_line = 1u;
            } else if (strcmp(value, "2") == 0) {
                app->world_light_tessellation_from_command_line = 2u;
            } else if (strcmp(value, "4") == 0) {
                app->world_light_tessellation_from_command_line = 4u;
            } else if (strcmp(value, "8") == 0) {
                app->world_light_tessellation_from_command_line = 8u;
            } else {
                return 0;
            }
            app->has_world_light_tessellation_from_command_line = UINT8_MAX;
        } else if (strcmp(argv[argument_index], "--renderer") == 0 &&
                   !app->has_renderer_backend_from_command_line) {
            if (!renderer_backend_from_string(argv[argument_index + 1],
                                              &app->renderer_backend_from_command_line)) {
                return 0;
            }
            app->has_renderer_backend_from_command_line = UINT8_MAX;
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
            "all_keys=%u volume=%u quicksave_load=%u load_autosave=%u always_run=%u "
            "world_light_tessellation=%u renderer=%s rtx_target_fps=%u rtx_debug_view=%s\n",
            (unsigned)(app->desktop_settings.start_level_index + 1u),
            app->desktop_settings.infinite_health != 0u ? 1u : 0u,
            app->desktop_settings.infinite_ammo != 0u ? 1u : 0u,
            app->desktop_settings.all_weapons != 0u ? 1u : 0u,
            app->desktop_settings.all_keys != 0u ? 1u : 0u,
            (unsigned)app->desktop_settings.volume,
            app->desktop_settings.quicksave_load != 0u ? 1u : 0u,
            app->desktop_settings.load_autosave != 0u ? 1u : 0u,
            app->desktop_settings.always_run != 0u ? 1u : 0u,
            (unsigned)app->desktop_settings.world_light_tessellation,
            renderer_backend_name(app->desktop_settings.renderer_backend),
            (unsigned)app->desktop_settings.rtx_target_fps,
            renderer_rtx_debug_view_name(app->desktop_settings.rtx_debug_view));
    return 1;
}

static int game_app_init(GameApp *app, int argc, char **argv)
{
    char error[256];
    RendererConfig renderer_config;
    uint8_t startup_autosave_loaded = 0u;

    if (!app || !game_app_parse_arguments(app, argc, argv)) {
        fprintf(stderr,
                "usage: %s [--data-root <directory>] [--level <A-P>] [--gpu-smoke <A-P|all>] "
                "[--world-light-tessellation <1|2|4|8>] [--renderer <opengl|rtx>]\n",
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
    if (!make_quicksave_path(app->quicksave_path, sizeof(app->quicksave_path))) {
        fprintf(stderr, "[PLATFORM] quicksave path is too long\n");
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
    renderer_config.backend = app->has_renderer_backend_from_command_line != 0u ?
        app->renderer_backend_from_command_line : app->desktop_settings.renderer_backend;
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
    renderer_config.world_light_tessellation =
        app->has_world_light_tessellation_from_command_line != 0u ?
        app->world_light_tessellation_from_command_line :
        app->desktop_settings.world_light_tessellation;
    renderer_config.rtx_target_fps = app->desktop_settings.rtx_target_fps;
    renderer_config.rtx_debug_view = app->desktop_settings.rtx_debug_view;
    fprintf(stdout, "[RENDER] backend=%s world_light_tessellation=%u\n",
            renderer_backend_name(renderer_config.backend),
            (unsigned)renderer_config.world_light_tessellation);
    app->mouse_present_width = renderer_config.window_width;
    app->mouse_present_height = renderer_config.window_height;
    app->renderer = renderer_create(&renderer_config, error, sizeof(error));
    if (!app->renderer) {
        fprintf(stderr, "[RENDER] %s\n", error);
        return 0;
    }
    if (!game_app_prepare_renderer_resources(app, error, sizeof(error))) {
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
    if (!app->gpu_smoke && app->desktop_settings.load_autosave != 0u) {
        /* Alien Breed 3D I control_loop.c's Continue/autosave route restores
         * the complete runtime and enters play without level flavour text. */
        if (!game_quicksave_load(&app->game, app->data_root, app->quicksave_path,
                                 error, sizeof(error))) {
            fprintf(stderr, "[AUTOSAVE] startup load from %s failed: %s\n",
                    app->quicksave_path, error);
            return 0;
        }
        startup_autosave_loaded = UINT8_MAX;
        fprintf(stdout, "[AUTOSAVE] startup restored Level %c from %s\n",
                (char)('A' + app->game.active_level_index), app->quicksave_path);
    }
    /* hires.s:Game_Begin's mt_init begins the source-selected packedtest module. */
    audio_sdl_set_music_enabled(app->audio, app->game.preferences.play_music);
    render_view_init(&app->view);
    render_view_set_source_yaw(&app->view, app->game.player.yaw);
    render_view_set_source_look(&app->view, app->game.player.aim_speed,
                                app->game.player.look_offset);
    scene_frame_begin(&app->source_frame);
    if (!game_bootstrap_submit_scene_frame(&app->game, &app->source_frame) ||
        !scene_vector_pose_history_update(&app->vector_pose_history, &app->source_frame) ||
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
    if (!app->gpu_smoke) {
        if (startup_autosave_loaded != 0u) {
            app->phase = GAME_APP_PHASE_GAMEPLAY;
        } else {
            /* The first port shows the selected level's story before its first
             * gameplay frame, then repeats this flow after each successful exit. */
            app->phase = GAME_APP_PHASE_LEVEL_TEXT_FADE_IN;
            app->transition_level_index = app->game.active_level_index;
            app->transition_phase_started_ms = SDL_GetTicks();
            app->transition_level_needs_load = 0u;
        }
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
    return game_bootstrap_submit_scene_frame(&app->game, &app->source_frame) &&
        scene_vector_pose_history_update(&app->vector_pose_history, &app->source_frame);
}

static int game_app_build_presentation_frame(GameApp *app)
{
    float interpolation_alpha;

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
    scene_vector_pose_history_apply(&app->vector_pose_history, &app->frame,
                                    interpolation_alpha);
    /*
     * RenderView is host-rate presentation state. The Player 1 companion is
     * already in eye space (objdrawhires.s:rotate_object), while its action
     * frame and authored offset were interpolated with the scene above.
     */
    app->frame.commands[0u].data.camera.yaw = render_view_yaw(&app->view);
    return 1;
}

static void game_app_clear_transition_input(GameApp *app)
{
    if (!app) {
        return;
    }
    memset(app->game.input.key_map, 0, sizeof(app->game.input.key_map));
    app->game.input.last_pressed_raw_key = 0u;
    app->game.input.pending_mouse_x = 0;
    app->game.input.mouse_y = app->game.input.old_mouse_y;
    app->mouse_remainder_x = 0;
    app->mouse_remainder_y = 0;
    (void)SDL_GetRelativeMouseState(NULL, NULL);
}

static int game_app_load_transition_level(GameApp *app,
                                          char *error, size_t error_size)
{
    if (!app || !game_bootstrap_start_selected_single_player(
                    &app->game, app->data_root, error, error_size)) {
        return 0;
    }
    audio_sdl_set_music_enabled(app->audio, app->game.preferences.play_music);
    render_view_set_source_yaw(&app->view, app->game.player.yaw);
    render_view_set_source_look(&app->view, app->game.player.aim_speed,
                                app->game.player.look_offset);
    scene_vector_pose_history_reset(&app->vector_pose_history);
    if (!game_app_capture_source_frame(app) ||
        !scene_frame_clone(&app->previous_source_frame, &app->source_frame)) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "unable to capture the newly loaded source level");
        }
        return 0;
    }
    game_vblank_clock_reset(&app->vblank_clock, SDL_GetPerformanceCounter(),
                            SDL_GetPerformanceFrequency());
    fprintf(stdout, "[GAME] Level %c loaded behind its story transition\n",
            (char)('A' + app->game.active_level_index));
    return 1;
}

static int game_app_present_level_transition(GameApp *app, uint8_t opacity,
                                             char *error, size_t error_size)
{
    SceneCommand presentation;
    int has_camera = 0;
    int has_environment = 0;

    if (!app) {
        return 0;
    }
    scene_frame_begin(&app->frame);
    for (size_t index = 0u; index < app->source_frame.count; ++index) {
        const SceneCommand *command = &app->source_frame.commands[index];

        if (command->type == SCENE_COMMAND_CAMERA && !has_camera) {
            if (!scene_frame_submit(&app->frame, command)) {
                return 0;
            }
            has_camera = 1;
        } else if (command->type == SCENE_COMMAND_ENVIRONMENT && !has_environment) {
            if (!scene_frame_submit(&app->frame, command)) {
                return 0;
            }
            has_environment = 1;
        }
    }
    if (!has_camera || !has_environment) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "level transition has no camera or source palette");
        }
        return 0;
    }
    memset(&presentation, 0, sizeof(presentation));
    presentation.type = SCENE_COMMAND_PRESENTATION;
    presentation.data.presentation.mode = SCENE_PRESENTATION_TEXT_SCREEN;
    presentation.data.presentation.hud_opacity = opacity;
    if (!scene_frame_submit(&app->frame, &presentation) ||
        !level_transition_submit_text(
            app->game.story_text.bytes, app->game.story_text.size,
            app->transition_level_index, &app->frame, error, error_size)) {
        return 0;
    }
    return renderer_present(app->renderer, &app->frame, &app->view,
                            error, error_size);
}

static void game_app_tick_level_transition(GameApp *app)
{
    char error[256];
    SDL_Event event;
    Uint32 now;
    Uint32 elapsed;
    int dismiss_requested = 0;
    uint8_t opacity = UINT8_MAX;

    now = SDL_GetTicks();
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            renderer_request_quit(app->renderer);
            return;
        }
        if (app->phase == GAME_APP_PHASE_LEVEL_TEXT_WAIT &&
            (Uint32)(now - app->transition_phase_started_ms) >=
                LEVEL_TRANSITION_MIN_DISMISS_MS &&
            ((event.type == SDL_KEYDOWN && event.key.repeat == 0u) ||
             event.type == SDL_MOUSEBUTTONDOWN ||
             event.type == SDL_CONTROLLERBUTTONDOWN ||
             event.type == SDL_JOYBUTTONDOWN ||
             event.type == SDL_FINGERDOWN)) {
            dismiss_requested = 1;
        }
    }

    now = SDL_GetTicks();
    elapsed = now - app->transition_phase_started_ms;
    if (app->phase == GAME_APP_PHASE_LEVEL_TEXT_FADE_IN) {
        uint32_t step = elapsed / LEVEL_TRANSITION_FADE_FRAME_MS;

        if (step >= LEVEL_TRANSITION_FADE_STEPS) {
            opacity = UINT8_MAX;
            if (app->transition_level_needs_load != 0u) {
                if (!game_app_load_transition_level(app, error, sizeof(error))) {
                    fprintf(stderr, "[GAME] level transition load failed: %s\n", error);
                    app->exit_code = 1;
                    renderer_request_quit(app->renderer);
                    return;
                }
                app->transition_level_needs_load = 0u;
            }
            app->phase = GAME_APP_PHASE_LEVEL_TEXT_WAIT;
            app->transition_phase_started_ms = SDL_GetTicks();
        } else {
            opacity = level_transition_alpha_for_step((int)step);
        }
    } else if (app->phase == GAME_APP_PHASE_LEVEL_TEXT_WAIT) {
        if (elapsed >= LEVEL_TRANSITION_MIN_DISMISS_MS &&
            (dismiss_requested ||
             (SDL_GetMouseState(NULL, NULL) &
              (SDL_BUTTON(SDL_BUTTON_LEFT) |
               SDL_BUTTON(SDL_BUTTON_MIDDLE) |
               SDL_BUTTON(SDL_BUTTON_RIGHT))) != 0u)) {
            app->phase = GAME_APP_PHASE_LEVEL_TEXT_FADE_OUT;
            app->transition_phase_started_ms = now;
        }
    } else if (app->phase == GAME_APP_PHASE_LEVEL_TEXT_FADE_OUT) {
        uint32_t step = elapsed / LEVEL_TRANSITION_FADE_FRAME_MS;

        if (step >= LEVEL_TRANSITION_FADE_STEPS) {
            app->phase = GAME_APP_PHASE_GAMEPLAY;
            game_app_clear_transition_input(app);
            game_vblank_clock_reset(&app->vblank_clock, SDL_GetPerformanceCounter(),
                                    SDL_GetPerformanceFrequency());
            return;
        }
        opacity = level_transition_alpha_for_step(
            LEVEL_TRANSITION_FADE_STEPS - 1 - (int)step);
    }
    if (!game_app_present_level_transition(app, opacity, error, sizeof(error))) {
        fprintf(stderr, "[RENDER] level transition failed: %s\n", error);
        app->exit_code = 1;
        renderer_request_quit(app->renderer);
    }
}

static void game_app_tick(GameApp *app)
{
    char error[256];
    SDL_Event event;
    uint32_t source_vblanks;
    int quicksave_requested = 0;
    int quickload_requested = 0;

    if (!app || !renderer_is_running(app->renderer)) {
        return;
    }
    if (app->phase != GAME_APP_PHASE_GAMEPLAY) {
        game_app_tick_level_transition(app);
        return;
    }
    while (SDL_PollEvent(&event)) {
        uint8_t raw_key;

        if (event.type == SDL_QUIT) {
            renderer_request_quit(app->renderer);
            break;
        }
        if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
            (event.key.keysym.scancode == SDL_SCANCODE_F5 ||
             event.key.keysym.scancode == SDL_SCANCODE_F9)) {
            /* Match the first port: these are one-shot host shortcuts, not source keys. */
            if (event.type == SDL_KEYDOWN && event.key.repeat == 0u) {
                quicksave_requested |= event.key.keysym.scancode == SDL_SCANCODE_F5;
                quickload_requested |= event.key.keysym.scancode == SDL_SCANCODE_F9;
            }
        } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
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

            /*
             * Sys_MouseY remains a source-controller accumulator. Horizontal
             * Sys_ReadMouse is applied once by the host-rate view below, then
             * sampled as the exact heading for the next 50 Hz gameplay tick.
             */
            game_input_add_mouse_motion(&app->game.input, 0, mouse_y);
            if (app->game.player.mouse_active != 0u) {
                render_view_add_mouse_yaw(&app->view, mouse_x);
                player_runtime_set_camera_yaw(
                    &app->game.player, render_view_yaw(&app->view));
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
    if (app->desktop_settings.quicksave_load != 0u && quicksave_requested) {
        if (game_quicksave_write(&app->game, app->quicksave_path,
                                 error, sizeof(error))) {
            fprintf(stdout, "[QUICKSAVE] F5 saved Level %c to %s\n",
                    (char)('A' + app->game.active_level_index), app->quicksave_path);
        } else {
            fprintf(stderr, "[QUICKSAVE] F5 save failed: %s\n", error);
        }
    }
    if (app->desktop_settings.quicksave_load != 0u && quickload_requested) {
        if (!game_quicksave_load(&app->game, app->data_root, app->quicksave_path,
                                 error, sizeof(error))) {
            fprintf(stderr, "[QUICKSAVE] F9 load failed: %s\n", error);
        } else {
            render_view_set_source_yaw(&app->view, app->game.player.yaw);
            render_view_set_source_look(&app->view, app->game.player.aim_speed,
                                        app->game.player.look_offset);
            app->mouse_remainder_x = 0;
            app->mouse_remainder_y = 0;
            scene_vector_pose_history_reset(&app->vector_pose_history);
            if (!game_app_capture_source_frame(app) ||
                !scene_frame_clone(&app->previous_source_frame, &app->source_frame)) {
                fprintf(stderr, "[SCENE] unable to capture quickloaded source frame\n");
                app->exit_code = 1;
                renderer_request_quit(app->renderer);
                return;
            }
            game_vblank_clock_reset(&app->vblank_clock, SDL_GetPerformanceCounter(),
                                    SDL_GetPerformanceFrequency());
            audio_sdl_set_music_enabled(app->audio, app->game.preferences.play_music);
            fprintf(stdout, "[QUICKSAVE] F9 restored Level %c from %s\n",
                    (char)('A' + app->game.active_level_index), app->quicksave_path);
        }
    }
    /*
     * hires.s:VBlankInterrupt produces one source frame at PAL 50 Hz.  Do not
     * advance Plr1_Control once per host present: its source X/Z velocity is
     * expressed per VBlank, so doing that makes movement display-rate dependent.
     */
    source_vblanks = game_vblank_clock_advance(&app->vblank_clock, SDL_GetPerformanceCounter());
    for (uint32_t vblank_index = 0u; vblank_index < source_vblanks; ++vblank_index) {
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
        /* Keyboard/inertial source turns become the same host-rate heading. */
        render_view_set_source_yaw(&app->view, app->game.player.yaw);
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
        uint16_t completed_level = app->game.active_level_index;

        if (app->game.session.level_finished != 0u &&
            completed_level + 1u < GAME_LINK_LEVEL_COUNT) {
            uint16_t next_level = (uint16_t)(completed_level + 1u);

            if (!game_session_select_level(&app->game.session, next_level,
                                           error, sizeof(error))) {
                fprintf(stderr, "[GAME] campaign advance failed: %s\n", error);
                app->exit_code = 1;
                renderer_request_quit(app->renderer);
                return;
            }
            fprintf(stdout, "[GAME] Level %c complete; transitioning to Level %c\n",
                    (char)('A' + completed_level), (char)('A' + next_level));
            app->transition_level_index = next_level;
            app->transition_level_needs_load = UINT8_MAX;
            app->phase = GAME_APP_PHASE_LEVEL_TEXT_FADE_IN;
            app->transition_phase_started_ms = SDL_GetTicks();
        } else {
            fprintf(stdout, "[GAME] Level %c %s; direct session is ending\n",
                    (char)('A' + completed_level),
                    app->game.session.level_finished != 0u ? "complete" : "failed");
            /* No source end-game intermission is implemented after Level P. */
            renderer_request_quit(app->renderer);
        }
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
    RendererBackend smoke_backend = app->has_renderer_backend_from_command_line != 0u ?
        app->renderer_backend_from_command_line :
        app->desktop_settings.renderer_backend;

    for (uint16_t level_index = first_level; level_index <= last_level; ++level_index) {
        SceneCommand source_effect_camera;
        uint64_t source_effect_background_checksum;
        uint64_t source_text_background_checksum;
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
        if (renderer_last_ui_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] GPU smoke health/ammo UI changed no visible pixels in Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        if (smoke_backend == RENDERER_BACKEND_VULKAN_RTX &&
            renderer_last_indirect_light_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] RTX smoke traced no indirect-light intersections "
                    "in Level %c\n", (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        /* Level B starts with authored emissive polygons in its source PVST.
         * Level A's emitters are behind zones excluded by the spawn PVST and
         * must not leak through those walls merely to satisfy this check. */
        if (smoke_backend == RENDERER_BACKEND_VULKAN_RTX && level_index == 1u &&
            renderer_last_direct_light_energy(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] RTX smoke produced no emissive direct-light radiance "
                    "in Level %c\n", (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        source_text_background_checksum = renderer_last_frame_rgb_checksum(app->renderer);
        {
            SceneCommand message_command;
            static const char message_text[] = "GPU TEXT";

            memset(&message_command, 0, sizeof(message_command));
            message_command.type = SCENE_COMMAND_HUD_TEXT;
            if (!scene_hud_text_set(&message_command.data.hud_text,
                                    message_text, sizeof(message_text) - 1u) ||
                !scene_frame_reserve(&app->frame, app->frame.count + 1u)) {
                fprintf(stderr,
                        "[SCENE] GPU source-message smoke setup failed for Level %c\n",
                        (char)('A' + level_index));
                app->exit_code = 1;
                return 0;
            }
            message_command.data.hud_text.y = 0;
            message_command.data.hud_text.reference_width = 320u;
            message_command.data.hud_text.reference_height = 256u;
            message_command.data.hud_text.style_id = level_index & 3u;
            message_command.data.hud_text.font = SCENE_HUD_FONT_FIRST_PORT_ASCII;
            message_command.data.hud_text.layout = SCENE_HUD_LAYOUT_TOP_CENTER;
            if (!scene_frame_submit(&app->frame, &message_command) ||
                !renderer_present(app->renderer, &app->frame, &app->view,
                                  error, sizeof(error))) {
                fprintf(stderr,
                        "[RENDER] GPU source-message smoke failed for Level %c: %s\n",
                        (char)('A' + level_index), error);
                app->exit_code = 1;
                return 0;
            }
            if (renderer_last_frame_rgb_checksum(app->renderer) ==
                source_text_background_checksum) {
                fprintf(stderr,
                        "[RENDER] GPU source-message tag %u changed no visible pixels "
                        "in Level %c\n",
                        (unsigned)message_command.data.hud_text.style_id,
                        (char)('A' + level_index));
                app->exit_code = 1;
                return 0;
            }
        }
        /* Exercise the renderer-neutral black story screen and the exact
         * authored TEXT_FILE record before the next ordinary world frame. */
        if (!scene_frame_clone(&app->source_frame, &app->frame)) {
            fprintf(stderr,
                    "[SCENE] GPU level-transition snapshot failed for Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        app->transition_level_index = level_index;
        if (!game_app_present_level_transition(app, UINT8_MAX,
                                               error, sizeof(error))) {
            fprintf(stderr,
                    "[RENDER] GPU level-transition smoke failed for Level %c: %s\n",
                    (char)('A' + level_index), error);
            app->exit_code = 1;
            return 0;
        }
        if (renderer_last_ui_coverage(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] GPU level-transition text changed no visible pixels "
                    "in Level %c\n", (char)('A' + level_index));
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
        /* Compare two explicit source states for OpenGL world lighting, the
         * renderer-neutral companion overlay, and RTX secondary Gouraud
         * transport. RTX primary geometry must still remain unaffected. */
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
        /* OpenGL world geometry must react directly to live
         * CurrentPointBrights. RTX consumes the same field only after a
         * secondary ray hits another surface. */
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
        if (smoke_backend == RENDERER_BACKEND_OPENGL &&
            renderer_last_frame_rgb_checksum(app->renderer) ==
                source_lighting_checksum) {
            fprintf(stderr,
                    "[RENDER] GPU smoke source Gouraud lighting did not change world output "
                    "for Level %c\n", (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        if (smoke_backend == RENDERER_BACKEND_VULKAN_RTX &&
            renderer_last_source_gouraud_energy(app->renderer) == 0u) {
            fprintf(stderr,
                    "[RENDER] RTX smoke produced no authored Gouraud radiance "
                    "at secondary hits in Level %c\n",
                    (char)('A' + level_index));
            app->exit_code = 1;
            return 0;
        }
        if (smoke_backend == RENDERER_BACKEND_VULKAN_RTX &&
            level_index == 0u) {
            for (uint32_t convergence_frame = 0u;
                 convergence_frame < 3u; ++convergence_frame) {
                if (!renderer_present(
                        app->renderer, &app->frame, &app->view,
                        error, sizeof(error))) {
                    fprintf(stderr,
                            "[RENDER] RTX adaptive-light convergence frame "
                            "failed: %s\n", error);
                    app->exit_code = 1;
                    return 0;
                }
            }
            if (renderer_last_light_shadow_samples(app->renderer) == 0u ||
                renderer_last_partition_guided_samples(app->renderer) == 0u ||
                renderer_last_light_guided_samples(app->renderer) == 0u) {
                fprintf(stderr,
                        "[RENDER] RTX adaptive-light history was not consumed "
                        "(shadow=%zu partition=%zu light=%zu)\n",
                        renderer_last_light_shadow_samples(app->renderer),
                        renderer_last_partition_guided_samples(app->renderer),
                        renderer_last_light_guided_samples(app->renderer));
                app->exit_code = 1;
                return 0;
            }
            fprintf(stdout,
                    "[RENDER] RTX adaptive-light history consumed "
                    "(shadow=%zu partition=%zu light=%zu)\n",
                    renderer_last_light_shadow_samples(app->renderer),
                    renderer_last_partition_guided_samples(app->renderer),
                    renderer_last_light_guided_samples(app->renderer));
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
        if (smoke_backend == RENDERER_BACKEND_VULKAN_RTX &&
            level_index == (app->gpu_smoke_all_levels != 0u ? 1u :
                                                               first_level)) {
            static const RendererRtxDebugView capture_views[] = {
                RENDERER_RTX_DEBUG_ALBEDO,
                RENDERER_RTX_DEBUG_NORMAL,
                RENDERER_RTX_DEBUG_ROUGHNESS,
                RENDERER_RTX_DEBUG_METALNESS,
                RENDERER_RTX_DEBUG_EMISSIVE,
                RENDERER_RTX_DEBUG_DIRECT,
                RENDERER_RTX_DEBUG_INDIRECT,
                RENDERER_RTX_DEBUG_SPECULAR,
                RENDERER_RTX_DEBUG_VARIANCE
            };
            uint64_t captures[
                sizeof(capture_views) / sizeof(capture_views[0])] = {0};
            int capture_has_direct =
                renderer_last_direct_light_energy(app->renderer) != 0u;

            for (size_t capture = 0u;
                 capture < sizeof(capture_views) / sizeof(capture_views[0]);
                 ++capture) {
                if (!renderer_set_rtx_debug_view(
                        app->renderer, capture_views[capture]) ||
                    !renderer_present(app->renderer, &app->frame, &app->view,
                                      error, sizeof(error))) {
                    fprintf(stderr,
                            "[RENDER] RTX %s debug capture failed: %s\n",
                            renderer_rtx_debug_view_name(capture_views[capture]),
                            error);
                    app->exit_code = 1;
                    return 0;
                }
                captures[capture] =
                    renderer_last_frame_rgb_checksum(app->renderer);
            }
            /* The direct-light diagnostic includes diffuse and specular
             * energy. A fully metallic view can therefore have a black
             * direct-diffuse channel while its specular channel is live. */
            if (captures[0] == captures[1] || captures[2] == captures[3] ||
                (capture_has_direct && captures[5] == captures[6] &&
                 captures[7] == captures[6])) {
                fprintf(stderr,
                        "[RENDER] RTX debug captures do not contain distinct PBR/lighting "
                        "channels: albedo=%llu normal=%llu roughness=%llu metalness=%llu "
                        "emissive=%llu direct=%llu indirect=%llu specular=%llu variance=%llu\n",
                        (unsigned long long)captures[0],
                        (unsigned long long)captures[1],
                        (unsigned long long)captures[2],
                        (unsigned long long)captures[3],
                        (unsigned long long)captures[4],
                        (unsigned long long)captures[5],
                        (unsigned long long)captures[6],
                        (unsigned long long)captures[7],
                        (unsigned long long)captures[8]);
                app->exit_code = 1;
                return 0;
            }
            if (!renderer_set_rtx_debug_view(
                    app->renderer, app->desktop_settings.rtx_debug_view)) {
                fprintf(stderr, "[RENDER] RTX debug capture restore failed\n");
                app->exit_code = 1;
                return 0;
            }
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
