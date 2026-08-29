#include "renderer_rtx.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { REFERENCE_FRAME_COUNT = 4, RADIANCE_CHANNEL_COUNT = 7 };

#define REFERENCE_ROUGHNESS_BELOW_016 UINT32_C(0xF0000001)
#define REFERENCE_ROUGHNESS_020 UINT32_C(0xF0000002)
#define REFERENCE_ROUGHNESS_030 UINT32_C(0xF0000003)
#define REFERENCE_ROUGHNESS_100 UINT32_C(0xF0000004)
#define REFERENCE_METAL_ROUGHNESS_020 UINT32_C(0xF0000005)
#define REFERENCE_EMITTER UINT32_C(0xF0000006)
#define REFERENCE_METAL_BELOW_016 UINT32_C(0xF0000007)
#define REFERENCE_ROUGHNESS_025 UINT32_C(0xF0000008)
#define REFERENCE_ADDITIVE UINT32_C(0xF0000100)

typedef struct {
    uint32_t material_id;
    const char *name;
    int expect_diffuse;
    int expect_specular;
} DirectMaterialCase;

typedef struct {
    uint16_t *values;
    size_t value_count;
} RadianceCapture;

typedef struct {
    unsigned burst_ceiling;
    double off_luminance;
    double response_luminance[12];
    double settled_luminance;
} IndirectRecoveryCapture;

static void initialize_wall_surface(SceneMeshSurface *surface,
                                    SceneVertex *vertices,
                                    uint32_t vertex_count,
                                    uint32_t material_id)
{
    *surface = (SceneMeshSurface){0};
    surface->material.source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    surface->material.source_asset_id = material_id;
    surface->geometry.vertices = vertices;
    surface->geometry.vertex_count = vertex_count;
    surface->geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    surface->geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    surface->geometry.texture_window.u_period = 1u;
    surface->geometry.texture_window.v_period = 1u;
}

static int present_reference(RendererRtx *renderer, SceneFrame *frame,
                             RenderView *view, char *error,
                             size_t error_size)
{
    error[0] = '\0';
    if (!renderer_rtx_present(renderer, frame, view, error, error_size)) {
        fprintf(stderr, "DXR lighting reference present failed: %s\n", error);
        return 0;
    }
    const size_t invalid =
        renderer_rtx_last_invalid_lighting_or_guide_pixels(renderer);
    if (invalid != 0u) {
        fprintf(stderr,
                "DXR lighting reference produced %zu non-finite lighting/guide "
                "pixels\n",
                invalid);
        return 0;
    }
    return 1;
}

static int capture_radiance_channel(
    RendererRtx *renderer, RendererRtxRadianceChannel selection,
    const char *channel, SceneFrame *frame, RenderView *view,
    RadianceCapture *capture, char *error, size_t error_size)
{
    if (!renderer_rtx_select_radiance_channel(renderer, selection)) {
        fprintf(stderr, "Could not select DXR radiance channel %s\n", channel);
        return 0;
    }
    if (!present_reference(renderer, frame, view, error, error_size)) {
        fprintf(stderr, "DXR channel %s readback failed\n", channel);
        return 0;
    }
    capture->value_count =
        renderer_rtx_last_noisy_radiance_value_count(renderer);
    capture->values = capture->value_count != 0u ?
        (uint16_t *)malloc(capture->value_count * sizeof(*capture->values)) :
        NULL;
    if (!capture->values ||
        !renderer_rtx_copy_last_noisy_radiance(
            renderer, capture->values, capture->value_count)) {
        fprintf(stderr, "DXR channel %s returned no noisy-HDR readback\n",
                channel);
        free(capture->values);
        *capture = (RadianceCapture){0};
        return 0;
    }
    return 1;
}

static float half_to_float(uint16_t encoded)
{
    const uint32_t sign = encoded >> 15u;
    const uint32_t exponent = (encoded >> 10u) & 0x1fu;
    const uint32_t mantissa = encoded & 0x3ffu;
    float magnitude;
    if (exponent == 0u) {
        magnitude = ldexpf((float)mantissa, -24);
    } else if (exponent == 0x1fu) {
        magnitude = mantissa == 0u ? INFINITY : NAN;
    } else {
        magnitude = ldexpf((float)(0x400u + mantissa),
                           (int)exponent - 25);
    }
    return sign != 0u ? -magnitude : magnitude;
}

static float half_ulp(uint16_t encoded)
{
    const uint32_t exponent = (encoded >> 10u) & 0x1fu;
    if (exponent == 0x1fu) {
        return INFINITY;
    }
    return ldexpf(1.0f, exponent == 0u ? -24 : (int)exponent - 25);
}

static int capture_indirect_luminance(RendererRtx *renderer,
                                      double *out_luminance)
{
    const size_t value_count =
        renderer_rtx_last_noisy_radiance_value_count(renderer);
    uint16_t *values = value_count != 0u ?
        (uint16_t *)malloc(value_count * sizeof(*values)) : NULL;
    if (!values || value_count % 4u != 0u ||
        !renderer_rtx_copy_last_noisy_radiance(
            renderer, values, value_count)) {
        fprintf(stderr,
                "DXR indirect-recovery oracle returned no RGBA16F readback\n");
        free(values);
        return 0;
    }
    double sum = 0.0;
    for (size_t index = 0u; index < value_count; index += 4u) {
        const float red = half_to_float(values[index]);
        const float green = half_to_float(values[index + 1u]);
        const float blue = half_to_float(values[index + 2u]);
        if (!isfinite(red) || !isfinite(green) || !isfinite(blue)) {
            fprintf(stderr,
                    "DXR indirect-recovery oracle stored non-finite radiance\n");
            free(values);
            return 0;
        }
        sum += 0.2126 * (double)red + 0.7152 * (double)green +
            0.0722 * (double)blue;
    }
    free(values);
    *out_luminance = sum / (double)(value_count / 4u);
    return 1;
}

static void set_recovery_emitter_position(SceneVertex *vertices, int active)
{
    const int32_t offset = active ? 0 : 24000;
    vertices[0].position =
        (SceneWorldPoint){350 + offset, 7680, -80 + offset};
    vertices[1].position =
        (SceneWorldPoint){450 + offset, 7680, -80 + offset};
    vertices[2].position =
        (SceneWorldPoint){400 + offset, -7680, -80 + offset};
}

static int run_indirect_recovery_capture(
    unsigned burst_ceiling, IndirectRecoveryCapture *capture)
{
    enum {
        RECOVERY_MATURE_FRAMES = 48,
        RECOVERY_OFF_CAPTURE_FRAMES = 4,
        RECOVERY_RESPONSE_FRAMES = 12,
        RECOVERY_SETTLE_FRAMES = 64,
        RECOVERY_SETTLED_CAPTURE_FRAMES = 8
    };
    char error[1024] = {0};
    RenderView view = {0};
    RendererRayTracingOptions options = {0};
    options.samples_per_pixel = 1u;
    options.indirect_samples_per_pixel = (uint8_t)burst_ceiling;
    options.diffuse_gi_scale = 0.75f;
    options.diffuse_gi_scale_set = UINT8_MAX;
    options.maximum_bounces = 3u;
    options.light_candidates = 16u;
    options.reservoir_sample_limit = 32u;
    options.reservoir_sample_limit_set = UINT8_MAX;
    options.radiance_clamp = 0.0f;
    options.exposure_bias_stops = -1.0f;
    options.exposure_bias_set = UINT8_MAX;
    options.ndf_trim = 0.9f;
    options.reconstruction = RENDERER_RAY_RECONSTRUCTION_OFF;
    options.output = RENDERER_OUTPUT_SDR;

    SceneVertex receiver_vertices[6] = {0};
    receiver_vertices[0].position = (SceneWorldPoint){-160, 7680, 320};
    receiver_vertices[1].position = (SceneWorldPoint){160, 7680, 320};
    receiver_vertices[2].position = (SceneWorldPoint){160, -7680, 320};
    receiver_vertices[3] = receiver_vertices[0];
    receiver_vertices[4] = receiver_vertices[2];
    receiver_vertices[5].position = (SceneWorldPoint){-160, -7680, 320};
    SceneVertex wall_vertices[6] = {0};
    wall_vertices[0].position = (SceneWorldPoint){-480, 23040, -160};
    wall_vertices[1].position = (SceneWorldPoint){480, 23040, -160};
    wall_vertices[2].position = (SceneWorldPoint){480, -23040, -160};
    wall_vertices[3] = wall_vertices[0];
    wall_vertices[4] = wall_vertices[2];
    wall_vertices[5].position = (SceneWorldPoint){-480, -23040, -160};
    SceneVertex light_vertices[3] = {0};
    set_recovery_emitter_position(light_vertices, 0);

    SceneMeshSurface static_surfaces[2] = {0};
    initialize_wall_surface(&static_surfaces[0], receiver_vertices, 6u,
                            REFERENCE_ROUGHNESS_025);
    initialize_wall_surface(&static_surfaces[1], wall_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    SceneMeshSurface light_surface = {0};
    initialize_wall_surface(&light_surface, light_vertices, 3u,
                            REFERENCE_EMITTER);
    SceneCommand commands[3] = {0};
    commands[0].type = SCENE_COMMAND_CAMERA;
    commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    commands[1].data.geometry_instance.source_instance_id = 10u;
    commands[1].data.geometry_instance.mesh.source_mesh_id = 10u;
    commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_STATIC;
    commands[1].data.geometry_instance.mesh.surfaces = static_surfaces;
    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    commands[2].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    commands[2].data.geometry_instance.source_instance_id = 11u;
    commands[2].data.geometry_instance.mesh.source_mesh_id = 11u;
    commands[2].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    commands[2].data.geometry_instance.mesh.surfaces = &light_surface;
    commands[2].data.geometry_instance.mesh.surface_count = 1u;
    SceneFrame frame = {0};
    frame.commands = commands;
    frame.count = 3u;

    RendererRtx *renderer = renderer_rtx_create(
        320, 180, "AB3D2 DXR indirect recovery oracle", 0, 1, 1u,
        &options, error, sizeof(error));
    if (!renderer) {
        fprintf(stderr,
                "DXR indirect-recovery renderer creation failed at ceiling %u: %s\n",
                burst_ceiling, error);
        return 0;
    }
    int valid = renderer_rtx_enable_noisy_radiance_readback(renderer) &&
        renderer_rtx_select_radiance_channel(
            renderer, RENDERER_RTX_RADIANCE_INDIRECT);
    double off_sum = 0.0;
    double settled_sum = 0.0;
    for (unsigned frame_index = 0u;
         valid && frame_index < RECOVERY_MATURE_FRAMES; ++frame_index) {
        valid = present_reference(
            renderer, &frame, &view, error, sizeof(error));
    }
    for (unsigned frame_index = 0u;
         valid && frame_index < RECOVERY_OFF_CAPTURE_FRAMES; ++frame_index) {
        double luminance = 0.0;
        valid = present_reference(
                    renderer, &frame, &view, error, sizeof(error)) &&
            capture_indirect_luminance(renderer, &luminance);
        off_sum += luminance;
    }
    capture->burst_ceiling = burst_ceiling;
    capture->off_luminance =
        off_sum / (double)RECOVERY_OFF_CAPTURE_FRAMES;

    set_recovery_emitter_position(light_vertices, 1);
    for (unsigned frame_index = 0u;
         valid && frame_index < RECOVERY_RESPONSE_FRAMES; ++frame_index) {
        valid = present_reference(
                    renderer, &frame, &view, error, sizeof(error)) &&
            capture_indirect_luminance(
                renderer, &capture->response_luminance[frame_index]);
    }
    for (unsigned frame_index = 0u;
         valid && frame_index < RECOVERY_SETTLE_FRAMES; ++frame_index) {
        valid = present_reference(
            renderer, &frame, &view, error, sizeof(error));
    }
    for (unsigned frame_index = 0u;
         valid && frame_index < RECOVERY_SETTLED_CAPTURE_FRAMES; ++frame_index) {
        double luminance = 0.0;
        valid = present_reference(
                    renderer, &frame, &view, error, sizeof(error)) &&
            capture_indirect_luminance(renderer, &luminance);
        settled_sum += luminance;
    }
    capture->settled_luminance =
        settled_sum / (double)RECOVERY_SETTLED_CAPTURE_FRAMES;
    renderer_rtx_destroy(renderer);
    return valid;
}

static double indirect_recovery_average(
    const IndirectRecoveryCapture *capture, size_t first_frame,
    size_t frame_count)
{
    const double range =
        capture->settled_luminance - capture->off_luminance;
    if (!(range > 0.0) || frame_count == 0u) {
        return 0.0;
    }
    double sum = 0.0;
    for (size_t frame_index = first_frame;
         frame_index < first_frame + frame_count; ++frame_index) {
        sum += (capture->response_luminance[frame_index] -
                capture->off_luminance) / range;
    }
    return sum / (double)frame_count;
}

static int run_indirect_recovery_reference(void)
{
    static const unsigned ceilings[] = {16u, 8u, 4u, 2u, 1u};
    IndirectRecoveryCapture captures[
        sizeof(ceilings) / sizeof(ceilings[0])] = {0};
    if (_putenv_s("AB3D2_DXR_RADIANCE_CHANNEL", "indirect") != 0 ||
        _putenv_s("AB3D2_DXR_INDIRECT_RECONSTRUCTION", "full") != 0) {
        fprintf(stderr, "Could not configure DXR indirect-recovery oracle\n");
        return 0;
    }
    int valid = 1;
    for (size_t ceiling_index = 0u;
         valid && ceiling_index < sizeof(ceilings) / sizeof(ceilings[0]);
         ++ceiling_index) {
        valid = run_indirect_recovery_capture(
            ceilings[ceiling_index], &captures[ceiling_index]);
    }
    if (valid) {
        const IndirectRecoveryCapture *control = &captures[0];
        const double control_range =
            control->settled_luminance - control->off_luminance;
        if (!(control_range > 0.0) ||
            control->off_luminance > control->settled_luminance * 0.05) {
            fprintf(stderr,
                    "DXR indirect-recovery scene did not isolate a light toggle "
                    "(off=%g settled=%g)\n",
                    control->off_luminance, control->settled_luminance);
            valid = 0;
        }
        double control_recovery = 0.0;
        double control_fourth_frame = 0.0;
        if (valid) {
            control_recovery = indirect_recovery_average(control, 2u, 6u);
            control_fourth_frame =
                (control->response_luminance[3] -
                 control->off_luminance) / control_range;
        }
        if (valid && (control_recovery < 0.4 ||
                      control_fourth_frame < 0.4)) {
            fprintf(stderr,
                    "DXR ceiling-16 indirect recovery is too slow "
                    "(six-frame average=%.4f fourth-frame=%.4f)\n",
                    control_recovery, control_fourth_frame);
            valid = 0;
        }
        for (size_t ceiling_index = 0u;
             valid && ceiling_index < sizeof(ceilings) / sizeof(ceilings[0]);
             ++ceiling_index) {
            const IndirectRecoveryCapture *candidate =
                &captures[ceiling_index];
            const double range = candidate->settled_luminance -
                candidate->off_luminance;
            const double recovery =
                indirect_recovery_average(candidate, 2u, 6u);
            const double recovery_ratio = control_recovery > 0.0 ?
                recovery / control_recovery : 0.0;
            const double settled_ratio = control->settled_luminance > 0.0 ?
                candidate->settled_luminance /
                    control->settled_luminance : 0.0;
            const int acceptable = ceiling_index == 0u ||
                (recovery_ratio >= 0.9 && settled_ratio >= 0.9 &&
                 settled_ratio <= 1.1);
            fprintf(stdout,
                    "[DXR-RECOVERY] ceiling=%u off=%.9g settled=%.9g "
                    "recovery=%.4f recovery_ratio=%.4f settled_ratio=%.4f "
                    "decision=%s response=",
                    candidate->burst_ceiling, candidate->off_luminance,
                    candidate->settled_luminance, recovery, recovery_ratio,
                    settled_ratio,
                    ceiling_index == 0u ? "control" :
                        acceptable ? "pass" : "reject");
            for (size_t frame_index = 0u;
                 frame_index < sizeof(candidate->response_luminance) /
                                   sizeof(candidate->response_luminance[0]);
                 ++frame_index) {
                const double response = range > 0.0 ?
                    (candidate->response_luminance[frame_index] -
                     candidate->off_luminance) / range : 0.0;
                fprintf(stdout, "%s%.4f", frame_index != 0u ? "," : "",
                        response);
            }
            fprintf(stdout, "\n");
            if (!(range > 0.0)) {
                fprintf(stderr,
                        "DXR indirect-recovery ceiling %u produced no "
                        "positive settled response\n",
                        candidate->burst_ceiling);
                valid = 0;
            }
        }
    }
    (void)_putenv_s("AB3D2_DXR_RADIANCE_CHANNEL", "");
    (void)_putenv_s("AB3D2_DXR_INDIRECT_RECONSTRUCTION", "");
    return valid;
}

static int validate_radiance_channel_sum(
    const char *const *channel_names, RadianceCapture *captures)
{
    const size_t value_count = captures[0].value_count;
    size_t positive_values[RADIANCE_CHANNEL_COUNT] = {0};
    size_t mismatches = 0u;
    float maximum_error = 0.0f;
    float maximum_tolerance = 0.0f;
    for (size_t channel = 1u; channel < RADIANCE_CHANNEL_COUNT; ++channel) {
        if (captures[channel].value_count != value_count) {
            fprintf(stderr, "DXR channel %s readback size did not match\n",
                    channel_names[channel]);
            return 0;
        }
    }
    for (size_t index = 0u; index < value_count; ++index) {
        const float combined = half_to_float(captures[0].values[index]);
        float isolated_sum = 0.0f;
        float tolerance = 2.0f * half_ulp(captures[0].values[index]);
        positive_values[0] += combined > 0.0f;
        for (size_t channel = 1u; channel < RADIANCE_CHANNEL_COUNT;
             ++channel) {
            const float isolated =
                half_to_float(captures[channel].values[index]);
            if (!isfinite(isolated)) {
                fprintf(stderr, "DXR channel %s stored non-finite radiance\n",
                        channel_names[channel]);
                return 0;
            }
            isolated_sum += isolated;
            tolerance += half_ulp(captures[channel].values[index]);
            positive_values[channel] += isolated > 0.0f;
        }
        if (!isfinite(combined)) {
            fprintf(stderr, "DXR combined channel stored non-finite radiance\n");
            return 0;
        }
        const float difference = fabsf(combined - isolated_sum);
        if (difference > maximum_error) {
            maximum_error = difference;
            maximum_tolerance = tolerance;
        }
        mismatches += difference > tolerance;
    }
    for (size_t channel = 0u; channel < RADIANCE_CHANNEL_COUNT; ++channel) {
        if (positive_values[channel] == 0u) {
            fprintf(stderr, "DXR channel %s had no positive stored radiance\n",
                    channel_names[channel]);
            return 0;
        }
    }
    if (mismatches != 0u) {
        fprintf(stderr,
                "DXR combined radiance exceeded accumulated FP16 storage "
                "tolerance in %zu values (maximum error=%g tolerance=%g)\n",
                mismatches, maximum_error, maximum_tolerance);
        return 0;
    }
    return 1;
}

static int run_radiance_channel_reference(void)
{
    static const char *const channel_names[RADIANCE_CHANNEL_COUNT] = {
        "combined", "emission", "direct-diffuse", "direct-specular",
        "indirect", "smooth-specular", "rough-specular",
    };
    char error[1024] = {0};
    RenderView view = {0};
    RendererRayTracingOptions options = {0};
    options.samples_per_pixel = 8u;
    options.indirect_samples_per_pixel = 8u;
    options.diffuse_gi_scale = 0.75f;
    options.diffuse_gi_scale_set = UINT8_MAX;
    options.maximum_bounces = 2u;
    options.light_candidates = 16u;
    options.reservoir_sample_limit = 0u;
    options.reservoir_sample_limit_set = UINT8_MAX;
    options.radiance_clamp = 0.0f;
    options.exposure_bias_stops = -1.0f;
    options.exposure_bias_set = UINT8_MAX;
    options.ndf_trim = 0.9f;
    options.reconstruction = RENDERER_RAY_RECONSTRUCTION_OFF;
    options.output = RENDERER_OUTPUT_SDR;

    SceneVertex receiver_vertices[6] = {0};
    receiver_vertices[0].position = (SceneWorldPoint){-160, 7680, 320};
    receiver_vertices[1].position = (SceneWorldPoint){160, 7680, 320};
    receiver_vertices[2].position = (SceneWorldPoint){160, -7680, 320};
    receiver_vertices[3] = receiver_vertices[0];
    receiver_vertices[4] = receiver_vertices[2];
    receiver_vertices[5].position = (SceneWorldPoint){-160, -7680, 320};
    SceneVertex wall_vertices[6] = {0};
    wall_vertices[0].position = (SceneWorldPoint){-480, 23040, -160};
    wall_vertices[1].position = (SceneWorldPoint){480, 23040, -160};
    wall_vertices[2].position = (SceneWorldPoint){480, -23040, -160};
    wall_vertices[3] = wall_vertices[0];
    wall_vertices[4] = wall_vertices[2];
    wall_vertices[5].position = (SceneWorldPoint){-480, -23040, -160};
    SceneVertex light_vertices[3] = {0};
    light_vertices[0].position = (SceneWorldPoint){350, 7680, -80};
    light_vertices[1].position = (SceneWorldPoint){450, 7680, -80};
    light_vertices[2].position = (SceneWorldPoint){400, -7680, -80};
    SceneMeshSurface surfaces[3] = {0};
    initialize_wall_surface(&surfaces[0], receiver_vertices, 6u,
                            REFERENCE_ROUGHNESS_025);
    initialize_wall_surface(&surfaces[1], wall_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    initialize_wall_surface(&surfaces[2], light_vertices, 3u,
                            REFERENCE_EMITTER);
    SceneCommand commands[3] = {0};
    commands[0].type = SCENE_COMMAND_CAMERA;
    commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    commands[1].data.geometry_instance.source_instance_id = 3u;
    commands[1].data.geometry_instance.mesh.source_mesh_id = 3u;
    commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_STATIC;
    commands[1].data.geometry_instance.mesh.surfaces = surfaces;
    commands[1].data.geometry_instance.mesh.surface_count = 3u;
    commands[2].type = SCENE_COMMAND_SPRITE_INSTANCE;
    commands[2].data.sprite_instance.source_mesh_id = 4u;
    commands[2].data.sprite_instance.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    commands[2].data.sprite_instance.sprite.position =
        (SceneWorldPoint){0, 0, 160};
    commands[2].data.sprite_instance.sprite.source =
        SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
    commands[2].data.sprite_instance.sprite.presentation =
        SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
    commands[2].data.sprite_instance.sprite.surface_attachment =
        SCENE_SPRITE_SURFACE_FREE;
    commands[2].data.sprite_instance.sprite.source_asset_id =
        REFERENCE_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_record_id = 4u;
    commands[2].data.sprite_instance.sprite.source_width = 200u;
    commands[2].data.sprite_instance.sprite.source_height = 100u;
    commands[2].data.sprite_instance.sprite.flags =
        SCENE_SPRITE_FLAG_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_clip_top_y = -32768;
    commands[2].data.sprite_instance.sprite.source_clip_bottom_y = 32767;
    SceneFrame frame = {0};
    frame.commands = commands;
    frame.count = 3u;

    if (_putenv_s("AB3D2_DXR_RADIANCE_CHANNEL", "combined") != 0 ||
        _putenv_s("AB3D2_DXR_INDIRECT_RECONSTRUCTION", "raw") != 0) {
        fprintf(stderr, "Could not configure DXR channel-sum reference\n");
        return 0;
    }
    RendererRtx *renderer = renderer_rtx_create(
        640, 360, "AB3D2 DXR radiance-channel reference", 0, 1, 1u,
        &options, error, sizeof(error));
    if (!renderer) {
        fprintf(stderr, "DXR channel reference creation failed: %s\n", error);
        return 0;
    }
    RadianceCapture captures[RADIANCE_CHANNEL_COUNT] = {0};
    int valid = renderer_rtx_enable_noisy_radiance_readback(renderer);
    for (size_t channel = 0u;
         valid && channel < RADIANCE_CHANNEL_COUNT; ++channel) {
        /* A new epoch invalidates temporal history and returns every channel
         * to sample zero without reinitializing Streamline's interposer. */
        frame.history_epoch = channel;
        valid = capture_radiance_channel(
            renderer, (RendererRtxRadianceChannel)channel,
            channel_names[channel], &frame, &view, &captures[channel],
            error, sizeof(error));
    }
    if (valid) {
        valid = validate_radiance_channel_sum(channel_names, captures);
    }
    for (size_t channel = 0u; channel < RADIANCE_CHANNEL_COUNT; ++channel) {
        free(captures[channel].values);
    }
    renderer_rtx_destroy(renderer);
    (void)_putenv_s("AB3D2_DXR_RADIANCE_CHANNEL", "");
    (void)_putenv_s("AB3D2_DXR_INDIRECT_RECONSTRUCTION", "");
    return valid;
}

static int validate_direct_case(RendererRtx *renderer, SceneFrame *frame,
                                RenderView *view, SceneMeshSurface *receiver,
                                const DirectMaterialCase *test_case,
                                char *error, size_t error_size)
{
    receiver->material.source_asset_id = test_case->material_id;
    for (int frame_index = 0; frame_index < REFERENCE_FRAME_COUNT;
         ++frame_index) {
        if (!present_reference(renderer, frame, view, error, error_size)) {
            return 0;
        }
        const size_t diffuse =
            renderer_rtx_last_direct_diffuse_coverage(renderer);
        const size_t specular =
            renderer_rtx_last_direct_specular_coverage(renderer);
        if ((diffuse != 0u) != test_case->expect_diffuse ||
            (specular != 0u) != test_case->expect_specular ||
            renderer_rtx_last_frame_rgb_checksum(renderer) == UINT64_C(0)) {
            fprintf(stderr,
                    "DXR lighting case %s failed at frame %d "
                    "(diffuse=%zu specular=%zu checksum=%llu)\n",
                    test_case->name, frame_index, diffuse, specular,
                    (unsigned long long)
                        renderer_rtx_last_frame_rgb_checksum(renderer));
            return 0;
        }
    }
    return 1;
}

static int validate_transport_case(RendererRtx *renderer, SceneFrame *frame,
                                   RenderView *view, const char *name,
                                   int expect_direct_diffuse,
                                   int expect_direct_specular,
                                   int expect_smooth_specular,
                                   int expect_radiance, char *error,
                                   size_t error_size)
{
    for (int frame_index = 0; frame_index < REFERENCE_FRAME_COUNT;
         ++frame_index) {
        if (!present_reference(renderer, frame, view, error, error_size)) {
            return 0;
        }
        const size_t direct_diffuse =
            renderer_rtx_last_direct_diffuse_coverage(renderer);
        const size_t direct_specular =
            renderer_rtx_last_direct_specular_coverage(renderer);
        const size_t smooth_specular =
            renderer_rtx_last_smooth_specular_coverage(renderer);
        const uint64_t checksum =
            renderer_rtx_last_frame_rgb_checksum(renderer);
        if ((direct_diffuse != 0u) != expect_direct_diffuse ||
            (direct_specular != 0u) != expect_direct_specular ||
            (smooth_specular != 0u) != expect_smooth_specular ||
            (checksum != UINT64_C(0)) != expect_radiance) {
            fprintf(stderr,
                    "DXR transport case %s failed at frame %d "
                    "(direct=%zu/%zu smooth=%zu checksum=%llu)\n",
                    name, frame_index, direct_diffuse, direct_specular,
                    smooth_specular, (unsigned long long)checksum);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const DirectMaterialCase material_cases[] = {
        {REFERENCE_ROUGHNESS_BELOW_016, "roughness-below-0.16", 1, 0},
        {REFERENCE_ROUGHNESS_020, "roughness-0.20", 1, 1},
        {REFERENCE_ROUGHNESS_030, "roughness-0.30", 1, 1},
        {REFERENCE_ROUGHNESS_100, "roughness-1.00", 1, 1},
        {REFERENCE_METAL_ROUGHNESS_020, "pure-metal-0.20", 0, 1},
    };
    static const char window_title[] = "AB3D2 DXR lighting reference test";
    char error[1024] = {0};
    RenderView view = {0};

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "--channel-sum") == 0) {
        const int valid = run_radiance_channel_reference();
        SDL_Quit();
        return valid ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "--indirect-recovery") == 0) {
        const int valid = run_indirect_recovery_reference();
        SDL_Quit();
        return valid ? 0 : 1;
    }
    if (argc != 1) {
        fprintf(stderr, "Unknown DXR lighting reference argument\n");
        SDL_Quit();
        return 1;
    }
    if (_putenv_s("AB3D2_DXR_RADIANCE_CHANNEL", "combined") != 0) {
        fprintf(stderr, "Could not select the combined DXR reference channel\n");
        SDL_Quit();
        return 1;
    }
    RendererRayTracingOptions options = {0};
    options.samples_per_pixel = 8u;
    options.indirect_samples_per_pixel = 1u;
    options.diffuse_gi_scale = 0.0f;
    options.diffuse_gi_scale_set = UINT8_MAX;
    options.maximum_bounces = 2u;
    options.light_candidates = 16u;
    options.reservoir_sample_limit = 0u;
    options.reservoir_sample_limit_set = UINT8_MAX;
    options.radiance_clamp = 0.0f;
    options.exposure_bias_stops = -1.0f;
    options.exposure_bias_set = UINT8_MAX;
    options.ndf_trim = 0.9f;
    options.reconstruction = RENDERER_RAY_RECONSTRUCTION_OFF;
    options.output = RENDERER_OUTPUT_SDR;
    RendererRtx *renderer = renderer_rtx_create(
        640, 360, window_title, 0, 1, 1u, &options, error, sizeof(error));
    if (!renderer) {
        fprintf(stderr, "DXR lighting reference creation failed: %s\n", error);
        SDL_Quit();
        return 1;
    }

    SceneVertex receiver_vertices[6] = {0};
    receiver_vertices[0].position = (SceneWorldPoint){-160, 7680, 320};
    receiver_vertices[1].position = (SceneWorldPoint){160, 7680, 320};
    receiver_vertices[2].position = (SceneWorldPoint){160, -7680, 320};
    receiver_vertices[3] = receiver_vertices[0];
    receiver_vertices[4] = receiver_vertices[2];
    receiver_vertices[5].position = (SceneWorldPoint){-160, -7680, 320};
    SceneVertex emitter_vertices[3] = {0};
    emitter_vertices[0].position = (SceneWorldPoint){-80, -12800, 160};
    emitter_vertices[1].position = (SceneWorldPoint){80, -12800, 160};
    emitter_vertices[2].position = (SceneWorldPoint){0, -15360, 160};

    SceneMeshSurface surfaces[3] = {0};
    initialize_wall_surface(&surfaces[0], receiver_vertices, 6u,
                            REFERENCE_ROUGHNESS_020);
    initialize_wall_surface(&surfaces[1], emitter_vertices, 3u,
                            REFERENCE_EMITTER);
    SceneCommand commands[3] = {0};
    commands[0].type = SCENE_COMMAND_CAMERA;
    commands[1].type = SCENE_COMMAND_GEOMETRY_INSTANCE;
    commands[1].data.geometry_instance.source_instance_id = 1u;
    commands[1].data.geometry_instance.mesh.source_mesh_id = 1u;
    commands[1].data.geometry_instance.mesh.acceleration_class =
        SCENE_ACCELERATION_CLASS_STATIC;
    commands[1].data.geometry_instance.mesh.surfaces = surfaces;
    commands[1].data.geometry_instance.mesh.surface_count = 1u;
    SceneFrame frame = {0};
    frame.commands = commands;
    frame.count = 2u;

    /* Geometry with a non-emissive PBR material and no emitter must remain
     * exact black; base colour is never an ambient or emission substitute. */
    if (!present_reference(renderer, &frame, &view, error, sizeof(error)) ||
        renderer_rtx_last_frame_rgb_checksum(renderer) != UINT64_C(0) ||
        renderer_rtx_last_direct_diffuse_coverage(renderer) != 0u ||
        renderer_rtx_last_direct_specular_coverage(renderer) != 0u) {
        fprintf(stderr,
                "DXR unlit material reference did not remain exact black\n");
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    for (size_t case_index = 0u;
         case_index < sizeof(material_cases) / sizeof(material_cases[0]);
         ++case_index) {
        if (!validate_direct_case(renderer, &frame, &view, &surfaces[0],
                                  &material_cases[case_index], error,
                                  sizeof(error))) {
            renderer_rtx_destroy(renderer);
            SDL_Quit();
            return 1;
        }
    }

    /* With no geometry behind a sub-0.16 pure-metal receiver, its companion
     * GGX ray misses and every named radiance channel remains black. */
    surfaces[0].material.source_asset_id = REFERENCE_METAL_BELOW_016;
    commands[1].data.geometry_instance.mesh.surface_count = 1u;
    if (!validate_transport_case(renderer, &frame, &view, "specular-miss",
                                 0, 0, 0, 0, error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* This source additive bitmap is behind the camera, so primary visibility
     * never crosses it. The reflected segment accumulates its authored
     * radiance, continues through the non-occluding quad, and then misses. */
    commands[2].type = SCENE_COMMAND_SPRITE_INSTANCE;
    commands[2].data.sprite_instance.source_mesh_id = 2u;
    commands[2].data.sprite_instance.acceleration_class =
        SCENE_ACCELERATION_CLASS_DYNAMIC;
    commands[2].data.sprite_instance.sprite.position =
        (SceneWorldPoint){0, 0, -80};
    commands[2].data.sprite_instance.sprite.source =
        SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
    commands[2].data.sprite_instance.sprite.presentation =
        SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
    commands[2].data.sprite_instance.sprite.surface_attachment =
        SCENE_SPRITE_SURFACE_FREE;
    commands[2].data.sprite_instance.sprite.source_asset_id =
        REFERENCE_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_record_id = 2u;
    commands[2].data.sprite_instance.sprite.source_width = 200u;
    commands[2].data.sprite_instance.sprite.source_height = 100u;
    commands[2].data.sprite_instance.sprite.flags =
        SCENE_SPRITE_FLAG_ADDITIVE;
    commands[2].data.sprite_instance.sprite.source_clip_top_y = -32768;
    commands[2].data.sprite_instance.sprite.source_clip_bottom_y = 32767;
    frame.count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "additive-on-reflected-miss", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    frame.count = 2u;

    /* A large emitter behind and offset from the camera is invisible to the
     * primary ray. The smooth-GGX companion is the only path that can reach
     * it from the camera-facing metal receiver. */
    SceneVertex reflected_emitter_vertices[6] = {0};
    reflected_emitter_vertices[0].position =
        (SceneWorldPoint){-320, 23040, -160};
    reflected_emitter_vertices[1].position =
        (SceneWorldPoint){480, 23040, -160};
    reflected_emitter_vertices[2].position =
        (SceneWorldPoint){480, -23040, -160};
    reflected_emitter_vertices[3] = reflected_emitter_vertices[0];
    reflected_emitter_vertices[4] = reflected_emitter_vertices[2];
    reflected_emitter_vertices[5].position =
        (SceneWorldPoint){-320, -23040, -160};
    initialize_wall_surface(&surfaces[1], reflected_emitter_vertices, 6u,
                            REFERENCE_EMITTER);
    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "reflected-off-camera-emitter", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* Replace the reflected source with a non-emissive wall. A separate
     * off-axis emitter sits between it and the camera but outside the mirror
     * segment, so the reached wall's local-light NEE supplies the reflection. */
    SceneVertex reflected_wall_vertices[6] = {0};
    reflected_wall_vertices[0].position =
        (SceneWorldPoint){-480, 23040, -160};
    reflected_wall_vertices[1].position =
        (SceneWorldPoint){480, 23040, -160};
    reflected_wall_vertices[2].position =
        (SceneWorldPoint){480, -23040, -160};
    reflected_wall_vertices[3] = reflected_wall_vertices[0];
    reflected_wall_vertices[4] = reflected_wall_vertices[2];
    reflected_wall_vertices[5].position =
        (SceneWorldPoint){-480, -23040, -160};
    SceneVertex reflected_light_vertices[3] = {0};
    reflected_light_vertices[0].position =
        (SceneWorldPoint){350, 7680, -80};
    reflected_light_vertices[1].position =
        (SceneWorldPoint){450, 7680, -80};
    reflected_light_vertices[2].position =
        (SceneWorldPoint){400, -7680, -80};
    initialize_wall_surface(&surfaces[1], reflected_wall_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    initialize_wall_surface(&surfaces[2], reflected_light_vertices, 3u,
                            REFERENCE_EMITTER);
    commands[1].data.geometry_instance.mesh.surface_count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "reflected-locally-lit-wall", 0, 0, 1, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    /* Put the emitter far outside the primary frustum. A nearby off-screen
     * blocker covers its complete receiver-to-light cone, turning a proven
     * direct response back into exact black without affecting camera rays. */
    SceneVertex side_light_vertices[3] = {0};
    side_light_vertices[0].position =
        (SceneWorldPoint){480, 5120, 160};
    side_light_vertices[1].position =
        (SceneWorldPoint){640, 5120, 160};
    side_light_vertices[2].position =
        (SceneWorldPoint){560, -5120, 160};
    SceneVertex blocker_vertices[6] = {0};
    blocker_vertices[0].position = (SceneWorldPoint){380, 12800, 180};
    blocker_vertices[1].position = (SceneWorldPoint){620, 12800, 180};
    blocker_vertices[2].position = (SceneWorldPoint){620, -12800, 180};
    blocker_vertices[3] = blocker_vertices[0];
    blocker_vertices[4] = blocker_vertices[2];
    blocker_vertices[5].position = (SceneWorldPoint){380, -12800, 180};
    surfaces[0].material.source_asset_id = REFERENCE_ROUGHNESS_020;
    initialize_wall_surface(&surfaces[1], side_light_vertices, 3u,
                            REFERENCE_EMITTER);
    initialize_wall_surface(&surfaces[2], blocker_vertices, 6u,
                            REFERENCE_ROUGHNESS_100);
    commands[1].data.geometry_instance.mesh.surface_count = 2u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "unoccluded-off-screen-direct", 1, 1, 0, 1,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }
    commands[1].data.geometry_instance.mesh.surface_count = 3u;
    if (!validate_transport_case(renderer, &frame, &view,
                                 "occluded-off-screen-direct", 0, 0, 0, 0,
                                 error, sizeof(error))) {
        renderer_rtx_destroy(renderer);
        SDL_Quit();
        return 1;
    }

    renderer_rtx_destroy(renderer);
    SDL_Quit();
    return 0;
}
