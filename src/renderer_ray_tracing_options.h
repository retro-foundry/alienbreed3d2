#ifndef AB3D2_RENDERER_RAY_TRACING_OPTIONS_H
#define AB3D2_RENDERER_RAY_TRACING_OPTIONS_H

#include <stdint.h>

/*
 * Presentation-only settings for the ray-traced backend, carried from ab3d2.ini
 * to the renderer boundary. None of them touch source assets or gameplay: they
 * trade image quality against frame cost.
 *
 * Zero on a quality/nit field normally means "keep the renderer's own default".
 * Output mode zero is the explicit automatic-monitor policy, although the
 * desktop application's shipped default is SDR to match Q2RTX's opt-in HDR.
 * The radiance clamp uses zero as its explicit off value. The reservoir history
 * limit likewise accepts zero; reservoir_sample_limit_set distinguishes that
 * value from an absent setting. Diffuse-GI transfer, exposure bias, and HDR
 * saturation also accept zero, so their accompanying set flags distinguish it
 * from an absent setting.
 * The tuned defaults and measurements live with the code that uses them, in
 * renderer_dxr/dxr_pipeline.cpp.
 */

typedef enum {
    /* Leave the renderer's configured Ray Reconstruction mode alone. */
    RENDERER_RAY_RECONSTRUCTION_DEFAULT = 0,
    RENDERER_RAY_RECONSTRUCTION_QUALITY,
    RENDERER_RAY_RECONSTRUCTION_BALANCED,
    RENDERER_RAY_RECONSTRUCTION_PERFORMANCE,
    RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE,
    RENDERER_RAY_RECONSTRUCTION_OFF
} RendererRayReconstructionMode;

typedef enum {
    /* Follow the Windows advanced-color state of the window's current monitor. */
    RENDERER_OUTPUT_AUTO = 0,
    /* Always present exact sRGB through an 8-bit UNORM swap chain. */
    RENDERER_OUTPUT_SDR,
    /* Require an FP16 scRGB swap chain; creation fails if HDR is unavailable. */
    RENDERER_OUTPUT_HDR
} RendererOutputMode;

/* Fresh RIS and low-frequency reconstruction defaults. Keep explicit zero
 * available only through reservoir_sample_limit_set for the history-off
 * diagnostic. Sixteen independent GI paths fill the 32-sample history in two
 * presented frames after a disocclusion or confirmed lighting change. Mature
 * pixels retain the sparse one-path-per-2x2-block schedule. The fixed-receiver
 * indirect-light recovery oracle rejects every lower tested ceiling because
 * none retains 90% of the early ceiling-16 response. */
enum {
    RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES = 16,
    RENDERER_RAY_TRACING_DEFAULT_INDIRECT_SAMPLES_PER_PIXEL = 16,
    RENDERER_RAY_TRACING_DEFAULT_RESERVOIR_SAMPLE_LIMIT = 32
};

/* Reduce secondary diffuse transfer modestly. Direct lighting and visible
 * emission are not affected. */
#define RENDERER_RAY_TRACING_DEFAULT_DIFFUSE_GI_SCALE 0.75f

typedef struct {
    /*
     * Fresh primary direct-light samples per pixel per frame. One through
     * eight. Indirect continuations have a separate budget below so increasing
     * GI quality does not repeat primary direct-light work.
     */
    uint8_t samples_per_pixel;
    /* Fresh diffuse-indirect paths per pixel per frame, one through 32. */
    uint8_t indirect_samples_per_pixel;
    /* Secondary diffuse transfer multiplier, zero through one. */
    float diffuse_gi_scale;
    uint8_t diffuse_gi_scale_set;
    /*
     * Path length, counting the primary hit. One is direct lighting only; each
     * further bounce adds its own next-event and continuation rays. One through
     * eight.
     */
    uint8_t maximum_bounces;
    /* Emitter candidates the direct-lighting reservoir draws per pixel. */
    uint16_t light_candidates;
    /*
     * Maximum historical sample count accepted from each reused previous-frame
     * reservoir. Zero explicitly disables spatiotemporal reuse when the
     * accompanying set flag is nonzero.
     */
    uint32_t reservoir_sample_limit;
    uint8_t reservoir_sample_limit_set;
    /* Zero disables the diagnostic per-sample firefly clamp. */
    float radiance_clamp;
    /* Post-tone-curve log2 exposure bias, -5 through 0 EV. */
    float exposure_bias_stops;
    uint8_t exposure_bias_set;
    /* GGX visible-normal sampling trim. */
    float ndf_trim;
    /* DLSS Ray Reconstruction mode, which also sets the path-traced
     * resolution the reconstruction upscales from. */
    RendererRayReconstructionMode reconstruction;
    /* Display-output policy. Hidden validation windows are always forced SDR. */
    RendererOutputMode output;
    /* Zero keeps Q2RTX's 800-nit scene default. */
    float hdr_peak_nits;
    /* Q2RTX-compatible percentage, 0 through 200; default 100. */
    float hdr_saturation_percent;
    uint8_t hdr_saturation_percent_set;
} RendererRayTracingOptions;

#endif
