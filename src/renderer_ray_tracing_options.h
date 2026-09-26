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
    /* A fifth of the pixels, reconstructed to twice the render size. */
    RENDERER_RAY_RECONSTRUCTION_HIGH_PERFORMANCE,
    /* NVIDIA's Ultra Performance: a third on each axis, to the window. */
    RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE,
    /* Ultra Performance reconstructed to two thirds of the window. */
    RENDERER_RAY_RECONSTRUCTION_EXTREME_PERFORMANCE,
    RENDERER_RAY_RECONSTRUCTION_OFF
} RendererRayReconstructionMode;

/*
 * Which Ray Reconstruction model to ask NGX for.
 *
 * DRIVER leaves sl::DLSSDPreset::eDefault in place, which sl_dlss_d.h
 * describes as behaviour that "may or may not change after an OTA" -- the
 * driver's choice rather than ours, and not reproducible between machines or
 * driver versions. Every other value pins a named model, so a build renders
 * the same way twice.
 *
 * D, E and F are all transformer models. On the pinned v2.14.1 SDK the header
 * calls F "Latest and default transformer model"; on v2.12.0 the same value
 * merely reverted to the default, so asking for it there did nothing.
 */
typedef enum {
    RENDERER_RAY_RECONSTRUCTION_PRESET_DRIVER = 0,
    RENDERER_RAY_RECONSTRUCTION_PRESET_D,
    RENDERER_RAY_RECONSTRUCTION_PRESET_E,
    RENDERER_RAY_RECONSTRUCTION_PRESET_F
} RendererRayReconstructionPreset;

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

/*
 * The per-emitter emission change that costs ReSTIR its whole temporal
 * history, as a fraction of that emitter's brighter state. Smaller changes
 * give up confidence in proportion rather than all of it.
 *
 * A reservoir holds the radiance its path carried, so changing what an emitter
 * emits does make that number wrong -- but by the amount the emitter moved,
 * not completely. Discarding every reservoir on screen whenever brightanim
 * advances a frame leaves the estimator with one sample per pixel for as long
 * as anything is animating, which surfaces as noise rising and falling in time
 * with the pulse. One means only a total extinction or ignition wipes the
 * history outright.
 *
 * Relative change cannot exceed one, so values above it are what buys headroom
 * past that: at two even an emitter going fully dark surrenders half its
 * confidence rather than all of it, and nothing ever reaches a complete wipe.
 * That is the direction to go when a light animates hard enough to reach one
 * every cycle and the noise arrives with it.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RESTIR_EMITTER_CHANGE_LIMIT 1.0f

/*
 * The Ray Reconstruction model, pinned rather than inherited.
 *
 * Leaving it unpinned means the driver's choice, which sl_dlss_d.h warns "may
 * or may not change after an OTA" -- the same build renders differently after
 * a driver update with nothing here to say why.
 *
 * D rather than F. F is the latest transformer model on SDK v2.14.1, and it
 * did reduce weapon ghosting -- but the 310.9.1 model that ships with that SDK
 * puts blotchy noise on surfaces while a light brightens, which 310.7 does
 * not. That was measured by swapping only the model DLL, with everything else
 * held still. The SDK therefore stays at v2.12.0, where F merely reverts to
 * the default and D is the current transformer model.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RR_PRESET     RENDERER_RAY_RECONSTRUCTION_PRESET_D

/*
 * Exponent of the power curve that pulls the temporal confidence cap toward one
 * as local sample duplication rises. Zero disables history reduction, and that
 * is the default because it is biased: the samples that spread through a
 * neighbourhood are the bright ones resampling favours, so lowering confidence
 * where they have spread lowers the weight of exactly those samples. With the
 * estimator otherwise unbiased it darkened a doorway-lit room by 11% at the
 * default reuse and 17% at 64 frames and 4 neighbours.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RESTIR_HISTORY_REDUCTION 0.0f
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
/*
 * Absolute log2 luminance, so the radiance calibration below moves it: the
 * value that meant luminance 1.0 before means 1/16 of it now.
 */
#define RENDERER_RAY_TRACING_DEFAULT_NOISE_FLOOR_STOPS (-4.0f)
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
#define RENDERER_RAY_TRACING_DEFAULT_MINIMUM_LUMINANCE 0.000625f
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
#define RENDERER_RAY_TRACING_DEFAULT_MAXIMUM_LUMINANCE 1.0f
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
/*
 * A sixteenth, which is the radiance calibration rather than an artistic
 * choice.
 *
 * The emissive factors in the material pack are arbitrary: the floor light and
 * the techno lights are both authored at 1600, against Q2RTX's shipped
 * materials where emissive_factor is 0.1 for 403 of about 512 entries and
 * never exceeds 0.5 outside three outliers. Matching those factors literally
 * would be wrong -- our emitters are small bright panels lighting 4%-albedo
 * surfaces where Q2RTX's are large dim ones, so the scenes only meter about
 * 16x apart even though the factors are 16000x apart, and copying the factors
 * would leave this game a thousand times darker than the reference.
 *
 * What is worth matching is the calibration: at a sixteenth,
 * rtx_max_luminance lands on Q2RTX's tm_max_luminance of 1.0 and the whole
 * absolute-luminance window sits where the ported tone curve was designed for
 * instead of four decades away from it. Every constant below that names an
 * absolute luminance moves with it, so the displayed image is unchanged and
 * only the units it is computed in have moved.
 */
#define RENDERER_RAY_TRACING_DEFAULT_LIGHT_SCALE 0.0625f
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
/*
 * 512, which is the 32 measured above times the sixteen the radiance
 * calibration took out, so Ray Reconstruction sees the same absolute values it
 * did before and this change is a pure change of units. Whether RR then wants
 * a different number is a separate question, and one this finally makes
 * askable: 32 used to sit against a ceiling of about 41.
 */
#define RENDERER_RAY_TRACING_DEFAULT_RR_INPUT_SCALE 512.0f
/*
 * Probability that a path's first bounce is aimed through one of its zone's
 * openings instead of drawn from the cosine distribution, zero through 0.9.
 *
 * A room lit only through a doorway gets all of its light from the few bounce
 * rays that happen to leave through that doorway. The rest return nothing, so
 * the room is a handful of very bright samples on black, and every one of them
 * reads as a firefly. The level already knows where its openings are: every
 * EdgeT that joins one zone to another. Aiming some bounces at the openings
 * whose far side can see a light makes those paths common instead of rare.
 *
 * It is unbiased. Both strategies are one mixture, and every path is weighted
 * by the cosine density over the mixture's density in its direction, so a
 * direction either strategy could produce is counted exactly once. The cosine
 * share keeps every direction reachable, which is why this stops short of
 * one, and it bounds a path's weight at 1 / (1 - this). Zero is plain cosine
 * sampling.
 */
#define RENDERER_RAY_TRACING_DEFAULT_PORTAL_SAMPLING 0.5f
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
/*
 * Whether authored emission follows the level's own animated vertex lighting.
 * Each world vertex's source lighting -- the fraction of fully lit the source
 * shades it at -- multiplies the light its surface emits, the texture the
 * camera sees included, so a zone whose CurrentPointBrights words carry an
 * Anim_BrightTable index pulses its emissive panels with newanims.s:brightanim
 * -- the floor_0101 panel beside Level A's start breathes on a four second
 * cycle -- and a zone the authors lit dimly has dim panels. Off leaves every
 * world emitter at its authored brightness.
 */
#define RENDERER_RAY_TRACING_DEFAULT_EMISSIVE_ANIMATION 1

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
    float portal_sampling;
    uint8_t portal_sampling_set;
    /* rtx_restir_emitter_change_limit; see
     * RENDERER_RAY_TRACING_DEFAULT_RESTIR_EMITTER_CHANGE_LIMIT. */
    float restir_emitter_change_limit;
    uint8_t restir_emitter_change_limit_set;
    /* rtx_emissive_animation; see RENDERER_RAY_TRACING_DEFAULT_EMISSIVE_ANIMATION. */
    uint8_t emissive_animation;
    uint8_t emissive_animation_set;
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
    /* rtx_rr_preset; see RENDERER_RAY_TRACING_DEFAULT_RR_PRESET. The flag
     * distinguishes an unset key from an explicit request for the driver's own
     * choice, which are different intentions and must not collapse. */
    RendererRayReconstructionPreset reconstruction_preset;
    uint8_t reconstruction_preset_set;
    /* Display-output policy. Hidden validation windows are always forced SDR. */
    RendererOutputMode output;
    /* Zero keeps Q2RTX's 800-nit scene default. */
    float hdr_peak_nits;
    /* Q2RTX-compatible percentage, 0 through 200; default 100. */
    float hdr_saturation_percent;
    uint8_t hdr_saturation_percent_set;
} RendererRayTracingOptions;

#endif
