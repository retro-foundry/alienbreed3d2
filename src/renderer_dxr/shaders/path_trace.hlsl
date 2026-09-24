static const float Pi = 3.14159265358979323846;
static const float RayEpsilon = 0.05;
static const uint InvalidIndex = 0xffffffffu;
/* Heterogeneous direct-light reservoirs use the index below for the analytic
 * environment. All real emitter indices are compact and strictly below
 * EmitterCount, so the value cannot alias source-authored geometry. */
static const uint EnvironmentLightIndex = 0xfffffffeu;
static const uint SceneInstanceMask = 0x01u;
static const uint WorldSurfacePrimitive = 0u;
static const uint ViewWeaponPrimitive = 1u;
static const uint WorldBillboardPrimitive = 2u;
static const uint WorldEffectPrimitive = 3u;
static const uint WorldVectorPrimitive = 4u;
static const float InvalidMotion = 65504.0;
/* Largest finite value of the RGBA16F buffers Ray Reconstruction reads. */
static const float HalfFloatMaximum = 65504.0;
/*
 * Mirrors `reconstruction::scene_far_plane` in dxr_reconstruction_math.h, which
 * also feeds the Streamline `cameraFar` constant. Background pixels must report
 * this distance in the linear-depth guide: with `depthInverted = eFalse` a zero
 * would place the sky at the near plane and invert every sky silhouette.
 */
static const float SceneFarPlane = 8192.0;

/* Layout mirrored by `DxrSceneVertex` in dxr_scene.h. */
struct SceneVertex
{
    float3 position;
    float2 textureCoordinate;
    uint materialIndex;
    uint emitterIndex;
    uint primitive;
    /* Packed 16-bit image-texel XY pairs; zero extent selects the full image. */
    uint textureWindowOrigin;
    uint textureWindowExtent;
    /* Per-vertex strength for explicitly authored additive effects. World PBR
     * polygon lights use neutral one; source Gouraud lighting is not radiance. */
    float emissiveScale;
    /* Camera-local source position for the view weapon; zero for the world. */
    float3 viewWeaponPosition;
    /* Emitted radiance, flat over the triangle and measured on the CPU from
     * the emissive texture; emissiveScale applies on top. See DxrSceneVertex. */
    float3 emission;
    /* The level's own lighting here, as a fraction of a fully lit surface.
     * Read only from the second bounce onward; see dxr_source_lighting.h. */
    float sourceIrradiance;
    /* The draw zone holding this surface; see DxrSceneVertex. */
    uint sourceZoneIndex;
};

struct SceneMaterial
{
    uint atlasX;
    uint atlasY;
    uint width;
    uint height;
    uint mipCount;
    float normalStrength;
    float specularFactor;
    float3 emissiveFactor;
};

/*
 * Layout mirrored by `DxrEmissiveTriangle` in dxr_scene.h. The alias pair is
 * Walker's alias table, built on the CPU in dxr_alias_table.h, so selecting an
 * emitter costs one lookup rather than a walk over every emitter.
 */
struct EmissiveTriangle
{
    uint firstVertex;
    float selectionProbability;
    float inverseArea;
    float aliasThreshold;
    uint aliasIndex;
    /* What light sampling emits: the mean of this triangle's emissive texture
     * over its own UVs, factor included and emissive scale not. See
     * DxrEmissiveTriangle in dxr_scene.h for why it is not the texel. */
    float3 radiance;
};

/* Mirrored by light_grid::Entry in dxr_light_grid.h. Each entry is one
 * independently built RIS proposal for its world-space cell. */
struct LightGridEntry
{
    uint emitterIndex;
    float inverseSelectionProbability;
};

struct IndirectSignal
{
    float4 luminanceSH;
    float2 chroma;
};

struct LightSelection
{
    uint emitterIndex;
    /* RIS correction for selecting this emitter, excluding the independent
     * uniform point sampled on its triangle. */
    float inverseProbability;
};

int lightGridCellForSurface(uint2 pixel, uint sampleIndex,
                            float3 surfacePosition);
LightSelection selectEmitterForCell(float selection, int cellIndex);

struct SurfacePayload
{
    float rayDistance;
    float2 barycentrics;
    uint primitiveIndex;
    uint hit;
};

struct ShadowPayload
{
    uint visible;
};

struct SurfaceData
{
    float3 position;
    float3 geometricNormal;
    float3 shadingNormal;
    float2 textureCoordinate;
    float3 baseColor;
    float roughness;
    float metalness;
    float specularFactor;
    float3 emission;
    /* Incident irradiance from the level's own lighting, already through the
     * source shading curve and the scale. Only bounce vertices consume it. */
    float sourceIrradiance;
    uint materialIndex;
    uint emitterIndex;
    uint primitive;
    uint textureWindowOrigin;
    uint textureWindowExtent;
    /* The draw zone holding this surface, plus one, so that the zero a
     * synthesised surface carries means "not known" rather than zone zero. */
    uint sourceZonePlusOne;
};

/*
 * One resampled light path, mirroring PathReservoir in
 * renderer_dxr/dxr_restir_reservoir.h. That header states the reasoning in
 * full and is covered by ab3d2_dxr_restir_reservoir_test; the arithmetic below
 * is the same arithmetic, not a second derivation of it.
 *
 * The reservoir holds ONE path chosen from every candidate it has seen, never
 * accumulated radiance.
 */
struct PathReservoir
{
    /* Reconnection vertex a shifted path rejoins this one at. */
    float3 translatedWorldPosition;
    /* Running resampling weight while streaming, contribution weight after
     * finalization. */
    float weightSum;

    float3 worldNormal;
    /* Effective candidate count this reservoir speaks for. */
    float m;

    /* Radiance arriving from the reconnection vertex along the stored path. */
    float3 radiance;
    float partialJacobian;

    /* RGB target function of the selected path at this pixel. */
    float3 targetFunction;
    float rcWiPdf;

    uint rcVertexLength;
    uint pathLength;
    /* Deterministic replay state: the seed and index ARE the path. */
    uint randomSeed;
    uint randomIndex;

    /* Frames the selected sample has survived. */
    uint age;
    /* Ancestry of the canonical sample this path descends from, which the
     * duplication map counts to find impoverished neighbourhoods. */
    uint ancestry;
    /* The primary surface this path was found at, carried in the reservoir
     * because there is no previous-frame G-buffer to compare against. A
     * reprojection landing on screen proves nothing; what has to match is the
     * surface the history actually represents. */
    uint primaryNormal;
    uint primaryMaterial;

    float3 primaryPosition;
    float primaryDepth;
};

struct BsdfEvaluation
{
    float3 diffuse;
    float3 specular;
    float3 value;
    float pdf;
};

struct DirectLightingSample
{
    float3 diffuse;
    float3 specular;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<SceneVertex> Vertices : register(t1);
StructuredBuffer<SceneMaterial> Materials : register(t2);
Texture2D<float4> BaseColorAtlas : register(t3);
Texture2D<float4> NormalAtlas : register(t4);
Texture2D<float4> MetalnessAtlas : register(t5);
Texture2D<float4> RoughnessAtlas : register(t6);
/* t7 still holds the emissive atlas, but nothing samples it: emission comes
 * from SceneVertex::emission, measured from that texture on the CPU. Leaving
 * the declaration out means any new read fails to compile rather than quietly
 * bringing texel emission back. */
StructuredBuffer<EmissiveTriangle> Emitters : register(t8);
StructuredBuffer<SceneVertex> PreviousVertices : register(t9);
ByteAddressBuffer BlueNoiseSampler : register(t10);
/*
 * One (first, count) pair per zone over ZoneLights, with the zone count in
 * element zero and zone z at element 1 + z; see DxrScene.
 */
StructuredBuffer<uint2> ZoneLightRanges : register(t11);
StructuredBuffer<uint> ZoneLights : register(t12);
SamplerState MaterialBilinearSampler : register(s0);

RWTexture2D<float4> NoisyRadiance : register(u0);
RWTexture2D<float4> DiffuseAlbedo : register(u1);
RWTexture2D<float4> SpecularAlbedo : register(u2);
RWTexture2D<float4> ShadingNormal : register(u3);
RWTexture2D<float> LinearRoughness : register(u4);
RWTexture2D<float> LinearDepth : register(u5);
RWTexture2D<float2> SceneMotion : register(u6);
RWTexture2D<float> SpecularHitDistance : register(u7);
RWTexture2D<float> DiffuseHitDistance : register(u8);
RWTexture2D<float> DiffuseHitDistanceHistory : register(u9);
/* Primary-ray coverage occupies words 0--4. Automatic-exposure diagnostics
 * occupy words 5--10. Direct-light lobe coverage occupies words 11--12, word
 * 13 counts non-finite radiance or mandatory RR guides, and word 14 records
 * smooth-GGX path coverage. Hidden GPU smoke reads them after the dispatch;
 * none is used to shade the image. */
/* Render-resolution reservoir grids. ReSTIR history is path history at the
 * internal rendering resolution and has nothing to do with the resolution DLSS
 * presents at, so these are never allocated at output extent. Both stay in the
 * unordered-access state for the whole frame, so the previous grid binds as a
 * UAV although this frame only reads it. */
RWStructuredBuffer<PathReservoir> CurrentReservoirs : register(u10);
RWStructuredBuffer<PathReservoir> PreviousReservoirs : register(u11);
/* Temporal output and spatial input. A pass may not read its neighbours out of
 * the grid it is writing, so the two stages hand over through this one. */
RWStructuredBuffer<PathReservoir> ResampleReservoirs : register(u22);
/* Ancestry of each pixel's surviving path, and the count of neighbours sharing
 * it. Both are render-resolution grids read a frame after they are written. */
RWStructuredBuffer<uint> SampleAncestry : register(u23);
RWStructuredBuffer<uint> DuplicationMap : register(u24);
/*
 * The canonical reservoir as it was before any reuse touched it.
 *
 * Ray Reconstruction expects temporally independent noise, and ReSTIR's whole
 * purpose is to correlate samples across frames. Enough correlation and the
 * denoiser reads it as structure rather than noise. Keeping the unresampled
 * sample lets final shading stochastically fall back to it, which is what
 * restores the independence the denoiser was built to assume.
 */
RWStructuredBuffer<PathReservoir> PreservedReservoirs : register(u25);
RWStructuredBuffer<uint> Diagnostics : register(u12);
RWStructuredBuffer<LightGridEntry> LightGrid : register(u13);
/* Demodulated diffuse-suffix lighting. Primary/burst shading writes a fresh
 * current-frame estimate into slot A. The retained history bindings stay at
 * one texel because final-radiance accumulation is no longer an active path. */
RWTexture2D<float4> IndirectRadianceA : register(u14);
RWTexture2D<float4> IndirectRadianceB : register(u15);
RWTexture2D<uint> IndirectHistoryMetadataA : register(u16);
RWTexture2D<float4> IndirectFiltered : register(u17);
RWStructuredBuffer<float> AutomaticExposure : register(u18);
RWTexture2D<float2> IndirectChromaA : register(u19);
RWTexture2D<float2> IndirectChromaB : register(u20);
RWTexture2D<uint> IndirectHistoryMetadataB : register(u21);
RWTexture2D<float2> StreamlineSceneMotion : register(u26);
RWTexture2D<uint> ViewWeaponHistories[2] : register(u27);
RWTexture2D<float> RayReconstructionDisocclusion : register(u29);
RWTexture2D<float> RayReconstructionBiasCurrentColor : register(u30);
RWTexture2D<uint> SurfaceParameters : register(u31);
/* Exact full-precision primary payload for the split-scheduling control.
 * Additive radiance uses NoisyRadiance as the adjacent handoff. */
RWTexture2D<uint4> PrimaryVisibilityBuffer : register(u32);
RWStructuredBuffer<uint2> BurstWorkItems : register(u33);
RWByteAddressBuffer BurstDispatchArguments : register(u34);

cbuffer FrameConstants : register(b0)
{
    float3 CameraPosition;
    float TanHalfFovY;
    float3 CameraForward;
    float Aspect;
    float3 CameraRight;
    uint SampleIndex;
    float3 CameraUp;
    uint MaximumDepth;
    uint OutputWidth;
    uint OutputHeight;
    uint TriangleCount;
    uint EmitterCount;
    float3 PreviousCameraPosition;
    uint HistoryValid;
    float3 PreviousCameraForward;
    float PreviousTanHalfFovY;
    float3 PreviousCameraRight;
    float PreviousAspect;
    float3 PreviousCameraUp;
    float JitterX;
    float JitterY;
    float PreviousJitterX;
    float PreviousJitterY;
    uint CandidateCount;
    float RadianceClamp;
    float NdfTrim;
    uint SamplesPerPixel;
    float ExposureDeltaSeconds;
    uint IndirectTemporalWindow;
    uint RadianceChannel;
    uint RayReconstructionWeaponPoseTransition;
    uint IndirectSamplesPerPixel;
    float3 LightGridCenter;
    uint LightGridRebuild;
    uint RayReconstructionActive;
    uint DiagnosticGuideMask;
    uint ValidationEnabled;
    uint SinglePrimaryDirectSurvivor;
    uint SingleContinuationLobe;
    uint IndirectLightSamples;
    uint BoundedBurstContinuations;
    uint CompactLocalPrimary;
    uint ProxyPrimaryCandidates;
    uint ForceSpecularGuide;
    float TracedSpecularRoughnessLimit;
    uint IndirectMode;
    /* Probability that final shading discards the resampled reservoir. */
    float ReservoirDecorrelation;
    /* Brightest emitter in the scene; see MaximumEmitterRadiance use below. */
    float MaximumEmitterRadiance;
    /* Cap on a reservoir's represented sample count. */
    uint ReservoirTemporalHistory;
    /* Spatial neighbours resampled per pixel. */
    uint ReservoirSpatialSamples;
    /* Search radius as a fraction of render height, so a DLSS quality change
     * cannot silently alter the image-space footprint searched. */
    float ReservoirSpatialRadius;
    /* Duplication-based history reduction strength; zero disables it. */
    float ReservoirHistoryReduction;
    /*
     * Radiance of a fully lit surface under the level's own lighting, which
     * bounce vertices return in place of tracing on. The game measures light
     * in palette rows, so this is what gives those rows a physical scale.
     * Zero restores pure path-traced indirect.
     */
    float SourceLightScale;
    /*
     * Zero routes candidate selection back to the scene-wide distribution,
     * which is what AB3D2_DXR_ZONE_LIGHTS=0 is for: it makes one build
     * measure both sides, so a comparison is never confounded by anything
     * else that changed between two builds.
     */
    uint ZoneLightsEnabled;
    /*
     * What Ray Reconstruction's input is multiplied by; post_process.hlsl
     * divides it back out. One when RR is not running. See
     * RENDERER_RAY_TRACING_DEFAULT_RR_INPUT_SCALE.
     */
    float ReconstructionInputScale;
};

cbuffer RayRootConstants : register(b1)
{
    /* Integer dimensions deliberately retain the shader-side reciprocal used
     * by the prior GetDimensions path, preserving deterministic sample math. */
    uint2 MaterialAtlasDimensions;
    uint InterleavedDeepDiffuse;
};

/*
 * ReSTIR reuse counters. Whether reuse is succeeding is not visible in the
 * image -- a rejected shift and an accepted one that contributes little look
 * identical -- so it is counted instead of inferred.
 */
static const uint DiagnosticTemporalConsidered = 22u;
static const uint DiagnosticTemporalSurfaceRejected = 23u;
static const uint DiagnosticTemporalShiftFailed = 24u;
static const uint DiagnosticTemporalAccepted = 25u;
static const uint DiagnosticSpatialConsidered = 26u;
static const uint DiagnosticSpatialAccepted = 27u;
/*
 * Energy either side of temporal reuse. Reuse must not change what a pixel is
 * worth on average, only how noisy that estimate is, so the two sums are
 * accumulated over the same pixels in the same frame and their ratio states
 * directly whether the estimator is biased or merely noisy.
 */
static const uint DiagnosticTemporalEnergyBefore = 28u;
static const uint DiagnosticTemporalEnergyAfter = 29u;
/* Mean shift Jacobian and the count behind it. For temporal reuse under a
 * near-static camera this should sit at one; anything else is the shift
 * changing the sample's density when the geometry says it should not. */
static const uint DiagnosticTemporalJacobianSum = 30u;
static const uint DiagnosticTemporalJacobianCount = 31u;
/* The confidence counts the weight is divided by. */
static const uint DiagnosticCanonicalM = 32u;
static const uint DiagnosticHistoryM = 33u;
/* Resolved radiance is target x weight. Splitting the ratio into those two
 * factors says whether reuse is inflating the weight or selecting brighter
 * samples without the weight falling to compensate. */
static const uint DiagnosticWeightBefore = 34u;
static const uint DiagnosticWeightAfter = 35u;
static const uint DiagnosticTargetBefore = 36u;
static const uint DiagnosticTargetAfter = 37u;
/* What the history reservoir resolves to as READ this frame. If the buffer is
 * intact this equals what the previous frame reported writing; if it does not,
 * something between the write and the read is changing it. */
static const uint DiagnosticHistoryEnergyRead = 38u;
/*
 * What this pass should output, computed per pixel from its own two inputs and
 * their confidences. Summing a prediction pixel by pixel avoids comparing a
 * ratio of sums against a sum of ratios, which is how the earlier factor split
 * misled me. If this disagrees with what the pass actually produced, the
 * disagreement is in one pixel's arithmetic and not in the statistics.
 */
static const uint DiagnosticPredictedAfter = 39u;
/*
 * Pixels that reached temporal reuse with, and without, a fresh canonical
 * sample. A pixel with none mixes nothing new in: its output is its history
 * copied forward, so whatever it holds persists indefinitely and never regresses
 * toward the mean the fresh samples describe.
 */
static const uint DiagnosticReuseWithCanonical = 40u;
static const uint DiagnosticReuseWithoutCanonical = 41u;
/* What the duplication map actually reports: sum of counts, pixels with any
 * duplicate at all, and pixels whose ancestry survived spatial reuse from a
 * neighbour rather than being their own. */
static const uint DiagnosticDuplicationSum = 42u;
static const uint DiagnosticDuplicationNonZero = 43u;
static const uint DiagnosticAncestryForeign = 44u;
/* Which guard refused a shift, in source order. */
static const uint DiagnosticShiftFail = 45u;
/* Log2 histogram of one indirect bounce's luminance, two stops per bucket from
 * 2^-8 upward. Whether the lighting energy sits in a sane range is a question
 * about this distribution, not about any single constant. */
static const uint DiagnosticLuminanceHistogram = 56u;
static const uint DiagnosticLuminanceBuckets = 16u;
/* Same per-pixel prediction the temporal pass passed at 1.002: what spatial
 * reuse must output, computed from its own inputs and their confidences. */
static const uint DiagnosticSpatialPredicted = 72u;
static const uint DiagnosticSpatialActual = 73u;
/* How often final shading throws the resampled reservoir away. Each fire
 * shades a single unresampled path, so a high rate means ReSTIR is doing
 * nothing for that pixel and the frame is effectively one sample per pixel. */
static const uint DiagnosticShaded = 74u;
static const uint DiagnosticDecorrelated = 75u;
static const uint DiagnosticFireflyReplaced = 76u;
static const uint DiagnosticAgeSum = 77u;


/* Mirrors RendererIndirectMode in renderer_ray_tracing_options.h. */
/*
 * Surface-compatibility tolerances. Temporal reuse is looking for the same
 * point on the same surface and can be strict; spatial reuse is deliberately
 * looking at a different point on what should be the same surface, so its
 * geometry tolerances are looser while identity stays exact.
 */
/* How far a reprojected surface may sit from where the motion vector says it
 * is, as a fraction of view depth. This is the ghosting control: too loose and
 * a disoccluded pixel inherits whatever was in front of it. */
/* How far a reconnection may change the sample's density before it is refused.
 * Near-degenerate geometry produces unbounded Jacobians, and an unbounded
 * resampling weight is a white pixel. */
static const float ReservoirMaximumJacobian = 8.0;
static const float ReservoirTemporalSeparation = 0.01;
static const float ReservoirTemporalDepthTolerance = 0.02;
static const float ReservoirTemporalNormalTolerance = 0.9;
static const float ReservoirSpatialDepthTolerance = 0.05;
static const float ReservoirSpatialNormalTolerance = 0.8;
/* The duplication window, matching the reference: 17x17, so 288 neighbours. */
static const int ReservoirDuplicationRadius = 8;
static const float ReservoirDuplicationNeighborCount = 288.0;

static const uint IndirectModePathTrace = 1u;
static const uint IndirectModeRestirPt = 2u;

static const uint RadianceChannelCombined = 0u;
static const uint RadianceChannelEmission = 1u;
static const uint RadianceChannelDirectDiffuse = 2u;
static const uint RadianceChannelDirectSpecular = 3u;
static const uint RadianceChannelIndirect = 4u;
static const uint RadianceChannelSmoothSpecular = 5u;
static const uint RadianceChannelRoughSpecular = 6u;

static const uint BlueNoiseSampleCount = 256u;
static const uint BlueNoiseDimensionCount = 256u;
static const uint BlueNoiseTileWidth = 128u;
static const uint BlueNoiseOptimizedDimensions = 8u;
static const uint BlueNoiseSobolOffset = 0u;
static const uint BlueNoiseScramblingOffset = 65536u;
static const uint BlueNoiseRankingOffset = 196608u;
static const uint MaximumDiffusePathDepth = 8u;
static const uint PathDimensionsPerBounce = 8u;
/* The first diffuse continuation is always traced. Only the lower-energy
 * suffix behind its reached surface is interleaved. The floor prevents rare
 * dark-path survivors from receiving an excessive inverse weight. */
static const float DeepDiffuseMinimumContinuationProbability = 0.25;
static const uint MaximumMaterialFilterTaps = 8u;
/*
 * How many additive layers one ray segment resolves before it gives up and
 * shades the next one as ordinary geometry.
 *
 * `objdrawhires.s:draw_bitmap_additive`, `draw_bitmap_glare` and `DOGLAREPOLY`
 * add their result into the frame buffer without testing or writing depth, so
 * an additive surface is light that never occludes and any number of them can
 * stack over one pixel. A path tracer has to spend a traversal on each, and the
 * limit only has to exceed what the source can actually pile up: a bullet, its
 * impact pop, and the glare panels around them. Sixteen is far above that, and
 * exhausting it leaves an opaque emissive quad rather than a hole in the world.
 */
static const uint AdditiveLayerLimit = 16u;
/* D3D12_DISPATCH_RAYS_DESC::Width follows the four shader-table address
 * ranges. Native code pins this ABI with an offsetof static assertion. */
static const uint BurstDispatchWidthOffset = 88u;
/*
 * The default RTXDI pass shape is staged: temporal reuse searches around the
 * motion-reprojected pixel, then a second dispatch samples current-frame
 * spatial neighbors. A fresh/disoccluded center takes eight neighbors while a
 * center with useful history takes one. Spatial matches use the looser
 * depth/normal thresholds appropriate for nearby, nonidentical points, but
 * still require the exact same material.
 */
/* NVIDIA's filter-free Ultra DI preset uses four ordinary spatial samples and
 * sixteen disocclusion-boost attempts. This renderer also uses ray-traced
 * correction and does not reuse final visibility, so those are the coherent
 * structural counts for its positive-history path. */
static const uint ReservoirNeighborOffsetCount = 256u;
static const uint ReservoirNeighborOffsetMask =
    ReservoirNeighborOffsetCount - 1u;
static const float IndirectShBasisL0 = 0.282095;
static const float IndirectShBasisL1 = 0.488603;
static const uint IndirectHistoryPrimitiveMask = 0x00ffffffu;
static const uint IndirectHistoryCountShift = 24u;
static const uint IndirectTemporalWindowMask = 0xffu;
static const uint IndirectLightingChangedFlag = 0x80000000u;
/* The compact identity-only history cannot prove that a moving subpixel
 * footprint still represents the same lighting point on a large triangle.
 * Keep reuse to stationary/subpixel receivers; measured larger-motion reuse
 * produced visible Shotgun trails even with exact primitive matching. */
static const float IndirectHistoryMotionLimit = 0.5;
static const uint ExposureSampleColumns = 32u;
static const uint ExposureSampleRows = 18u;
static const uint ExposureHistogramBinCount = 64u;
/*
 * The metered luminance range, matching Q2RTX's min_log_luminance -24 and
 * max_log_luminance +8 in tone_mapping_utils.glsl.
 *
 * The previous ceiling of 64 was below what the renderer actually produces:
 * single indirect bounces reach several thousand, so everything above 64
 * collapsed into the top histogram bin. A bright doorway seen from a dark
 * corridor therefore metered as one saturated bucket rather than as its real
 * distribution, the exposure was driven by a number that had lost its scale,
 * and the dark side of the frame went to the noise floor. Widening the range is
 * what lets the histogram describe a scene that spans both.
 */
static const float ExposureMinimumLuminance = 5.9604645e-8;
static const float ExposureMaximumLuminance = 256.0;
/*
 * Q2RTX's tm_low_percentile and tm_high_percentile. This pass is diagnostic --
 * nothing reads AutomaticExposure for shading, the displayed exposure comes
 * from post_process.hlsl -- but it reported a number that disagreed with the
 * one actually in force. Metering the 10th to 98th percentile spans nearly the
 * whole histogram, so both the darkest regions and the brightest outliers drag
 * it; the display path deliberately meters a narrow upper band that neither can
 * move. A diagnostic that does not measure what the renderer does is worse than
 * none, because it is believed.
 */
static const uint ExposureLowPercentileNumerator = 70u;
static const uint ExposureHighPercentileNumerator = 90u;
static const uint ExposurePercentileDenominator = 100u;
static const float ExposureMeteringKey = 0.014;
static const float ExposureMinimum = 0.125;
static const float ExposureMaximum = 4096.0;
static const float ExposureDarkAdaptationRate = 1.0;
static const float ExposureLightAdaptationRate = 4.0;
static const float ExposureMaximumDeltaSeconds = 0.25;
static const uint ReservoirCanonicalStream = 0x10200u;
static const uint ReservoirDecorrelationStream = 0x10b00u;
/* How far above the local canonical mean a resolved value may sit before it is
 * treated as a firefly, and an absolute floor so dark regions are not policed
 * against a near-zero mean. Zero threshold disables the filter. */
static const float ReservoirFireflyThreshold = 4.0;
static const float ReservoirFireflyStoreThreshold = 16.0;
static const float ReservoirFireflyFloor = 0.05;
static const uint ReservoirTemporalStream = 0x10800u;
static const uint ReservoirSpatialStream = 0x10900u;
static const uint ReservoirSpatialOffsetStream = 0x10a00u;
static const uint SecondaryDirectStream = 0x10600u;
static const uint DiffusePrimaryPolygonStream = 0x10700u;
/* Secondary polygon-light candidates need one nonoverlapping 1024-candidate
 * block per bounce. Keep them outside the compact control streams;
 * sampleStream hashes the full sequence index and advances with SampleIndex. */
static const uint DiffuseIndirectPolygonStream = 0x30000u;
static const uint SmoothSpecularDirectionStream = 0x10900u;
static const uint SmoothSpecularPolygonStream = 0x34000u;
static const uint ContinuationLobeSelectionStream = 0x10b00u;
static const uint DiffuseStratificationStream = 0x10c00u;
/* Keep primary light selection in the sampler's unused dimension range. The
 * configurable tail falls back to the unbounded hash stream before the 256
 * Sobol dimensions wrap and begin repeating candidates. */
static const uint PrimaryDirectBlueNoiseDimension = 64u;
static const uint PrimaryDirectBlueNoiseCandidateLimit =
    (BlueNoiseDimensionCount - PrimaryDirectBlueNoiseDimension) / 4u;
/* Primary direct lighting shades two independent RIS survivors rather than
 * asking one binary visibility result to represent every candidate. Splitting
 * the default 16 candidates into two groups leaves enough proposals in each
 * group to find sparse emissive texels while averaging two visibility results.
 * One candidate still reduces exactly to the single-survivor estimator. */
static const uint PrimaryDirectVisibilitySampleLimit = 2u;
/* `rtx_light_candidates` is capped at 1024. Two secondary-light survivors
 * partition that block into disjoint interleaved groups, while each bounce owns
 * a complete new block. */
static const uint IndirectPolygonCandidateStreamCount = 1024u;
static const uint IndirectPolygonLightSampleLimit = 2u;
static const uint DiffusePolygonBounceStreamStride =
    IndirectPolygonCandidateStreamCount;
static const uint SecondaryLocalSampleCount = 2u;
static const uint SecondaryEnvironmentSampleCount = 1u;
/*
 * Regular-grid ReGIR dimensions mirrored by dxr_light_grid.h. The grid is
 * rebuilt around CameraPosition before the primary dispatch whenever ReSTIR is
 * enabled. A lookup outside its 8192-unit span uses the full global proposal.
 */
static const uint LightGridCellsPerAxis = 16u;
static const uint LightGridCellCount =
    LightGridCellsPerAxis * LightGridCellsPerAxis * LightGridCellsPerAxis;
static const uint LightGridLightsPerCell = 512u;
static const uint LightGridBuildSamples = 8u;
static const uint LightGridRefreshPhaseCount = 16u;
static const uint CompactPrimaryCandidateLimit = 8u;
static const uint CompactPrimaryGlobalCandidates = 6u;
static const uint LightGridEntryCount =
    LightGridCellCount * LightGridLightsPerCell;
static const float LightGridCellSize = 512.0;
static const float LightGridExtent =
    LightGridCellSize * float(LightGridCellsPerAxis);
static const uint LightGridBuildStream = 0x20000u;
static const uint LightGridLookupStream = 0x20100u;

/*
 * The `pcg4d` integer hash from Jarzynski and Olano, "Hash Functions for GPU
 * Rendering", JCGT 9(3), 2020. Mirrored by `sample_stream::pcg4d` in
 * dxr_sample_stream.h, which pins this transcription in a CPU test.
 *
 * The candidate loop needs three dimensions per candidate across tens of
 * candidates. The pinned Heitz tables optimize eight dimensions in total, so
 * drawing candidates from them would reuse the same eight ranking and scrambling
 * channels through a tile translation and give neighbouring pixels correlated
 * candidates. The blue-noise sampler therefore keeps only the primary-hit
 * decisions it was built for.
 */
uint4 hashSampleStream(uint4 value)
{
    value = value * 1664525u + 1013904223u;
    value.x += value.y * value.w;
    value.y += value.z * value.x;
    value.z += value.x * value.y;
    value.w += value.y * value.z;
    value ^= value >> 16u;
    value.x += value.y * value.w;
    value.y += value.z * value.x;
    value.z += value.x * value.y;
    value.w += value.y * value.z;
    return value;
}

/* Four uniforms in [0, 1). The 24-bit shift keeps every result strictly below
 * one, which the alias-table bucket index relies on. */
float4 sampleStream(uint2 pixel, uint sampleIndex, uint sequenceIndex)
{
    uint4 hashed = hashSampleStream(
        uint4(pixel.x, pixel.y, sampleIndex, sequenceIndex));
    return float4(hashed >> 8u) * (1.0 / 16777216.0);
}

/* A project-authored fixed low-discrepancy disk, serving the same contract as
 * RTXDI's neighbor-offset buffer without importing its data or generator. The
 * cyclic start changes per pixel/frame, but consecutive spatial attempts walk
 * well-separated angles and bit-reversed radii instead of forming a fresh
 * random clump every frame. */
float2 reservoirNeighborOffset(uint sequenceIndex)
{
    uint index = sequenceIndex & ReservoirNeighborOffsetMask;
    uint radiusIndex = reversebits(index) >> 24u;
    float radius = sqrt((float(radiusIndex) + 0.5) /
                        float(ReservoirNeighborOffsetCount));
    const float goldenAngle = 2.39996322972865332;
    float angle = (float(index) + 0.5) * goldenAngle;
    return radius * float2(cos(angle), sin(angle));
}

/*
 * Octahedral normal packing, the standard mapping surveyed by Cigolle et al.,
 * "A Survey of Efficient Representations for Independent Unit Vectors", JCGT
 * 3(2), 2014. Sixteen bits per component is far more precise than the cosine
 * comparison that consumes it.
 */
uint packOctahedralNormal(float3 normal)
{
    float3 absolute = abs(normal);
    float2 projected = normal.xy /
        max(absolute.x + absolute.y + absolute.z, 1.0e-8);
    if (normal.z < 0.0) {
        projected = (1.0 - abs(projected.yx)) *
            float2(projected.x >= 0.0 ? 1.0 : -1.0,
                   projected.y >= 0.0 ? 1.0 : -1.0);
    }
    uint2 quantized = uint2(round(saturate(projected * 0.5 + 0.5) * 65535.0));
    return quantized.x | (quantized.y << 16u);
}

float3 unpackOctahedralNormal(uint packed)
{
    float2 projected = float2(uint2(packed & 0xffffu, packed >> 16u)) *
        (2.0 / 65535.0) - 1.0;
    float3 normal = float3(projected,
                           1.0 - abs(projected.x) - abs(projected.y));
    float fold = saturate(-normal.z);
    normal.x += normal.x >= 0.0 ? -fold : fold;
    normal.y += normal.y >= 0.0 ? -fold : fold;
    return normalize(normal);
}

/*
 * The reservoir stores the two canonical randoms that generated its area sample
 * rather than barycentrics, because the mapping is deterministic and pixel
 * independent. Sixteen-bit fixed point is used rather than half floats so the
 * resolution stays uniform across the unit interval. Candidates are quantized as
 * they are generated, so the weight a reservoir stores always describes exactly
 * the sample it stores.
 */
uint packPositionSample(float2 positionSample)
{
    uint2 quantized = uint2(round(saturate(positionSample) * 65535.0));
    return quantized.x | (quantized.y << 16u);
}

float2 unpackPositionSample(uint packed)
{
    return float2(uint2(packed & 0xffffu, packed >> 16u)) * (1.0 / 65535.0);
}

float2 quantizePositionSample(float2 positionSample)
{
    return unpackPositionSample(packPositionSample(positionSample));
}

uint packSurfaceF0(float3 f0)
{
    uint3 quantized = uint3(round(saturate(f0) * 1023.0));
    return quantized.r | (quantized.g << 10u) | (quantized.b << 20u);
}

float3 unpackSurfaceF0(uint packed)
{
    return float3(packed & 0x3ffu,
                  (packed >> 10u) & 0x3ffu,
                  (packed >> 20u) & 0x3ffu) * (1.0 / 1023.0);
}

uint blueNoiseByte(uint byteOffset)
{
    uint word = BlueNoiseSampler.Load(byteOffset & ~3u);
    return (word >> ((byteOffset & 3u) * 8u)) & 0xffu;
}

/*
 * Heitz et al., "A Low-Discrepancy Sampler that Distributes Monte Carlo
 * Errors as a Blue Noise in Screen Space", SIGGRAPH Talks 2019. The source
 * tables contain 256 Sobol dimensions and eight optimized ranking/scrambling
 * channels. Higher dimension groups use translated tiles as recommended for
 * padding the optimized channels without returning to mutable RNG state. A
 * second translation between 256-sample blocks avoids an aligned long-run
 * repeat while retaining the reference sequence within each block.
 */
float sampleBlueNoise(uint2 pixel, uint sampleIndex, uint dimension)
{
    dimension &= BlueNoiseDimensionCount - 1u;
    uint dimensionGroup = dimension / BlueNoiseOptimizedDimensions;
    uint sampleCycle = sampleIndex / BlueNoiseSampleCount;
    uint2 tilePixel = (pixel + uint2(dimensionGroup * 37u + sampleCycle * 53u,
                                     dimensionGroup * 59u + sampleCycle * 97u)) & 127u;
    uint tileIndex = tilePixel.x + tilePixel.y * BlueNoiseTileWidth;
    uint optimizedDimension =
        dimension & (BlueNoiseOptimizedDimensions - 1u);
    uint keyIndex = optimizedDimension +
        tileIndex * BlueNoiseOptimizedDimensions;
    uint rankedSampleIndex =
        (sampleIndex & (BlueNoiseSampleCount - 1u)) ^
        blueNoiseByte(BlueNoiseRankingOffset + keyIndex);
    uint value = blueNoiseByte(
        BlueNoiseSobolOffset + dimension +
        rankedSampleIndex * BlueNoiseDimensionCount);
    value ^= blueNoiseByte(BlueNoiseScramblingOffset + keyIndex);
    return (0.5 + float(value)) / float(BlueNoiseSampleCount);
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

IndirectSignal emptyIndirectSignal()
{
    IndirectSignal signal = (IndirectSignal)0;
    return signal;
}

/* First-order directional luminance plus unprojected opponent chroma. These
 * are standard real spherical-harmonic basis constants. The representation
 * retains the current sample's incident direction for rough-specular shading;
 * temporal GI accumulates this same representation. */
IndirectSignal indirectSignalFromRadiance(float3 color, float3 direction)
{
    IndirectSignal signal = emptyIndirectSignal();
    float opponentCo = color.r - color.b;
    float opponentBase = color.b + opponentCo * 0.5;
    float opponentCg = color.g - opponentBase;
    float opponentY = max(opponentBase + opponentCg * 0.5, 0.0);
    signal.luminanceSH = float4(
        direction * IndirectShBasisL1, IndirectShBasisL0) *
        opponentY;
    signal.chroma = float2(opponentCo, opponentCg);
    return signal;
}

IndirectSignal scaleIndirectSignal(IndirectSignal signal, float scale)
{
    signal.luminanceSH *= scale;
    signal.chroma *= scale;
    return signal;
}

uint currentIndirectHistorySlot()
{
    return (IndirectTemporalWindow & IndirectTemporalWindowMask) > 1u ?
        SampleIndex & 1u : 0u;
}

IndirectSignal loadIndirectSignal(uint slot, int2 pixel)
{
    IndirectSignal signal;
    signal.luminanceSH = slot == 0u ?
        IndirectRadianceA[pixel] : IndirectRadianceB[pixel];
    signal.chroma = slot == 0u ?
        IndirectChromaA[pixel] : IndirectChromaB[pixel];
    return signal;
}

void storeCurrentIndirectSignal(uint2 pixel, IndirectSignal signal)
{
    if (currentIndirectHistorySlot() == 0u) {
        IndirectRadianceA[pixel] = signal.luminanceSH;
        IndirectChromaA[pixel] = signal.chroma;
    } else {
        IndirectRadianceB[pixel] = signal.luminanceSH;
        IndirectChromaB[pixel] = signal.chroma;
    }
}

uint loadIndirectHistoryMetadata(uint slot, int2 pixel)
{
    return slot == 0u ?
        IndirectHistoryMetadataA[pixel] : IndirectHistoryMetadataB[pixel];
}

void storeCurrentIndirectHistoryMetadata(uint2 pixel, uint metadata)
{
    if (currentIndirectHistorySlot() == 0u) {
        IndirectHistoryMetadataA[pixel] = metadata;
    } else {
        IndirectHistoryMetadataB[pixel] = metadata;
    }
}

uint packIndirectHistoryMetadata(uint primitiveIndex, uint effectiveCount)
{
    return (primitiveIndex & IndirectHistoryPrimitiveMask) |
        (min(effectiveCount, 255u) << IndirectHistoryCountShift);
}

uint indirectHistoryPrimitive(uint metadata)
{
    return metadata & IndirectHistoryPrimitiveMask;
}

uint indirectHistoryEffectiveCount(uint metadata)
{
    return metadata >> IndirectHistoryCountShift;
}

bool finiteIndirectSignal(IndirectSignal signal)
{
    return !any(isnan(signal.luminanceSH)) &&
        !any(isinf(signal.luminanceSH)) &&
        !any(isnan(signal.chroma)) && !any(isinf(signal.chroma));
}

void recordIndirectTemporalDiagnostic(bool accepted, uint effectiveCount)
{
    if (ValidationEnabled == 0u ||
        (IndirectTemporalWindow & IndirectTemporalWindowMask) <= 1u) {
        return;
    }
    InterlockedAdd(Diagnostics[accepted ? 16u : 17u], 1u);
    uint bucket = clamp(effectiveCount, 1u, 4u) - 1u;
    InterlockedAdd(Diagnostics[18u + bucket], 1u);
}

/* The opponent-color portion of IndirectSignal is linear and reversible.
 * RR-only mode uses this exact RGB decode without renderer-owned filtering. */
float3 decodeIndirectSignalColor(IndirectSignal signal)
{
    float opponentY = signal.luminanceSH.w / IndirectShBasisL0;
    float base = opponentY - signal.chroma.y * 0.5;
    float green = signal.chroma.y + base;
    float blue = base - signal.chroma.x * 0.5;
    float red = blue + signal.chroma.x;
    return float3(red, green, blue);
}

float powerHeuristic(float firstPdf, float secondPdf)
{
    float first = firstPdf * firstPdf;
    float second = secondPdf * secondPdf;
    return first / max(first + second, 1.0e-8);
}

float3 environmentRadiance(float3 direction)
{
    float horizon = saturate(direction.y * 0.5 + 0.5);
    return lerp(float3(0.025, 0.035, 0.055), float3(0.8, 0.95, 1.2),
                horizon * horizon);
}

void coordinateSystem(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 helper = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) :
                                            float3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, normal));
    bitangent = cross(normal, tangent);
}

float3 cosineHemisphere(float3 normal, float2 sampleValue)
{
    float first = sampleValue.x;
    float second = sampleValue.y;
    float radius = sqrt(first);
    float angle = 2.0 * Pi * second;
    float3 tangent;
    float3 bitangent;
    coordinateSystem(normal, tangent, bitangent);
    return normalize(tangent * (radius * cos(angle)) +
                     bitangent * (radius * sin(angle)) +
                     normal * sqrt(max(0.0, 1.0 - first)));
}

/* Stratify each pixel's genuine diffuse paths instead of synthesizing
 * radiance between pixels. The radial ordinal covers every equal-area disk
 * band once. A frame/pixel-dependent circular permutation decorrelates the
 * azimuthal strata, followed by a common rotation that prevents fixed seams.
 * A complete per-pixel set remains a standard cosine-hemisphere estimator;
 * changing diagnostic history length never changes or rewinds these samples. */
float2 stratifiedDiffuseDirectionSample(uint2 pixel, uint sampleIndex,
                                        uint continuationIndex,
                                        float2 jitter)
{
    uint count = max(IndirectSamplesPerPixel, 1u);
    uint ordinal = sampleIndex % count;
    uint frameIndex = sampleIndex / count;
    float4 random = sampleStream(
        pixel, frameIndex,
        DiffuseStratificationStream + continuationIndex);
    uint angularOrdinal =
        (ordinal + uint(random.x * float(count))) % count;
    float2 stratified = (float2(ordinal, angularOrdinal) + jitter) /
        float(count);
    stratified.y = frac(stratified.y + random.y);
    return stratified;
}

struct MaterialTextureWindow
{
    uint2 origin;
    uint2 extent;
};

MaterialTextureWindow materialTextureWindow(SceneMaterial material,
                                             uint packedOrigin,
                                             uint packedExtent)
{
    MaterialTextureWindow window;
    window.origin = uint2(packedOrigin & 0xffffu, packedOrigin >> 16u);
    window.extent = uint2(packedExtent & 0xffffu, packedExtent >> 16u);
    if (window.extent.x == 0u || window.extent.y == 0u) {
        window.origin = 0u;
        window.extent = uint2(material.width, material.height);
    }
    return window;
}

uint2 materialTexel(SceneMaterial material, float2 textureCoordinate,
                    uint packedWindowOrigin, uint packedWindowExtent)
{
    MaterialTextureWindow window = materialTextureWindow(
        material, packedWindowOrigin, packedWindowExtent);
    float2 wrapped = frac(textureCoordinate);
    return uint2(material.atlasX, material.atlasY) + window.origin +
        min(uint2(wrapped * float2(window.extent)), window.extent - 1u);
}

uint materialMipYOffset(uint baseHeight, uint level)
{
    uint offset = 0u;
    for (uint index = 0u; index < level; ++index) {
        offset += max(1u, baseHeight >> index) + 2u;
    }
    return offset;
}

/* Scene compilation wraps every material level in a one-texel repeat gutter.
 * All source subwindows are isolated into complete material tiles before this
 * point, so hardware bilinear filtering is both exact at repeat seams and one
 * texture operation instead of four explicit loads. */
float2 materialAtlasInverseDimensions()
{
    return rcp(float2(MaterialAtlasDimensions));
}

float4 sampleMaterialAtlasLevelHardware(
    Texture2D<float4> atlas, SceneMaterial material,
    float2 textureCoordinate, uint packedWindowOrigin,
    uint packedWindowExtent, uint level, float2 inverseAtlasDimensions)
{
    MaterialTextureWindow window = materialTextureWindow(
        material, packedWindowOrigin, packedWindowExtent);
    uint2 levelExtent = max(uint2(1u, 1u), window.extent >> level);
    uint2 origin = uint2(material.atlasX, material.atlasY) +
        uint2(window.origin.x >> level,
              materialMipYOffset(material.height, level) +
                  (window.origin.y >> level));
    float2 atlasPosition = float2(origin) +
        frac(textureCoordinate) * float2(levelExtent);
    return atlas.SampleLevel(MaterialBilinearSampler,
                             atlasPosition * inverseAtlasDimensions,
                             0.0);
}

float4 sampleMaterialAtlasHardware(Texture2D<float4> atlas,
                                   SceneMaterial material,
                                   float2 textureCoordinate,
                                   uint packedWindowOrigin,
                                   uint packedWindowExtent)
{
    return sampleMaterialAtlasLevelHardware(
        atlas, material, textureCoordinate, packedWindowOrigin,
        packedWindowExtent, 0u, materialAtlasInverseDimensions());
}

/*
 * Toksvig specular antialiasing, matching Q2RTX's AdjustRoughnessToksvig.
 *
 * A mip-averaged normal map loses its high-frequency detail, and the averaged
 * normal gets shorter as it does. Widening roughness by exactly that shortening
 * keeps the specular lobe as broad as the detail it can no longer resolve, so a
 * surface keeps its material character with distance instead of flattening into
 * a narrow highlight that shimmers. Without it a normal map simply fades out.
 */
static const float ToksvigStrength = 1.0;

/* Square of roughness to a Phong specular power, and back. */
float roughnessSquareToSpecPower(float alpha)
{
    return max(0.01, 2.0 / (alpha * alpha + 1.0e-4) - 2.0);
}

float specPowerToRoughnessSquare(float power)
{
    return clamp(sqrt(max(0.0, 2.0 / (power + 2.0))), 0.0, 1.0);
}

float adjustRoughnessToksvig(float roughness, float normalMapLength,
                             float mipLevel)
{
    float effect = ToksvigStrength * clamp(mipLevel, 0.0, 1.0);
    if (!(effect > 0.0) || !(normalMapLength > 0.0)) {
        return roughness;
    }
    /* Deliberately not squaring the roughness here, as in the reference. */
    float shininess = roughnessSquareToSpecPower(roughness) * effect;
    float factor = normalMapLength /
        lerp(shininess, 1.0, normalMapLength);
    factor = max(factor, 0.01);
    return specPowerToRoughnessSquare(factor * shininess / effect);
}

struct MaterialFilterFootprint
{
    float mipLevel;
    float2 majorAxis;
    uint sampleCount;
};

float4 sampleMaterialAtlasTrilinearHardware(
    Texture2D<float4> atlas, SceneMaterial material,
    float2 textureCoordinate, uint packedWindowOrigin,
    uint packedWindowExtent, float mipLevel,
    float2 inverseAtlasDimensions)
{
    uint mipCount = max(material.mipCount, 1u);
    float boundedLevel = clamp(mipLevel, 0.0, float(mipCount - 1u));
    uint lowerLevel = uint(floor(boundedLevel));
    uint upperLevel = min(lowerLevel + 1u, mipCount - 1u);
    float4 lower = sampleMaterialAtlasLevelHardware(
        atlas, material, textureCoordinate, packedWindowOrigin,
        packedWindowExtent, lowerLevel, inverseAtlasDimensions);
    float blend = frac(boundedLevel);
    if (blend <= 0.0) {
        return lower;
    }
    return lerp(
        lower,
        sampleMaterialAtlasLevelHardware(
            atlas, material, textureCoordinate, packedWindowOrigin,
            packedWindowExtent, upperLevel, inverseAtlasDimensions),
        blend);
}

float4 sampleMaterialAtlasFilteredHardware(
    Texture2D<float4> atlas, SceneMaterial material,
    float2 textureCoordinate, uint packedWindowOrigin,
    uint packedWindowExtent, MaterialFilterFootprint filter,
    float2 inverseAtlasDimensions)
{
    float4 value = 0.0;
    float inverseCount = rcp(float(filter.sampleCount));
    [loop]
    for (uint sampleIndex = 0u; sampleIndex < filter.sampleCount;
         ++sampleIndex) {
        float position = (float(sampleIndex) + 0.5) * inverseCount - 0.5;
        value += sampleMaterialAtlasTrilinearHardware(
            atlas, material,
            textureCoordinate + filter.majorAxis * position,
            packedWindowOrigin, packedWindowExtent, filter.mipLevel,
            inverseAtlasDimensions);
    }
    return value * inverseCount;
}

void triangleFrame(uint firstVertex, float3 incomingDirection,
                   out float3 geometricNormal, out float3 tangent,
                   out float3 bitangent)
{
    SceneVertex first = Vertices[firstVertex + 0u];
    SceneVertex second = Vertices[firstVertex + 1u];
    SceneVertex third = Vertices[firstVertex + 2u];
    float3 firstEdge = second.position - first.position;
    float3 secondEdge = third.position - first.position;
    geometricNormal = normalize(cross(firstEdge, secondEdge));
    if (dot(geometricNormal, incomingDirection) > 0.0) {
        geometricNormal = -geometricNormal;
    }
    float2 firstUvEdge = second.textureCoordinate - first.textureCoordinate;
    float2 secondUvEdge = third.textureCoordinate - first.textureCoordinate;
    float determinant = firstUvEdge.x * secondUvEdge.y -
                        firstUvEdge.y * secondUvEdge.x;
    if (abs(determinant) > 1.0e-8) {
        tangent = (firstEdge * secondUvEdge.y - secondEdge * firstUvEdge.y) /
                  determinant;
        tangent = tangent - geometricNormal * dot(geometricNormal, tangent);
        if (dot(tangent, tangent) > 1.0e-10) {
            tangent = normalize(tangent);
            float3 sourceBitangent =
                (secondEdge * firstUvEdge.x - firstEdge * secondUvEdge.x) /
                determinant;
            float handedness = dot(cross(geometricNormal, tangent),
                                   sourceBitangent) < 0.0 ? -1.0 : 1.0;
            bitangent = cross(geometricNormal, tangent) * handedness;
            return;
        }
    }
    coordinateSystem(geometricNormal, tangent, bitangent);
}

/* Ray shaders have no screen-space derivatives. Differentiate the camera ray's
 * intersection with the hit plane along both screen axes, transform those
 * axes into authored texel space, and retain the resulting ellipse. The
 * packed software mip atlas does not expose those levels to SampleGrad, so a
 * bounded line filter covers the long axis while explicit trilinear samples
 * cover the short axis.
 * Walls, floors, and ceilings have mip chains; other primitives stay exactly
 * level zero. */
MaterialFilterFootprint worldMaterialFilterFootprint(
    SceneMaterial material, SceneVertex first, SceneVertex second,
    SceneVertex third, float3 surfacePosition, float3 geometricNormal)
{
    MaterialFilterFootprint filter;
    filter.mipLevel = 0.0;
    filter.majorAxis = 0.0;
    filter.sampleCount = 1u;
    if (material.mipCount <= 1u || first.textureWindowExtent == 0u) {
        return filter;
    }
    float3 firstEdge = second.position - first.position;
    float3 secondEdge = third.position - first.position;
    float firstSquared = dot(firstEdge, firstEdge);
    float crossed = dot(firstEdge, secondEdge);
    float secondSquared = dot(secondEdge, secondEdge);
    float determinant = firstSquared * secondSquared - crossed * crossed;
    if (determinant <= 1.0e-12) {
        return filter;
    }
    float3 firstDual =
        (secondSquared * firstEdge - crossed * secondEdge) / determinant;
    float3 secondDual =
        (firstSquared * secondEdge - crossed * firstEdge) / determinant;
    float2 firstUvEdge = second.textureCoordinate - first.textureCoordinate;
    float2 secondUvEdge = third.textureCoordinate - first.textureCoordinate;
    MaterialTextureWindow window = materialTextureWindow(
        material, first.textureWindowOrigin, first.textureWindowExtent);
    float3 uGradient =
        (firstUvEdge.x * firstDual + secondUvEdge.x * secondDual) *
        float(window.extent.x);
    float3 vGradient =
        (firstUvEdge.y * firstDual + secondUvEdge.y * secondDual) *
        float(window.extent.y);

    float3 cameraOffset = surfacePosition - CameraPosition;
    float viewDepth = dot(cameraOffset, CameraForward);
    if (viewDepth <= RayEpsilon) {
        return filter;
    }
    uint tracingWidth = 0u;
    uint tracingHeight = 0u;
    LinearDepth.GetDimensions(tracingWidth, tracingHeight);
    float2 renderSize = float2(max(
        uint2(tracingWidth, tracingHeight), uint2(1u, 1u)));
    float3 cameraRay = cameraOffset / viewDepth;
    float planeDenominator = dot(geometricNormal, cameraRay);
    if (abs(planeDenominator) <= 1.0e-6) {
        filter.mipLevel = float(material.mipCount - 1u);
        return filter;
    }
    float differentialScale = viewDepth * 2.0 * TanHalfFovY / renderSize.y;
    float3 xDifferential = differentialScale *
        (CameraRight - cameraRay *
            (dot(geometricNormal, CameraRight) / planeDenominator));
    float3 yDifferential = differentialScale *
        (CameraUp - cameraRay *
            (dot(geometricNormal, CameraUp) / planeDenominator));
    float2 xTexels = float2(dot(uGradient, xDifferential),
                            dot(vGradient, xDifferential)) *
        (renderSize.x / float(max(OutputWidth, 1u)));
    float2 yTexels = float2(dot(uGradient, yDifferential),
                            dot(vGradient, yDifferential)) *
        (renderSize.y / float(max(OutputHeight, 1u)));

    /* Singular values of the screen-to-texel Jacobian give the long and short
     * axes without combining unrelated worst-case directions. */
    float uu = xTexels.x * xTexels.x + yTexels.x * yTexels.x;
    float uv = xTexels.x * xTexels.y + yTexels.x * yTexels.y;
    float vv = xTexels.y * xTexels.y + yTexels.y * yTexels.y;
    float discriminant = sqrt(max(
        (uu - vv) * (uu - vv) + 4.0 * uv * uv, 0.0));
    float majorEigenvalue = max(0.5 * (uu + vv + discriminant), 0.0);
    float minorEigenvalue = max(0.5 * (uu + vv - discriminant), 0.0);
    float2 majorDirection;
    if (abs(uv) > 1.0e-8) {
        majorDirection = normalize(float2(majorEigenvalue - vv, uv));
    } else {
        majorDirection = uu >= vv ? float2(1.0, 0.0) : float2(0.0, 1.0);
    }
    float major = max(sqrt(majorEigenvalue), 1.0);
    float minor = max(sqrt(minorEigenvalue), 1.0);
    float boundedAnisotropy = min(
        major / minor, float(MaximumMaterialFilterTaps));
    filter.sampleCount = uint(ceil(boundedAnisotropy));
    float perSampleFootprint = max(
        minor, major / float(filter.sampleCount));
    filter.mipLevel = clamp(
        log2(perSampleFootprint), 0.0, float(material.mipCount - 1u));
    filter.majorAxis = majorDirection * sqrt(majorEigenvalue) /
        float2(window.extent);
    return filter;
}

SurfaceData loadSurface(SurfacePayload payload, float3 incomingDirection)
{
    uint firstVertex = payload.primitiveIndex * 3u;
    SceneVertex first = Vertices[firstVertex + 0u];
    SceneVertex second = Vertices[firstVertex + 1u];
    SceneVertex third = Vertices[firstVertex + 2u];
    float firstWeight =
        1.0 - payload.barycentrics.x - payload.barycentrics.y;
    SurfaceData surface;
    surface.position = first.position * firstWeight +
        second.position * payload.barycentrics.x +
        third.position * payload.barycentrics.y;
    surface.textureCoordinate = first.textureCoordinate * firstWeight +
        second.textureCoordinate * payload.barycentrics.x +
        third.textureCoordinate * payload.barycentrics.y;
    surface.materialIndex = first.materialIndex;
    surface.emitterIndex = first.emitterIndex;
    surface.primitive = first.primitive;
    surface.textureWindowOrigin = first.textureWindowOrigin;
    surface.textureWindowExtent = first.textureWindowExtent;
    float3 tangent;
    float3 bitangent;
    triangleFrame(firstVertex, incomingDirection, surface.geometricNormal,
                  tangent, bitangent);
    SceneMaterial material = Materials[surface.materialIndex];
    MaterialFilterFootprint filter = worldMaterialFilterFootprint(
        material, first, second, third, surface.position,
        surface.geometricNormal);
    float2 inverseAtlasDimensions = materialAtlasInverseDimensions();
    surface.baseColor = saturate(
        sampleMaterialAtlasFilteredHardware(
            BaseColorAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            filter, inverseAtlasDimensions).rgb);
    float3 tangentNormal =
        sampleMaterialAtlasFilteredHardware(
            NormalAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            filter, inverseAtlasDimensions).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalStrength;
    /* Length before normalizing: how much detail the mip average threw away. */
    float normalMapLength = saturate(length(tangentNormal));
    tangentNormal = normalize(float3(tangentNormal.xy,
                                     max(tangentNormal.z, 1.0e-4)));
    surface.shadingNormal = normalize(tangent * tangentNormal.x +
        bitangent * tangentNormal.y + surface.geometricNormal * tangentNormal.z);
    if (dot(surface.shadingNormal, surface.geometricNormal) <= 0.0) {
        surface.shadingNormal = surface.geometricNormal;
    }
    surface.metalness = saturate(
        sampleMaterialAtlasFilteredHardware(
            MetalnessAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            filter, inverseAtlasDimensions).r);
    surface.specularFactor = saturate(material.specularFactor);
    surface.roughness = clamp(
        sampleMaterialAtlasFilteredHardware(
            RoughnessAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            filter, inverseAtlasDimensions).r,
        0.045, 1.0);
    surface.roughness = clamp(
        adjustRoughnessToksvig(surface.roughness, normalMapLength,
                               filter.mipLevel),
        0.045, 1.0);
    float emissionScale = first.emissiveScale * firstWeight +
        second.emissiveScale * payload.barycentrics.x +
        third.emissiveScale * payload.barycentrics.y;
    /* Flat per triangle; the emissive texture is never sampled. */
    surface.emission = first.emission * emissionScale;
    surface.sourceZonePlusOne = first.sourceZoneIndex + 1u;
    surface.sourceIrradiance = SourceLightScale * (
        first.sourceIrradiance * firstWeight +
        second.sourceIrradiance * payload.barycentrics.x +
        third.sourceIrradiance * payload.barycentrics.y);
    return surface;
}

float3 previousSurfacePosition(SurfacePayload payload)
{
    uint firstVertex = payload.primitiveIndex * 3u;
    SceneVertex first = PreviousVertices[firstVertex + 0u];
    SceneVertex second = PreviousVertices[firstVertex + 1u];
    SceneVertex third = PreviousVertices[firstVertex + 2u];
    float firstWeight =
        1.0 - payload.barycentrics.x - payload.barycentrics.y;
    return first.position * firstWeight +
        second.position * payload.barycentrics.x +
        third.position * payload.barycentrics.y;
}

float3 currentViewWeaponPosition(SurfacePayload payload)
{
    uint firstVertex = payload.primitiveIndex * 3u;
    SceneVertex first = Vertices[firstVertex + 0u];
    SceneVertex second = Vertices[firstVertex + 1u];
    SceneVertex third = Vertices[firstVertex + 2u];
    float firstWeight =
        1.0 - payload.barycentrics.x - payload.barycentrics.y;
    return first.viewWeaponPosition * firstWeight +
        second.viewWeaponPosition * payload.barycentrics.x +
        third.viewWeaponPosition * payload.barycentrics.y;
}

float3 previousViewWeaponPosition(SurfacePayload payload)
{
    uint firstVertex = payload.primitiveIndex * 3u;
    SceneVertex first = PreviousVertices[firstVertex + 0u];
    SceneVertex second = PreviousVertices[firstVertex + 1u];
    SceneVertex third = PreviousVertices[firstVertex + 2u];
    float firstWeight =
        1.0 - payload.barycentrics.x - payload.barycentrics.y;
    return first.viewWeaponPosition * firstWeight +
        second.viewWeaponPosition * payload.barycentrics.x +
        third.viewWeaponPosition * payload.barycentrics.y;
}

/*
 * One ray segment resolved through the additive geometry standing in it.
 *
 * The source renderer draws its glares, additive bitmaps and `predoglare`
 * vector faces with the destination unchanged apart from the addition, and the
 * OpenGL path reproduces that as `glBlendFunc(GL_ONE, GL_ONE)` with
 * `glDepthMask(GL_FALSE)`: the effect adds light, is occluded by anything in
 * front of it, and occludes nothing itself. The path-traced equivalent is a
 * surface a ray passes straight through, collecting its emission on the way and
 * keeping both its direction and its throughput. That is why an additive hit
 * does not consume a bounce - it is not a scattering event - and why the
 * reconstruction guides come from the first surface behind the effect instead
 * of the effect itself, which has no depth, normal or albedo of its own.
 *
 * The accumulated radiance is the whole additive contribution for the segment,
 * and it needs no MIS weight: `compile_emissive_triangles` in dxr_scene.cpp
 * keeps every `world_effect` triangle out of the emitter list, so no
 * next-event estimator ever samples one and a BSDF-sampled path is the only
 * estimator that sees it.
 */
struct SegmentTraversal
{
    /* The first hit that is not additive, or a miss. */
    SurfacePayload payload;
    float3 additiveRadiance;
    /* Distance from the segment's own origin, across every layer passed. */
    float distance;
    uint additiveLayers;
};

SegmentTraversal traceSegment(RayDesc ray)
{
    SegmentTraversal result;
    result.additiveRadiance = 0.0;
    result.distance = 0.0;
    result.additiveLayers = 0u;
    float travelled = 0.0;
    const float reach = ray.TMax;
    for (uint layer = 0u; layer <= AdditiveLayerLimit; ++layer) {
        SurfacePayload payload;
        payload.rayDistance = 0.0;
        payload.barycentrics = 0.0;
        payload.primitiveIndex = InvalidIndex;
        payload.hit = 0u;
        TraceRay(Scene, RAY_FLAG_NONE, SceneInstanceMask,
                 0, 0, 0, ray, payload);
        result.payload = payload;
        result.distance = travelled + payload.rayDistance;
        if (payload.hit == 0u) {
            return result;
        }
        if (Vertices[payload.primitiveIndex * 3u].primitive !=
            WorldEffectPrimitive) {
            return result;
        }
        if (layer == AdditiveLayerLimit) {
            /* Out of layers. Leave this one for the caller to shade as an
             * ordinary emissive surface, which also adds the emission this
             * loop would otherwise have counted twice. */
            return result;
        }
        SurfaceData surface = loadSurface(payload, ray.Direction);
        result.additiveRadiance += surface.emission;
        ++result.additiveLayers;
        travelled += payload.rayDistance;
        /* Resume just past the layer with the segment's remaining reach, so
         * passing through costs distance rather than extending it. */
        float remaining = reach - travelled;
        if (remaining <= RayEpsilon) {
            result.payload.hit = 0u;
            return result;
        }
        ray.Origin = surface.position + ray.Direction * RayEpsilon;
        ray.TMax = remaining;
    }
    return result;
}

/* NVIDIA Streamline v2.12.0 ProgrammingGuideDLSS_RR.md section 4.2.1. */
float3 reconstructionSpecularAlbedo(float3 specularColor,
                                    float linearRoughness,
                                    float normalView)
{
    float alpha = linearRoughness * linearRoughness;
    float noV = abs(normalView);
    float4 x = float4(1.0, noV, noV * noV, noV * noV * noV);
    float4 y = float4(1.0, alpha, alpha * alpha, alpha * alpha * alpha);
    float2x2 m1 = float2x2(0.99044, -1.28514,
                           1.29678, -0.755907);
    float3x3 m2 = float3x3(1.0, 2.92338, 59.4188,
                           20.3225, -27.0302, 222.592,
                           121.563, 626.13, 316.627);
    float2x2 m3 = float2x2(0.0365463, 3.32707,
                           9.0632, -9.04756);
    float3x3 m4 = float3x3(1.0, 3.59685, -1.36772,
                           9.04401, -16.3174, 9.22949,
                           5.56589, 19.7886, -20.2123);
    float bias = dot(mul(m1, x.xy), y.xy) /
        dot(mul(m2, x.xyw), y.xyw);
    float scale = dot(mul(m3, x.xy), y.xy) /
        dot(mul(m4, x.xzw), y.xyw);
    bias *= saturate(specularColor.g * 50.0);
    return specularColor * max(0.0, scale) + max(0.0, bias);
}

bool projectWorldToPixel(float3 worldPosition, float3 cameraPosition,
                         float3 cameraForward, float3 cameraRight,
                         float3 cameraUp, float tanHalfFovY, float aspect,
                         float2 dimensions, out float2 pixel)
{
    float3 relative = worldPosition - cameraPosition;
    float viewDepth = dot(relative, cameraForward);
    if (viewDepth <= 1.0e-6) {
        pixel = 0.0;
        return false;
    }
    float2 ndc = float2(
        dot(relative, cameraRight) / (viewDepth * aspect * tanHalfFovY),
        dot(relative, cameraUp) / (viewDepth * tanHalfFovY));
    if (any(isnan(ndc)) || any(isinf(ndc))) {
        pixel = 0.0;
        return false;
    }
    pixel = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * dimensions;
    return true;
}

bool projectDirectionToPixel(float3 direction, float3 cameraForward,
                             float3 cameraRight, float3 cameraUp,
                             float tanHalfFovY, float aspect,
                             float2 dimensions, out float2 pixel)
{
    return projectWorldToPixel(direction, 0.0, cameraForward, cameraRight,
                               cameraUp, tanHalfFovY, aspect, dimensions,
                               pixel);
}

float2 surfaceMotion(SurfacePayload payload, SurfaceData surface,
                     float2 dimensions, bool viewWeapon)
{
    if (HistoryValid == 0u) {
        return InvalidMotion.xx;
    }
    float2 currentPixel;
    float2 previousPixel;
    bool currentValid;
    bool previousValid;
    if (viewWeapon) {
        float3 currentPosition = currentViewWeaponPosition(payload);
        float3 previousPosition = previousViewWeaponPosition(payload);
        currentValid = currentPosition.z > 1.0e-6;
        previousValid = previousPosition.z > 1.0e-6;
        if (currentValid) {
            float2 currentNdc = float2(
                currentPosition.x /
                    (currentPosition.z * Aspect * TanHalfFovY),
                currentPosition.y / (currentPosition.z * TanHalfFovY));
            currentValid = !any(isnan(currentNdc)) &&
                !any(isinf(currentNdc));
            currentPixel = float2(currentNdc.x * 0.5 + 0.5,
                                  0.5 - currentNdc.y * 0.5) * dimensions;
        }
        if (previousValid) {
            float2 previousNdc = float2(
                previousPosition.x /
                    (previousPosition.z * PreviousAspect *
                     PreviousTanHalfFovY),
                previousPosition.y /
                    (previousPosition.z * PreviousTanHalfFovY));
            previousValid = !any(isnan(previousNdc)) &&
                !any(isinf(previousNdc));
            previousPixel = float2(previousNdc.x * 0.5 + 0.5,
                                   0.5 - previousNdc.y * 0.5) * dimensions;
        }
    } else {
        currentValid = projectWorldToPixel(
            surface.position, CameraPosition, CameraForward, CameraRight,
            CameraUp, TanHalfFovY, Aspect, dimensions, currentPixel);
        previousValid = projectWorldToPixel(
            previousSurfacePosition(payload), PreviousCameraPosition,
            PreviousCameraForward, PreviousCameraRight, PreviousCameraUp,
            PreviousTanHalfFovY, PreviousAspect, dimensions, previousPixel);
    }
    if (!currentValid || !previousValid) {
        return InvalidMotion.xx;
    }
    /* A displacement larger than the input extent cannot reference useful
     * history. Bound it to a finite off-screen reprojection that survives FP16
     * storage instead of rounding 65500 back to the 65504 invalid sentinel. */
    return clamp(previousPixel - currentPixel, -dimensions, dimensions);
}

float2 environmentMotion(float3 direction, float2 dimensions)
{
    if (HistoryValid == 0u) {
        return InvalidMotion.xx;
    }
    float2 currentPixel;
    float2 previousPixel;
    bool currentValid = projectDirectionToPixel(
        direction, CameraForward, CameraRight, CameraUp, TanHalfFovY, Aspect,
        dimensions, currentPixel);
    bool previousValid = projectDirectionToPixel(
        direction, PreviousCameraForward, PreviousCameraRight,
        PreviousCameraUp, PreviousTanHalfFovY, PreviousAspect, dimensions,
        previousPixel);
    if (!currentValid || !previousValid) {
        return InvalidMotion.xx;
    }
    return clamp(previousPixel - currentPixel, -dimensions, dimensions);
}

/*
 * The two directions of temporal correspondence, mirroring
 * renderer_dxr/dxr_temporal_correspondence.h, which states the convention in
 * full and is covered by ab3d2_dxr_temporal_correspondence_test.
 *
 * Motion points backwards in time in unjittered pixels, so
 * `previousPixel = currentPixel + motion`. Scene motion stays jitter-free
 * because jitter moves where a pixel is sampled and not where the world is;
 * jitter enters only here, when a projected position has to be turned into a
 * texel of a grid that was itself sampled through a jitter.
 *
 * No other shader function may reason about the sign of a jitter term.
 */
float2 currentToPreviousPixel(uint2 pixel, float2 motion)
{
    return float2(pixel) + 0.5 + motion +
        float2(JitterX - PreviousJitterX, JitterY - PreviousJitterY);
}

float2 previousToCurrentPixel(float2 previousPixel, float2 motion)
{
    return previousPixel - motion -
        float2(JitterX - PreviousJitterX, JitterY - PreviousJitterY);
}

/* Store current weapon coverage beside a short per-pixel rejection lifetime.
 * On a pose change, the exact new and prior silhouettes are scrubbed without
 * discarding RR history from the surrounding scene. */
bool updateRayReconstructionWeaponHistory(uint2 pixel,
                                          bool currentViewWeapon)
{
    static const uint WeaponCoverageBit = 0x100u;
    static const uint RejectionLifetimeMask = 0xffu;
    static const uint RejectionFrameCount = 4u;
    uint currentSlot = SampleIndex & 1u;
    uint previousSlot = 1u - currentSlot;
    uint previousHistory = HistoryValid != 0u ?
        ViewWeaponHistories[previousSlot][pixel] : 0u;
    uint rejectionFrames = previousHistory & RejectionLifetimeMask;
    rejectionFrames = rejectionFrames > 0u ? rejectionFrames - 1u : 0u;

    if (RayReconstructionWeaponPoseTransition != 0u) {
        bool previousViewWeapon =
            (previousHistory & WeaponCoverageBit) != 0u;
        if (currentViewWeapon || previousViewWeapon) {
            rejectionFrames = RejectionFrameCount;
        }
    }

    ViewWeaponHistories[currentSlot][pixel] =
        (currentViewWeapon ? WeaponCoverageBit : 0u) | rejectionFrames;
    return rejectionFrames != 0u;
}

/* `motionVectorsInvalidValue` is only consumed by Streamline when it must add
 * camera motion itself. This renderer supplies dense camera motion, so passing
 * the private FP16 sentinel through to RR would instead look like an enormous
 * valid vector. Preserve that sentinel in SceneMotion for renderer-owned
 * history/debugging, but give Streamline a finite vector. Temporal rejection
 * belongs in RayReconstructionDisocclusion rather than being encoded as fake
 * motion. */
void writeRayReconstructionMotion(uint2 pixel, float2 motion,
                                  float2 dimensions)
{
    bool invalid = any(abs(motion) >= InvalidMotion);
    SceneMotion[pixel] = motion;
    StreamlineSceneMotion[pixel] = invalid ? dimensions : motion;
}

float3 giPrimaryWorldPosition(uint2 pixel, uint2 dimensions, float depth)
{
    float2 screen = (float2(pixel) + 0.5 + float2(JitterX, JitterY)) /
        float2(dimensions);
    float2 ndc = float2(screen.x * 2.0 - 1.0,
                        1.0 - screen.y * 2.0);
    float3 direction = normalize(CameraForward +
        CameraRight * (ndc.x * Aspect * TanHalfFovY) +
        CameraUp * (ndc.y * TanHalfFovY));
    float projected = max(dot(direction, CameraForward), 1.0e-6);
    return CameraPosition + direction * (depth / projected);
}

uint reservoirIndex(uint2 pixel, uint2 dimensions)
{
    return pixel.y * dimensions.x + pixel.x;
}

PathReservoir emptyReservoir()
{
    return (PathReservoir)0;
}

bool reservoirValid(PathReservoir reservoir)
{
    return reservoir.m > 0.0;
}

float reservoirLuminance(float3 value)
{
    return dot(value, float3(0.2126, 0.7152, 0.0722));
}

/*
 * Builds the reservoir for one freshly traced path. `samplePdf` is the density
 * the path tracer actually drew it with; callers that already divided the
 * radiance through by that density pass one.
 */
PathReservoir makeReservoir(float3 targetFunction, uint randomSeed,
                            uint randomIndex, uint rcVertexLength,
                            uint pathLength, float partialJacobian,
                            float rcWiPdf, float3 translatedWorldPosition,
                            float3 worldNormal, float3 radiance,
                            float samplePdf)
{
    PathReservoir reservoir = (PathReservoir)0;
    reservoir.translatedWorldPosition = translatedWorldPosition;
    reservoir.worldNormal = worldNormal;
    reservoir.radiance = radiance;
    reservoir.targetFunction = targetFunction;
    reservoir.weightSum = samplePdf > 0.0 ? 1.0 / samplePdf : 0.0;
    reservoir.m = 1.0;
    reservoir.partialJacobian = partialJacobian;
    reservoir.rcWiPdf = rcWiPdf;
    reservoir.rcVertexLength = rcVertexLength;
    reservoir.pathLength = pathLength;
    reservoir.randomSeed = randomSeed;
    reservoir.randomIndex = randomIndex;
    return reservoir;
}

/*
 * The general resampling step. The caller states the target function and the
 * normalization separately because a shifted path is evaluated in the receiving
 * pixel's domain while its normalization carries the source reservoir's weight,
 * candidate count and the shift Jacobian.
 *
 * A non-finite resampling weight is discarded rather than propagated: one NaN
 * in the running sum makes every later comparison false, and the pixel stays
 * dead for as long as the buffer lives.
 */
bool resampleReservoir(inout PathReservoir target, PathReservoir candidate,
                       float random, float3 candidateTargetFunction,
                       float sampleNormalization, float sampleM)
{
    float risWeight = reservoirLuminance(candidateTargetFunction) *
        sampleNormalization;
    if (isnan(risWeight) || isinf(risWeight) || risWeight < 0.0) {
        risWeight = 0.0;
    }
    if (isnan(sampleM) || isinf(sampleM) || sampleM < 0.0) {
        return false;
    }

    target.m += sampleM;
    target.weightSum += risWeight;

    bool select = random * target.weightSum < risWeight;
    if (select) {
        target.translatedWorldPosition = candidate.translatedWorldPosition;
        target.worldNormal = candidate.worldNormal;
        target.radiance = candidate.radiance;
        target.targetFunction = candidateTargetFunction;
        target.partialJacobian = candidate.partialJacobian;
        target.rcWiPdf = candidate.rcWiPdf;
        target.rcVertexLength = candidate.rcVertexLength;
        target.pathLength = candidate.pathLength;
        target.randomSeed = candidate.randomSeed;
        target.randomIndex = candidate.randomIndex;
        target.age = candidate.age;
        target.ancestry = candidate.ancestry;
    }
    return select;
}

/* Merges a whole reservoir in. Normalization of the result is deferred until
 * every candidate has been seen. */
bool combineReservoir(inout PathReservoir target, PathReservoir candidate,
                      float random, float3 candidateTargetFunction)
{
    return resampleReservoir(target, candidate, random, candidateTargetFunction,
                             candidate.weightSum * candidate.m, candidate.m);
}

void finalizeResampling(inout PathReservoir reservoir, float numerator,
                        float denominator)
{
    float weight = denominator > 0.0 ?
        numerator * reservoir.weightSum / denominator : 0.0;
    reservoir.weightSum = (isnan(weight) || isinf(weight)) ? 0.0 : weight;
}

/* The radiance this reservoir contributes once resampling has finished. */
/* Bounded so a single outlier cannot saturate the accumulator and hide the
 * ratio it is there to report. */
uint quantizeEnergy(float3 radiance)
{
    float value = reservoirLuminance(max(radiance, 0.0));
    if (isnan(value) || isinf(value)) {
        return 0u;
    }
    return uint(min(value, 64.0) * 16.0);
}

float3 resolvedRadiance(PathReservoir reservoir)
{
    if (!reservoirValid(reservoir) || isnan(reservoir.weightSum) ||
        isinf(reservoir.weightSum)) {
        return 0.0;
    }
    return reservoir.targetFunction * reservoir.weightSum;
}

/*
 * Q2RTX's clamp_output, from path_tracer_rgen.h. It bounds every lighting
 * result to MAX_OUTPUT_VALUE and turns a non-finite one into black, and it is
 * production behaviour there rather than a diagnostic: direct, specular and
 * each indirect bounce all pass through it.
 *
 * Without it one improbable path carries an unbounded radiance into the frame.
 * That is a firefly on its own, and under resampling it is worse, because the
 * reservoir will happily select it, hold it for the length of its history and
 * copy it into its neighbours. Bounding the sample as it is produced stops it
 * ever becoming a reservoir sample, which no amount of filtering afterwards
 * can undo as cleanly.
 */
float3 clampOutput(float3 value)
{
    if (any(isnan(value)) || any(isinf(value))) {
        return 0.0;
    }
    if (!(RadianceClamp > 0.0)) {
        return max(value, 0.0);
    }
    return clamp(value, 0.0, RadianceClamp);
}

float3 fresnelSchlick(float cosine, float3 reflectance)
{
    float factor = pow(1.0 - saturate(cosine), 5.0);
    return reflectance + (1.0 - reflectance) * factor;
}

float3 surfaceF0(SurfaceData surface)
{
    float dielectricF0 = 0.04 * surface.specularFactor;
    return lerp(dielectricF0.xxx, surface.baseColor, surface.metalness);
}

float3 diffuseReflectance(SurfaceData surface)
{
    return surface.baseColor * (1.0 - surface.metalness);
}

float ggxDistribution(float normalHalf, float alpha)
{
    float alphaSquared = alpha * alpha;
    float denominator = normalHalf * normalHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(Pi * denominator * denominator, 1.0e-8);
}

float smithG1(float normalDirection, float alpha)
{
    float alphaSquared = alpha * alpha;
    float cosineSquared = normalDirection * normalDirection;
    return (2.0 * normalDirection) /
        max(normalDirection +
            sqrt(alphaSquared + (1.0 - alphaSquared) * cosineSquared),
            1.0e-8);
}

float specularProbability(float3 diffuseReflectance, float3 f0)
{
    float diffuseWeight = luminance(diffuseReflectance);
    float specularWeight = luminance(f0);
    return clamp(specularWeight /
                 max(diffuseWeight + specularWeight, 1.0e-5), 0.05, 0.95);
}

BsdfEvaluation evaluateBsdf(SurfaceData surface, float3 viewDirection,
                            float3 lightDirection)
{
    BsdfEvaluation result;
    result.diffuse = 0.0;
    result.specular = 0.0;
    result.value = 0.0;
    result.pdf = 0.0;
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (normalView <= 0.0 || normalLight <= 0.0) {
        return result;
    }
    float3 halfVector = normalize(viewDirection + lightDirection);
    float normalHalf = saturate(dot(surface.shadingNormal, halfVector));
    float viewHalf = saturate(dot(viewDirection, halfVector));
    float alpha = surface.roughness * surface.roughness;
    float3 f0 = surfaceF0(surface);
    float3 fresnel = fresnelSchlick(viewHalf, f0);
    float distribution = ggxDistribution(normalHalf, alpha);
    float viewMasking = smithG1(normalView, alpha);
    float geometry = viewMasking * smithG1(normalLight, alpha);
    float3 specular = distribution * geometry * fresnel /
        max(4.0 * normalView * normalLight, 1.0e-7);
    float3 diffuseReflectance = surface.baseColor * (1.0 - surface.metalness);
    float3 diffuse = (1.0 - fresnel) * diffuseReflectance / Pi;
    float chooseSpecular = specularProbability(diffuseReflectance, f0);
    float diffusePdf = normalLight / Pi;
    float specularPdf = distribution * viewMasking /
        max(4.0 * normalView * NdfTrim, 1.0e-7);
    result.diffuse = diffuse;
    result.specular = specular;
    result.value = result.diffuse + result.specular;
    result.pdf = lerp(diffusePdf, specularPdf, chooseSpecular);
    return result;
}

float directSpecularWeight(float linearRoughness)
{
    return smoothstep(0.16, 0.20, linearRoughness);
}

float3 sampleGgxVisibleNormal(float3 viewDirection, float alpha,
                              float2 sampleValue)
{
    float3 stretchedView = normalize(
        float3(alpha * viewDirection.x, alpha * viewDirection.y,
               viewDirection.z));
    float lensSquared = dot(stretchedView.xy, stretchedView.xy);
    float3 firstTangent = lensSquared > 0.0 ?
        float3(-stretchedView.y, stretchedView.x, 0.0) / sqrt(lensSquared) :
        float3(1.0, 0.0, 0.0);
    float3 secondTangent = cross(stretchedView, firstTangent);
    float radius = sqrt(sampleValue.x * NdfTrim);
    float angle = 2.0 * Pi * sampleValue.y;
    float first = radius * cos(angle);
    float second = radius * sin(angle);
    float interpolation = 0.5 * (1.0 + stretchedView.z);
    second = lerp(sqrt(max(0.0, 1.0 - first * first)), second,
                  interpolation);
    float3 stretchedNormal = first * firstTangent + second * secondTangent +
        sqrt(max(0.0, 1.0 - first * first - second * second)) * stretchedView;
    return normalize(float3(alpha * stretchedNormal.x,
                            alpha * stretchedNormal.y,
                            max(0.0, stretchedNormal.z)));
}

bool sampleBsdf(SurfaceData surface, float3 viewDirection,
                float chooseSample, float2 directionSample,
                out float3 lightDirection, out BsdfEvaluation evaluation,
                out bool sampledSpecular)
{
    float3 diffuseReflectance = surface.baseColor * (1.0 - surface.metalness);
    float3 f0 = surfaceF0(surface);
    float chooseSpecular = specularProbability(diffuseReflectance, f0);
    sampledSpecular = chooseSample < chooseSpecular;
    if (sampledSpecular) {
        float3 tangent;
        float3 bitangent;
        coordinateSystem(surface.shadingNormal, tangent, bitangent);
        float3 localView = float3(dot(viewDirection, tangent),
                                  dot(viewDirection, bitangent),
                                  dot(viewDirection, surface.shadingNormal));
        float alpha = surface.roughness * surface.roughness;
        float3 localHalf = sampleGgxVisibleNormal(localView, alpha,
                                                   directionSample);
        float3 halfVector = normalize(tangent * localHalf.x +
            bitangent * localHalf.y + surface.shadingNormal * localHalf.z);
        lightDirection = reflect(-viewDirection, halfVector);
    } else {
        lightDirection = cosineHemisphere(surface.shadingNormal,
                                           directionSample);
    }
    evaluation = evaluateBsdf(surface, viewDirection, lightDirection);
    return evaluation.pdf > 0.0 &&
        dot(surface.geometricNormal, lightDirection) > 0.0;
}

bool traceVisibility(float3 origin, float3 direction, float maximumDistance,
                     uint instanceMask)
{
    if (maximumDistance <= RayEpsilon) {
        return false;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = RayEpsilon;
    ray.TMax = maximumDistance;
    ShadowPayload payload;
    payload.visible = 0u;
    TraceRay(Scene,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                 RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             instanceMask, 0, 0, 1, ray, payload);
    return payload.visible != 0u;
}

/*
 * Whether a reservoir's primary surface may supply history to this one.
 *
 * Mirrors surface_compatible() in dxr_temporal_correspondence.h. Identity is
 * checked before geometry on purpose: a matching depth and normal is not
 * evidence that two frames saw the same thing, and a reprojection can land on a
 * texel holding a different object entirely. Accepting that is how one
 * surface's lighting gets painted onto another and then held there by the very
 * confidence that reuse accumulates.
 */
bool reservoirSurfaceCompatible(PathReservoir reservoir, SurfaceData surface,
                                float surfaceDepth, float depthTolerance,
                                float normalTolerance, float separationLimit)
{
    if (!reservoirValid(reservoir)) {
        return false;
    }
    if (!(reservoir.primaryDepth > 0.0) || !(surfaceDepth > 0.0)) {
        return false;
    }
    if (reservoir.primaryMaterial != surface.materialIndex) {
        return false;
    }
    float difference = abs(reservoir.primaryDepth - surfaceDepth);
    if (difference > depthTolerance * max(reservoir.primaryDepth,
                                          surfaceDepth)) {
        return false;
    }
    float3 storedNormal = unpackOctahedralNormal(reservoir.primaryNormal);
    if (dot(storedNormal, surface.geometricNormal) < normalTolerance) {
        return false;
    }

    /*
     * Where the two surfaces actually are, which the checks above cannot
     * establish between them. A wall seen a moment ago and a different part of
     * the same wall revealed behind a moving object agree about material, about
     * normal, and to within a couple of percent about depth -- and reusing one
     * for the other is precisely what leaves a lit silhouette trailing after
     * whatever moved. Depth alone measures distance along the view direction
     * and says nothing about lateral displacement.
     */
    float3 separation = reservoir.primaryPosition - surface.position;

    /* Off-plane distance rejects a neighbour that lies on a different surface
     * parallel to this one, which is how light leaks through a thin wall. */
    float planar = abs(dot(separation, surface.geometricNormal));
    if (planar > depthTolerance * surfaceDepth) {
        return false;
    }

    /*
     * A temporal candidate additionally has to be the SAME point, because
     * reprojection claims it is. A spatial neighbour is deliberately a
     * different point on the same surface, so it passes a negative limit and
     * is held only to the plane test above.
     */
    if (separationLimit > 0.0 &&
        dot(separation, separation) > separationLimit * separationLimit) {
        return false;
    }
    return true;
}

/*
 * The reconnection shift: rebuild a stored path as seen from a different
 * primary surface by connecting that surface to the stored reconnection vertex.
 *
 * Reuse of a path is not reuse of its visibility. The connection this creates
 * did not exist in the source path, so it is traced rather than assumed; a
 * neighbouring pixel agreeing about depth and normal says nothing about whether
 * the same vertex can still be seen from here.
 *
 * Returns the shifted path's target function in the receiving domain, and the
 * Jacobian that converts the sample's density between the two domains.
 */
bool shiftReservoir(PathReservoir source, SurfaceData surface,
                    uint instanceMask, out float3 shiftedTarget,
                    out float jacobian)
{
    shiftedTarget = 0.0;
    jacobian = 0.0;
    if (!reservoirValid(source)) {
        InterlockedAdd(Diagnostics[DiagnosticShiftFail + 0u], 1u); return false;
    }

    bool environmentSample = source.rcVertexLength == 0u;
    float3 offsetOrigin = surface.position +
        surface.geometricNormal * RayEpsilon;
    float3 direction;
    float distance;
    if (environmentSample) {
        /* A distant sample carries a direction rather than a point, so the
         * shift is a parallel transport and its density does not change. */
        direction = normalize(source.worldNormal);
        distance = SceneFarPlane;
        jacobian = 1.0;
    } else {
        float3 offset = source.translatedWorldPosition - offsetOrigin;
        float lengthSquared = dot(offset, offset);
        if (!(lengthSquared > 1.0e-9)) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 1u], 1u); return false;
        }
        distance = sqrt(lengthSquared);
        direction = offset / distance;
        /* The reconnection vertex must still face the new receiver. */
        float emissionCosine = dot(source.worldNormal, -direction);
        if (!(emissionCosine > 1.0e-4)) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 2u], 1u); return false;
        }
        float3 sourceOffset = source.translatedWorldPosition -
            source.primaryPosition;
        float sourceLengthSquared = dot(sourceOffset, sourceOffset);
        if (!(sourceLengthSquared > 1.0e-9)) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 3u], 1u); return false;
        }
        float sourceCosine = dot(source.worldNormal,
                                 -normalize(sourceOffset));
        if (!(sourceCosine > 1.0e-4)) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 4u], 1u); return false;
        }
        /* Equation (11) of the ReSTIR GI paper: the ratio of solid angles the
         * reconnection subtends from the two receivers. */
        jacobian = (emissionCosine * sourceLengthSquared) /
            (sourceCosine * lengthSquared);
        if (isnan(jacobian) || isinf(jacobian) || jacobian <= 0.0) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 5u], 1u); return false;
        }
        /*
         * A reconnection that is nearly degenerate -- the receiver almost in
         * the reconnection vertex's plane, or almost on top of it -- has an
         * unbounded Jacobian. The guards above only keep it finite: with a
         * grazing cosine and a short segment it can still reach many orders of
         * magnitude, and it enters the resampling weight with nothing to
         * balance it, so that candidate wins with certainty and the pixel
         * resolves to an enormous radiance. Adding neighbours adds chances to
         * draw such a pair, which is why instability grew with neighbour count.
         *
         * Reject rather than clamp. A clamped weight is still a sample the
         * estimator cannot justify; refusing it lets the remaining candidates
         * carry the pixel, exactly as a failed shift already does.
         */
        if (jacobian > ReservoirMaximumJacobian ||
            jacobian < 1.0 / ReservoirMaximumJacobian) {
            InterlockedAdd(Diagnostics[DiagnosticShiftFail + 6u], 1u); return false;
        }
    }

    float receiverCosine = dot(surface.shadingNormal, direction);
    if (!(receiverCosine > 1.0e-4)) {
        InterlockedAdd(Diagnostics[DiagnosticShiftFail + 7u], 1u); return false;
    }
    if (dot(surface.geometricNormal, direction) <= 0.0) {
        InterlockedAdd(Diagnostics[DiagnosticShiftFail + 8u], 1u); return false;
    }
    if (!traceVisibility(offsetOrigin, direction,
                         environmentSample ? SceneFarPlane :
                             distance - RayEpsilon,
                         instanceMask)) {
        InterlockedAdd(Diagnostics[DiagnosticShiftFail + 9u], 1u); return false;
    }

    /* The cosines above are validity gates, not weights: the shifted path's
     * target is the stored radiance in the same units the canonical candidate
     * uses, so the two can be compared without either needing an inverse. */
    shiftedTarget = source.radiance * receiverCosine;
    if (any(isnan(shiftedTarget)) || any(isinf(shiftedTarget))) {
        InterlockedAdd(Diagnostics[DiagnosticShiftFail + 10u], 1u); return false;
    }
    return true;
}

/*
 * Walker alias selection from one uniform: the scaled uniform's integer part
 * picks the bucket and its fraction decides between the bucket's own index and
 * its alias.
 */
uint selectEmitter(float selection)
{
    float scaled = saturate(selection) * float(EmitterCount);
    uint bucket = min(uint(scaled), EmitterCount - 1u);
    float fractional = scaled - float(bucket);
    return fractional < Emitters[bucket].aliasThreshold ? bucket :
        Emitters[bucket].aliasIndex;
}

/* A direct-light sample's full identity. Local emitters store a triangle index
 * and packed canonical area-sampling randoms. The analytic environment stores
 * EnvironmentLightIndex and a packed world-space direction. Both forms are
 * independent of the receiving pixel and therefore survive temporal/spatial
 * reuse without reconstructing the generating ray. */
struct EmitterSample
{
    uint emitterIndex;
    uint positionSample;
    bool valid;
};

struct EmitterEvaluation
{
    /* The unshadowed physical integrand in solid-angle measure. Local,
     * environment, and BRDF strategies are combined in their balance-heuristic
     * source density before this luminance target enters reservoir streaming. */
    float3 contribution;
    float3 diffuseContribution;
    float3 specularContribution;
    float targetPdf;
    float sourcePdf;
    float brdfPdf;
    float3 lightDirection;
    float lightDistance;
    bool valid;
};

EmitterEvaluation evaluateEmitterSampleForFrame(SurfaceData surface,
                                                float3 viewDirection,
                                                EmitterSample lightSample,
                                                bool previousFrame)
{
    EmitterEvaluation evaluation;
    evaluation.contribution = 0.0;
    evaluation.diffuseContribution = 0.0;
    evaluation.specularContribution = 0.0;
    evaluation.targetPdf = 0.0;
    evaluation.sourcePdf = 0.0;
    evaluation.brdfPdf = 0.0;
    evaluation.lightDirection = 0.0;
    evaluation.lightDistance = 0.0;
    evaluation.valid = false;
    if (!lightSample.valid || lightSample.emitterIndex >= EmitterCount) {
        return evaluation;
    }
    EmissiveTriangle emitter = Emitters[lightSample.emitterIndex];
    SceneVertex first;
    SceneVertex second;
    SceneVertex third;
    if (previousFrame) {
        first = PreviousVertices[emitter.firstVertex + 0u];
        second = PreviousVertices[emitter.firstVertex + 1u];
        third = PreviousVertices[emitter.firstVertex + 2u];
    } else {
        first = Vertices[emitter.firstVertex + 0u];
        second = Vertices[emitter.firstVertex + 1u];
        third = Vertices[emitter.firstVertex + 2u];
    }
    float2 positionSample = unpackPositionSample(lightSample.positionSample);
    float root = sqrt(positionSample.x);
    float secondRandom = positionSample.y;
    float3 barycentrics = float3(1.0 - root,
                                root * (1.0 - secondRandom),
                                root * secondRandom);
    float3 lightPosition = first.position * barycentrics.x +
        second.position * barycentrics.y + third.position * barycentrics.z;
    float3 toLight = lightPosition - surface.position;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= RayEpsilon * RayEpsilon) {
        return evaluation;
    }
    float distance = sqrt(distanceSquared);
    float3 lightDirection = toLight / distance;
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (normalLight <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return evaluation;
    }
    float3 lightNormal = normalize(cross(second.position - first.position,
                                         third.position - first.position));
    /*
     * A back face emits nothing. The winding gives each emitter a front, and
     * an emissive surface only radiates from it: a glowing floor panel lights
     * the room above it, not the inside of the platform it caps. Taking the
     * absolute value here instead made every emitter two-sided, so any gap in
     * the geometry behind one leaked its light into space it should never
     * have reached.
     */
    float lightCosine = dot(lightNormal, -lightDirection);
    if (lightCosine <= 1.0e-6) {
        return evaluation;
    }
    float lightPdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    if (!(lightPdf > 0.0)) {
        return evaluation;
    }
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    /* The triangle's measured mean, not the texel at this point: a sparse map
     * read per texel returns mostly black and occasionally a full-strength
     * strip. See DxrEmissiveTriangle::radiance in dxr_scene.h. */
    float3 emittedRadiance = emitter.radiance * lightEmissiveScale;
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    evaluation.diffuseContribution =
        bsdf.diffuse * emittedRadiance * normalLight;
    evaluation.specularContribution =
        bsdf.specular * emittedRadiance * normalLight *
        directSpecularWeight(surface.roughness);
    evaluation.contribution = evaluation.diffuseContribution +
        evaluation.specularContribution;
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = lightPdf;
    evaluation.brdfPdf = bsdf.pdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = distance;
    evaluation.valid = true;
    return evaluation;
}

/* Candidate streaming needs geometry, the receiving BSDF, and a proposal
 * target. The global alias weight is area times the triangle's measured
 * emitted luminance, so multiplying its categorical probability by inverse
 * area yields that luminance up to one common normalization -- which is what
 * light sampling now emits, so the proxy and the evaluated contribution agree
 * in everything but the receiving geometry. */
EmitterEvaluation evaluateEmitterProxy(SurfaceData surface,
                                       float3 viewDirection,
                                       EmitterSample lightSample)
{
    EmitterEvaluation evaluation = (EmitterEvaluation)0;
    evaluation.valid = false;
    if (!lightSample.valid || lightSample.emitterIndex >= EmitterCount) {
        return evaluation;
    }
    EmissiveTriangle emitter = Emitters[lightSample.emitterIndex];
    SceneVertex first = Vertices[emitter.firstVertex + 0u];
    SceneVertex second = Vertices[emitter.firstVertex + 1u];
    SceneVertex third = Vertices[emitter.firstVertex + 2u];
    float2 positionSample = unpackPositionSample(lightSample.positionSample);
    float root = sqrt(positionSample.x);
    float3 barycentrics = float3(
        1.0 - root,
        root * (1.0 - positionSample.y),
        root * positionSample.y);
    float3 lightPosition = first.position * barycentrics.x +
        second.position * barycentrics.y + third.position * barycentrics.z;
    float3 toLight = lightPosition - surface.position;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= RayEpsilon * RayEpsilon) {
        return evaluation;
    }
    float distance = sqrt(distanceSquared);
    float3 lightDirection = toLight / distance;
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (normalLight <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return evaluation;
    }
    float3 lightNormal = normalize(cross(second.position - first.position,
                                         third.position - first.position));
    /*
     * A back face emits nothing. The winding gives each emitter a front, and
     * an emissive surface only radiates from it: a glowing floor panel lights
     * the room above it, not the inside of the platform it caps. Taking the
     * absolute value here instead made every emitter two-sided, so any gap in
     * the geometry behind one leaked its light into space it should never
     * have reached.
     */
    float lightCosine = dot(lightNormal, -lightDirection);
    if (lightCosine <= 1.0e-6) {
        return evaluation;
    }
    float sourcePdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    float emissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    float emittedProxy = emitter.selectionProbability *
        emitter.inverseArea * max(emissiveScale, 0.0);
    if (!(sourcePdf > 0.0) || !(emittedProxy > 0.0)) {
        return evaluation;
    }
    BsdfEvaluation bsdf = evaluateBsdf(
        surface, viewDirection, lightDirection);
    evaluation.diffuseContribution =
        bsdf.diffuse * emittedProxy * normalLight;
    evaluation.specularContribution =
        bsdf.specular * emittedProxy * normalLight *
        directSpecularWeight(surface.roughness);
    evaluation.contribution = evaluation.diffuseContribution +
        evaluation.specularContribution;
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = sourcePdf;
    evaluation.brdfPdf = bsdf.pdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = distance;
    evaluation.valid = evaluation.targetPdf > 0.0;
    return evaluation;
}

/* Plain authored-polygon NEE for one diffuse receiver. The primary receiver
 * supplies the directly lit baseline; evaluating the same estimator at every
 * reached continuation supplies the indirect-polygon-light path suffix.
 * This deliberately does not call the full PBR evaluator, sample the analytic
 * environment, read the light grid, or publish/reuse a reservoir. */
EmitterEvaluation evaluateDiffusePolygonSample(SurfaceData surface,
                                                EmitterSample lightSample)
{
    EmitterEvaluation evaluation;
    evaluation.contribution = 0.0;
    evaluation.diffuseContribution = 0.0;
    evaluation.specularContribution = 0.0;
    evaluation.targetPdf = 0.0;
    evaluation.sourcePdf = 0.0;
    evaluation.brdfPdf = 0.0;
    evaluation.lightDirection = 0.0;
    evaluation.lightDistance = 0.0;
    evaluation.valid = false;
    if (!lightSample.valid || lightSample.emitterIndex >= EmitterCount) {
        return evaluation;
    }
    EmissiveTriangle emitter = Emitters[lightSample.emitterIndex];
    SceneVertex first = Vertices[emitter.firstVertex + 0u];
    SceneVertex second = Vertices[emitter.firstVertex + 1u];
    SceneVertex third = Vertices[emitter.firstVertex + 2u];
    float2 positionSample = unpackPositionSample(lightSample.positionSample);
    float root = sqrt(positionSample.x);
    float3 barycentrics = float3(
        1.0 - root,
        root * (1.0 - positionSample.y),
        root * positionSample.y);
    float3 lightPosition = first.position * barycentrics.x +
        second.position * barycentrics.y + third.position * barycentrics.z;
    float3 toLight = lightPosition - surface.position;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= RayEpsilon * RayEpsilon) {
        return evaluation;
    }
    float lightDistance = sqrt(distanceSquared);
    float3 lightDirection = toLight / lightDistance;
    float receiverCosine = saturate(dot(surface.shadingNormal,
                                        lightDirection));
    if (receiverCosine <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return evaluation;
    }
    float3 lightNormal = normalize(cross(second.position - first.position,
                                         third.position - first.position));
    /*
     * A back face emits nothing. The winding gives each emitter a front, and
     * an emissive surface only radiates from it: a glowing floor panel lights
     * the room above it, not the inside of the platform it caps. Taking the
     * absolute value here instead made every emitter two-sided, so any gap in
     * the geometry behind one leaked its light into space it should never
     * have reached.
     */
    float lightCosine = dot(lightNormal, -lightDirection);
    if (lightCosine <= 1.0e-6) {
        return evaluation;
    }
    float sourcePdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    if (!(sourcePdf > 0.0) || isnan(sourcePdf) || isinf(sourcePdf)) {
        return evaluation;
    }
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    /* The triangle's measured mean; see evaluateEmitterSampleForFrame. */
    float3 emittedRadiance = emitter.radiance * lightEmissiveScale;
    if (!any(emittedRadiance > 0.0)) {
        return evaluation;
    }
    /*
     * Bound the estimate by the brightest emitter in the scene.
     *
     * The solid-angle density carries distanceSquared, so the estimator goes as
     * area times cosine over distance squared and is unbounded as the shading
     * point approaches an emitter -- the only guard being that the two are not
     * literally coincident. Standing near or looking at a large bright panel
     * therefore produces single samples far above anything in the scene:
     * measured, indirect bounces reached 4096 where no emitter exceeds 200.
     *
     * A diffuse surface cannot leave more radiance than arrives at it, so
     * clamping to the brightest emitter removes those and nothing else. This is
     * the singularity itself, not the resampling on top of it, which is why the
     * noise appears with ReSTIR disabled as well.
     */
    evaluation.contribution = (diffuseReflectance(surface) / Pi) *
        emittedRadiance * receiverCosine;
    if (MaximumEmitterRadiance > 0.0) {
        evaluation.contribution =
            min(evaluation.contribution, MaximumEmitterRadiance);
    }
    evaluation.diffuseContribution = evaluation.contribution;
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = sourcePdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = lightDistance;
    evaluation.valid = evaluation.targetPdf > 0.0;
    return evaluation;
}

/*
 * The solid angle this emitter covers as seen from a point, times its own
 * luminance: what Q2RTX's light_lists.h:spherical_tri_area returns, and the
 * mass its light CDF is built from.
 *
 * A candidate's worth to a surface is how much of that surface's hemisphere
 * the light fills, not how much power the light has. The two differ by
 * distance squared and by orientation, which is exactly the difference between
 * the dim fixture in this corridor and the bright one across the level.
 *
 * Zero for a light behind the surface, and zero for one whose own facing is
 * away -- emitters are one-sided here, so a back face contributes nothing.
 */
float emitterSolidAngleMass(uint emitterIndex, float3 position, float3 normal)
{
    EmissiveTriangle emitter = Emitters[emitterIndex];
    float3 a = Vertices[emitter.firstVertex + 0u].position - position;
    float3 b = Vertices[emitter.firstVertex + 1u].position - position;
    float3 c = Vertices[emitter.firstVertex + 2u].position - position;
    if (dot(normal, a) <= 0.0 && dot(normal, b) <= 0.0 &&
        dot(normal, c) <= 0.0) {
        return 0.0;
    }
    float3 facing = cross(b - a, c - a);
    if (dot(facing, a) >= 0.0 && dot(facing, b) >= 0.0 &&
        dot(facing, c) >= 0.0) {
        return 0.0;
    }
    float3 unitA = normalize(a);
    float3 unitB = normalize(b);
    float3 unitC = normalize(c);
    /* Van Oosterom and Strackee: the area of the spherical triangle. */
    float area = 2.0 * atan2(
        abs(dot(unitA, cross(unitB, unitC))),
        1.0 + dot(unitA, unitB) + dot(unitB, unitC) + dot(unitA, unitC));
    return max(area - 1.0e-5, 0.0) * luminance(emitter.radiance);
}

/*
 * Draw a candidate from this surface's own zone rather than from the level.
 *
 * Two things had to change together, and only the pair is worth anything.
 *
 * The list: the scene-wide alias table proposes emitters by power, so in a
 * level whose lights sit in 31 of its 134 zones nearly every candidate a
 * corridor draws is a light in another room. Measured on Level A, 94% of
 * candidate draws were emitters the receiving zone cannot see. They were never
 * wrong -- the shadow ray rejected them -- but they were the whole candidate
 * budget.
 *
 * The draw: a static table weighted by power, sampled with replacement, keeps
 * proposing the brightest light in the zone however far away it is, and can
 * miss the dim one beside the surface entirely. Q2RTX instead walks a strided
 * partition of up to eight DISTINCT lights and builds its CDF from each one's
 * solid angle at this exact point, which is the measure that actually decides
 * how much light arrives. This follows light_lists.h:sample_light_list,
 * MAX_BRUTEFORCE_SAMPLING and all.
 *
 * Striding rather than taking the first eight is what keeps every light in a
 * long list reachable: the partition is chosen at random and the pdf carries
 * the partition count, so nothing is lost and the estimator stays unbiased.
 *
 * Returns false when the zone is unknown, has no list, or nothing in the
 * chosen partition faces the surface; the caller falls back to the scene-wide
 * distribution.
 */
static const uint ZoneBruteForceLights = 8u;

bool selectEmitterForZone(float selection, SurfaceData surface,
                          out LightSelection result)
{
    result.emitterIndex = InvalidIndex;
    result.inverseProbability = 0.0;
    if (ZoneLightsEnabled == 0u || surface.sourceZonePlusOne == 0u) {
        return false;
    }
    uint zoneCount = ZoneLightRanges[0].x;
    uint zone = surface.sourceZonePlusOne - 1u;
    if (zone >= zoneCount) {
        return false;
    }
    uint2 range = ZoneLightRanges[1u + zone];
    if (range.y == 0u) {
        return false;
    }
    /* One partition of the list, chosen uniformly, walked with that stride. */
    float partitions = ceil(float(range.y) / float(ZoneBruteForceLights));
    float partitioned = saturate(selection) * partitions;
    float chosen = min(floor(partitioned), partitions - 1.0);
    float random = partitioned - chosen;
    uint stride = uint(partitions);
    uint start = range.x + uint(chosen);
    uint end = range.x + range.y;

    float masses[8];
    float mass = 0.0;
    uint slot = 0u;
    uint index = start;
    [unroll]
    for (slot = 0u; slot < ZoneBruteForceLights; ++slot) {
        masses[slot] = 0.0;
        if (index >= end) {
            continue;
        }
        uint emitterIndex = ZoneLights[index];
        if (emitterIndex < EmitterCount) {
            masses[slot] = emitterSolidAngleMass(
                emitterIndex, surface.position, surface.shadingNormal);
            mass += masses[slot];
        }
        index += stride;
    }
    if (!(mass > 0.0)) {
        return false;
    }
    /* Pick from the CDF those masses form. */
    float target = random * mass;
    uint selectedSlot = 0u;
    float selectedMass = 0.0;
    [unroll]
    for (slot = 0u; slot < ZoneBruteForceLights; ++slot) {
        if (masses[slot] <= 0.0) {
            continue;
        }
        selectedSlot = slot;
        selectedMass = masses[slot];
        target -= masses[slot];
        if (target <= 0.0) {
            break;
        }
    }
    uint selectedIndex = start + selectedSlot * stride;
    if (selectedIndex >= end || !(selectedMass > 0.0)) {
        return false;
    }
    result.emitterIndex = ZoneLights[selectedIndex];
    /* The partition was one of `partitions`, so its probability divides in. */
    result.inverseProbability = (mass * partitions) / selectedMass;
    return true;
}

float3 sampleDiffusePolygonLightSurvivor(
    uint2 pixel, uint sampleIndex, uint stream, int lightGridCell,
    uint candidateStart, uint candidateStride, uint candidateCount,
    SurfaceData surface)
{
    /* Fresh RIS rejects black texels and poor geometric connections before the
     * one survivor spends a visibility ray. There is no temporal/spatial reuse
     * here: CandidateCount changes current-frame proposal quality only. */
    EmitterSample selected = (EmitterSample)0;
    selected.emitterIndex = InvalidIndex;
    selected.valid = false;
    float weightSum = 0.0;
    uint groupCandidateCount = 0u;
    for (uint candidate = candidateStart; candidate < candidateCount;
         candidate += candidateStride) {
        ++groupCandidateCount;
        float4 random = sampleStream(
            pixel, sampleIndex, stream + candidate);
        LightSelection lightSelection;
        if (!selectEmitterForZone(random.x, surface, lightSelection)) {
            lightSelection = selectEmitterForCell(random.x, lightGridCell);
        }
        EmitterSample lightSample;
        lightSample.emitterIndex = lightSelection.emitterIndex;
        lightSample.positionSample = packPositionSample(random.yz);
        lightSample.valid = true;
        EmitterEvaluation evaluation = evaluateDiffusePolygonSample(
            surface, lightSample);
        float globalProbability = Emitters[
            lightSample.emitterIndex].selectionProbability;
        float conditionalAreaPdf = evaluation.valid &&
                globalProbability > 0.0 ?
            evaluation.sourcePdf / globalProbability : 0.0;
        float proposalPdf = conditionalAreaPdf > 0.0 &&
                lightSelection.inverseProbability > 0.0 ?
            conditionalAreaPdf / lightSelection.inverseProbability : 0.0;
        float weight = proposalPdf > 0.0 ?
            evaluation.targetPdf / proposalPdf : 0.0;
        weightSum += weight;
        if (weight > 0.0 && random.w * weightSum < weight) {
            selected = lightSample;
        }
    }
    if (!selected.valid || !(weightSum > 0.0)) {
        return 0.0;
    }
    EmitterEvaluation selectedEvaluation = evaluateDiffusePolygonSample(
        surface, selected);
    if (!selectedEvaluation.valid ||
        !traceVisibility(surface.position +
                             surface.geometricNormal * RayEpsilon,
                         selectedEvaluation.lightDirection,
                         selectedEvaluation.lightDistance - RayEpsilon,
                         SceneInstanceMask)) {
        return 0.0;
    }
    float inversePdf = weightSum /
        (float(groupCandidateCount) * selectedEvaluation.targetPdf);
    return selectedEvaluation.contribution * inversePdf;
}

float3 sampleDiffusePolygonLight(uint2 pixel, uint sampleIndex,
                                 uint stream, bool localProposal,
                                 SurfaceData surface,
                                 uint requestedLightSampleCount)
{
    if (EmitterCount == 0u) {
        return 0.0;
    }
    uint candidateCount = max(CandidateCount, 1u);
    uint lightSampleCount = min(candidateCount, clamp(
        requestedLightSampleCount, 1u, IndirectPolygonLightSampleLimit));
    int lightGridCell = localProposal ? lightGridCellForSurface(
        pixel, sampleIndex, surface.position) : -1;
    float3 sum = 0.0;
    /* Partition the existing candidate budget into disjoint RIS groups. Each
     * group performs its own selection and fresh visibility test without
     * doubling material/geometry candidate evaluation. A fixed-count average
     * is required: an occluded or empty survivor is zero, not a reason to
     * renormalize the remaining samples. Across frames the sequence advances
     * with SampleIndex, while diffuse continuation and roulette decisions
     * retain their screen-space blue-noise dimensions. */
    [loop]
    for (uint lightSample = 0u; lightSample < lightSampleCount;
         ++lightSample) {
        sum += sampleDiffusePolygonLightSurvivor(
            pixel, sampleIndex, stream, lightGridCell, lightSample,
            lightSampleCount, candidateCount, surface);
    }
    return sum / float(lightSampleCount);
}

/* Full material-dependent local-light NEE for the primary receiver. Diffuse
 * and GGX specular stream through RIS as one target and share the selected
 * sample, visibility result, and unbiased normalization. The diffuse-only
 * continuation estimator above remains deliberately unchanged. */
DirectLightingSample samplePrimaryPolygonLight(
    uint2 pixel, uint sampleIndex, SurfaceData surface,
    float3 viewDirection)
{
    DirectLightingSample result = (DirectLightingSample)0;
    if (EmitterCount == 0u) {
        return result;
    }
    uint candidateCount = CompactLocalPrimary != 0u ?
        min(max(CandidateCount, 1u), CompactPrimaryCandidateLimit) :
        max(CandidateCount, 1u);
    uint visibilitySampleLimit = SinglePrimaryDirectSurvivor != 0u ?
        1u : PrimaryDirectVisibilitySampleLimit;
    uint visibilitySampleCount = min(candidateCount, visibilitySampleLimit);
    int lightGridCell = CompactLocalPrimary != 0u ?
        lightGridCellForSurface(pixel, sampleIndex, surface.position) : -1;
    uint localCandidateCount = candidateCount >
            CompactPrimaryGlobalCandidates ?
        candidateCount - CompactPrimaryGlobalCandidates : 0u;
    for (uint visibilitySample = 0u;
         visibilitySample < visibilitySampleCount; ++visibilitySample) {
        EmitterSample selected = (EmitterSample)0;
        selected.emitterIndex = InvalidIndex;
        selected.valid = false;
        float selectedStreamingTarget = 0.0;
        float weightSum = 0.0;
        uint groupCandidateCount = 0u;
        for (uint candidate = visibilitySample; candidate < candidateCount;
             candidate += visibilitySampleCount) {
            uint dimension = PrimaryDirectBlueNoiseDimension + candidate * 4u;
            float4 random = candidate <
                    PrimaryDirectBlueNoiseCandidateLimit ?
                float4(
                    sampleBlueNoise(pixel, sampleIndex, dimension + 0u),
                    sampleBlueNoise(pixel, sampleIndex, dimension + 1u),
                    sampleBlueNoise(pixel, sampleIndex, dimension + 2u),
                    sampleBlueNoise(pixel, sampleIndex, dimension + 3u)) :
                sampleStream(
                    pixel, sampleIndex,
                    DiffusePrimaryPolygonStream + candidate);
            /*
             * Primary hits keep the scene-wide distribution. A zone table
             * costs the firing frame most of its temporal stability: the
             * muzzle flash travels with the player, so the zone it belongs to
             * changes as they move and a lit surface gains and loses it
             * between frames. A directly visible surface is also the case the
             * light grid already handles; the wasted candidates are the ones
             * drawn at bounce vertices.
             */
            LightSelection lightSelection;
            if (CompactLocalPrimary != 0u &&
                candidate >= CompactPrimaryGlobalCandidates) {
                uint localOrdinal =
                    candidate - CompactPrimaryGlobalCandidates;
                float localSelection =
                    (random.x + float(localOrdinal)) /
                    float(localCandidateCount);
                lightSelection = selectEmitterForCell(
                    localSelection, lightGridCell);
            } else {
                lightSelection.emitterIndex = selectEmitter(random.x);
                lightSelection.inverseProbability = 1.0 / Emitters[
                    lightSelection.emitterIndex].selectionProbability;
            }
            EmitterSample lightSample;
            lightSample.emitterIndex = lightSelection.emitterIndex;
            lightSample.positionSample = packPositionSample(random.yz);
            lightSample.valid = true;
            EmitterEvaluation evaluation;
            if (ProxyPrimaryCandidates != 0u) {
                evaluation = evaluateEmitterProxy(
                    surface, viewDirection, lightSample);
            } else {
                evaluation = evaluateEmitterSampleForFrame(
                    surface, viewDirection, lightSample, false);
            }
            float globalProbability = Emitters[
                lightSample.emitterIndex].selectionProbability;
            float conditionalAreaPdf = evaluation.valid &&
                    globalProbability > 0.0 ?
                evaluation.sourcePdf / globalProbability : 0.0;
            float proposalPdf = conditionalAreaPdf > 0.0 &&
                    lightSelection.inverseProbability > 0.0 ?
                conditionalAreaPdf /
                    lightSelection.inverseProbability : 0.0;
            float weight = proposalPdf > 0.0 ?
                evaluation.targetPdf / proposalPdf : 0.0;
            weightSum += weight;
            groupCandidateCount += 1u;
            if (weight > 0.0 && random.w * weightSum < weight) {
                selected = lightSample;
                selectedStreamingTarget = evaluation.targetPdf;
            }
        }
        if (!selected.valid || !(weightSum > 0.0)) {
            continue;
        }
        EmitterEvaluation selectedEvaluation = evaluateEmitterSampleForFrame(
            surface, viewDirection, selected, false);
        if (!selectedEvaluation.valid ||
            !any(selectedEvaluation.contribution > 0.0) ||
            !traceVisibility(
                surface.position + surface.geometricNormal * RayEpsilon,
                selectedEvaluation.lightDirection,
                selectedEvaluation.lightDistance - RayEpsilon,
                SceneInstanceMask)) {
            continue;
        }
        float normalizationTarget = ProxyPrimaryCandidates != 0u ?
            selectedStreamingTarget : selectedEvaluation.targetPdf;
        float inversePdf = normalizationTarget > 0.0 ? weightSum /
            (float(groupCandidateCount) * normalizationTarget) : 0.0;
        result.diffuse += selectedEvaluation.diffuseContribution * inversePdf;
        result.specular += selectedEvaluation.specularContribution * inversePdf;
    }
    float inverseVisibilitySampleCount = 1.0 / float(visibilitySampleCount);
    result.diffuse *= inverseVisibilitySampleCount;
    result.specular *= inverseVisibilitySampleCount;
    return result;
}

struct DiffusePathSample
{
    /* Outgoing diffuse radiance at the first indirect surface, including all
     * later configured surfaces. Primary albedo is deliberately absent. */
    float3 radiance;
    float3 firstDirection;
    float firstDistance;
    SurfacePayload firstPayload;
    SurfaceData firstSurface;
    bool firstHit;
};

/* Trace the diffuse suffix behind the primary receiver.
 *
 * Every continuation uses the standard cosine-weighted Lambertian estimator.
 * Each additional surface contributes its polygon-light NEE after every
 * preceding diffuse albedo has been applied.
 * MaximumDepth counts the primary surface, so depth three executes two real
 * continuation rays and shades both reached surfaces. */
DiffusePathSample sampleDiffusePath(uint2 pixel, uint sampleIndex,
                                    SurfaceData primarySurface)
{
    DiffusePathSample result = (DiffusePathSample)0;
    result.firstDirection = primarySurface.geometricNormal;
    result.firstDistance = SceneFarPlane;
    result.firstPayload.primitiveIndex = InvalidIndex;
    float3 suffixThroughput = 1.0;
    SurfaceData departureSurface = primarySurface;
    uint pathDepth = min(MaximumDepth, MaximumDiffusePathDepth);
    uint indirectPathCount = max(IndirectSamplesPerPixel, 1u);
    uint pathOrdinal = sampleIndex % indirectPathCount;
    /* Spend the optional second visibility result coherently every third frame
     * and rotate the chosen path stratum on each active phase. All base strata
     * still trace every frame. Coherent dispatch avoids the DXR lane divergence
     * measured with a per-pixel mask, while the underlying blue-noise direction
     * and hash-stream candidate indices continue advancing normally. */
    uint diffuseLightSampleCount = IndirectLightSamples > 1u &&
            SampleIndex % 3u == 0u &&
            pathOrdinal == (SampleIndex / 3u) % indirectPathCount ?
        2u : 1u;

    [loop]
    for (uint continuationIndex = 0u;
         continuationIndex + 1u < pathDepth;
         ++continuationIndex) {
        uint dimension = PathDimensionsPerBounce * continuationIndex;
        float2 directionSample = float2(
            sampleBlueNoise(pixel, sampleIndex, dimension + 6u),
            sampleBlueNoise(pixel, sampleIndex, dimension + 7u));
        directionSample = stratifiedDiffuseDirectionSample(
            pixel, sampleIndex, continuationIndex, directionSample);
        float3 bounceDirection = cosineHemisphere(
            departureSurface.geometricNormal, directionSample);
        if (dot(departureSurface.geometricNormal, bounceDirection) <= 0.0) {
            break;
        }
        if (continuationIndex == 0u) {
            result.firstDirection = bounceDirection;
        }

        RayDesc bounceRay;
        bounceRay.Origin = departureSurface.position +
            departureSurface.geometricNormal * RayEpsilon;
        bounceRay.Direction = bounceDirection;
        bounceRay.TMin = RayEpsilon;
        bounceRay.TMax = SceneFarPlane;
        SegmentTraversal bounceSegment = traceSegment(bounceRay);
        result.radiance +=
            suffixThroughput * bounceSegment.additiveRadiance;

        if (continuationIndex == 0u) {
            result.firstDistance = bounceSegment.payload.hit != 0u ?
                bounceSegment.distance : SceneFarPlane;
            result.firstPayload = bounceSegment.payload;
        }
        if (bounceSegment.payload.hit == 0u) {
            break;
        }

        SurfaceData reachedSurface = loadSurface(
            bounceSegment.payload, bounceRay.Direction);
        /* Room-scale diffuse transport follows geometry rather than normal-map
         * detail at every indirect vertex. */
        reachedSurface.shadingNormal = reachedSurface.geometricNormal;
        if (continuationIndex == 0u) {
            result.firstSurface = reachedSurface;
            result.firstHit = true;
        }

        uint lightStream = DiffuseIndirectPolygonStream +
            continuationIndex * DiffusePolygonBounceStreamStride;
        float3 directAtSurface = sampleDiffusePolygonLight(
            pixel, sampleIndex, lightStream, true, reachedSurface,
            diffuseLightSampleCount);
        /*
         * The level's own lighting at the surface this bounce reached. It is
         * the same quantity as the polygon sample above -- light arriving here
         * -- so it reflects the same way, but it is exact rather than
         * estimated. Dark parts of the level are lit almost entirely through
         * this path, and sampling alone left them at the noise floor.
         */
        directAtSurface += diffuseReflectance(reachedSurface) *
            reachedSurface.sourceIrradiance;
        result.radiance += suffixThroughput * directAtSurface;

        if (continuationIndex + 2u >= pathDepth) {
            break;
        }
        suffixThroughput *= diffuseReflectance(reachedSurface);
        if (luminance(suffixThroughput) <= 1.0e-6 ||
            any(isnan(suffixThroughput)) ||
            any(isinf(suffixThroughput))) {
            break;
        }
        /* Q2RTX ships one continuation by default and makes deeper transport
         * optional. Retain this renderer's accepted deep suffix without paying
         * for it on every path: a blue-noise Russian-roulette decision after
         * the fully evaluated first surface preserves the estimator with 1/p
         * weighting. One decision owns the complete remaining suffix, so
         * explicitly requested depths above three do not compound variance. */
        if (InterleavedDeepDiffuse != 0u && continuationIndex == 0u) {
            uint rouletteDimension = PathDimensionsPerBounce + 5u;
            float continuationProbability = clamp(
                max(suffixThroughput.x,
                    max(suffixThroughput.y, suffixThroughput.z)),
                DeepDiffuseMinimumContinuationProbability, 1.0);
            float continuationSample = sampleBlueNoise(
                pixel, sampleIndex, rouletteDimension);
            if (continuationSample >= continuationProbability) {
                break;
            }
            suffixThroughput *= rcp(continuationProbability);
        }
        departureSurface = reachedSurface;
    }
    return result;
}

/*
 * How much of this surface's specular is reconstructed from the filtered
 * diffuse signal rather than traced. Reconstruction carries no directional
 * detail - it comes from an L0/L1 spherical harmonic - so a surface fully
 * reconstructed cannot mirror the scene, however sharp its roughness says it
 * should be. TracedSpecularRoughnessLimit is where that takeover completes;
 * rtx_specular_roughness raises it to buy real reflections on rough surfaces
 * for a continuation ray each. The blend keeps its original 2:3 shape.
 */
float fakeSpecularWeight(float linearRoughness)
{
    float limit = max(TracedSpecularRoughnessLimit, 0.3);
    return smoothstep(limit * (2.0 / 3.0), limit, linearRoughness);
}

float continuationSpecularProbability(SurfaceData surface,
                                      float3 viewDirection)
{
    float realSpecularWeight = 1.0 - fakeSpecularWeight(surface.roughness);
    if (!(realSpecularWeight > 0.0)) {
        return 0.0;
    }
    float diffuseWeight = luminance(diffuseReflectance(surface));
    if (!(diffuseWeight > 1.0e-6)) {
        return 1.0;
    }
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    float specularWeight = luminance(
        fresnelSchlick(normalView, surfaceF0(surface))) * realSpecularWeight;
    return clamp(specularWeight /
                 max(diffuseWeight + specularWeight, 1.0e-6), 0.05, 0.95);
}

bool sampleIndependentGgxSpecular(
    SurfaceData surface, float3 viewDirection, float2 directionSample,
    out float3 lightDirection, out float3 throughput)
{
    lightDirection = 0.0;
    throughput = 0.0;
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    if (!(normalView > 0.0)) {
        return false;
    }
    float3 tangent;
    float3 bitangent;
    coordinateSystem(surface.shadingNormal, tangent, bitangent);
    float3 localView = float3(dot(viewDirection, tangent),
                              dot(viewDirection, bitangent),
                              normalView);
    float alpha = surface.roughness * surface.roughness;
    float3 localHalf = sampleGgxVisibleNormal(
        localView, alpha, directionSample);
    float3 halfVector = normalize(tangent * localHalf.x +
        bitangent * localHalf.y + surface.shadingNormal * localHalf.z);
    lightDirection = normalize(reflect(-viewDirection, halfVector));
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (!(normalLight > 0.0) ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return false;
    }
    BsdfEvaluation evaluation = evaluateBsdf(
        surface, viewDirection, lightDirection);
    float normalHalf = saturate(dot(surface.shadingNormal, halfVector));
    float distribution = ggxDistribution(normalHalf, alpha);
    float specularPdf = distribution * smithG1(normalView, alpha) /
        max(4.0 * normalView * NdfTrim, 1.0e-7);
    if (!(specularPdf > 0.0)) {
        return false;
    }
    throughput = evaluation.specular * (normalLight / specularPdf);
    return !any(isnan(throughput)) && !any(isinf(throughput));
}

/* One independent first-bounce GGX estimator accompanies every primary
 * direct sample. It does not probabilistically discard or alter the accepted
 * diffuse-GI continuation. */
float3 sampleSmoothSpecularPath(uint2 pixel, uint sampleIndex,
                                SurfaceData primarySurface,
                                float3 viewDirection)
{
    if (MaximumDepth < 2u) {
        return 0.0;
    }
    float realWeight = 1.0 - fakeSpecularWeight(
        primarySurface.roughness);
    if (!(realWeight > 0.0)) {
        return 0.0;
    }
    float2 directionSample = sampleStream(
        pixel, sampleIndex, SmoothSpecularDirectionStream).xy;
    float3 rayDirection;
    float3 throughput;
    if (!sampleIndependentGgxSpecular(
            primarySurface, viewDirection, directionSample,
            rayDirection, throughput)) {
        return 0.0;
    }
    throughput *= realWeight;
    RayDesc ray;
    ray.Origin = primarySurface.position +
        primarySurface.geometricNormal * RayEpsilon;
    ray.Direction = rayDirection;
    ray.TMin = RayEpsilon;
    ray.TMax = SceneFarPlane;
    SegmentTraversal segment = traceSegment(ray);
    float3 result = throughput * segment.additiveRadiance;
    if (segment.payload.hit == 0u) {
        return result;
    }
    SurfaceData reachedSurface = loadSurface(
        segment.payload, rayDirection);
    float hitAttenuation = 1.0 / max(
        1.0, segment.distance * primarySurface.roughness * 0.02);
    float emissionComplement =
        reachedSurface.emitterIndex < EmitterCount ?
            1.0 - directSpecularWeight(primarySurface.roughness) : 1.0;
    float3 reachedRadiance = reachedSurface.emission * emissionComplement;
    /* As in sampleDiffusePath: what the reflected surface is lit by. */
    reachedRadiance += diffuseReflectance(reachedSurface) *
        reachedSurface.sourceIrradiance;
    if (EmitterCount > 0u) {
        reachedRadiance += sampleDiffusePolygonLight(
            pixel, sampleIndex, SmoothSpecularPolygonStream, true,
            reachedSurface, 1u);
    }
    result += throughput * reachedRadiance * hitAttenuation;
    return result;
}


EmitterEvaluation evaluateEnvironmentSample(SurfaceData surface,
                                             float3 viewDirection,
                                             EmitterSample lightSample)
{
    EmitterEvaluation evaluation;
    evaluation.contribution = 0.0;
    evaluation.diffuseContribution = 0.0;
    evaluation.specularContribution = 0.0;
    evaluation.targetPdf = 0.0;
    evaluation.sourcePdf = 0.0;
    evaluation.brdfPdf = 0.0;
    evaluation.lightDirection = 0.0;
    evaluation.lightDistance = SceneFarPlane;
    evaluation.valid = false;
    if (!lightSample.valid ||
        lightSample.emitterIndex != EnvironmentLightIndex) {
        return evaluation;
    }
    float3 lightDirection = unpackOctahedralNormal(lightSample.positionSample);
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (normalLight <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return evaluation;
    }
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    if (!(bsdf.pdf > 0.0)) {
        return evaluation;
    }
    float3 environment = environmentRadiance(lightDirection);
    evaluation.diffuseContribution = bsdf.diffuse * environment * normalLight;
    evaluation.specularContribution = bsdf.specular * environment *
        normalLight * directSpecularWeight(surface.roughness);
    evaluation.contribution = evaluation.diffuseContribution +
        evaluation.specularContribution;
    evaluation.targetPdf = luminance(evaluation.contribution);
    /* The project analytic sky is sampled by a cosine-hemisphere environment
     * strategy. Unlike a lat-long map it has no texel distribution to
     * presample, so this is its exact solid-angle proposal density. */
    evaluation.sourcePdf = normalLight / Pi;
    evaluation.brdfPdf = bsdf.pdf;
    evaluation.lightDirection = lightDirection;
    evaluation.valid = true;
    return evaluation;
}

bool directSampleIndexValid(uint lightIndex)
{
    return lightIndex < EmitterCount || lightIndex == EnvironmentLightIndex;
}

EmitterEvaluation evaluateDirectSampleForFrame(SurfaceData surface,
                                                float3 viewDirection,
                                                EmitterSample lightSample,
                                                bool previousFrame)
{
    if (lightSample.emitterIndex == EnvironmentLightIndex) {
        return evaluateEnvironmentSample(surface, viewDirection, lightSample);
    }
    return evaluateEmitterSampleForFrame(surface, viewDirection, lightSample,
                                         previousFrame);
}

float3 lightGridCellCenter(uint cellIndex)
{
    uint3 position;
    position.x = cellIndex % LightGridCellsPerAxis;
    uint yz = cellIndex / LightGridCellsPerAxis;
    position.y = yz % LightGridCellsPerAxis;
    position.z = yz / LightGridCellsPerAxis;
    float3 origin = LightGridCenter - LightGridExtent * 0.5;
    return origin + (float3(position) + 0.5) * LightGridCellSize;
}

/*
 * Importance of a triangle to arbitrary receivers in one jitter-expanded
 * cell. The project-owned target uses RMS receiver distance, area/distance^2
 * solid angle capped at a hemisphere, and a conservative luminance proxy. It
 * is a proposal target, never a radiance clamp.
 */
float lightGridVolumeTarget(uint emitterIndex, float3 cellCenter)
{
    EmissiveTriangle emitter = Emitters[emitterIndex];
    SceneVertex first = Vertices[emitter.firstVertex + 0u];
    SceneVertex second = Vertices[emitter.firstVertex + 1u];
    SceneVertex third = Vertices[emitter.firstVertex + 2u];
    float3 emitterCenter =
        (first.position + second.position + third.position) / 3.0;
    float3 difference = emitterCenter - cellCenter;
    float distanceSquared = dot(difference, difference);
    /* A full cell diagonal bounds the cell plus the +/- half-cell lookup
     * jitter. For a uniform spherical receiver volume, E[r^2] = 3 R^2 / 5. */
    float volumeRadius = LightGridCellSize * sqrt(3.0);
    float rmsDistanceSquared = distanceSquared +
        0.6 * volumeRadius * volumeRadius;
    float area = 1.0 / emitter.inverseArea;
    float approximateSolidAngle = min(
        area / rmsDistanceSquared, 2.0 * Pi);
    return approximateSolidAngle *
        (emitter.selectionProbability / area);
}

[shader("raygeneration")]
void BuildLightGrid()
{
    uint2 dispatchIndex = DispatchRaysIndex().xy;
    uint entryIndex = LightGridRebuild != 0u ? dispatchIndex.y :
        dispatchIndex.y * LightGridRefreshPhaseCount +
            SampleIndex % LightGridRefreshPhaseCount;
    uint lightSlot = dispatchIndex.x * LightGridLightsPerCell + entryIndex;
    if (lightSlot >= LightGridEntryCount) {
        return;
    }
    LightGridEntry output;
    output.emitterIndex = InvalidIndex;
    output.inverseSelectionProbability = 0.0;
    if (EmitterCount == 0u) {
        LightGrid[lightSlot] = output;
        return;
    }

    uint cellIndex = lightSlot / LightGridLightsPerCell;
    float3 cellCenter = lightGridCellCenter(cellIndex);
    float weightSum = 0.0;
    float selectedTarget = 0.0;
    for (uint candidate = 0u; candidate < LightGridBuildSamples;
         ++candidate) {
        float4 random = sampleStream(
            uint2(lightSlot & 0xffffu, lightSlot >> 16u), SampleIndex,
            LightGridBuildStream + candidate);
        uint emitterIndex = selectEmitter(random.x);
        float sourceProbability =
            Emitters[emitterIndex].selectionProbability;
        float target = lightGridVolumeTarget(emitterIndex, cellCenter);
        float weight = sourceProbability > 0.0 ?
            target / sourceProbability : 0.0;
        weightSum += weight;
        if (weight > 0.0 && random.y * weightSum < weight) {
            output.emitterIndex = emitterIndex;
            selectedTarget = target;
        }
    }
    if (selectedTarget > 0.0) {
        output.inverseSelectionProbability =
            weightSum /
            (float(LightGridBuildSamples) * selectedTarget);
    }
    LightGrid[lightSlot] = output;
}

int lightGridCellForSurface(uint2 pixel, uint sampleIndex,
                            float3 surfacePosition)
{
    float3 jitter = sampleStream(
        pixel, sampleIndex, LightGridLookupStream).xyz - 0.5;
    float3 samplingPosition =
        surfacePosition + jitter * LightGridCellSize;
    float3 origin = LightGridCenter - LightGridExtent * 0.5;
    int3 cell = int3(floor(
        (samplingPosition - origin) / LightGridCellSize));
    if (any(cell < 0) ||
        any(cell >= int3(LightGridCellsPerAxis,
                         LightGridCellsPerAxis,
                         LightGridCellsPerAxis))) {
        return -1;
    }
    return cell.x + (cell.y + cell.z * int(LightGridCellsPerAxis)) *
        int(LightGridCellsPerAxis);
}

LightSelection selectEmitterForCell(float selection, int cellIndex)
{
    LightSelection result;
    if (cellIndex >= 0) {
        uint slot = min(uint(selection * float(LightGridLightsPerCell)),
                        LightGridLightsPerCell - 1u);
        LightGridEntry entry = LightGrid[
            uint(cellIndex) * LightGridLightsPerCell + slot];
        if (entry.emitterIndex < EmitterCount &&
            entry.inverseSelectionProbability > 0.0) {
            result.emitterIndex = entry.emitterIndex;
            result.inverseProbability =
                entry.inverseSelectionProbability;
            return result;
        }
    }
    result.emitterIndex = selectEmitter(selection);
    result.inverseProbability =
        1.0 / Emitters[result.emitterIndex].selectionProbability;
    return result;
}

EmitterEvaluation evaluateEmitterSample(SurfaceData surface,
                                        float3 viewDirection,
                                        EmitterSample lightSample)
{
    return evaluateEmitterSampleForFrame(surface, viewDirection, lightSample,
                                         false);
}

EmitterEvaluation evaluatePreviousEmitterSample(SurfaceData surface,
                                                float3 viewDirection,
                                                EmitterSample lightSample)
{
    return evaluateEmitterSampleForFrame(surface, viewDirection, lightSample,
                                         true);
}

EmitterEvaluation evaluateDirectSample(SurfaceData surface,
                                       float3 viewDirection,
                                       EmitterSample lightSample)
{
    return evaluateDirectSampleForFrame(surface, viewDirection, lightSample,
                                        false);
}

EmitterEvaluation evaluatePreviousDirectSample(SurfaceData surface,
                                               float3 viewDirection,
                                               EmitterSample lightSample)
{
    return evaluateDirectSampleForFrame(surface, viewDirection, lightSample,
                                        true);
}

bool traceEmitterVisibility(SurfaceData surface, EmitterEvaluation evaluation,
                            uint instanceMask)
{
    return traceVisibility(surface.position +
                               surface.geometricNormal * RayEpsilon,
                           evaluation.lightDirection,
                           evaluation.lightDistance - RayEpsilon,
                           instanceMask);
}

bool traceDirectVisibility(SurfaceData surface, EmitterEvaluation evaluation,
                           uint instanceMask)
{
    return traceVisibility(surface.position +
                               surface.geometricNormal * RayEpsilon,
                           evaluation.lightDirection,
                           evaluation.lightDistance - RayEpsilon,
                           instanceMask);
}

EmitterSample environmentDirectSample(SurfaceData surface, float2 random);
EmitterSample traceBrdfDirectSample(SurfaceData surface,
                                    float3 viewDirection,
                                    float chooseSample,
                                    float2 directionSample,
                                    out float sampledBrdfPdf);
float directInitialMixturePdf(EmitterSample sample,
                              EmitterEvaluation evaluation,
                              float localStrategyWeight,
                              float environmentStrategyWeight,
                              float brdfStrategyWeight,
                              float localProposalPdf);
void streamDirectInitialCandidate(EmitterSample candidate,
                                  EmitterEvaluation evaluation,
                                  float mixturePdf, float acceptance,
                                  inout EmitterSample selected,
                                  inout float weightSum);

/* Secondary surfaces do not own a screen-space history buffer. They instead
 * reuse the camera-centered ReGIR entries shared by every path in a world-space
 * cell, resample two local candidates plus one analytic-environment candidate,
 * plus an independent BRDF candidate over the environment domain, and trace
 * visibility only for the survivor. The continuation ray then carries indirect
 * transport without counting that direct domain again. */
float3 sampleSecondaryDirectLighting(uint2 pixel, uint sampleIndex, uint depth,
                                     SurfaceData surface,
                                     float3 viewDirection,
                                     uint instanceMask)
{
    uint localSampleCount = EmitterCount > 0u ?
        SecondaryLocalSampleCount : 0u;
    const uint brdfSampleCount = 1u;
    uint totalSampleCount = localSampleCount +
        SecondaryEnvironmentSampleCount + brdfSampleCount;
    float inverseTotalSampleCount = 1.0 / float(totalSampleCount);
    float localStrategyWeight =
        float(localSampleCount) * inverseTotalSampleCount;
    float environmentStrategyWeight =
        float(SecondaryEnvironmentSampleCount) * inverseTotalSampleCount;
    float brdfStrategyWeight =
        float(brdfSampleCount) * inverseTotalSampleCount;
    int lightGridCell = lightGridCellForSurface(
        pixel, sampleIndex + depth * 131u, surface.position);
    EmitterSample selected = (EmitterSample)0;
    selected.emitterIndex = InvalidIndex;
    selected.positionSample = 0u;
    selected.valid = false;
    float weightSum = 0.0;

    for (uint candidate = 0u; candidate < localSampleCount; ++candidate) {
        float4 random = sampleStream(
            pixel, sampleIndex,
            SecondaryDirectStream + depth * 16u + candidate);
        float selection = (random.x + float(candidate)) /
            float(localSampleCount);
        LightSelection lightSelection;
        if (!selectEmitterForZone(selection, surface, lightSelection)) {
            lightSelection = selectEmitterForCell(selection, lightGridCell);
        }
        EmitterSample candidateSample;
        candidateSample.emitterIndex = lightSelection.emitterIndex;
        candidateSample.positionSample = packPositionSample(random.yz);
        candidateSample.valid = true;
        EmitterEvaluation evaluation = evaluateDirectSample(
            surface, viewDirection, candidateSample);
        float globalProbability = Emitters[
            candidateSample.emitterIndex].selectionProbability;
        float conditionalAreaPdf = evaluation.valid &&
                globalProbability > 0.0 ?
            evaluation.sourcePdf / globalProbability : 0.0;
        float localProposalPdf = conditionalAreaPdf > 0.0 &&
                lightSelection.inverseProbability > 0.0 ?
            conditionalAreaPdf / lightSelection.inverseProbability : 0.0;
        float mixturePdf = directInitialMixturePdf(
            candidateSample, evaluation, localStrategyWeight,
            environmentStrategyWeight, brdfStrategyWeight,
            localProposalPdf);
        streamDirectInitialCandidate(candidateSample, evaluation, mixturePdf,
                                     random.w, selected, weightSum);
    }

    float4 environmentRandom = sampleStream(
        pixel, sampleIndex,
        SecondaryDirectStream + depth * 16u +
            SecondaryLocalSampleCount);
    EmitterSample environmentSample = environmentDirectSample(
        surface, environmentRandom.xy);
    EmitterEvaluation environmentEvaluation = evaluateDirectSample(
        surface, viewDirection, environmentSample);
    float environmentMixturePdf = directInitialMixturePdf(
        environmentSample, environmentEvaluation, localStrategyWeight,
        environmentStrategyWeight, brdfStrategyWeight, 0.0);
    streamDirectInitialCandidate(
        environmentSample, environmentEvaluation, environmentMixturePdf,
        environmentRandom.z, selected, weightSum);

    float4 brdfRandom = sampleStream(
        pixel, sampleIndex,
        SecondaryDirectStream + depth * 16u +
            SecondaryLocalSampleCount + SecondaryEnvironmentSampleCount);
    float sampledBrdfPdf;
    EmitterSample brdfSample = traceBrdfDirectSample(
        surface, viewDirection, brdfRandom.x, brdfRandom.yz,
        sampledBrdfPdf);
    EmitterEvaluation brdfEvaluation = evaluateDirectSample(
        surface, viewDirection, brdfSample);
    float brdfMixturePdf = sampledBrdfPdf > 0.0 ?
        directInitialMixturePdf(
            brdfSample, brdfEvaluation, localStrategyWeight,
            environmentStrategyWeight, brdfStrategyWeight, 0.0) : 0.0;
    streamDirectInitialCandidate(
        brdfSample, brdfEvaluation, brdfMixturePdf, brdfRandom.w,
        selected, weightSum);

    if (!selected.valid) {
        return 0.0;
    }
    EmitterEvaluation selectedEvaluation = evaluateDirectSample(
        surface, viewDirection, selected);
    if (!selectedEvaluation.valid ||
        !(selectedEvaluation.targetPdf > 0.0) ||
        !traceDirectVisibility(surface, selectedEvaluation, instanceMask)) {
        return 0.0;
    }
    float inversePdf = weightSum /
        (float(totalSampleCount) * selectedEvaluation.targetPdf);
    return selectedEvaluation.contribution * inversePdf;
}

EmitterSample environmentDirectSample(SurfaceData surface, float2 random)
{
    EmitterSample sample;
    sample.emitterIndex = EnvironmentLightIndex;
    sample.positionSample = packOctahedralNormal(
        cosineHemisphere(surface.shadingNormal, random));
    sample.valid = true;
    return sample;
}

EmitterSample traceBrdfDirectSample(SurfaceData surface,
                                    float3 viewDirection,
                                    float chooseSample,
                                    float2 directionSample,
                                    out float sampledBrdfPdf)
{
    EmitterSample sample;
    sample.emitterIndex = InvalidIndex;
    sample.positionSample = 0u;
    sample.valid = false;
    sampledBrdfPdf = 0.0;
    float3 lightDirection;
    BsdfEvaluation bsdf;
    bool sampledSpecular;
    if (!sampleBsdf(surface, viewDirection, chooseSample, directionSample,
                    lightDirection, bsdf, sampledSpecular)) {
        return sample;
    }
    sampledBrdfPdf = bsdf.pdf;
    RayDesc ray;
    ray.Origin = surface.position + surface.geometricNormal * RayEpsilon;
    ray.Direction = lightDirection;
    ray.TMin = RayEpsilon;
    ray.TMax = SceneFarPlane;
    SegmentTraversal segment = traceSegment(ray);
    if (segment.payload.hit == 0u) {
        sample.emitterIndex = EnvironmentLightIndex;
        sample.positionSample = packOctahedralNormal(lightDirection);
        sample.valid = true;
        return sample;
    }
    /* Mesh emitters stay in the ReGIR strategy. Evaluating the probability of
     * an arbitrary BRDF-discovered emitter would require a reverse lookup
     * through the stochastic per-cell table; substituting the global light PDF
     * is a different proposal and creates rare oversized MIS weights. The
     * analytic environment has an exact PDF and can safely overlap BRDF. */
    return sample;
}

float directInitialMixturePdf(EmitterSample sample,
                              EmitterEvaluation evaluation,
                              float localStrategyWeight,
                              float environmentStrategyWeight,
                              float brdfStrategyWeight,
                              float localProposalPdf)
{
    bool environmentSample =
        sample.emitterIndex == EnvironmentLightIndex;
    float lightPdf = environmentSample ?
        environmentStrategyWeight * evaluation.sourcePdf :
        localStrategyWeight * localProposalPdf;
    float brdfPdf = environmentSample ? evaluation.brdfPdf : 0.0;
    return lightPdf + brdfStrategyWeight * brdfPdf;
}

void streamDirectInitialCandidate(EmitterSample candidate,
                                  EmitterEvaluation evaluation,
                                  float mixturePdf, float acceptance,
                                  inout EmitterSample selected,
                                  inout float weightSum)
{
    float weight = candidate.valid && evaluation.valid &&
            evaluation.targetPdf > 0.0 && mixturePdf > 0.0 ?
        evaluation.targetPdf / mixturePdf : 0.0;
    weightSum += weight;
    if (weight > 0.0 && acceptance * weightSum < weight) {
        selected = candidate;
    }
}

void writeMissGuides(uint2 pixel, float3 unjitteredDirection,
                     float2 dimensions)
{
    /*
     * The background has no surface, so the guides must describe the most
     * distant, fully rough, camera-facing surface Ray Reconstruction can accept
     * rather than a degenerate one. A zero normal is not unit length and a zero
     * linear depth is the nearest representable distance, which turns every
     * silhouette against the sky into an inverted depth discontinuity.
     */
    DiffuseAlbedo[pixel] = 0.0;
    SpecularAlbedo[pixel] = 0.0;
    /* Roughness is packed in the normal alpha channel for DLSS-RR. The
     * standalone texture exists only for its explicit debug view. */
    ShadingNormal[pixel] = float4(-unjitteredDirection, 1.0);
    if ((DiagnosticGuideMask & 1u) != 0u) {
        LinearRoughness[pixel] = 1.0;
    }
    LinearDepth[pixel] = SceneFarPlane;
    writeRayReconstructionMotion(
        pixel, environmentMotion(unjitteredDirection, dimensions),
        dimensions);
    SpecularHitDistance[pixel] = 0.0;
    SurfaceParameters[pixel] = 0u;
    if ((DiagnosticGuideMask & 2u) != 0u) {
        DiffuseHitDistance[pixel] = 0.0;
    }
}

float deterministicSpecularHitDistance(SurfaceData surface,
                                       float3 viewDirection)
{
    float3 mirrorDirection = normalize(reflect(
        -viewDirection, surface.shadingNormal));
    if (dot(surface.geometricNormal, mirrorDirection) <= 0.0) {
        return SceneFarPlane;
    }
    RayDesc mirrorRay;
    mirrorRay.Origin = surface.position +
        surface.geometricNormal * RayEpsilon;
    mirrorRay.Direction = mirrorDirection;
    mirrorRay.TMin = RayEpsilon;
    mirrorRay.TMax = SceneFarPlane;
    SegmentTraversal segment = traceSegment(mirrorRay);
    return segment.payload.hit != 0u ?
        segment.distance : SceneFarPlane;
}

void writeSurfaceGuides(uint2 pixel, SurfacePayload payload,
                        SurfaceData surface, uint2 dimensions,
                        float3 viewDirection)
{
    /* Publish the material channels that describe the combined noisy signal.
     * The deterministic mirror trace is a current-geometry guide and is
     * independent of the stochastic radiance sample. */
    DiffuseAlbedo[pixel] = float4(diffuseReflectance(surface), 1.0);
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    SpecularAlbedo[pixel] = float4(reconstructionSpecularAlbedo(
        surfaceF0(surface), surface.roughness, normalView), 1.0);
    ShadingNormal[pixel] = float4(
        surface.shadingNormal, surface.roughness);
    if ((DiagnosticGuideMask & 1u) != 0u) {
        LinearRoughness[pixel] = surface.roughness;
    }
    LinearDepth[pixel] = surface.primitive == ViewWeaponPrimitive ?
        currentViewWeaponPosition(payload).z :
        max(0.0, dot(surface.position - CameraPosition, CameraForward));
    writeRayReconstructionMotion(
        pixel, surfaceMotion(payload, surface, float2(dimensions),
                             surface.primitive == ViewWeaponPrimitive),
        float2(dimensions));
    bool writeSpecularGuide = RayReconstructionActive != 0u ||
        ForceSpecularGuide != 0u || (DiagnosticGuideMask & 4u) != 0u;
    SpecularHitDistance[pixel] = writeSpecularGuide ?
        deterministicSpecularHitDistance(surface, viewDirection) : 0.0;
    SurfaceParameters[pixel] = packSurfaceF0(surfaceF0(surface));
    if ((DiagnosticGuideMask & 2u) != 0u) {
        DiffuseHitDistance[pixel] = 0.0;
    }
}

/*
 * Clears this pixel's reservoir at the start of the frame.
 *
 * Indirect paths are traced from a compacted work list, so a pixel that gets no
 * continuation this frame never writes a canonical reservoir. Without this it
 * would keep whatever the grid held two frames ago and the resampling passes
 * would treat that as a current sample -- and on the very first frame, before
 * anything has been written at all, it would be uninitialised memory read as
 * path positions and traced against.
 */
void clearCurrentReservoir(uint2 pixel, uint2 dimensions)
{
    if (IndirectMode == IndirectModeRestirPt) {
        CurrentReservoirs[reservoirIndex(pixel, dimensions)] =
            emptyReservoir();
    }
}

/*
 * Records which surface a pixel is looking at, independently of whether it got
 * an indirect path this frame.
 *
 * Indirect paths come from a compacted work list, so only some pixels produce a
 * canonical sample. The rest still need their surface known, because temporal
 * reuse can supply them a path from history and cannot validate that history
 * against a surface nobody wrote down. A stamped reservoir carries a surface
 * and no sample: primaryDepth is what says the surface is known, and M is what
 * says whether a path has been found for it yet.
 */
void stampReservoirSurface(uint2 pixel, uint2 dimensions, SurfaceData surface,
                           float depth)
{
    if (IndirectMode != IndirectModeRestirPt) {
        return;
    }
    uint index = reservoirIndex(pixel, dimensions);
    PathReservoir reservoir = CurrentReservoirs[index];
    reservoir.primaryPosition = surface.position;
    reservoir.primaryNormal = packOctahedralNormal(surface.geometricNormal);
    reservoir.primaryMaterial = surface.materialIndex;
    reservoir.primaryDepth = depth;
    CurrentReservoirs[index] = reservoir;
}

void shadePrimary(uint2 pixel, uint2 dimensions, float3 direction,
                  float3 unjitteredDirection,
                  SegmentTraversal primarySegment)
{
    SurfacePayload primaryPayload = primarySegment.payload;
    uint primaryPrimitive = primaryPayload.hit != 0u ?
        Vertices[primaryPayload.primitiveIndex * 3u].primitive : InvalidIndex;
    bool rejectRayReconstructionHistory =
        updateRayReconstructionWeaponHistory(
            pixel, primaryPrimitive == ViewWeaponPrimitive);
    RayReconstructionDisocclusion[pixel] =
        rejectRayReconstructionHistory ? 1.0 : 0.0;
    RayReconstructionBiasCurrentColor[pixel] =
        primaryPrimitive == ViewWeaponPrimitive ? 1.0 : 0.0;
    bool includeVisibleEmission =
        RadianceChannel == RadianceChannelCombined ||
        RadianceChannel == RadianceChannelEmission;
    float3 resolvedRadiance = includeVisibleEmission ?
        primarySegment.additiveRadiance : 0.0;
    IndirectSignal resolvedIndirectSignal = emptyIndirectSignal();
    uint indirectSampleCount = 0u;
    if (primaryPayload.hit == 0u) {
        writeMissGuides(pixel, unjitteredDirection, float2(dimensions));
    } else {
        SurfaceData surface = loadSurface(primaryPayload, direction);
        writeSurfaceGuides(pixel, primaryPayload, surface, dimensions,
                           -direction);
        stampReservoirSurface(pixel, dimensions, surface,
                              dot(surface.position - CameraPosition,
                                  CameraForward));
        /* Base colour is a reconstruction/material guide, not self-emission.
         * Visible source radiance is deterministic. Primary local-light NEE
         * evaluates material-dependent diffuse and GGX together; the accepted
         * diffuse-only continuation remains in sampleDiffusePath. */
        if (includeVisibleEmission) {
            resolvedRadiance += surface.emission;
        }
        float3 primaryThroughput = diffuseReflectance(surface);
        /* Continuation rays can collect crossed additive layers or reached
         * authored emission even when the scene has no polygon emitter.
         * Keep only the polygon-light samplers themselves conditional on
         * EmitterCount; do not suppress smooth or diffuse transport here. */
        if (EmitterCount > 0u || MaximumDepth >= 2u) {
            uint directSampleCount = max(SamplesPerPixel, 1u);
            /* Direct polygon NEE and fresh indirect continuation counts are
             * independent. Extra GI paths spend no primary shadow ray. */
            indirectSampleCount = MaximumDepth >= 2u &&
                    luminance(primaryThroughput) > 1.0e-6 ?
                max(IndirectSamplesPerPixel, 1u) : 0u;
            bool deferBurstContinuation =
                BoundedBurstContinuations != 0u && indirectSampleCount > 0u;
            if (deferBurstContinuation) {
                uint appendSlot = 0u;
                BurstDispatchArguments.InterlockedAdd(
                    BurstDispatchWidthOffset, 1u, appendSlot);
                uint capacity = dimensions.x * dimensions.y;
                uint workIndex = appendSlot > 0u ?
                    appendSlot - 1u : capacity;
                if (workIndex < capacity) {
                    BurstWorkItems[workIndex] = uint2(
                        pixel.y * dimensions.x + pixel.x,
                        indirectSampleCount);
                } else {
                    /* Preserve radiance even if a future append bug violates
                     * the one-item-per-pixel proof. Hidden validation fails
                     * explicitly instead of dropping the path. */
                    deferBurstContinuation = false;
                    if (ValidationEnabled != 0u) {
                        InterlockedAdd(Diagnostics[15], 1u);
                    }
                }
            }
            uint pathSampleCount = max(
                directSampleCount,
                deferBurstContinuation ? 0u : indirectSampleCount);
            DirectLightingSample directRadiance =
                (DirectLightingSample)0;
            float3 smoothSpecularRadiance = 0.0;
            IndirectSignal indirectSignalSum = emptyIndirectSignal();
            for (uint sampleOrdinal = 0u;
                 sampleOrdinal < pathSampleCount;
                ++sampleOrdinal) {
                uint directSampleIndex = SampleIndex *
                    max(SamplesPerPixel, 1u) + sampleOrdinal;
                /* Keep a fixed per-frame stride when adaptive sampling drops
                 * from its burst ceiling to one path. Changing the stride with
                 * the selected count would revisit old low-discrepancy indices
                 * at the transition. */
                uint indirectSampleIndex = indirectSampleCount > 0u ?
                    SampleIndex * max(IndirectSamplesPerPixel, 1u) +
                        sampleOrdinal : 0u;
                DirectLightingSample sampleDirect =
                    (DirectLightingSample)0;
                if (sampleOrdinal < directSampleCount) {
                    sampleDirect = samplePrimaryPolygonLight(
                        pixel, directSampleIndex, surface,
                        -direction);
                }
                bool traceSmoothSpecular =
                    sampleOrdinal < directSampleCount;
                bool traceDiffuseContinuation =
                    sampleOrdinal < indirectSampleCount;
                float smoothSpecularScale = 1.0;
                float diffuseContinuationScale = 1.0;
                if (SingleContinuationLobe != 0u &&
                    traceSmoothSpecular && traceDiffuseContinuation) {
                    float smoothProbability =
                        continuationSpecularProbability(surface, -direction);
                    bool chooseSmooth = sampleStream(
                        pixel, directSampleIndex,
                        ContinuationLobeSelectionStream).x <
                            smoothProbability;
                    traceSmoothSpecular = chooseSmooth;
                    traceDiffuseContinuation = !chooseSmooth;
                    smoothSpecularScale = chooseSmooth ?
                        rcp(max(smoothProbability, 1.0e-6)) : 0.0;
                    diffuseContinuationScale = !chooseSmooth ?
                        rcp(max(1.0 - smoothProbability, 1.0e-6)) : 0.0;
                }
                /* The dense pass repeats the exact lobe choice and diffuse
                 * sample stream. Smooth continuation and all direct work stay
                 * here, while burst/disoccluded GI never sets this flag. */
                if (deferBurstContinuation) {
                    traceDiffuseContinuation = false;
                }
                float3 sampleSmoothSpecular = traceSmoothSpecular ?
                    sampleSmoothSpecularPath(
                        pixel, directSampleIndex, surface,
                        -direction) * smoothSpecularScale : 0.0;
                float3 sampleIndirectIncident = 0.0;
                float3 sampleIndirectDirection = surface.geometricNormal;
                if (traceDiffuseContinuation) {
                    DiffusePathSample pathSample = sampleDiffusePath(
                        pixel, indirectSampleIndex, surface);
                    sampleIndirectDirection = pathSample.firstDirection;
                    sampleIndirectIncident = pathSample.radiance *
                        diffuseContinuationScale;
                    if (sampleOrdinal == 0u &&
                        (DiagnosticGuideMask & 2u) != 0u) {
                        DiffuseHitDistance[pixel] = pathSample.firstDistance;
                    }
                }
                float3 sampleRadiance = sampleDirect.diffuse +
                    sampleDirect.specular +
                    sampleSmoothSpecular +
                    primaryThroughput * sampleIndirectIncident;
                if (any(isnan(sampleRadiance)) || any(isinf(sampleRadiance)) ||
                    any(isnan(sampleIndirectIncident)) ||
                    any(isinf(sampleIndirectIncident))) {
                    continue;
                }
                float sampleLuminance = luminance(sampleRadiance);
                if (RadianceClamp > 0.0 && sampleLuminance > RadianceClamp) {
                    float scale = RadianceClamp / sampleLuminance;
                    sampleDirect.diffuse *= scale;
                    sampleDirect.specular *= scale;
                    sampleSmoothSpecular *= scale;
                    sampleIndirectIncident *= scale;
                }
                if (sampleOrdinal < directSampleCount) {
                    directRadiance.diffuse += sampleDirect.diffuse;
                    directRadiance.specular += sampleDirect.specular;
                    smoothSpecularRadiance += sampleSmoothSpecular;
                }
                if (sampleOrdinal < indirectSampleCount) {
                    IndirectSignal sampleIndirectSignal =
                        indirectSignalFromRadiance(
                            sampleIndirectIncident, sampleIndirectDirection);
                    indirectSignalSum.luminanceSH +=
                        sampleIndirectSignal.luminanceSH;
                    indirectSignalSum.chroma += sampleIndirectSignal.chroma;
                }
            }
            if (directSampleCount > 0u) {
                float inverseDirectCount = 1.0 /
                    float(directSampleCount);
                float3 averageDirectDiffuse = directRadiance.diffuse *
                    inverseDirectCount;
                float3 averageDirectSpecular = directRadiance.specular *
                    inverseDirectCount;
                float3 averageSmoothSpecular = smoothSpecularRadiance *
                    inverseDirectCount;
                if (ValidationEnabled != 0u) {
                    if (luminance(averageDirectDiffuse) > 1.0e-6) {
                        InterlockedAdd(Diagnostics[11], 1u);
                    }
                    if (luminance(averageDirectSpecular) > 1.0e-6) {
                        InterlockedAdd(Diagnostics[12], 1u);
                    }
                    if (luminance(averageSmoothSpecular) > 1.0e-6) {
                        InterlockedAdd(Diagnostics[14], 1u);
                    }
                }
                if (RadianceChannel == RadianceChannelCombined ||
                    RadianceChannel == RadianceChannelDirectDiffuse) {
                    resolvedRadiance += averageDirectDiffuse;
                }
                if (RadianceChannel == RadianceChannelCombined ||
                    RadianceChannel == RadianceChannelDirectSpecular) {
                    resolvedRadiance += averageDirectSpecular;
                }
                if (RadianceChannel == RadianceChannelCombined ||
                    RadianceChannel == RadianceChannelSmoothSpecular) {
                    resolvedRadiance += averageSmoothSpecular;
                }
            }
            if (indirectSampleCount > 0u) {
                resolvedIndirectSignal = scaleIndirectSignal(
                    indirectSignalSum, 1.0 / float(indirectSampleCount));
            }
        }
    }
    NoisyRadiance[pixel] = float4(resolvedRadiance, 1.0);
    storeCurrentIndirectSignal(pixel, resolvedIndirectSignal);
    if ((IndirectTemporalWindow & IndirectTemporalWindowMask) > 1u) {
        uint historyMetadata = primaryPayload.hit != 0u &&
                indirectSampleCount > 0u ?
            packIndirectHistoryMetadata(
                primaryPayload.primitiveIndex, 1u) : 0u;
        storeCurrentIndirectHistoryMetadata(pixel, historyMetadata);
    }
    if (ValidationEnabled != 0u) {
        if (primaryPrimitive == ViewWeaponPrimitive) {
            uint3 encoded = uint3(saturate(resolvedRadiance) * 255.0);
            InterlockedAdd(Diagnostics[0], 1u);
            InterlockedAdd(Diagnostics[1],
                encoded.r * 3u + encoded.g * 5u + encoded.b * 7u);
        }
        if (primaryPrimitive == WorldBillboardPrimitive ||
            primaryPrimitive == WorldEffectPrimitive) {
            InterlockedAdd(Diagnostics[2], 1u);
        }
        if (primaryPrimitive == WorldVectorPrimitive) {
            InterlockedAdd(Diagnostics[3], 1u);
        }
        /* Additive layers remain non-occluding and their source emission is
         * visible, but they are excluded from the polygon-light distribution. */
        if (primarySegment.additiveLayers != 0u) {
            InterlockedAdd(Diagnostics[4], 1u);
        }
        float4 diffuseGuide = DiffuseAlbedo[pixel];
        float4 specularGuide = SpecularAlbedo[pixel];
        float4 normalRoughnessGuide = ShadingNormal[pixel];
        float depthGuide = LinearDepth[pixel];
        float2 motionGuide = SceneMotion[pixel];
        float specularDistanceGuide = SpecularHitDistance[pixel];
        bool invalidLightingOrGuide =
            any(isnan(resolvedRadiance)) || any(isinf(resolvedRadiance)) ||
            any(isnan(diffuseGuide)) || any(isinf(diffuseGuide)) ||
            any(isnan(specularGuide)) || any(isinf(specularGuide)) ||
            any(isnan(normalRoughnessGuide)) ||
            any(isinf(normalRoughnessGuide)) ||
            isnan(depthGuide) || isinf(depthGuide) ||
            any(isnan(motionGuide)) || any(isinf(motionGuide)) ||
            isnan(specularDistanceGuide) || isinf(specularDistanceGuide);
        if (invalidLightingOrGuide) {
            InterlockedAdd(Diagnostics[13], 1u);
        }
    }
}

void primaryDirections(uint2 pixel, uint2 dimensions,
                       out float3 direction,
                       out float3 unjitteredDirection)
{
    float2 jitter = float2(JitterX, JitterY);
    float2 screen = (float2(pixel) + 0.5 + jitter) / float2(dimensions);
    float2 ndc = float2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    direction = normalize(CameraForward +
        CameraRight * (ndc.x * Aspect * TanHalfFovY) +
        CameraUp * (ndc.y * TanHalfFovY));
    float2 unjitteredScreen =
        (float2(pixel) + 0.5) / float2(dimensions);
    float2 unjitteredNdc = float2(unjitteredScreen.x * 2.0 - 1.0,
                                  1.0 - unjitteredScreen.y * 2.0);
    unjitteredDirection = normalize(CameraForward +
        CameraRight * (unjitteredNdc.x * Aspect * TanHalfFovY) +
        CameraUp * (unjitteredNdc.y * TanHalfFovY));
}

SegmentTraversal tracePrimary(float3 direction)
{
    /* Keep primary visibility pixel-centred. Stochastic variation belongs only
     * to continuation and polygon samples, so stable edges do not shake. */
    RayDesc ray;
    ray.Origin = CameraPosition;
    ray.Direction = direction;
    ray.TMin = RayEpsilon;
    ray.TMax = SceneFarPlane;
    return traceSegment(ray);
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    clearCurrentReservoir(pixel, dimensions);
    float3 direction;
    float3 unjitteredDirection;
    primaryDirections(pixel, dimensions, direction, unjitteredDirection);
    shadePrimary(pixel, dimensions, direction, unjitteredDirection,
                 tracePrimary(direction));
}

[shader("raygeneration")]
void PrimaryVisibility()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    clearCurrentReservoir(pixel, dimensions);
    float3 direction;
    float3 unjitteredDirection;
    primaryDirections(pixel, dimensions, direction, unjitteredDirection);
    SegmentTraversal segment = tracePrimary(direction);
    PrimaryVisibilityBuffer[pixel] = uint4(
        segment.payload.primitiveIndex,
        asuint(segment.payload.barycentrics.x),
        asuint(segment.payload.barycentrics.y),
        segment.additiveLayers);
    NoisyRadiance[pixel] = float4(segment.additiveRadiance, 1.0);
}

[shader("raygeneration")]
void ShadePrimary()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    float3 direction;
    float3 unjitteredDirection;
    primaryDirections(pixel, dimensions, direction, unjitteredDirection);
    uint4 packed = PrimaryVisibilityBuffer[pixel];
    SegmentTraversal segment;
    segment.payload.rayDistance = 0.0;
    segment.payload.barycentrics = float2(
        asfloat(packed.y), asfloat(packed.z));
    segment.payload.primitiveIndex = packed.x;
    segment.payload.hit = packed.x != InvalidIndex ? 1u : 0u;
    segment.additiveRadiance = NoisyRadiance[pixel].rgb;
    segment.distance = 0.0;
    segment.additiveLayers = packed.w;
    shadePrimary(pixel, dimensions, direction, unjitteredDirection, segment);
}


/* One compact entry represents one burst pixel and its exact configured path
 * count. The argument Width starts at one for a sentinel thread, then primary
 * shading appends at most one entry per internal pixel. This bounds storage by
 * width*height and traced continuations by width*height*IndirectSamplesPerPixel
 * without a fixed-content cap or an overflow drop. */
[shader("raygeneration")]
void BurstContinuation()
{
    uint dispatchIndex = DispatchRaysIndex().x;
    if (dispatchIndex == 0u) {
        return;
    }
    uint workIndex = dispatchIndex - 1u;
    uint fullWidth = 0u;
    uint fullHeight = 0u;
    PrimaryVisibilityBuffer.GetDimensions(fullWidth, fullHeight);
    uint capacity = fullWidth * fullHeight;
    if (workIndex >= capacity || fullWidth == 0u || fullHeight == 0u) {
        return;
    }

    uint2 item = BurstWorkItems[workIndex];
    uint pixelIndex = item.x;
    uint sampleCount = item.y;
    if (sampleCount == 0u || pixelIndex >= capacity) {
        return;
    }
    uint2 dimensions = uint2(fullWidth, fullHeight);
    uint2 pixel = uint2(pixelIndex % fullWidth, pixelIndex / fullWidth);
    uint4 packed = PrimaryVisibilityBuffer[pixel];
    if (packed.x == InvalidIndex) {
        return;
    }

    SurfacePayload payload;
    payload.rayDistance = 0.0;
    payload.barycentrics = float2(asfloat(packed.y), asfloat(packed.z));
    payload.primitiveIndex = packed.x;
    payload.hit = 1u;
    float3 direction;
    float3 unjitteredDirection;
    primaryDirections(pixel, dimensions, direction, unjitteredDirection);
    SurfaceData surface = loadSurface(payload, direction);

    IndirectSignal signalSum = emptyIndirectSignal();
    /*
     * ReSTIR streams the same traced paths as resampling candidates instead of
     * averaging them. One path survives per pixel rather than the mean of all
     * of them, which is noisier on its own and is meant to be: the quality is
     * recovered by reusing that survivor across pixels and frames, not by
     * tracing more of them. Averaging first would throw away the very thing
     * reuse needs, because a mean is not a path and cannot be shifted.
     */
    bool restir = IndirectMode == IndirectModeRestirPt;
    PathReservoir reservoir = emptyReservoir();
    uint canonicalAncestry = 0u;
    if (restir) {
        /* Ancestry identifies this pixel's canonical path for as long as its
         * descendants survive, which is what the duplication map counts. */
        canonicalAncestry = (reservoirIndex(pixel, dimensions) << 8u) |
            (SampleIndex & 0xffu) | 1u;
    }
    uint directSampleCount = max(SamplesPerPixel, 1u);
    for (uint sampleOrdinal = 0u;
         sampleOrdinal < sampleCount; ++sampleOrdinal) {
        uint directSampleIndex = SampleIndex * directSampleCount +
            sampleOrdinal;
        bool traceDiffuse = true;
        float diffuseScale = 1.0;
        if (SingleContinuationLobe != 0u &&
            sampleOrdinal < directSampleCount) {
            float smoothProbability =
                continuationSpecularProbability(surface, -direction);
            bool chooseSmooth = sampleStream(
                pixel, directSampleIndex,
                ContinuationLobeSelectionStream).x < smoothProbability;
            traceDiffuse = !chooseSmooth;
            diffuseScale = !chooseSmooth ?
                rcp(max(1.0 - smoothProbability, 1.0e-6)) : 0.0;
        }
        if (!traceDiffuse) {
            continue;
        }

        uint indirectSampleIndex =
            SampleIndex * max(IndirectSamplesPerPixel, 1u) +
            sampleOrdinal;
        DiffusePathSample pathSample = sampleDiffusePath(
            pixel, indirectSampleIndex, surface);
        float3 incident = clampOutput(pathSample.radiance * diffuseScale);
        {
            float sampleLuminance = luminance(incident);
            if (sampleLuminance > 0.0) {
                int bucket = int(floor((log2(sampleLuminance) + 8.0) * 0.5));
                bucket = clamp(bucket, 0,
                               int(DiagnosticLuminanceBuckets) - 1);
                InterlockedAdd(
                    Diagnostics[DiagnosticLuminanceHistogram + uint(bucket)],
                    1u);
            }
        }
        if (sampleOrdinal == 0u &&
            (DiagnosticGuideMask & 2u) != 0u) {
            DiffuseHitDistance[pixel] = pathSample.firstDistance;
        }
        if (any(isnan(incident)) || any(isinf(incident))) {
            continue;
        }
        if (restir) {
            /*
             * The target function is the stored radiance weighted by the
             * receiver's geometry, and the sampling density is that same
             * cosine, because the path tracer's cosine-weighted estimator has
             * already divided the radiance through by the rest of it. Choosing
             * them as a matched pair is what makes a single canonical
             * candidate resolve to exactly the radiance the averaging path
             * would have produced, and what lets a shifted path be compared
             * against it in the same units.
             */
            /*
             * The target function is the demodulated incoming radiance itself,
             * with no geometric weighting. Folding the receiver cosine in would
             * importance-sample slightly better, but the estimate then has to
             * be divided by that cosine to get back to the units the rest of
             * the pipeline works in, and at a grazing reconnection that
             * division is unbounded. A target that needs no inverse cannot
             * produce one.
             */
            float canonicalCosine = saturate(
                dot(surface.shadingNormal, pathSample.firstDirection));
            if (!(canonicalCosine > 1.0e-3)) {
                continue;
            }
            float3 canonicalTarget = incident * canonicalCosine;
            PathReservoir candidate = makeReservoir(
                canonicalTarget, canonicalAncestry, indirectSampleIndex,
                pathSample.firstHit ? 1u : 0u, MaximumDepth, 1.0, 1.0,
                pathSample.firstHit ? pathSample.firstSurface.position :
                    surface.position + pathSample.firstDirection *
                        SceneFarPlane,
                pathSample.firstHit ?
                    pathSample.firstSurface.geometricNormal :
                    pathSample.firstDirection,
                incident, canonicalCosine);
            candidate.ancestry = canonicalAncestry;
            candidate.primaryPosition = surface.position;
            candidate.primaryNormal =
                packOctahedralNormal(surface.geometricNormal);
            candidate.primaryMaterial = surface.materialIndex;
            candidate.primaryDepth = dot(surface.position - CameraPosition,
                                         CameraForward);
            float acceptance = sampleStream(
                pixel, indirectSampleIndex,
                ReservoirCanonicalStream).x;
            combineReservoir(reservoir, candidate, acceptance,
                             canonicalTarget);
        }
        IndirectSignal sampleSignal = indirectSignalFromRadiance(
            incident, pathSample.firstDirection);
        signalSum.luminanceSH += sampleSignal.luminanceSH;
        signalSum.chroma += sampleSignal.chroma;
    }

    if (restir) {
        /* Carry the surface stamp through, so a pixel whose candidates all
         * failed still tells the reuse passes what it is looking at. */
        PathReservoir stamped =
            CurrentReservoirs[reservoirIndex(pixel, dimensions)];
        reservoir.primaryPosition = stamped.primaryPosition;
        reservoir.primaryNormal = stamped.primaryNormal;
        reservoir.primaryMaterial = stamped.primaryMaterial;
        reservoir.primaryDepth = stamped.primaryDepth;
        /* Normalizing by the selected target and the candidate count is the
         * uniform-MIS contribution weight. This pixel's own domain is the only
         * proposer so far; temporal and spatial reuse add theirs. */
        finalizeResampling(reservoir,
                           reservoirLuminance(reservoir.targetFunction),
                           reservoirLuminance(reservoir.targetFunction) *
                               reservoir.m);
        CurrentReservoirs[reservoirIndex(pixel, dimensions)] = reservoir;
    }

    IndirectSignal signal = scaleIndirectSignal(
        signalSum, rcp(float(sampleCount)));
    storeCurrentIndirectSignal(pixel, signal);
}


float3 evaluateGgxSpecularTimesCos(
    float3 viewDirection, float3 lightDirection, float3 normal,
    float linearRoughness, float3 f0)
{
    float normalView = saturate(dot(normal, viewDirection));
    float normalLight = saturate(dot(normal, lightDirection));
    if (!(normalView > 0.0) || !(normalLight > 0.0)) {
        return 0.0;
    }
    float3 halfVector = normalize(viewDirection + lightDirection);
    float normalHalf = saturate(dot(normal, halfVector));
    float viewHalf = saturate(dot(viewDirection, halfVector));
    float alpha = linearRoughness * linearRoughness;
    float distribution = ggxDistribution(normalHalf, alpha);
    float geometry = smithG1(normalView, alpha) *
        smithG1(normalLight, alpha);
    float3 fresnel = fresnelSchlick(viewHalf, f0);
    return fresnel * distribution * geometry /
        max(4.0 * normalView, 1.0e-7);
}

float3 reconstructRoughSpecular(
    IndirectSignal filteredSignal, float3 position,
    float3 shadingNormal, float materialRoughness, float3 f0)
{
    float blendWeight = fakeSpecularWeight(materialRoughness);
    if (!(filteredSignal.luminanceSH.w > 0.0) ||
        !(blendWeight > 0.0)) {
        return 0.0;
    }
    float3 viewDirection = normalize(CameraPosition - position);
    float3 incomingDirection = filteredSignal.luminanceSH.xyz /
        filteredSignal.luminanceSH.w *
        (IndirectShBasisL0 / IndirectShBasisL1);
    float incomingLength = length(incomingDirection);
    float effectiveRoughness = materialRoughness;
    float compensation = 1.0;
    if (incomingLength >= 1.0) {
        incomingDirection /= incomingLength;
    } else {
        float3 dominantDirection = incomingDirection /
            (incomingLength + 1.0e-6);
        float3 mirrorDirection = reflect(
            -viewDirection, shadingNormal);
        incomingDirection = lerp(
            mirrorDirection, dominantDirection, incomingLength);
        effectiveRoughness = lerp(
            1.0, materialRoughness,
            pow(incomingLength, 3.0));
        compensation = pow(effectiveRoughness + 1.0, 3.0);
    }
    float3 incidentColor = max(
        decodeIndirectSignalColor(filteredSignal), 0.0);
    float3 brdf = evaluateGgxSpecularTimesCos(
        viewDirection, incomingDirection, shadingNormal,
        effectiveRoughness, f0);
    return incidentColor * brdf * blendWeight * compensation;
}

/* The current-frame estimate remains a dedicated linear directional signal
 * until composition. Sampling variance is reduced at each reached surface;
 * final radiance is never fed back into later frames. */
/*
 * Loads the primary surface a resampling pass is shading, or reports that this
 * pixel has none.
 */
bool loadResamplingSurface(uint2 pixel, PathReservoir reservoir,
                           out SurfaceData surface, out float depth)
{
    surface = (SurfaceData)0;
    depth = 0.0;
    depth = reservoir.primaryDepth;
    if (!(depth > 0.0)) {
        return false;
    }
    surface.position = reservoir.primaryPosition;
    surface.geometricNormal =
        unpackOctahedralNormal(reservoir.primaryNormal);
    float3 shading = ShadingNormal[pixel].xyz;
    surface.shadingNormal = dot(shading, shading) > 1.0e-6 ?
        normalize(shading) : surface.geometricNormal;
    surface.materialIndex = reservoir.primaryMaterial;
    return true;
}

/*
 * PASS 3: temporal reuse.
 *
 * The previous frame's reservoir is found through the one authoritative motion
 * mapping, not by searching nearby for something that looks close enough.
 * Newly revealed geometry genuinely has no history, and stretching a
 * neighbour's path into it would manufacture exactly the sort of persistently
 * wrong lighting that a temporal reconstructor afterwards makes harder to see,
 * not easier. Spatial reuse in the current frame is the recovery mechanism.
 */
[shader("raygeneration")]
void ResampleTemporal()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint index = reservoirIndex(pixel, dimensions);
    if (IndirectMode != IndirectModeRestirPt) {
        return;
    }

    PathReservoir canonical = CurrentReservoirs[index];
    /* Keep the unresampled sample before reuse overwrites this slot. */
    PreservedReservoirs[index] = canonical;
    SurfaceData surface;
    float depth;
    if (!loadResamplingSurface(pixel, canonical, surface, depth)) {
        ResampleReservoirs[index] = canonical;
        return;
    }

    /*
     * Stream into a fresh reservoir rather than adding to the canonical one.
     * The canonical pass has already normalized its weight into a contribution
     * weight, and a contribution weight and a running resampling sum are not
     * the same quantity; adding one to the other produces a number that means
     * nothing and, because this frame's reservoir is next frame's history,
     * compounds instead of merely being wrong once.
     */
    PathReservoir current = emptyReservoir();
    current.primaryPosition = canonical.primaryPosition;
    current.primaryNormal = canonical.primaryNormal;
    current.primaryMaterial = canonical.primaryMaterial;
    current.primaryDepth = canonical.primaryDepth;

    float3 selectedTarget = 0.0;
    if (reservoirValid(canonical)) {
        combineReservoir(current, canonical,
                         sampleStream(pixel, SampleIndex,
                                      ReservoirCanonicalStream).y,
                         canonical.targetFunction);
        selectedTarget = canonical.targetFunction;
    }

    bool reused = false;
    float3 historyResolved = 0.0;
    if (HistoryValid != 0u && ReservoirTemporalHistory > 1u) {
        float2 motion = SceneMotion[pixel];
        if (all(abs(motion) < InvalidMotion)) {
            float2 previousPixel = currentToPreviousPixel(pixel, motion);
            int2 previousCoordinate = int2(floor(previousPixel));
            if (all(previousCoordinate >= 0) &&
                all(previousCoordinate < int2(dimensions))) {
                PathReservoir history = PreviousReservoirs[
                    reservoirIndex(uint2(previousCoordinate), dimensions)];
                InterlockedAdd(
                    Diagnostics[DiagnosticTemporalConsidered], 1u);
                if (!reservoirSurfaceCompatible(
                        history, surface, depth,
                        ReservoirTemporalDepthTolerance,
                        ReservoirTemporalNormalTolerance,
                        ReservoirTemporalSeparation * depth)) {
                    InterlockedAdd(
                        Diagnostics[DiagnosticTemporalSurfaceRejected], 1u);
                }
                if (reservoirSurfaceCompatible(
                        history, surface, depth,
                        ReservoirTemporalDepthTolerance,
                        ReservoirTemporalNormalTolerance,
                        ReservoirTemporalSeparation * depth)) {
                    /* Duplication-based history reduction: where one ancestry
                     * has colonised a neighbourhood its descendants are not the
                     * independent samples their combined confidence claims, so
                     * the cap falls toward one. */
                    float maximumHistory = float(ReservoirTemporalHistory);
                    if (ReservoirHistoryReduction > 0.0) {
                        float ratio = saturate(
                            float(DuplicationMap[reservoirIndex(uint2(previousCoordinate), dimensions)]) /
                            ReservoirDuplicationNeighborCount);
                        float powerFactor = 0.1 *
                            exp2(6.0 * (1.0 - ReservoirHistoryReduction) - 3.0);
                        float t = pow(max(ratio, 0.0), powerFactor);
                        maximumHistory = max(1.0,
                                             lerp(maximumHistory, 1.0, t));
                    }
                    float historyM = min(history.m, maximumHistory);
                    /*
                     * Reject by age as well as capping confidence. Resampling
                     * selects for brightness, so without this the brightest
                     * sample in a neighbourhood keeps winning and never dies --
                     * a ghost that follows the surface indefinitely however
                     * stale its stored radiance has become. This is the
                     * reference's age check, and it is what forces a refresh.
                     */
                    if (float(history.age) >= maximumHistory) {
                        historyM = 0.0;
                    }
                    if (historyM > 0.0) {
                        float3 shiftedTarget;
                        float jacobian;
                        bool shifted = shiftReservoir(
                            history, surface, SceneInstanceMask,
                            shiftedTarget, jacobian);
                        InterlockedAdd(
                            Diagnostics[shifted ?
                                DiagnosticTemporalAccepted :
                                DiagnosticTemporalShiftFailed], 1u);
                        if (shifted) {
                            InterlockedAdd(
                                Diagnostics[DiagnosticTemporalJacobianSum],
                                uint(min(jacobian, 64.0) * 1024.0));
                            InterlockedAdd(
                                Diagnostics[DiagnosticTemporalJacobianCount],
                                1u);
                            float acceptance = sampleStream(
                                pixel, SampleIndex,
                                ReservoirTemporalStream).x;
                            if (resampleReservoir(
                                    current, history, acceptance,
                                    shiftedTarget,
                                    history.weightSum * historyM * jacobian,
                                    historyM)) {
                                selectedTarget = shiftedTarget;
                            }
                            reused = true;
                            historyResolved = resolvedRadiance(history);
                        }
                    }
                }
            }
        }
    }

    /*
     * The balance-heuristic denominator: every domain that could have proposed
     * the selected sample contributes that sample's target function evaluated
     * in ITS domain, weighted by the confidence it speaks for. Skipping the
     * neighbour's term does not break the image, it just makes it brighter than
     * the scene is.
     */
    float piSum = reservoirLuminance(selectedTarget) * current.m;
    finalizeResampling(current, reservoirLuminance(selectedTarget),
                       piSum * reservoirLuminance(selectedTarget));
    /*
     * Only pixels that actually produced a canonical sample can say anything
     * about bias. Indirect paths come from a work list, so a skipped pixel has
     * no "before" value at all, and counting its zero would measure temporal
     * reuse filling in coverage rather than inflating energy.
     */
    if (reused) {
        InterlockedAdd(Diagnostics[reservoirValid(canonical) ?
                           DiagnosticReuseWithCanonical :
                           DiagnosticReuseWithoutCanonical], 1u);
    }
    if (reused && reservoirValid(canonical)) {
        InterlockedAdd(Diagnostics[DiagnosticHistoryEnergyRead],
                       quantizeEnergy(historyResolved));
        {
            float canonicalEnergy =
                reservoirLuminance(resolvedRadiance(canonical));
            float historyEnergy = reservoirLuminance(historyResolved);
            float canonicalCount = canonical.m;
            float historyCount = max(current.m - canonical.m, 0.0);
            float total = canonicalCount + historyCount;
            float predicted = total > 0.0 ?
                (canonicalEnergy * canonicalCount +
                 historyEnergy * historyCount) / total : 0.0;
            InterlockedAdd(Diagnostics[DiagnosticPredictedAfter],
                           quantizeEnergy(predicted.xxx));
        }
        InterlockedAdd(Diagnostics[DiagnosticCanonicalM],
                       uint(min(canonical.m, 255.0) * 16.0));
        InterlockedAdd(Diagnostics[DiagnosticHistoryM],
                       uint(min(max(current.m - canonical.m, 0.0), 255.0) *
                            16.0));
        InterlockedAdd(Diagnostics[DiagnosticWeightBefore],
                       uint(min(max(canonical.weightSum, 0.0), 64.0) * 1024.0));
        InterlockedAdd(Diagnostics[DiagnosticWeightAfter],
                       uint(min(max(current.weightSum, 0.0), 64.0) * 1024.0));
        InterlockedAdd(Diagnostics[DiagnosticTargetBefore],
                       quantizeEnergy(canonical.targetFunction));
        InterlockedAdd(Diagnostics[DiagnosticTargetAfter],
                       quantizeEnergy(current.targetFunction));
        InterlockedAdd(Diagnostics[DiagnosticTemporalEnergyBefore],
                       quantizeEnergy(resolvedRadiance(canonical)));
        InterlockedAdd(Diagnostics[DiagnosticTemporalEnergyAfter],
                       quantizeEnergy(resolvedRadiance(current)));
    }
    current.age = reused ? min(current.age + 1u, 0xffffu) : 0u;
    /* Cap the confidence on the way out, not merely where history is read. An
     * uncapped M inflates the normalization a later pass multiplies back in,
     * and because this frame's reservoir is next frame's history the error
     * compounds rather than merely biasing one image. */
    current.m = min(current.m, float(ReservoirTemporalHistory));
    ResampleReservoirs[index] = current;
}

/*
 * PASS 5: spatial reuse.
 *
 * Neighbours come from a low-discrepancy disk rather than a fixed pattern, and
 * the radius is a fraction of the render height so that changing the DLSS
 * quality mode cannot quietly change how much of the image is searched.
 */
[shader("raygeneration")]
void ResampleSpatial()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint index = reservoirIndex(pixel, dimensions);
    if (IndirectMode != IndirectModeRestirPt) {
        return;
    }

    PathReservoir centre = ResampleReservoirs[index];
    SurfaceData surface;
    float depth;
    if (ReservoirSpatialSamples == 0u ||
        !loadResamplingSurface(pixel, centre, surface, depth)) {
        CurrentReservoirs[index] = centre;
        return;
    }

    /* As in the temporal pass: the incoming reservoir carries a contribution
     * weight, so it is streamed in rather than added to. */
    PathReservoir current = emptyReservoir();
    current.primaryPosition = centre.primaryPosition;
    current.primaryNormal = centre.primaryNormal;
    current.primaryMaterial = centre.primaryMaterial;
    current.primaryDepth = centre.primaryDepth;

    float3 selectedTarget = 0.0;
    if (reservoirValid(centre)) {
        combineReservoir(current, centre,
                         sampleStream(pixel, SampleIndex,
                                      ReservoirSpatialStream).y,
                         centre.targetFunction);
        selectedTarget = centre.targetFunction;
    }
    float ownM = centre.m;
    float radius = max(ReservoirSpatialRadius * float(dimensions.y), 1.0);

    /* Each accepted neighbour is remembered so the normalization can ask every
     * one of them what the surviving sample would have been worth in its
     * domain. */
    uint acceptedCount = 0u;
    uint acceptedIndex[8];
    float acceptedM[8];
    /*
     * Which domain the surviving sample came from, and what it was worth
     * there. The sample demonstrably exists in its own source domain -- it was
     * found there -- so that domain's term in the denominator must never
     * depend on a reverse shift happening to succeed.
     */
    int selectedEntry = -1;
    float3 selectedSourceTarget = 0.0;

    for (uint tap = 0u; tap < ReservoirSpatialSamples && tap < 8u; ++tap) {
        float2 offset = reservoirNeighborOffset(
            sampleStream(pixel, SampleIndex + tap,
                         ReservoirSpatialOffsetStream).x *
            float(ReservoirNeighborOffsetCount));
        int2 neighborPixel = int2(pixel) + int2(round(offset * radius));
        if (all(neighborPixel == int2(pixel))) {
            continue;
        }
        if (any(neighborPixel < 0) || any(neighborPixel >= int2(dimensions))) {
            continue;
        }
        uint neighborIndex =
            reservoirIndex(uint2(neighborPixel), dimensions);
        PathReservoir neighbor = ResampleReservoirs[neighborIndex];
        InterlockedAdd(Diagnostics[DiagnosticSpatialConsidered], 1u);
        if (!reservoirSurfaceCompatible(neighbor, surface, depth,
                                        ReservoirSpatialDepthTolerance,
                                        ReservoirSpatialNormalTolerance,
                                        -1.0)) {
            continue;
        }
        float3 shiftedTarget;
        float jacobian;
        if (!shiftReservoir(neighbor, surface, SceneInstanceMask,
                            shiftedTarget, jacobian)) {
            continue;
        }
        InterlockedAdd(Diagnostics[DiagnosticSpatialAccepted], 1u);
        float acceptance = sampleStream(pixel, SampleIndex + tap,
                                        ReservoirSpatialStream).x;
        if (resampleReservoir(current, neighbor, acceptance, shiftedTarget,
                              neighbor.weightSum * neighbor.m * jacobian,
                              neighbor.m)) {
            selectedTarget = shiftedTarget;
            selectedEntry = int(acceptedCount);
            selectedSourceTarget = neighbor.targetFunction;
        }
        acceptedIndex[acceptedCount] = neighborIndex;
        acceptedM[acceptedCount] = neighbor.m;
        ++acceptedCount;
    }

    /*
     * Evaluating the surviving sample in each contributing neighbour's domain
     * needs the reverse shift, which is the step that is easy to omit and
     * impossible to spot afterwards: without it the weight is divided among
     * fewer proposers than really competed and the image comes out bright.
     */
    float piSum = reservoirLuminance(selectedTarget) * ownM;
    for (uint entry = 0u; entry < acceptedCount; ++entry) {
        /*
         * The domain the sample came from is counted from what the sample was
         * worth there, not from a reverse shift. A reverse shift that fails
         * numerically would otherwise drop its own source out of the
         * denominator, and since every accepted neighbour has already added to
         * the numerator the weight inflates once per failure -- which is why
         * instability grew with neighbour count rather than falling.
         */
        if (int(entry) == selectedEntry) {
            piSum += reservoirLuminance(selectedSourceTarget) *
                acceptedM[entry];
            continue;
        }
        PathReservoir neighbor = ResampleReservoirs[acceptedIndex[entry]];
        SurfaceData neighborSurface;
        float neighborDepth;
        uint2 neighborPosition = uint2(
            acceptedIndex[entry] % dimensions.x,
            acceptedIndex[entry] / dimensions.x);
        if (!loadResamplingSurface(neighborPosition, neighbor,
                                   neighborSurface, neighborDepth)) {
            continue;
        }
        float3 reverseTarget;
        float reverseJacobian;
        if (shiftReservoir(current, neighborSurface, SceneInstanceMask,
                           reverseTarget, reverseJacobian)) {
            piSum += reservoirLuminance(reverseTarget) * acceptedM[entry];
        }
    }

    float predictedEnergy = 0.0;
    float predictedWeight = 0.0;
    if (reservoirValid(centre)) {
        predictedEnergy += reservoirLuminance(resolvedRadiance(centre)) * ownM;
        predictedWeight += ownM;
    }
    for (uint probe = 0u; probe < acceptedCount; ++probe) {
        PathReservoir neighbour = ResampleReservoirs[acceptedIndex[probe]];
        predictedEnergy +=
            reservoirLuminance(resolvedRadiance(neighbour)) * acceptedM[probe];
        predictedWeight += acceptedM[probe];
    }
    finalizeResampling(current, reservoirLuminance(selectedTarget),
                       piSum * reservoirLuminance(selectedTarget));
    if (predictedWeight > 0.0) {
        InterlockedAdd(Diagnostics[DiagnosticSpatialPredicted],
                       quantizeEnergy((predictedEnergy /
                                       predictedWeight).xxx));
        InterlockedAdd(Diagnostics[DiagnosticSpatialActual],
                       quantizeEnergy(resolvedRadiance(current)));
    }
    current.m = min(current.m, float(ReservoirTemporalHistory));
    /*
     * Reject a firefly before it is stored, not only before it is shown.
     *
     * Final shading already replaces an outlier with this pixel's preserved
     * sample, which fixes the pixel on screen. It does nothing about the
     * reservoir, which is still written here and becomes both next frame's
     * history and its neighbours' spatial candidate. The contamination
     * therefore keeps spreading through the grid while the image looks clean,
     * and surfaces again whenever the filter happens not to fire -- which is
     * why adding neighbours made the tail explode while the mean stayed right.
     *
     * The reference is the preserved canonical samples, which are noisy but
     * unresampled and so cannot themselves be contaminated by reuse.
     */
    if (ReservoirFireflyThreshold > 0.0 && reservoirValid(current)) {
        float localCanonical = 0.0;
        uint localCount = 0u;
        for (int fy = -2; fy <= 2; ++fy) {
            for (int fx = -2; fx <= 2; ++fx) {
                int2 probe = int2(pixel) + int2(fx, fy);
                if (any(probe < 0) || any(probe >= int2(dimensions))) {
                    continue;
                }
                PathReservoir fresh = PreservedReservoirs[
                    reservoirIndex(uint2(probe), dimensions)];
                if (reservoirValid(fresh)) {
                    localCanonical +=
                        reservoirLuminance(resolvedRadiance(fresh));
                    ++localCount;
                }
            }
        }
        if (localCount > 0u) {
            localCanonical /= float(localCount);
            /* Held to a looser bound than the one final shading applies.
             * This rejection also stops the sample propagating, so a
             * legitimately bright reservoir caught here is not merely hidden
             * for a frame but prevented from reaching its neighbours, and
             * during a lighting change that is the difference between
             * recovering promptly and not. */
            if (reservoirLuminance(resolvedRadiance(current)) >
                ReservoirFireflyStoreThreshold * localCanonical +
                    ReservoirFireflyFloor) {
                PathReservoir preserved = PreservedReservoirs[index];
                if (reservoirValid(preserved)) {
                    current = preserved;
                }
            }
        }
    }
    CurrentReservoirs[index] = current;

    /* The duplication map reads this next frame to find neighbourhoods that
     * have filled with descendants of one path. */
    SampleAncestry[index] = current.ancestry;
}

/*
 * The duplication map itself: how many neighbours carry this pixel's ancestry.
 * Counted over the same window the reference uses, and read by temporal reuse
 * on the following frame.
 */
[shader("raygeneration")]
void ComputeDuplicationMap()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    if (IndirectMode != IndirectModeRestirPt ||
        ReservoirHistoryReduction <= 0.0) {
        DuplicationMap[reservoirIndex(pixel, dimensions)] = 0u;
        return;
    }
    uint own = SampleAncestry[reservoirIndex(pixel, dimensions)];
    if (own == 0u) {
        DuplicationMap[reservoirIndex(pixel, dimensions)] = 0u;
        return;
    }
    uint count = 0u;
    for (int dy = -ReservoirDuplicationRadius;
         dy <= ReservoirDuplicationRadius; ++dy) {
        for (int dx = -ReservoirDuplicationRadius;
             dx <= ReservoirDuplicationRadius; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            int2 neighbor = int2(pixel) + int2(dx, dy);
            if (any(neighbor < 0) || any(neighbor >= int2(dimensions))) {
                continue;
            }
            if (SampleAncestry[reservoirIndex(uint2(neighbor), dimensions)] == own) {
                ++count;
            }
        }
    }
    DuplicationMap[reservoirIndex(pixel, dimensions)] = min(count, 255u);
    InterlockedAdd(Diagnostics[DiagnosticDuplicationSum], min(count, 255u));
    if (count > 0u) {
        InterlockedAdd(Diagnostics[DiagnosticDuplicationNonZero], 1u);
    }
    /* Ancestry encodes the pixel that generated the canonical sample, so a
     * mismatch against this pixel means the sample was inherited. */
    if ((own >> 8u) != reservoirIndex(pixel, dimensions)) {
        InterlockedAdd(Diagnostics[DiagnosticAncestryForeign], 1u);
    }
}

/*
 * RR's input in the units RR is given it. The host keeps the scale low enough
 * for the brightest emitter to fit; the bound only catches a resampling
 * outlier that the multiply would otherwise carry to infinity. A comparison
 * rather than min() so that NaN reaches RR exactly as it did unscaled, and a
 * scale of one leaves the input untouched.
 */
float4 scaleReconstructionInput(float4 radiance)
{
    if (ReconstructionInputScale == 1.0) {
        return radiance;
    }
    float3 scaled = radiance.rgb * ReconstructionInputScale;
    radiance.rgb = select(scaled > HalfFloatMaximum, HalfFloatMaximum, scaled);
    return radiance;
}

[shader("raygeneration")]
void ReconstructIndirect()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    bool indirectOnly = RadianceChannel == RadianceChannelIndirect;
    bool roughOnly = RadianceChannel == RadianceChannelRoughSpecular;
    bool includeIndirect =
        RadianceChannel == RadianceChannelCombined || indirectOnly;
    bool includeRoughSpecular =
        RadianceChannel == RadianceChannelCombined || roughOnly;
    /* Isolate the final secondary-diffuse contribution at composition time.
     * Direct lighting is still evaluated so this diagnostic changes neither
     * the GI proposal stream nor any RR guide. Early returns remain black. */
    if (indirectOnly || roughOnly) {
        NoisyRadiance[pixel] = float4(0.0, 0.0, 0.0, 1.0);
    }
    float4 centerAlbedo = DiffuseAlbedo[pixel];
    if (centerAlbedo.a <= 0.0) {
        IndirectFiltered[pixel] = 0.0;
        NoisyRadiance[pixel] = scaleReconstructionInput(NoisyRadiance[pixel]);
        return;
    }
    IndirectSignal integratedSignal = loadIndirectSignal(
        currentIndirectHistorySlot(), int2(pixel));
    float3 filteredIncident = decodeIndirectSignalColor(integratedSignal);
    if (IndirectMode == IndirectModeRestirPt) {
        /*
         * ReSTIR decides which single path the DIFFUSE estimate resolves to.
         * It must not also become the directional signal: that signal is a
         * spherical-harmonic irradiance field which rough specular is
         * reconstructed from, and encoding one path's direction into it turns a
         * smooth field into a delta spike. Reconstructing specular off a spike
         * is how a surface acquires a blown-out patch that the averaged signal
         * never had. The traced paths are still accumulated for that field, so
         * only the diffuse term is replaced here.
         */
        uint resolvedIndex = reservoirIndex(pixel, dimensions);
        PathReservoir resolved = CurrentReservoirs[resolvedIndex];
        /*
         * Stochastic decorrelation, as a one-sample MIS between the resampled
         * reservoir and the preserved initial one. The probability is scaled by
         * how long this pixel has held the same sample without a fresh one
         * winning: a reservoir that keeps refreshing is already independent
         * enough, and only a stale one needs replacing.
         */
        InterlockedAdd(Diagnostics[DiagnosticShaded], 1u);
        InterlockedAdd(Diagnostics[DiagnosticAgeSum],
                       min(resolved.age, 255u));
        if (ReservoirDecorrelation > 0.0) {
            float stagnancy = saturate(float(resolved.age) /
                max(float(ReservoirTemporalHistory), 1.0));
            float probability = saturate(ReservoirDecorrelation * stagnancy);
            float draw = sampleStream(pixel, SampleIndex,
                                      ReservoirDecorrelationStream).x;
            if (draw < probability) {
                PathReservoir preserved = PreservedReservoirs[resolvedIndex];
                if (reservoirValid(preserved)) {
                    resolved = preserved;
                    InterlockedAdd(Diagnostics[DiagnosticDecorrelated], 1u);
                }
            }
        }
        /*
         * Firefly replacement. A reservoir whose resolved radiance is far above
         * what the fresh samples around it say is plausible is a resampling
         * outlier: one improbable path given an enormous contribution weight,
         * which spatial reuse then copies into its neighbours as a bright patch.
         * Compare against the local mean of the unresampled canonical samples,
         * which are noisy but unbiased, and replace with this pixel's own
         * preserved sample when the resolved value exceeds it by more than the
         * filter permits. That is the reference's firefly filter with the
         * decorrelation slot as the replacement, and it removes a firefly in the
         * frame it appears rather than letting it be smeared for twenty.
         */
        if (ReservoirFireflyThreshold > 0.0) {
            float localCanonical = 0.0;
            uint localCount = 0u;
            for (int fy = -2; fy <= 2; ++fy) {
                for (int fx = -2; fx <= 2; ++fx) {
                    int2 neighbour = int2(pixel) + int2(fx, fy);
                    if (any(neighbour < 0) ||
                        any(neighbour >= int2(dimensions))) {
                        continue;
                    }
                    PathReservoir sample = PreservedReservoirs[
                        reservoirIndex(uint2(neighbour), dimensions)];
                    if (reservoirValid(sample)) {
                        localCanonical +=
                            reservoirLuminance(resolvedRadiance(sample));
                        ++localCount;
                    }
                }
            }
            if (localCount > 0u) {
                localCanonical /= float(localCount);
                float resolvedLuminance =
                    reservoirLuminance(resolvedRadiance(resolved));
                if (resolvedLuminance >
                    ReservoirFireflyThreshold * localCanonical +
                        ReservoirFireflyFloor) {
                    PathReservoir preserved =
                        PreservedReservoirs[resolvedIndex];
                    if (reservoirValid(preserved)) {
                        resolved = preserved;
                        InterlockedAdd(
                            Diagnostics[DiagnosticFireflyReplaced], 1u);
                    }
                }
            }
        }
        float3 contribution = resolvedRadiance(resolved);
        float3 shadingNormal = normalize(ShadingNormal[pixel].xyz);
        float3 toReconnection = resolved.rcVertexLength == 0u ?
            normalize(resolved.worldNormal) :
            normalize(resolved.translatedWorldPosition -
                      resolved.primaryPosition);
        float resolveCosine = saturate(dot(shadingNormal, toReconnection));
        filteredIncident = resolveCosine > 0.05 ?
            contribution / resolveCosine : 0.0;
        if (any(isnan(filteredIncident)) || any(isinf(filteredIncident))) {
            filteredIncident = 0.0;
        }
    }
    IndirectSignal specularSignal = integratedSignal;
    bool hasSpecularSignal = true;
    if (any(isnan(filteredIncident)) || any(isinf(filteredIncident))) {
        filteredIncident = 0.0;
    }
    filteredIncident = max(filteredIncident, 0.0);
    IndirectFiltered[pixel] = float4(filteredIncident, 1.0);
    float3 reconstructed = filteredIncident * centerAlbedo.rgb;
    float reconstructedLuminance = luminance(reconstructed);
    if (RadianceClamp > 0.0 &&
        reconstructedLuminance > RadianceClamp) {
        reconstructed *= RadianceClamp / reconstructedLuminance;
    }
    float3 roughSpecular = 0.0;
    if (hasSpecularSignal && includeRoughSpecular) {
        float4 packedShadingNormal = ShadingNormal[pixel];
        float3 primaryPosition = giPrimaryWorldPosition(
            pixel, dimensions, LinearDepth[pixel]);
        roughSpecular = reconstructRoughSpecular(
            specularSignal, primaryPosition,
            normalize(packedShadingNormal.xyz),
            saturate(packedShadingNormal.w),
            unpackSurfaceF0(SurfaceParameters[pixel]));
        if (any(isnan(roughSpecular)) || any(isinf(roughSpecular))) {
            roughSpecular = 0.0;
        }
        roughSpecular = max(roughSpecular, 0.0);
        float roughLuminance = luminance(roughSpecular);
        if (RadianceClamp > 0.0 && roughLuminance > RadianceClamp) {
            roughSpecular *= RadianceClamp / roughLuminance;
        }
    }
    float4 noisy = NoisyRadiance[pixel];
    if (includeIndirect) {
        noisy.rgb = indirectOnly ? reconstructed :
            noisy.rgb + reconstructed;
    }
    if (includeRoughSpecular) {
        noisy.rgb += roughSpecular;
    }
    NoisyRadiance[pixel] = scaleReconstructionInput(noisy);
}

float exposureHistogramLuminance(uint index)
{
    float minimumLog = log2(ExposureMinimumLuminance);
    float maximumLog = log2(ExposureMaximumLuminance);
    float position = (float(min(index, ExposureHistogramBinCount - 1u)) +
                      0.5) / float(ExposureHistogramBinCount);
    return exp2(lerp(minimumLog, maximumLog, position));
}

/* A fixed exposure cannot show both AB3D2's authored light panels and the
 * indirect energy they carry into an otherwise black corridor. This
 * project-owned metering stage builds a sparse luminance histogram in one
 * thread, gives the central region a modest second vote, and measures only
 * the 10th--98th percentile span. Exact black is not promoted into a grey
 * sample. A bounded target then adapts with elapsed-time exponential rates:
 * entering darkness is deliberately slower than reacting to a bright source. */
[shader("raygeneration")]
void CalculateAutomaticExposure()
{
    if (any(DispatchRaysIndex().xy != 0u)) {
        return;
    }
    uint2 dimensions = uint2(
        max(DispatchRaysDimensions().x, 1u),
        max(DispatchRaysDimensions().y, 1u));
    uint histogram[ExposureHistogramBinCount];
    [loop]
    for (uint bin = 0u; bin < ExposureHistogramBinCount; ++bin) {
        histogram[bin] = 0u;
    }
    uint totalWeight = 0u;
    float minimumLog = log2(ExposureMinimumLuminance);
    float maximumLog = log2(ExposureMaximumLuminance);
    [loop]
    for (uint sampleY = 0u; sampleY < ExposureSampleRows; ++sampleY) {
        [loop]
        for (uint sampleX = 0u; sampleX < ExposureSampleColumns; ++sampleX) {
            uint2 samplePixel = min(
                uint2((sampleX * 2u + 1u) * dimensions.x /
                          (ExposureSampleColumns * 2u),
                      (sampleY * 2u + 1u) * dimensions.y /
                          (ExposureSampleRows * 2u)),
                dimensions - 1u);
            if (DiffuseAlbedo[samplePixel].a <= 0.0) {
                continue;
            }
            float sampleLuminance = luminance(
                max(NoisyRadiance[samplePixel].rgb, 0.0));
            if (!(sampleLuminance > 0.0) || !isfinite(sampleLuminance)) {
                continue;
            }
            float sampleLog = log2(clamp(
                sampleLuminance, ExposureMinimumLuminance,
                ExposureMaximumLuminance));
            float normalized = saturate(
                (sampleLog - minimumLog) / (maximumLog - minimumLog));
            uint bin = min(uint(normalized *
                                float(ExposureHistogramBinCount)),
                           ExposureHistogramBinCount - 1u);
            float2 normalizedPosition =
                (float2(samplePixel) + 0.5) / float2(dimensions);
            float2 centered = normalizedPosition * 2.0 - 1.0;
            uint weight = dot(centered, centered) <= 0.25 ? 2u : 1u;
            histogram[bin] += weight;
            totalWeight += weight;
        }
    }
    uint lowRank = totalWeight * ExposureLowPercentileNumerator /
        ExposurePercentileDenominator;
    uint unclampedHighRank = totalWeight * ExposureHighPercentileNumerator /
        ExposurePercentileDenominator;
    uint highRank = min(totalWeight,
        max(lowRank + (totalWeight > 0u ? 1u : 0u), unclampedHighRank));
    uint cumulative = 0u;
    uint includedWeight = 0u;
    float weightedLogSum = 0.0;
    float lowPercentileLuminance = 0.0;
    float highPercentileLuminance = 0.0;
    [loop]
    for (uint meterBin = 0u; meterBin < ExposureHistogramBinCount;
         ++meterBin) {
        uint next = cumulative + histogram[meterBin];
        uint includedBegin = max(cumulative, lowRank);
        uint includedEnd = min(next, highRank);
        if (includedEnd > includedBegin) {
            uint included = includedEnd - includedBegin;
            float binLuminance = exposureHistogramLuminance(meterBin);
            if (includedWeight == 0u) {
                lowPercentileLuminance = binLuminance;
            }
            highPercentileLuminance = binLuminance;
            weightedLogSum += float(included) * log2(binLuminance);
            includedWeight += included;
        }
        cumulative = next;
    }
    float averageLuminance = includedWeight > 0u ?
        exp2(weightedLogSum / float(includedWeight)) : 0.0;
    float targetExposure = 1.0;
    if (includedWeight > 0u && averageLuminance > 0.0 &&
        isfinite(averageLuminance)) {
        targetExposure = clamp(
            ExposureMeteringKey / max(averageLuminance,
                                      ExposureMinimumLuminance),
            ExposureMinimum, ExposureMaximum);
    }
    float previousExposure = AutomaticExposure[0];
    float adaptedExposure = targetExposure;
    if (HistoryValid != 0u && isfinite(previousExposure) &&
        previousExposure > 0.0) {
        float elapsed = isfinite(ExposureDeltaSeconds) ?
            clamp(ExposureDeltaSeconds, 0.0,
                  ExposureMaximumDeltaSeconds) : 0.0;
        float rate = targetExposure > previousExposure ?
            ExposureDarkAdaptationRate : ExposureLightAdaptationRate;
        float adaptationWeight = 1.0 - exp(-rate * elapsed);
        adaptedExposure = lerp(previousExposure, targetExposure,
                               adaptationWeight);
    }
    AutomaticExposure[0] = adaptedExposure;
    Diagnostics[5] = asuint(targetExposure);
    Diagnostics[6] = asuint(adaptedExposure);
    Diagnostics[7] = asuint(averageLuminance);
    Diagnostics[8] = asuint(lowPercentileLuminance);
    Diagnostics[9] = asuint(highPercentileLuminance);
    Diagnostics[10] = includedWeight;
}

[shader("anyhit")]
void AnyHit(inout SurfacePayload payload,
            BuiltInTriangleIntersectionAttributes attributes)
{
    uint firstVertex = (InstanceID() + PrimitiveIndex()) * 3u;
    SceneVertex first = Vertices[firstVertex + 0u];
    /*
     * Additive geometry casts no shadow. `glDepthMask(GL_FALSE)` is what the
     * OpenGL path uses to say the same thing, and the source never had a depth
     * buffer to write: a glare or additive bitmap adds light and takes none
     * away. Visibility rays are exactly the rays that must not see it, and they
     * are the only rays that skip the closest-hit shader, so the ray's own
     * flags separate them from the surface rays that have to pass through the
     * layer and collect its emission.
     */
    if (first.primitive == WorldEffectPrimitive &&
        (RayFlags() & RAY_FLAG_SKIP_CLOSEST_HIT_SHADER) != 0u) {
        IgnoreHit();
    }
    SceneVertex second = Vertices[firstVertex + 1u];
    SceneVertex third = Vertices[firstVertex + 2u];
    float firstWeight = 1.0 - attributes.barycentrics.x -
        attributes.barycentrics.y;
    float2 textureCoordinate = first.textureCoordinate * firstWeight +
        second.textureCoordinate * attributes.barycentrics.x +
        third.textureCoordinate * attributes.barycentrics.y;
    SceneMaterial material = Materials[first.materialIndex];
    /* Artist-authored billboard/vector cutouts use the material manifest's
     * mask threshold. Opaque world BLAS skip this shader. */
    if (BaseColorAtlas.Load(int3(
            materialTexel(material, textureCoordinate,
                          first.textureWindowOrigin,
                          first.textureWindowExtent), 0)).a < 0.5) {
        IgnoreHit();
    }
}

[shader("miss")]
void SurfaceMiss(inout SurfacePayload payload)
{
    payload.hit = 0u;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.visible = 1u;
}

[shader("closesthit")]
void ClosestHit(inout SurfacePayload payload,
                BuiltInTriangleIntersectionAttributes attributes)
{
    payload.rayDistance = RayTCurrent();
    payload.barycentrics = attributes.barycentrics;
    payload.primitiveIndex = InstanceID() + PrimitiveIndex();
    payload.hit = 1u;
}
