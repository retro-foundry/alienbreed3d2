#include <stdio.h>
#include <string.h>

#include "desktop_settings.h"

static int expect_settings(const DesktopSettings *settings, uint16_t level, int health, int ammo,
                           int weapons, int keys, int quicksave_load, int load_autosave,
                           int always_run, uint8_t volume, uint8_t world_light_tessellation,
                           RendererBackend renderer_backend, uint16_t rtx_target_fps,
                           RendererRtxDebugView rtx_debug_view)
{
    return settings->start_level_index == level &&
        (settings->infinite_health != 0u) == health &&
        (settings->infinite_ammo != 0u) == ammo &&
        (settings->all_weapons != 0u) == weapons &&
        (settings->all_keys != 0u) == keys &&
        (settings->quicksave_load != 0u) == quicksave_load &&
        (settings->load_autosave != 0u) == load_autosave &&
        (settings->always_run != 0u) == always_run && settings->volume == volume &&
        settings->world_light_tessellation == world_light_tessellation &&
        settings->renderer_backend == renderer_backend &&
        settings->rtx_target_fps == rtx_target_fps &&
        settings->rtx_debug_view == rtx_debug_view;
}

int main(void)
{
    static const char settings_text[] =
        "# desktop session preferences\n"
        "start_level = 16\n"
        "infinite_health = yes\n"
        "infinite_ammo = on\n"
        "all_weapons = true\n"
        "all_keys = yes\n"
        "quicksave_load = yes\n"
        "load_autosave = on\n"
        "run_default = off\n"
        "volume = 37\n"
        "world_light_tessellation = 8\n"
        "renderer = RTX\n"
        "rtx_target_fps = 75\n"
        "rtx_debug_view = indirect\n"
        "unrelated_source_option = keep\n";
    DesktopSettings settings;
    char error[256] = {0};

    desktop_settings_default(&settings);
    if (!expect_settings(&settings, 0u, 0, 0, 0, 0, 0, 0, 1, 100u, 4u,
                         RENDERER_BACKEND_OPENGL, 60u, RENDERER_RTX_DEBUG_FINAL)) {
        fprintf(stderr, "desktop settings defaults differ from the documented template\n");
        return 1;
    }
    if (!desktop_settings_parse(&settings, settings_text, strlen(settings_text), error, sizeof(error)) ||
        !expect_settings(&settings, 15u, 1, 1, 1, 1, 1, 1, 0, 37u, 8u,
                         RENDERER_BACKEND_VULKAN_RTX, 75u,
                         RENDERER_RTX_DEBUG_INDIRECT)) {
        fprintf(stderr, "desktop settings parser did not apply valid values: %s\n", error);
        return 1;
    }

    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "renderer=vulkan\n", 16u,
                               error, sizeof(error)) ||
        strstr(error, "renderer") == NULL) {
        fprintf(stderr, "invalid renderer option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (!desktop_settings_parse(&settings, "renderer=opengl\n", 16u,
                                error, sizeof(error)) ||
        settings.renderer_backend != RENDERER_BACKEND_OPENGL ||
        !desktop_settings_parse(&settings, "renderer=rtx\n", 13u,
                                error, sizeof(error)) ||
        settings.renderer_backend != RENDERER_BACKEND_VULKAN_RTX) {
        fprintf(stderr, "valid renderer options were rejected: %s\n", error);
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "all_keys=maybe\n", 15u,
                               error, sizeof(error)) ||
        strstr(error, "all_keys") == NULL) {
        fprintf(stderr, "invalid all-keys option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "quicksave_load=maybe\n", 21u,
                               error, sizeof(error)) ||
        strstr(error, "quicksave_load") == NULL) {
        fprintf(stderr, "invalid quicksave/load option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "load_autosave=maybe\n",
                               strlen("load_autosave=maybe\n"), error, sizeof(error)) ||
        strstr(error, "load_autosave") == NULL) {
        fprintf(stderr, "invalid startup autosave option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "start_level=0\n", 14u, error, sizeof(error)) ||
        strstr(error, "start_level") == NULL) {
        fprintf(stderr, "invalid one-indexed start level was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "volume=101\n", 11u, error, sizeof(error)) ||
        strstr(error, "volume") == NULL) {
        fprintf(stderr, "invalid volume was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "world_light_tessellation=3\n", 27u,
                               error, sizeof(error)) ||
        strstr(error, "world_light_tessellation") == NULL) {
        fprintf(stderr, "invalid world-light tessellation was not reported clearly\n");
        return 1;
    }
    for (uint8_t factor = 1u; factor <= 8u; factor = (uint8_t)(factor * 2u)) {
        char text[64];
        int length = snprintf(text, sizeof(text), "world_light_tessellation=%u\n", factor);

        desktop_settings_default(&settings);
        if (length <= 0 ||
            !desktop_settings_parse(&settings, text, (size_t)length, error, sizeof(error)) ||
            settings.world_light_tessellation != factor) {
            fprintf(stderr, "valid world-light tessellation %u was rejected: %s\n", factor,
                    error);
            return 1;
        }
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "rtx_target_fps=29\n", 18u,
                               error, sizeof(error)) ||
        strstr(error, "rtx_target_fps") == NULL) {
        fprintf(stderr, "invalid RTX target frame rate was not reported clearly\n");
        return 1;
    }
    for (uint16_t rate = 30u; rate <= 240u;
         rate = rate == 30u ? 60u : 240u) {
        char text[48];
        int length = snprintf(text, sizeof(text),
                              "rtx_target_fps=%u\n", rate);

        desktop_settings_default(&settings);
        if (length <= 0 || !desktop_settings_parse(
                &settings, text, (size_t)length, error, sizeof(error)) ||
            settings.rtx_target_fps != rate) {
            fprintf(stderr, "valid RTX target rate was rejected: %s\n", error);
            return 1;
        }
        if (rate == 240u) break;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "rtx_target_fps=fast\n", 20u,
                               error, sizeof(error)) ||
        strstr(error, "rtx_target_fps") == NULL) {
        fprintf(stderr, "malformed RTX target rate was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(&settings, "rtx_debug_view=lighting\n", 24u,
                               error, sizeof(error)) ||
        strstr(error, "rtx_debug_view") == NULL) {
        fprintf(stderr, "invalid RTX debug view was not reported clearly\n");
        return 1;
    }
    for (int view = RENDERER_RTX_DEBUG_FINAL;
         view <= RENDERER_RTX_DEBUG_VARIANCE; ++view) {
        char text[64];
        int length = snprintf(text, sizeof(text), "rtx_debug_view=%s\n",
                              renderer_rtx_debug_view_name((RendererRtxDebugView)view));

        desktop_settings_default(&settings);
        if (length <= 0 || !desktop_settings_parse(
                &settings, text, (size_t)length, error, sizeof(error)) ||
            settings.rtx_debug_view != (RendererRtxDebugView)view) {
            fprintf(stderr, "valid RTX debug view was rejected: %s\n", error);
            return 1;
        }
    }
    return 0;
}
