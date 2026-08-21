#include <stdio.h>
#include <string.h>

#include "desktop_settings.h"

static int expect_settings(
    const DesktopSettings *settings, uint16_t level, int health, int ammo,
    int weapons, int keys, int quicksave_load, int load_autosave,
    int always_run, uint8_t volume, uint8_t world_light_tessellation,
    RendererBackend renderer_backend)
{
    return settings->start_level_index == level &&
        (settings->infinite_health != 0u) == health &&
        (settings->infinite_ammo != 0u) == ammo &&
        (settings->all_weapons != 0u) == weapons &&
        (settings->all_keys != 0u) == keys &&
        (settings->quicksave_load != 0u) == quicksave_load &&
        (settings->load_autosave != 0u) == load_autosave &&
        (settings->always_run != 0u) == always_run &&
        settings->volume == volume &&
        settings->world_light_tessellation == world_light_tessellation &&
        settings->renderer_backend == renderer_backend;
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
        "unrelated_source_option = keep\n";
    DesktopSettings settings;
    char error[256] = {0};

    desktop_settings_default(&settings);
    if (!expect_settings(
            &settings, 0u, 0, 0, 0, 0, 0, 0, 1, 100u, 4u,
            RENDERER_BACKEND_OPENGL)) {
        fprintf(stderr, "desktop settings defaults differ from the documented template\n");
        return 1;
    }
    if (!desktop_settings_parse(
            &settings, settings_text, strlen(settings_text), error, sizeof(error)) ||
        !expect_settings(
            &settings, 15u, 1, 1, 1, 1, 1, 1, 0, 37u, 8u,
            RENDERER_BACKEND_RTX)) {
        fprintf(stderr, "desktop settings parser did not apply valid values: %s\n", error);
        return 1;
    }

    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "renderer=vulkan\n", 16u, error, sizeof(error)) ||
        strstr(error, "renderer") == NULL) {
        fprintf(stderr, "invalid renderer option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (!desktop_settings_parse(
            &settings, "renderer=opengl\n", 16u, error, sizeof(error)) ||
        settings.renderer_backend != RENDERER_BACKEND_OPENGL ||
        !desktop_settings_parse(
            &settings, "renderer=rtx\n", 13u, error, sizeof(error)) ||
        settings.renderer_backend != RENDERER_BACKEND_RTX) {
        fprintf(stderr, "valid renderer options were rejected: %s\n", error);
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "all_keys=maybe\n", 15u, error, sizeof(error)) ||
        strstr(error, "all_keys") == NULL) {
        fprintf(stderr, "invalid all-keys option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "quicksave_load=maybe\n", 21u, error, sizeof(error)) ||
        strstr(error, "quicksave_load") == NULL) {
        fprintf(stderr, "invalid quicksave/load option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "load_autosave=maybe\n",
            strlen("load_autosave=maybe\n"), error, sizeof(error)) ||
        strstr(error, "load_autosave") == NULL) {
        fprintf(stderr, "invalid startup autosave option was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "start_level=0\n", 14u, error, sizeof(error)) ||
        strstr(error, "start_level") == NULL) {
        fprintf(stderr, "invalid one-indexed start level was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "volume=101\n", 11u, error, sizeof(error)) ||
        strstr(error, "volume") == NULL) {
        fprintf(stderr, "invalid volume was not reported clearly\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (desktop_settings_parse(
            &settings, "world_light_tessellation=3\n", 27u,
            error, sizeof(error)) ||
        strstr(error, "world_light_tessellation") == NULL) {
        fprintf(stderr, "invalid world-light tessellation was not reported clearly\n");
        return 1;
    }
    for (uint8_t factor = 1u; factor <= 8u; factor = (uint8_t)(factor * 2u)) {
        char text[64];
        int length = snprintf(
            text, sizeof(text), "world_light_tessellation=%u\n", factor);

        desktop_settings_default(&settings);
        if (length <= 0 || !desktop_settings_parse(
                &settings, text, (size_t)length, error, sizeof(error)) ||
            settings.world_light_tessellation != factor) {
            fprintf(stderr, "valid world-light tessellation %u was rejected: %s\n",
                    factor, error);
            return 1;
        }
    }

    /*
     * Ray-traced backend settings. Every field defaults to zero, which is what
     * the renderer reads as "keep your own default", so an INI that mentions
     * none of them must leave the whole block zeroed.
     */
    desktop_settings_default(&settings);
    if (settings.ray_tracing.samples_per_pixel != 0u ||
        settings.ray_tracing.maximum_bounces != 0u ||
        settings.ray_tracing.light_candidates != 0u ||
        settings.ray_tracing.reservoir_sample_limit != 0u ||
        settings.ray_tracing.reservoir_sample_limit_set != 0u ||
        settings.ray_tracing.radiance_clamp != 0.0f ||
        settings.ray_tracing.exposure != 0.0f ||
        settings.ray_tracing.ndf_trim != 0.0f ||
        settings.ray_tracing.reconstruction !=
            RENDERER_RAY_RECONSTRUCTION_DEFAULT) {
        fprintf(stderr, "ray-tracing settings did not default to the renderer's own\n");
        return 1;
    }
    {
        static const char text[] =
            "rtx_samples_per_pixel=4\n"
            "rtx_max_bounces=2\n"
            "rtx_light_candidates=16\n"
            "rtx_reservoir_limit=32\n"
            "rtx_radiance_clamp=50.5\n"
            "rtx_exposure=1.25\n"
            "rtx_ndf_trim=0.75\n"
            "rtx_ray_reconstruction=performance\n";

        desktop_settings_default(&settings);
        if (!desktop_settings_parse(&settings, text, sizeof(text) - 1u,
                                    error, sizeof(error)) ||
            settings.ray_tracing.samples_per_pixel != 4u ||
            settings.ray_tracing.maximum_bounces != 2u ||
            settings.ray_tracing.light_candidates != 16u ||
            settings.ray_tracing.reservoir_sample_limit != 32u ||
            settings.ray_tracing.reservoir_sample_limit_set == 0u ||
            settings.ray_tracing.radiance_clamp < 50.4f ||
            settings.ray_tracing.radiance_clamp > 50.6f ||
            settings.ray_tracing.exposure < 1.24f ||
            settings.ray_tracing.exposure > 1.26f ||
            settings.ray_tracing.ndf_trim < 0.74f ||
            settings.ray_tracing.ndf_trim > 0.76f ||
            settings.ray_tracing.reconstruction !=
                RENDERER_RAY_RECONSTRUCTION_PERFORMANCE) {
            fprintf(stderr, "ray-tracing settings were not applied: %s\n", error);
            return 1;
        }
    }
    /*
     * Zero means "renderer default" on every field, so a key that names it has
     * to be rejected rather than silently meaning nothing. The reservoir limit
     * is the exception: zero is a real setting there and is also its default.
     */
    {
        static const char *const rejected[] = {
            "rtx_samples_per_pixel=0\n",
            "rtx_samples_per_pixel=9\n",
            "rtx_max_bounces=0\n",
            "rtx_light_candidates=0\n",
            "rtx_radiance_clamp=0\n",
            "rtx_exposure=0\n",
            "rtx_ndf_trim=0\n",
            "rtx_ndf_trim=1.5\n",
            "rtx_ray_reconstruction=fastest\n",
        };
        size_t index;

        for (index = 0u; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
            desktop_settings_default(&settings);
            if (desktop_settings_parse(&settings, rejected[index],
                                       strlen(rejected[index]),
                                       error, sizeof(error))) {
                fprintf(stderr, "ray-tracing setting \"%s\" was accepted\n",
                        rejected[index]);
                return 1;
            }
        }
    }
    desktop_settings_default(&settings);
    if (!desktop_settings_parse(&settings, "rtx_reservoir_limit=0\n", 22u,
                                error, sizeof(error)) ||
        settings.ray_tracing.reservoir_sample_limit != 0u ||
        settings.ray_tracing.reservoir_sample_limit_set == 0u) {
        fprintf(stderr, "a zero reservoir limit was not accepted: %s\n", error);
        return 1;
    }
    return 0;
}
