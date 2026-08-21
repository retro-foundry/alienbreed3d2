#ifndef AB3D2_RENDERER_RAY_TRACING_OPTIONS_H
#define AB3D2_RENDERER_RAY_TRACING_OPTIONS_H

#include <stdint.h>

/*
 * Presentation-only settings for the ray-traced backend, carried from ab3d2.ini
 * to the renderer boundary. None of them touch source assets or gameplay: they
 * trade image quality against frame cost.
 *
 * Zero on a quality field normally means "keep the renderer's own default".
 * The reservoir history limit is the exception because zero is its explicit
 * diagnostic/off value; reservoir_sample_limit_set distinguishes that value
 * from an absent setting. The tuned defaults and the measurements behind them
 * live with the code that uses them, in renderer_dxr/dxr_pipeline.cpp.
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

/* Accepted ReGIR/ReSTIR production defaults. Keep explicit zero available only
 * through reservoir_sample_limit_set for the history-off diagnostic. */
enum {
    RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES = 16,
    RENDERER_RAY_TRACING_DEFAULT_RESERVOIR_SAMPLE_LIMIT = 20
};

typedef struct {
    /*
     * Fresh path-traced samples per pixel per frame. This is the direct
     * quality-for-cost dial: every sample repeats the whole path, so two cost
     * about twice one, and the estimator's variance falls as their number.
     * One through eight.
     */
    uint8_t samples_per_pixel;
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
    /* Ceiling on a single sample's luminance, which bounds fireflies. */
    float radiance_clamp;
    /* Linear multiplier applied before tone mapping. */
    float exposure;
    /* DLSS Ray Reconstruction mode, which also sets the path-traced
     * resolution the reconstruction upscales from. */
    RendererRayReconstructionMode reconstruction;
} RendererRayTracingOptions;

#endif
