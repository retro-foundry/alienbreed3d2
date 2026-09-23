#ifndef AB3D2_RENDERER_RAY_TRACING_OPTIONS_H
#define AB3D2_RENDERER_RAY_TRACING_OPTIONS_H

#include <stdint.h>

/*
 * Presentation-only settings for the ray-traced backend, carried from ab3d2.ini
 * to the renderer boundary. None of them touch source assets or gameplay: they
 * trade image quality against frame cost.
 *
 * Zero on a quality/nit field normally means "keep the renderer's own default",
 * and every enum's zero is likewise the "renderer decides" value. Output mode
 * zero is the explicit automatic-monitor policy, although the desktop
 * application's shipped default is SDR to match Q2RTX's opt-in HDR. The
 * radiance clamp uses zero as its explicit off value. Diffuse-GI transfer,
 * exposure bias, HDR saturation, the ReSTIR spatial radius and the ReSTIR
 * history-reduction strength all accept zero as a meaningful value, so each
 * carries a set flag that distinguishes it from an absent setting.
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

/*
 * Which estimator produces indirect lighting. ReSTIR PT resamples whole light
 * paths between pixels and frames; the plain path tracer draws independent
 * fresh paths every frame and is the reference the resampled estimator is
 * validated against.
 */
typedef enum {
    RENDERER_INDIRECT_DEFAULT = 0,
    /* Independent fresh paths per frame. No reservoir reuse. */
    RENDERER_INDIRECT_PATH_TRACE,
    /* ReSTIR PT Enhanced spatiotemporal path resampling. */
    RENDERER_INDIRECT_RESTIR_PT
} RendererIndirectMode;

/*
 * Which stage owns reconstruction of the noisy ray-traced signal. Ray
 * Reconstruction replaces a conventional denoiser rather than running after
 * one, so selecting it disables the renderer's own spatial filter.
 */
typedef enum {
    RENDERER_DENOISER_DEFAULT = 0,
    /* DLSS Ray Reconstruction denoises and upscales in one pass. */
    RENDERER_DENOISER_RAY_RECONSTRUCTION,
    /* Renderer-owned render-resolution spatial filter ahead of DLSS SR. */
    RENDERER_DENOISER_SPATIAL,
    /* Hand the raw resampled signal straight to DLSS SR. Diagnostic. */
    RENDERER_DENOISER_OFF
} RendererDenoiserMode;

/*
 * How ReSTIR PT decides that a pair of consecutive path vertices may be
 * reconnected, which bounds how far a shifted path has to be replayed.
 */
typedef enum {
    RENDERER_RECONNECTION_DEFAULT = 0,
    /* Footprint criterion of ReSTIR PT Enhanced. */
    RENDERER_RECONNECTION_FOOTPRINT,
    /* Classic roughness-and-distance cutoffs. Diagnostic control. */
    RENDERER_RECONNECTION_FIXED
} RendererReconnectionMode;

/*
 * Render-resolution diagnostic views. Every one of them is inspected before any
 * upscaling, because a reconstructed image cannot prove the estimator
 * underneath it is correct.
 */
typedef enum {
    RENDERER_DEBUG_VIEW_OFF = 0,
    /* Estimator isolation. */
    RENDERER_DEBUG_VIEW_REFERENCE,
    RENDERER_DEBUG_VIEW_CANONICAL,
    RENDERER_DEBUG_VIEW_TEMPORAL,
    RENDERER_DEBUG_VIEW_SPATIAL,
    /* Temporal correspondence. */
    RENDERER_DEBUG_VIEW_MOTION,
    RENDERER_DEBUG_VIEW_REPROJECTION,
    RENDERER_DEBUG_VIEW_REJECTION,
    RENDERER_DEBUG_VIEW_HISTORY_AGE,
    /* Reservoir statistics. */
    RENDERER_DEBUG_VIEW_RESERVOIR_M,
    RENDERER_DEBUG_VIEW_ANCESTRY,
    RENDERER_DEBUG_VIEW_DUPLICATION
} RendererDebugView;

/* Fresh current-frame path-sampling defaults. Four stratified diffuse paths
 * interleave one additional secondary-light RIS estimate every third frame. */
enum {
    RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES = 16,
    RENDERER_RAY_TRACING_DEFAULT_INDIRECT_SAMPLES_PER_PIXEL = 4,
    RENDERER_RAY_TRACING_DEFAULT_INDIRECT_LIGHT_SAMPLES = 2
};

/*
 * ReSTIR PT resampling defaults.
 *
 * The temporal confidence cap is the headline correlation control: a reservoir
 * that survives reuse raises its represented sample count M every frame, and
 * capping M bounds how long one path may keep speaking for a pixel. Twenty is
 * the usual starting point; duplication-based history reduction lowers the cap
 * further wherever one ancestor has colonised a neighbourhood.
 *
 * Two spatial neighbours is what reciprocal pairing makes affordable, because a
 * pair's forward and reverse shifts are computed together rather than twice.
 */
enum {
    RENDERER_RAY_TRACING_DEFAULT_RESTIR_TEMPORAL_HISTORY = 20,
    RENDERER_RAY_TRACING_DEFAULT_RESTIR_SPATIAL_SAMPLES = 2
};

/*
 * Spatial search radius as a fraction of render height rather than a pixel
 * count, so a DLSS quality-mode change cannot silently alter the image-space
 * footprint the estimator searches.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RESTIR_SPATIAL_RADIUS 0.03f

/* Footprint threshold scaling the primary ray's footprint, per ReSTIR PT
 * Enhanced. Larger values reconnect sooner and replay less. */
#define RENDERER_RAY_TRACING_DEFAULT_RESTIR_CONNECTION_FOOTPRINT 1.0f

/* Exponent of the power curve that pulls the temporal confidence cap toward one
 * as local sample duplication rises. Zero disables history reduction. */
#define RENDERER_RAY_TRACING_DEFAULT_RESTIR_HISTORY_REDUCTION 1.0f
/* Radiance of a fully lit surface under the level's own vertex lighting, which
 * bounce vertices return in place of tracing on. Indirect light is pure path
 * tracing by default: the fill lifts the dark end of the frame measurably, but
 * it did not fix the grain it was built for, so it is opt-in rather than
 * something every scene pays for.
 *
 * The unit is scene radiance, so a value has to be read against what the level
 * emits: floor_0101 emits 164 and the wall fixtures 1.5 to 7.7, which puts the
 * useful range around 64 to 128. One is about a 160th of the brightest thing
 * in frame and looks identical to zero. */
#define RENDERER_RAY_TRACING_DEFAULT_SOURCE_LIGHT_SCALE 0.0f

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
    /* Maximum RIS estimates on the temporally interleaved diffuse stratum. */
    uint8_t indirect_light_samples;
    /* Secondary diffuse transfer multiplier, zero through one. */
    float diffuse_gi_scale;
    uint8_t diffuse_gi_scale_set;
    /*
     * Roughness at which specular stops being traced and is reconstructed from
     * the filtered diffuse signal instead. Reconstruction is far cheaper but
     * carries no directional detail, so a surface above this limit cannot
     * mirror the scene. 0.3 is the long-standing behaviour; 1 traces specular
     * on every surface, at the cost of a continuation ray for each.
     */
    float specular_roughness_limit;
    uint8_t specular_roughness_limit_set;
    /*
     * Path length, counting the primary hit. One is direct lighting only; each
     * further bounce adds its own next-event and continuation rays. One through
     * eight.
     */
    uint8_t maximum_bounces;
    /* Emitter candidates the direct-lighting reservoir draws per pixel. */
    uint16_t light_candidates;
    /* Zero disables the diagnostic per-sample firefly clamp. */
    float radiance_clamp;
    /* Post-tone-curve log2 exposure bias, -5 through 0 EV. */
    float exposure_bias_stops;
    uint8_t exposure_bias_set;
    /* GGX visible-normal sampling trim. */
    float ndf_trim;
    /* Which estimator produces indirect lighting. */
    RendererIndirectMode indirect_mode;
    /*
     * Cap on a ReSTIR reservoir's represented sample count M. One disables
     * temporal reuse without disabling ReSTIR itself. One through 64.
     */
    uint8_t restir_temporal_history;
    /* Spatial neighbours resampled per pixel, zero through eight. */
    uint8_t restir_spatial_samples;
    /* Spatial search radius as a fraction of render height. */
    float restir_spatial_radius;
    uint8_t restir_spatial_radius_set;
    /* Which vertex pairs ReSTIR PT may reconnect. */
    RendererReconnectionMode restir_reconnection;
    /* Footprint threshold scale for the footprint reconnection criterion. */
    float restir_connection_footprint;
    uint8_t restir_connection_footprint_set;
    /* Duplication-based history reduction strength; zero disables it. */
    float restir_history_reduction;
    uint8_t restir_history_reduction_set;
    float source_light_scale;
    uint8_t source_light_scale_set;
    /*
     * Probability that final shading discards the resampled reservoir and
     * shades the preserved initial sample instead, trading variance for the
     * temporal independence Ray Reconstruction expects. Zero through one.
     */
    float restir_decorrelation;
    uint8_t restir_decorrelation_set;
    /*
     * DLSS Super Resolution quality mode, which also sets the internal
     * resolution the path tracer and every ReSTIR buffer run at.
     */
    RendererRayReconstructionMode reconstruction;
    /* Which stage reconstructs the noisy ray-traced signal. */
    RendererDenoiserMode denoiser;
    /* Render-resolution diagnostic view, inspected before any upscaling. */
    RendererDebugView debug_view;
    /* Display-output policy. Hidden validation windows are always forced SDR. */
    RendererOutputMode output;
    /* Zero keeps Q2RTX's 800-nit scene default. */
    float hdr_peak_nits;
    /* Q2RTX-compatible percentage, 0 through 200; default 100. */
    float hdr_saturation_percent;
    uint8_t hdr_saturation_percent_set;
} RendererRayTracingOptions;

#endif
