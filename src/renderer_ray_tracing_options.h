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
/*
 * The two tone-mapping constants that do not survive the trip from Q2RTX.
 *
 * Everything else in the tone mapper is a faithful port and needs no
 * adjustment, because everything else is unitless: the 70th and 90th metering
 * percentiles, the adaptation speeds, the Reinhard blend, the knee, the white
 * point and the seven-stop display range all mean the same thing whatever the
 * scene's radiance happens to be. These two are measured in absolute scene
 * luminance, so they are calibrated to Q2RTX's content and have to be
 * recalibrated for this game's.
 *
 * Where the tone curve stops treating a luminance as signal, in photographic
 * stops -- Q2RTX's tm_noise_stops. Below this the curve flattens towards a
 * plain exposure line instead of stretching the histogram, so regions the path
 * tracer only has noise for read dark rather than showing their grain.
 *
 * Q2RTX ships -12. At -12 this game's frames contain NO true black at all once
 * anything clips: the dark half of the image lifts into a flat featureless
 * band with the path tracer's residual wobble riding on top of it, which is
 * what reads as static whenever a light is on screen. Measured on the
 * saved-state smoke with a clipping pose, the fraction of the frame below 8 of
 * 255 is 0.0% at -12, 20.1% at -10 and 33.0% at -8, with the lit areas
 * unchanged throughout.
 *
 * Zero is the top of the range and what this game ships. It leaves the
 * histogram stretching to luminances above one and puts everything below on
 * the plain exposure line, which is the darkest and steadiest setting
 * available: measured against -10, the frozen frame's temporal outliers go
 * 7 -> 0 and the firing frame's 25917 -> 20386, while 47% of the frame reads
 * below 8 of 255 and the lit surfaces are untouched. A level authored with
 * more light in its dark corners would want a lower value; this one does not
 * have any, so stretching its dark end only reveals what the path tracer is
 * unsure about.
 */
#define RENDERER_RAY_TRACING_DEFAULT_NOISE_FLOOR_STOPS (0.0f)
/*
 * The darkest scene luminance auto-exposure will meter to -- Q2RTX's
 * tm_min_luminance, and the cap on how far exposure can open up, since the
 * gain it produces is 0.125 divided by this.
 *
 * Q2RTX ships 0.0002, which permits 625x. A view of this game's dark geometry
 * meters around 0.0036 and asks for 34x, and at that gain the path tracer's
 * noise is amplified with everything else; worse, exposure needs seconds to
 * travel that far, so during a turn it is always mid-ramp and the dark end
 * drifts upward frame after frame, undoing the noise floor.
 *
 * At 0.01 the same view is capped to 12.5x. Measured against 0.0002 on that
 * view: true black 24.6% -> 37.5% of the frame, noise amplitude in the dark
 * regions 9.4 -> 7.9, and the clipped fraction unchanged at 8.2%, so the lights
 * are untouched and only the amplification of the darks has gone.
 */
#define RENDERER_RAY_TRACING_DEFAULT_MINIMUM_LUMINANCE 0.01f
/*
 * The brightest scene luminance auto-exposure will meter to -- Q2RTX's
 * tm_max_luminance, and the floor on exposure gain, which is 0.125 divided by
 * this.
 *
 * Q2RTX ships 1.0. This game's emitters are authored far above Q2RTX's: with
 * the floor light and the techno lights both at 1600, a view of them meters
 * around 8.7, so the clamp is exceeded eightfold and exposure sits pinned at
 * its floor of 0.125 with nowhere left to go. The frame then blows out --
 * 14% of it saturated -- and no amount of further authoring can be
 * compensated for, because the control has already bottomed out.
 *
 * Raising it restores the range exposure needs to bring a bright scene back
 * down. Lower it to force a scene to read brighter than it is.
 */
#define RENDERER_RAY_TRACING_DEFAULT_MAXIMUM_LUMINANCE 16.0f
/*
 * Multiplies every material's authored emissive factor, so it scales what the
 * lights radiate and what next-event estimation samples them as together.
 *
 * One is the level as authored. It is a single control over the whole level's
 * light rather than a per-material edit: the relationship between the lights
 * stays as the artist set it, and only the overall level moves. Raising it
 * forces more light into the scene and pushes the tone mapper's exposure down
 * to compensate, so rtx_max_luminance has to have the range for it.
 */
#define RENDERER_RAY_TRACING_DEFAULT_LIGHT_SCALE 1.0f
/*
 * The factor Ray Reconstruction's input is multiplied by, and its output
 * divided by again before bloom and tone mapping. It is a change of units
 * only: nothing is clamped or discarded, and with RR off it does nothing.
 *
 * RR is not scale invariant. A room lit only through a doorway averages about
 * 0.00015 in scene radiance, and at that absolute level RR reconstructs it as
 * blotches and sparkles -- but only while something bright is on screen. Turn
 * away from the doorway and the same room denoises cleanly. The RR input
 * pixels for the room were byte-identical either way, so this is RR, not the
 * path tracer. Neither the tagged exposure texture nor the tone curve moved it.
 *
 * Measured on that room with the doorway in view: 4 is still blotchy, 12.5
 * partly clean, 32 and 64 clean. 32 is the smallest that clears it, and it
 * leaves the brightest authored emitter (1600) inside half-float range. The
 * renderer lowers it further whenever the scene's brightest emitter would
 * otherwise overflow, so rtx_light_scale cannot push the input to infinity.
 * One restores the unscaled input.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RR_INPUT_SCALE 32.0f
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
    float noise_floor_stops;
    uint8_t noise_floor_stops_set;
    float minimum_luminance;
    uint8_t minimum_luminance_set;
    float maximum_luminance;
    uint8_t maximum_luminance_set;
    float light_scale;
    uint8_t light_scale_set;
    float rr_input_scale;
    uint8_t rr_input_scale_set;
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
