#include "desktop_settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DESKTOP_SETTINGS_LEVEL_COUNT = 16u,
    DESKTOP_SETTINGS_LINE_CAPACITY = 512u
};

static void desktop_settings_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static char *desktop_settings_trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static int desktop_settings_equals_ci(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int desktop_settings_parse_bool(const char *text, uint8_t *out_value)
{
    if (!text || !out_value) {
        return 0;
    }
    if (desktop_settings_equals_ci(text, "1") || desktop_settings_equals_ci(text, "true") ||
        desktop_settings_equals_ci(text, "yes") || desktop_settings_equals_ci(text, "on")) {
        *out_value = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(text, "0") || desktop_settings_equals_ci(text, "false") ||
        desktop_settings_equals_ci(text, "no") || desktop_settings_equals_ci(text, "off")) {
        *out_value = 0u;
        return 1;
    }
    return 0;
}

static int desktop_settings_parse_unsigned(const char *text, unsigned long maximum,
                                           unsigned long *out_value)
{
    char *end;
    unsigned long value;

    if (!text || !*text || !out_value || text[0] == '-') {
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *desktop_settings_trim(end) != '\0' || value > maximum) {
        return 0;
    }
    *out_value = value;
    return 1;
}

/* A positive, finite decimal for settings whose zero remains an absence marker. */
static int desktop_settings_parse_positive_float(const char *text, double maximum,
                                                 float *out_value)
{
    char *end;
    double value;

    if (!text || !*text || !out_value) {
        return 0;
    }
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *desktop_settings_trim(end) != '\0' ||
        !(value > 0.0) || value > maximum) {
        return 0;
    }
    *out_value = (float)value;
    return 1;
}

static int desktop_settings_parse_float_range(const char *text, double minimum,
                                              double maximum, float *out_value)
{
    char *end;
    double value;

    if (!text || !*text || !out_value) {
        return 0;
    }
    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *desktop_settings_trim(end) != '\0' ||
        !(value >= minimum && value <= maximum)) {
        return 0;
    }
    *out_value = (float)value;
    return 1;
}

/*
 * Render-resolution diagnostic views. Every name maps to a buffer inspected
 * before any upscaling runs, because a reconstructed image cannot establish
 * that the estimator underneath it is correct.
 */
static int desktop_settings_parse_debug_view(const char *value,
                                             RendererDebugView *out)
{
    static const struct {
        const char *name;
        RendererDebugView view;
    } views[] = {
        {"off", RENDERER_DEBUG_VIEW_OFF},
        {"reference", RENDERER_DEBUG_VIEW_REFERENCE},
        {"canonical", RENDERER_DEBUG_VIEW_CANONICAL},
        {"temporal", RENDERER_DEBUG_VIEW_TEMPORAL},
        {"spatial", RENDERER_DEBUG_VIEW_SPATIAL},
        {"motion", RENDERER_DEBUG_VIEW_MOTION},
        {"reprojection", RENDERER_DEBUG_VIEW_REPROJECTION},
        {"rejection", RENDERER_DEBUG_VIEW_REJECTION},
        {"history-age", RENDERER_DEBUG_VIEW_HISTORY_AGE},
        {"history_age", RENDERER_DEBUG_VIEW_HISTORY_AGE},
        {"reservoir-m", RENDERER_DEBUG_VIEW_RESERVOIR_M},
        {"reservoir_m", RENDERER_DEBUG_VIEW_RESERVOIR_M},
        {"ancestry", RENDERER_DEBUG_VIEW_ANCESTRY},
        {"duplication", RENDERER_DEBUG_VIEW_DUPLICATION},
    };
    size_t index;
    for (index = 0u; index < sizeof(views) / sizeof(views[0]); ++index) {
        if (desktop_settings_equals_ci(value, views[index].name)) {
            *out = views[index].view;
            return 1;
        }
    }
    return 0;
}

static int desktop_settings_apply_line(DesktopSettings *settings, char *line,
                                       size_t line_number, char *error, size_t error_size)
{
    char *equals = strchr(line, '=');
    char *key;
    char *value;
    unsigned long number;

    if (!equals) {
        return 1;
    }
    *equals = '\0';
    key = desktop_settings_trim(line);
    value = desktop_settings_trim(equals + 1);
    if (*key == '\0') {
        (void)snprintf(error, error_size, "ab3d2.ini line %zu has no key", line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "start_level")) {
        if (!desktop_settings_parse_unsigned(value, DESKTOP_SETTINGS_LEVEL_COUNT, &number) ||
            number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: start_level must be 1 through %u", line_number,
                           DESKTOP_SETTINGS_LEVEL_COUNT);
            return 0;
        }
        settings->start_level_index = (uint16_t)(number - 1u);
        return 1;
    }
    if (desktop_settings_equals_ci(key, "infinite_health")) {
        if (!desktop_settings_parse_bool(value, &settings->infinite_health)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: infinite_health must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "infinite_ammo")) {
        if (!desktop_settings_parse_bool(value, &settings->infinite_ammo)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: infinite_ammo must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "all_weapons")) {
        if (!desktop_settings_parse_bool(value, &settings->all_weapons)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: all_weapons must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "all_keys")) {
        if (!desktop_settings_parse_bool(value, &settings->all_keys)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: all_keys must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "quicksave_load") ||
        desktop_settings_equals_ci(key, "quick_save_load") ||
        desktop_settings_equals_ci(key, "quickload_save")) {
        if (!desktop_settings_parse_bool(value, &settings->quicksave_load)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: quicksave_load must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "load_autosave")) {
        if (!desktop_settings_parse_bool(value, &settings->load_autosave)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: load_autosave must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "always_run") ||
        desktop_settings_equals_ci(key, "run_default")) {
        if (!desktop_settings_parse_bool(value, &settings->always_run)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: always_run must be a boolean", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "volume")) {
        if (!desktop_settings_parse_unsigned(value, 100u, &number)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: volume must be 0 through 100", line_number);
            return 0;
        }
        settings->volume = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "world_light_tessellation")) {
        if (!desktop_settings_parse_unsigned(value, 8u, &number) ||
            (number != 1u && number != 2u && number != 4u && number != 8u)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: world_light_tessellation must be 1, 2, 4, or 8",
                           line_number);
            return 0;
        }
        settings->world_light_tessellation = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "renderer")) {
        if (!renderer_backend_from_string(value, &settings->renderer_backend)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: renderer must be opengl or rtx",
                           line_number);
            return 0;
        }
        return 1;
    }
    /*
     * Ray-traced backend settings. Each is presentation-only and trades image
     * quality against frame cost; an absent key leaves the renderer's own
     * documented default in place.
     */
    if (desktop_settings_equals_ci(key, "rtx_samples_per_pixel")) {
        if (!desktop_settings_parse_unsigned(value, 8u, &number) || number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_samples_per_pixel must be 1 through 8",
                           line_number);
            return 0;
        }
        settings->ray_tracing.samples_per_pixel = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_indirect_samples")) {
        if (!desktop_settings_parse_unsigned(value, 32u, &number) || number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_indirect_samples must be 1 through 32",
                           line_number);
            return 0;
        }
        settings->ray_tracing.indirect_samples_per_pixel = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_indirect_light_samples")) {
        if (!desktop_settings_parse_unsigned(value, 2u, &number) || number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_indirect_light_samples must be 1 or 2",
                           line_number);
            return 0;
        }
        settings->ray_tracing.indirect_light_samples = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_gi_temporal_frames")) {
        (void)snprintf(error, error_size,
                       "ab3d2.ini line %zu: rtx_gi_temporal_frames was retired; temporal reuse now belongs to ReSTIR PT, so use rtx_restir_temporal_history",
                       line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "rtx_diffuse_gi")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 1.0,
                &settings->ray_tracing.diffuse_gi_scale)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_diffuse_gi must be 0 through 1",
                           line_number);
            return 0;
        }
        settings->ray_tracing.diffuse_gi_scale_set = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_specular_roughness")) {
        if (!desktop_settings_parse_float_range(
                value, 0.3, 1.0,
                &settings->ray_tracing.specular_roughness_limit)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_specular_roughness must be 0.3 through 1",
                           line_number);
            return 0;
        }
        settings->ray_tracing.specular_roughness_limit_set = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_max_bounces")) {
        if (!desktop_settings_parse_unsigned(value, 8u, &number) || number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_max_bounces must be 1 through 8",
                           line_number);
            return 0;
        }
        settings->ray_tracing.maximum_bounces = (uint8_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_light_candidates")) {
        if (!desktop_settings_parse_unsigned(value, 1024u, &number) || number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_light_candidates must be 1 through 1024",
                           line_number);
            return 0;
        }
        settings->ray_tracing.light_candidates = (uint16_t)number;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_reservoir_limit")) {
        (void)snprintf(error, error_size,
                       "ab3d2.ini line %zu: rtx_reservoir_limit was retired; the ReSTIR PT confidence cap is rtx_restir_temporal_history",
                       line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "rtx_radiance_clamp")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 100000.0,
                &settings->ray_tracing.radiance_clamp)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_radiance_clamp must be 0 through "
                           "100000", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_exposure")) {
        (void)snprintf(error, error_size,
                       "ab3d2.ini line %zu: rtx_exposure was replaced by the Q2RTX-compatible rtx_exposure_bias",
                       line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "rtx_exposure_bias")) {
        if (!desktop_settings_parse_float_range(
                value, -5.0, 0.0,
                &settings->ray_tracing.exposure_bias_stops)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_exposure_bias must be -5 through 0 EV",
                           line_number);
            return 0;
        }
        settings->ray_tracing.exposure_bias_set = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_ndf_trim")) {
        if (!desktop_settings_parse_positive_float(value, 1.0,
                                                  &settings->ray_tracing.ndf_trim) ||
            settings->ray_tracing.ndf_trim < 0.1f) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_ndf_trim must be 0.1 through 1",
                           line_number);
            return 0;
        }
        return 1;
    }
    /*
     * The DLSS Super Resolution quality ladder, which also fixes the internal
     * resolution every ray-traced and ReSTIR buffer runs at. rtx_denoiser
     * separately decides whether Ray Reconstruction or a renderer-owned filter
     * reconstructs the noisy signal, so the older rtx_ray_reconstruction
     * spelling survives as an alias for this ladder alone.
     */
    if (desktop_settings_equals_ci(key, "rtx_dlss") ||
        desktop_settings_equals_ci(key, "rtx_ray_reconstruction")) {
        if (desktop_settings_equals_ci(value, "quality")) {
            settings->ray_tracing.reconstruction = RENDERER_RAY_RECONSTRUCTION_QUALITY;
        } else if (desktop_settings_equals_ci(value, "balanced")) {
            settings->ray_tracing.reconstruction = RENDERER_RAY_RECONSTRUCTION_BALANCED;
        } else if (desktop_settings_equals_ci(value, "performance")) {
            settings->ray_tracing.reconstruction = RENDERER_RAY_RECONSTRUCTION_PERFORMANCE;
        } else if (desktop_settings_equals_ci(value, "ultra-performance") ||
                   desktop_settings_equals_ci(value, "ultra_performance")) {
            settings->ray_tracing.reconstruction =
                RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE;
        } else if (desktop_settings_equals_ci(value, "off")) {
            settings->ray_tracing.reconstruction = RENDERER_RAY_RECONSTRUCTION_OFF;
        } else {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: %s must be quality, balanced, "
                           "performance, ultra-performance, or off",
                           line_number, key);
            return 0;
        }
        return 1;
    }
    /*
     * Which stage reconstructs the noisy ray-traced signal. Ray Reconstruction
     * replaces a conventional denoiser rather than running after one, so the
     * renderer's own spatial filter is disabled whenever it is selected.
     */
    if (desktop_settings_equals_ci(key, "rtx_denoiser")) {
        if (desktop_settings_equals_ci(value, "ray-reconstruction") ||
            desktop_settings_equals_ci(value, "ray_reconstruction")) {
            settings->ray_tracing.denoiser =
                RENDERER_DENOISER_RAY_RECONSTRUCTION;
        } else if (desktop_settings_equals_ci(value, "spatial")) {
            settings->ray_tracing.denoiser = RENDERER_DENOISER_SPATIAL;
        } else if (desktop_settings_equals_ci(value, "off")) {
            settings->ray_tracing.denoiser = RENDERER_DENOISER_OFF;
        } else {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_denoiser must be "
                           "ray-reconstruction, spatial, or off", line_number);
            return 0;
        }
        return 1;
    }
    /* Which estimator produces indirect lighting. */
    if (desktop_settings_equals_ci(key, "rtx_indirect_mode")) {
        if (desktop_settings_equals_ci(value, "path-trace") ||
            desktop_settings_equals_ci(value, "path_trace")) {
            settings->ray_tracing.indirect_mode = RENDERER_INDIRECT_PATH_TRACE;
        } else if (desktop_settings_equals_ci(value, "restir-pt") ||
                   desktop_settings_equals_ci(value, "restir_pt")) {
            settings->ray_tracing.indirect_mode = RENDERER_INDIRECT_RESTIR_PT;
        } else {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_indirect_mode must be "
                           "path-trace or restir-pt", line_number);
            return 0;
        }
        return 1;
    }
    /*
     * Cap on a reservoir's represented sample count M. This is the headline
     * temporal-correlation control: one disables temporal reuse while leaving
     * spatial resampling active.
     */
    if (desktop_settings_equals_ci(key, "rtx_restir_temporal_history")) {
        if (!desktop_settings_parse_unsigned(value, 64u, &number) ||
            number == 0u) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_temporal_history must be 1 through 64",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_temporal_history = (uint8_t)number;
        return 1;
    }
    /* Spatial neighbours resampled per pixel. Zero disables spatial reuse. */
    if (desktop_settings_equals_ci(key, "rtx_restir_spatial_samples")) {
        if (!desktop_settings_parse_unsigned(value, 8u, &number)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_spatial_samples must be 0 through 8",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_spatial_samples = (uint8_t)number;
        return 1;
    }
    /*
     * Expressed as a fraction of render height so that changing the DLSS
     * quality mode cannot silently alter the image-space footprint searched.
     */
    if (desktop_settings_equals_ci(key, "rtx_restir_spatial_radius")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 0.25,
                &settings->ray_tracing.restir_spatial_radius)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_spatial_radius must be 0 through 0.25 of render height",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_spatial_radius_set = UINT8_MAX;
        return 1;
    }
    /* Which consecutive vertex pairs a shifted path may reconnect through. */
    if (desktop_settings_equals_ci(key, "rtx_restir_reconnection")) {
        if (desktop_settings_equals_ci(value, "footprint")) {
            settings->ray_tracing.restir_reconnection =
                RENDERER_RECONNECTION_FOOTPRINT;
        } else if (desktop_settings_equals_ci(value, "fixed")) {
            settings->ray_tracing.restir_reconnection =
                RENDERER_RECONNECTION_FIXED;
        } else {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_reconnection must be "
                           "footprint or fixed", line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_restir_connection_footprint")) {
        if (!desktop_settings_parse_float_range(
                value, 0.1, 10.0,
                &settings->ray_tracing.restir_connection_footprint)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_connection_footprint must be 0.1 through 10",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_connection_footprint_set = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_noise_floor")) {
        if (!desktop_settings_parse_float_range(
                value, -24.0, 0.0,
                &settings->ray_tracing.noise_floor_stops)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_noise_floor must be -24 through 0",
                           line_number);
            return 0;
        }
        settings->ray_tracing.noise_floor_stops_set = UINT8_MAX;
        return 1;
    }
    /* Zero returns indirect light to pure path tracing. */
    if (desktop_settings_equals_ci(key, "rtx_bounce_light")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 1024.0,
                &settings->ray_tracing.source_light_scale)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_bounce_light must be 0 through 1024",
                           line_number);
            return 0;
        }
        settings->ray_tracing.source_light_scale_set = UINT8_MAX;
        return 1;
    }
    /* Zero disables duplication-based history reduction entirely. */
    if (desktop_settings_equals_ci(key, "rtx_restir_history_reduction")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 4.0,
                &settings->ray_tracing.restir_history_reduction)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_history_reduction must be 0 through 4",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_history_reduction_set = UINT8_MAX;
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_restir_decorrelation")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 1.0,
                &settings->ray_tracing.restir_decorrelation)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_restir_decorrelation must be 0 through 1",
                           line_number);
            return 0;
        }
        settings->ray_tracing.restir_decorrelation_set = UINT8_MAX;
        return 1;
    }
    /* Render-resolution diagnostic view, inspected before any upscaling. */
    if (desktop_settings_equals_ci(key, "rtx_debug_view")) {
        if (!desktop_settings_parse_debug_view(
                value, &settings->ray_tracing.debug_view)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_debug_view must be off, reference, "
                           "canonical, temporal, spatial, motion, reprojection, "
                           "rejection, history-age, reservoir-m, ancestry, or duplication",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_output")) {
        if (desktop_settings_equals_ci(value, "auto")) {
            settings->ray_tracing.output = RENDERER_OUTPUT_AUTO;
        } else if (desktop_settings_equals_ci(value, "sdr")) {
            settings->ray_tracing.output = RENDERER_OUTPUT_SDR;
        } else if (desktop_settings_equals_ci(value, "hdr")) {
            settings->ray_tracing.output = RENDERER_OUTPUT_HDR;
        } else {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_output must be auto, sdr, or hdr",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_hdr_peak_nits")) {
        if (!desktop_settings_parse_float_range(
                value, 100.0, 2000.0,
                &settings->ray_tracing.hdr_peak_nits)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_hdr_peak_nits must be 100 through 2000",
                           line_number);
            return 0;
        }
        return 1;
    }
    if (desktop_settings_equals_ci(key, "rtx_hdr_paper_white_nits")) {
        (void)snprintf(error, error_size,
                       "ab3d2.ini line %zu: rtx_hdr_paper_white_nits was removed; Q2RTX does not apply paper white to the scene",
                       line_number);
        return 0;
    }
    if (desktop_settings_equals_ci(key, "rtx_hdr_saturation")) {
        if (!desktop_settings_parse_float_range(
                value, 0.0, 200.0,
                &settings->ray_tracing.hdr_saturation_percent)) {
            (void)snprintf(error, error_size,
                           "ab3d2.ini line %zu: rtx_hdr_saturation must be 0 through 200 percent",
                           line_number);
            return 0;
        }
        settings->ray_tracing.hdr_saturation_percent_set = UINT8_MAX;
        return 1;
    }
    return 1;
}

void desktop_settings_default(DesktopSettings *settings)
{
    if (!settings) {
        return;
    }
    memset(settings, 0, sizeof(*settings));
    settings->always_run = UINT8_MAX;
    settings->volume = 100u;
    settings->world_light_tessellation = 4u;
    settings->renderer_backend = RENDERER_BACKEND_OPENGL;
    settings->ray_tracing.output = RENDERER_OUTPUT_SDR;
}

int desktop_settings_parse(DesktopSettings *settings, const char *text, size_t text_size,
                           char *error, size_t error_size)
{
    size_t offset = 0u;
    size_t line_number = 0u;

    if (!settings || (!text && text_size != 0u)) {
        desktop_settings_set_error(error, error_size, "desktop settings parser received null input");
        return 0;
    }
    while (offset < text_size) {
        char line[DESKTOP_SETTINGS_LINE_CAPACITY];
        size_t line_length = 0u;
        char *trimmed;

        ++line_number;
        while (offset < text_size && text[offset] != '\n') {
            if (line_length + 1u >= sizeof(line)) {
                (void)snprintf(error, error_size,
                               "ab3d2.ini line %zu exceeds %u bytes", line_number,
                               DESKTOP_SETTINGS_LINE_CAPACITY - 1u);
                return 0;
            }
            line[line_length++] = text[offset++];
        }
        if (offset < text_size && text[offset] == '\n') {
            ++offset;
        }
        line[line_length] = '\0';
        trimmed = desktop_settings_trim(line);
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        if (!desktop_settings_apply_line(settings, trimmed, line_number, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

DesktopSettingsLoadResult desktop_settings_load_file(DesktopSettings *settings,
                                                     const char *path,
                                                     char *error, size_t error_size)
{
    FILE *file;
    long file_size;
    char *text;
    size_t bytes_read;
    int parsed;

    if (!settings || !path || !*path) {
        desktop_settings_set_error(error, error_size, "desktop settings path is empty");
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) {
            return DESKTOP_SETTINGS_LOAD_NOT_FOUND;
        }
        (void)snprintf(error, error_size, "could not open %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (file_size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        (void)snprintf(error, error_size, "could not measure %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    if ((unsigned long)file_size > SIZE_MAX - 1u) {
        fclose(file);
        (void)snprintf(error, error_size, "%s is too large", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    text = malloc((size_t)file_size + 1u);
    if (!text) {
        fclose(file);
        desktop_settings_set_error(error, error_size, "out of memory loading desktop settings");
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    bytes_read = fread(text, 1u, (size_t)file_size, file);
    if (fclose(file) != 0 || bytes_read != (size_t)file_size) {
        free(text);
        (void)snprintf(error, error_size, "could not read %s", path);
        return DESKTOP_SETTINGS_LOAD_ERROR;
    }
    text[bytes_read] = '\0';
    parsed = desktop_settings_parse(settings, text, bytes_read, error, error_size);
    free(text);
    return parsed ? DESKTOP_SETTINGS_LOAD_OK : DESKTOP_SETTINGS_LOAD_ERROR;
}
