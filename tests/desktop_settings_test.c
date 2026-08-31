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

    /* Ray-traced numeric settings remain absent by default. Display output is
     * explicitly SDR because Q2RTX makes HDR opt-in. */
    if (RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES != 16 ||
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_SAMPLES_PER_PIXEL != 4 ||
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_LIGHT_SAMPLES != 2 ||
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_TEMPORAL_FRAMES != 1 ||
        RENDERER_RAY_TRACING_DEFAULT_RESERVOIR_SAMPLE_LIMIT != 32 ||
        RENDERER_RAY_TRACING_DEFAULT_DIFFUSE_GI_SCALE != 0.75f) {
        fprintf(stderr, "ray-traced lighting production defaults changed\n");
        return 1;
    }
    desktop_settings_default(&settings);
    if (settings.ray_tracing.samples_per_pixel != 0u ||
        settings.ray_tracing.indirect_samples_per_pixel != 0u ||
        settings.ray_tracing.indirect_light_samples != 0u ||
        settings.ray_tracing.indirect_temporal_frames != 0u ||
        settings.ray_tracing.diffuse_gi_scale != 0.0f ||
        settings.ray_tracing.diffuse_gi_scale_set != 0u ||
        settings.ray_tracing.maximum_bounces != 0u ||
        settings.ray_tracing.light_candidates != 0u ||
        settings.ray_tracing.reservoir_sample_limit != 0u ||
        settings.ray_tracing.reservoir_sample_limit_set != 0u ||
        settings.ray_tracing.radiance_clamp != 0.0f ||
        settings.ray_tracing.exposure_bias_stops != 0.0f ||
        settings.ray_tracing.exposure_bias_set != 0u ||
        settings.ray_tracing.ndf_trim != 0.0f ||
        settings.ray_tracing.reconstruction !=
            RENDERER_RAY_RECONSTRUCTION_DEFAULT ||
        settings.ray_tracing.output != RENDERER_OUTPUT_SDR ||
        settings.ray_tracing.hdr_peak_nits != 0.0f ||
        settings.ray_tracing.hdr_saturation_percent != 0.0f ||
        settings.ray_tracing.hdr_saturation_percent_set != 0u) {
        fprintf(stderr, "ray-tracing settings did not default to the renderer's own\n");
        return 1;
    }
    {
        static const char text[] =
            "rtx_samples_per_pixel=4\n"
            "rtx_indirect_samples=12\n"
            "rtx_indirect_light_samples=2\n"
            "rtx_gi_temporal_frames=1\n"
            "rtx_diffuse_gi=0.625\n"
            "rtx_max_bounces=2\n"
            "rtx_light_candidates=16\n"
            "rtx_reservoir_limit=32\n"
            "rtx_radiance_clamp=0\n"
            "rtx_exposure_bias=-1.25\n"
            "rtx_ndf_trim=0.75\n"
            "rtx_ray_reconstruction=performance\n"
            "rtx_output=hdr\n"
            "rtx_hdr_peak_nits=1200\n"
            "rtx_hdr_saturation=125\n";

        desktop_settings_default(&settings);
        if (!desktop_settings_parse(&settings, text, sizeof(text) - 1u,
                                    error, sizeof(error)) ||
            settings.ray_tracing.samples_per_pixel != 4u ||
            settings.ray_tracing.indirect_samples_per_pixel != 12u ||
            settings.ray_tracing.indirect_light_samples != 2u ||
            settings.ray_tracing.indirect_temporal_frames != 1u ||
            settings.ray_tracing.diffuse_gi_scale != 0.625f ||
            settings.ray_tracing.diffuse_gi_scale_set == 0u ||
            settings.ray_tracing.maximum_bounces != 2u ||
            settings.ray_tracing.light_candidates != 16u ||
            settings.ray_tracing.reservoir_sample_limit != 32u ||
            settings.ray_tracing.reservoir_sample_limit_set == 0u ||
            settings.ray_tracing.radiance_clamp != 0.0f ||
            settings.ray_tracing.exposure_bias_stops < -1.26f ||
            settings.ray_tracing.exposure_bias_stops > -1.24f ||
            settings.ray_tracing.exposure_bias_set == 0u ||
            settings.ray_tracing.ndf_trim < 0.74f ||
            settings.ray_tracing.ndf_trim > 0.76f ||
            settings.ray_tracing.reconstruction !=
                RENDERER_RAY_RECONSTRUCTION_PERFORMANCE ||
            settings.ray_tracing.output != RENDERER_OUTPUT_HDR ||
            settings.ray_tracing.hdr_peak_nits != 1200.0f ||
            settings.ray_tracing.hdr_saturation_percent != 125.0f ||
            settings.ray_tracing.hdr_saturation_percent_set == 0u) {
            fprintf(stderr, "ray-tracing settings were not applied: %s\n", error);
            return 1;
        }
    }
    {
        static const char text[] =
            "rtx_diffuse_gi=0\n"
            "rtx_exposure_bias=0\n"
            "rtx_hdr_saturation=0\n";

        desktop_settings_default(&settings);
        if (!desktop_settings_parse(&settings, text, sizeof(text) - 1u,
                                    error, sizeof(error)) ||
            settings.ray_tracing.diffuse_gi_scale != 0.0f ||
            settings.ray_tracing.diffuse_gi_scale_set == 0u ||
            settings.ray_tracing.exposure_bias_stops != 0.0f ||
            settings.ray_tracing.exposure_bias_set == 0u ||
            settings.ray_tracing.hdr_saturation_percent != 0.0f ||
            settings.ray_tracing.hdr_saturation_percent_set == 0u) {
            fprintf(stderr, "zero GI/EV/saturation settings were not preserved: %s\n",
                    error);
            return 1;
        }
    }
    /*
     * Zero is meaningful for diffuse GI, the radiance clamp, exposure bias,
     * reservoir limit, and HDR saturation. Other zero/default sentinels remain
     * invalid.
     */
    {
        static const char *const rejected[] = {
            "rtx_samples_per_pixel=0\n",
            "rtx_samples_per_pixel=9\n",
            "rtx_indirect_samples=0\n",
            "rtx_indirect_samples=33\n",
            "rtx_indirect_light_samples=0\n",
            "rtx_indirect_light_samples=3\n",
            "rtx_gi_temporal_frames=0\n",
            "rtx_gi_temporal_frames=2\n",
            "rtx_diffuse_gi=-0.01\n",
            "rtx_diffuse_gi=1.01\n",
            "rtx_max_bounces=0\n",
            "rtx_light_candidates=0\n",
            "rtx_radiance_clamp=-0.1\n",
            "rtx_radiance_clamp=100001\n",
            "rtx_exposure_bias=-5.1\n",
            "rtx_exposure_bias=0.1\n",
            "rtx_exposure=1\n",
            "rtx_ndf_trim=0\n",
            "rtx_ndf_trim=1.5\n",
            "rtx_ray_reconstruction=fastest\n",
            "rtx_output=automatic\n",
            "rtx_hdr_peak_nits=99\n",
            "rtx_hdr_peak_nits=2001\n",
            "rtx_hdr_paper_white_nits=200\n",
            "rtx_hdr_saturation=-1\n",
            "rtx_hdr_saturation=201\n",
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
    {
        static const char *const values[] = {"auto", "sdr", "hdr"};
        static const RendererOutputMode modes[] = {
            RENDERER_OUTPUT_AUTO, RENDERER_OUTPUT_SDR, RENDERER_OUTPUT_HDR};
        size_t index;
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); ++index) {
            char text[64];
            int length = snprintf(text, sizeof(text), "rtx_output=%s\n",
                                  values[index]);
            desktop_settings_default(&settings);
            if (length <= 0 || !desktop_settings_parse(
                    &settings, text, (size_t)length, error, sizeof(error)) ||
                settings.ray_tracing.output != modes[index]) {
                fprintf(stderr, "rtx_output=%s was rejected: %s\n",
                        values[index], error);
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
