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

/*
 * One direct-lighting reservoir per pixel, mirrored by `DxrLightReservoir` in
 * dxr_pipeline.h. Bitterli et al. 2020 store the surviving sample, its unbiased
 * contribution weight, and the number of candidates it stands for. The target pdf
 * is deliberately not stored: recomputing it from the sample at the reusing
 * pixel's own surface is what makes reuse across pixels exact.
 *
 * The owning surface travels with the reservoir so temporal reuse can be
 * validated without a second G-buffer history. For geometry that moves, the
 * comparison is against the surface's previous world position, which the motion
 * vector already needs.
 */
struct PackedLightReservoir
{
    uint emitterIndex;
    uint positionSample;
    float unbiasedWeight;
    uint sampleCount;
    float3 surfacePosition;
    uint surfaceNormal;
    float2 surfaceTextureCoordinate;
    uint surfaceGeometricNormal;
    uint surfaceMaterialIndex;
    uint surfaceTextureWindowOrigin;
    uint surfaceTextureWindowExtent;
};

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
    uint materialIndex;
    uint emitterIndex;
    uint primitive;
    uint textureWindowOrigin;
    uint textureWindowExtent;
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
Texture2D<float4> EmissiveAtlas : register(t7);
StructuredBuffer<EmissiveTriangle> Emitters : register(t8);
StructuredBuffer<SceneVertex> PreviousVertices : register(t9);
ByteAddressBuffer BlueNoiseSampler : register(t10);
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
RWStructuredBuffer<PackedLightReservoir> CurrentReservoirs : register(u10);
/* Read-only this frame, but declared as a UAV so both reservoir buffers can stay
 * in the unordered-access state and the frame needs no state transitions. */
RWStructuredBuffer<PackedLightReservoir> PreviousReservoirs : register(u11);
/* Primary-ray coverage occupies words 0--4. Automatic-exposure diagnostics
 * occupy words 5--10. Direct-light lobe coverage occupies words 11--12, word
 * 13 counts non-finite radiance or mandatory RR guides, and word 14 records
 * smooth-GGX path coverage. Hidden GPU smoke reads them after the dispatch;
 * none is used to shade the image. */
RWStructuredBuffer<uint> Diagnostics : register(u12);
RWStructuredBuffer<LightGridEntry> LightGrid : register(u13);
/* Current-frame demodulated diffuse-suffix lighting. RayGeneration or the
 * bounded continuation burst writes one genuine per-pixel path estimate. */
RWTexture2D<float4> IndirectRadiance : register(u14);
RWTexture2D<float4> IndirectFiltered : register(u17);
RWStructuredBuffer<float> AutomaticExposure : register(u18);
RWTexture2D<float2> IndirectChroma : register(u19);
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
    uint ReservoirSampleLimit;
    float RadianceClamp;
    float NdfTrim;
    uint SamplesPerPixel;
    float ExposureDeltaSeconds;
    uint ReservedIndirectReconstruction;
    uint RadianceChannel;
    uint RayReconstructionWeaponPoseTransition;
    uint IndirectSamplesPerPixel;
    float3 LightGridCenter;
    uint LightGridRebuild;
    uint RayReconstructionActive;
    uint DiagnosticGuideMask;
    float DiffuseGiScale;
    uint ValidationEnabled;
    uint SinglePrimaryDirectSurvivor;
    uint SingleContinuationLobe;
    uint ReservedDenseMatureContinuations;
    uint BoundedBurstContinuations;
    uint CompactLocalPrimary;
    uint ProxyPrimaryCandidates;
    uint ForceSpecularGuide;
};

cbuffer RayRootConstants : register(b1)
{
    /* Integer dimensions deliberately retain the shader-side reciprocal used
     * by the prior GetDimensions path, preserving deterministic sample math. */
    uint2 MaterialAtlasDimensions;
    uint InterleavedDeepDiffuse;
};

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
static const uint ReservoirTemporalSearchAttempts = 9u;
static const float ReservoirTemporalSearchRadius = 4.0;
/* NVIDIA's filter-free Ultra DI preset uses four ordinary spatial samples and
 * sixteen disocclusion-boost attempts. This renderer also uses ray-traced
 * correction and does not reuse final visibility, so those are the coherent
 * structural counts for its positive-history path. */
static const uint ReservoirSpatialSampleCount = 4u;
static const uint ReservoirDisocclusionSampleCount = 16u;
static const uint ReservoirSpatialDomainCount = 16u;
static const uint ReservoirInitialSampleCount = 1u;
static const uint ReservoirNaiveSampleThreshold = 2u;
static const uint ReservoirNeighborOffsetCount = 256u;
static const uint ReservoirNeighborOffsetMask =
    ReservoirNeighborOffsetCount - 1u;
static const float ReservoirSpatialRadius = 32.0;
static const float ReservoirDepthTolerance = 0.1;
static const float ReservoirNormalTolerance = 0.5;
static const float IndirectShBasisL0 = 0.282095;
static const float IndirectShBasisL1 = 0.488603;
static const uint ExposureSampleColumns = 32u;
static const uint ExposureSampleRows = 18u;
static const uint ExposureHistogramBinCount = 64u;
static const float ExposureMinimumLuminance = 0.00001;
static const float ExposureMaximumLuminance = 64.0;
static const uint ExposureLowPercentileNumerator = 10u;
static const uint ExposureHighPercentileNumerator = 98u;
static const uint ExposurePercentileDenominator = 100u;
static const float ExposureMeteringKey = 0.014;
static const float ExposureMinimum = 0.125;
static const float ExposureMaximum = 4096.0;
static const float ExposureDarkAdaptationRate = 1.0;
static const float ExposureLightAdaptationRate = 4.0;
static const float ExposureMaximumDeltaSeconds = 0.25;
static const uint ReservoirTemporalSearchStream = 0x10000u;
static const uint ReservoirSpatialStream = 0x10100u;
static const uint ReservoirAcceptanceStream = 0x10200u;
static const uint ReservoirSpatialAcceptanceStream = 0x10300u;
static const uint ReservoirEnvironmentStream = 0x10400u;
static const uint ReservoirBrdfStream = 0x10500u;
static const uint SecondaryDirectStream = 0x10600u;
static const uint DiffusePrimaryPolygonStream = 0x10700u;
static const uint DiffuseIndirectPolygonStream = 0x10800u;
static const uint SmoothSpecularDirectionStream = 0x10900u;
static const uint SmoothSpecularPolygonStream = 0x10a00u;
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
/* `rtx_light_candidates` is capped at 1024. Give every indirect surface a
 * disjoint candidate stream so changing path depth adds samples instead of
 * replaying the first secondary vertex's light choices. */
static const uint DiffusePolygonBounceStreamStride = 1024u;
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
 * no temporal or spatial filter consumes it. */
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
 * DLSS Ray Reconstruction stays the only spatial/temporal reconstructor. */
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
    float emissionScale = first.emissiveScale * firstWeight +
        second.emissiveScale * payload.barycentrics.x +
        third.emissiveScale * payload.barycentrics.y;
    surface.emission =
        sampleMaterialAtlasFilteredHardware(
            EmissiveAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            filter, inverseAtlasDimensions).rgb *
        material.emissiveFactor * emissionScale;
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
 * Scene motion is jitter-free for Streamline, but renderer-owned history is a
 * pixel-addressed copy of the previous jittered frame. The current primary ray
 * passes through `pixel + 0.5 + current jitter`; adding the motion reaches the
 * previous projection, and removing the previous jitter converts that
 * projection back to the previous buffer's pixel grid.
 */
float2 reprojectHistoryPixel(uint2 pixel, float2 motion)
{
    return float2(pixel) + 0.5 + motion +
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
    float2 lightUv = first.textureCoordinate * barycentrics.x +
        second.textureCoordinate * barycentrics.y +
        third.textureCoordinate * barycentrics.z;
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
    float lightCosine = abs(dot(lightNormal, -lightDirection));
    if (lightCosine <= 1.0e-6) {
        return evaluation;
    }
    float lightPdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    if (!(lightPdf > 0.0)) {
        return evaluation;
    }
    SceneMaterial lightMaterial = Materials[first.materialIndex];
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    float3 emittedRadiance =
        sampleMaterialAtlasHardware(
            EmissiveAtlas, lightMaterial, lightUv,
            first.textureWindowOrigin,
            first.textureWindowExtent).rgb *
        lightMaterial.emissiveFactor * lightEmissiveScale;
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
 * target, but it need not sample the emitter texture. The global alias weight
 * is area times the maximum authored emissive luminance, so multiplying its
 * categorical probability by inverse area yields a positive radiance proxy
 * up to one common normalization. RIS later divides by the selected proxy
 * target and applies the exact textured contribution, preserving the integral
 * while sparse/black texels correctly contribute zero. */
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
    float lightCosine = abs(dot(lightNormal, -lightDirection));
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
    float2 lightUv = first.textureCoordinate * barycentrics.x +
        second.textureCoordinate * barycentrics.y +
        third.textureCoordinate * barycentrics.z;
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
    float lightCosine = abs(dot(lightNormal, -lightDirection));
    if (lightCosine <= 1.0e-6) {
        return evaluation;
    }
    float sourcePdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    if (!(sourcePdf > 0.0) || isnan(sourcePdf) || isinf(sourcePdf)) {
        return evaluation;
    }
    SceneMaterial lightMaterial = Materials[first.materialIndex];
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    float3 emittedRadiance = sampleMaterialAtlasHardware(
        EmissiveAtlas, lightMaterial, lightUv,
        first.textureWindowOrigin,
        first.textureWindowExtent).rgb *
        lightMaterial.emissiveFactor * lightEmissiveScale;
    if (!any(emittedRadiance > 0.0)) {
        return evaluation;
    }
    evaluation.contribution = (diffuseReflectance(surface) / Pi) *
        emittedRadiance * receiverCosine;
    evaluation.diffuseContribution = evaluation.contribution;
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = sourcePdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = lightDistance;
    evaluation.valid = evaluation.targetPdf > 0.0;
    return evaluation;
}

float3 sampleDiffusePolygonLight(uint2 pixel, uint sampleIndex,
                                 uint stream, bool localProposal,
                                 SurfaceData surface)
{
    if (EmitterCount == 0u) {
        return 0.0;
    }
    /* Fresh RIS rejects black texels and poor geometric connections before the
     * one survivor spends a visibility ray. There is no temporal/spatial reuse
     * here: CandidateCount changes current-frame proposal quality only. */
    uint candidateCount = max(CandidateCount, 1u);
    EmitterSample selected = (EmitterSample)0;
    selected.emitterIndex = InvalidIndex;
    selected.valid = false;
    float weightSum = 0.0;
    int lightGridCell = localProposal ? lightGridCellForSurface(
        pixel, sampleIndex, surface.position) : -1;
    for (uint candidate = 0u; candidate < candidateCount; ++candidate) {
        float4 random = sampleStream(
            pixel, sampleIndex, stream + candidate);
        LightSelection lightSelection = selectEmitterForCell(
            random.x, lightGridCell);
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
        (float(candidateCount) * selectedEvaluation.targetPdf);
    return selectedEvaluation.contribution * inversePdf;
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
            pixel, sampleIndex, lightStream, true, reachedSurface);
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

float fakeSpecularWeight(float linearRoughness)
{
    return smoothstep(0.20, 0.30, linearRoughness);
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
    if (EmitterCount > 0u) {
        reachedRadiance += sampleDiffusePolygonLight(
            pixel, sampleIndex, SmoothSpecularPolygonStream, true,
            reachedSurface);
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
        LightSelection lightSelection = selectEmitterForCell(
            selection, lightGridCell);
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

bool loadPreviousReservoirAt(int2 previousPixel, uint2 dimensions,
                             SurfaceData surface,
                             out PackedLightReservoir previous)
{
    previous = (PackedLightReservoir)0;
    if (any(previousPixel < 0) ||
        any(previousPixel >= int2(dimensions))) {
        return false;
    }
    previous = PreviousReservoirs[
        uint(previousPixel.y) * dimensions.x + uint(previousPixel.x)];
    return previous.sampleCount > 0u &&
        previous.surfaceMaterialIndex == surface.materialIndex;
}

/* The central temporal lookup describes the same moving surface, so compare
 * against its known previous world position rather than assuming a static
 * world-space point. */
bool temporalReservoirMatches(PackedLightReservoir previous,
                              SurfaceData surface,
                              float3 previousPosition)
{
    float expectedDepth = max(dot(
        previousPosition - PreviousCameraPosition, PreviousCameraForward),
        RayEpsilon);
    float previousDepth = dot(
        previous.surfacePosition - PreviousCameraPosition,
        PreviousCameraForward);
    if (!(previousDepth > RayEpsilon) ||
        abs(previousDepth - expectedDepth) >
            ReservoirDepthTolerance * expectedDepth) {
        return false;
    }
    return dot(surface.shadingNormal,
               unpackOctahedralNormal(previous.surfaceNormal)) >=
            ReservoirNormalTolerance &&
        dot(surface.geometricNormal,
            unpackOctahedralNormal(previous.surfaceGeometricNormal)) >=
            ReservoirNormalTolerance;
}

bool loadCurrentReservoirAt(int2 currentPixel, uint2 dimensions,
                            SurfaceData surface,
                            out PackedLightReservoir current)
{
    current = (PackedLightReservoir)0;
    if (any(currentPixel < 0) || any(currentPixel >= int2(dimensions))) {
        return false;
    }
    current = CurrentReservoirs[
        uint(currentPixel.y) * dimensions.x + uint(currentPixel.x)];
    return current.sampleCount > 0u &&
        current.surfaceMaterialIndex == surface.materialIndex;
}

/* A current-frame spatial neighbor is a nearby point rather than the
 * identical point. Use view depth for scale-independent edge stopping,
 * together with material and both normal frames to prevent cross-surface light
 * leaking. */
bool currentSpatialReservoirMatches(PackedLightReservoir current,
                                    SurfaceData surface)
{
    float expectedDepth = max(dot(
        surface.position - CameraPosition, CameraForward),
        RayEpsilon);
    float neighborDepth = dot(
        current.surfacePosition - CameraPosition, CameraForward);
    if (!(neighborDepth > RayEpsilon) ||
        abs(neighborDepth - expectedDepth) >
            ReservoirDepthTolerance * expectedDepth) {
        return false;
    }
    return dot(surface.shadingNormal,
               unpackOctahedralNormal(current.surfaceNormal)) >=
            ReservoirNormalTolerance &&
        dot(surface.geometricNormal,
            unpackOctahedralNormal(current.surfaceGeometricNormal)) >=
            ReservoirNormalTolerance;
}

bool findTemporalReservoir(uint2 pixel, uint2 dimensions, uint sampleIndex,
                           SurfaceData surface, float3 previousPosition,
                           float2 motion, out int2 previousPixel)
{
    previousPixel = -1;
    if (HistoryValid == 0u || ReservoirSampleLimit == 0u ||
        any(abs(motion) > 65500.0)) {
        return false;
    }
    float2 reprojected = reprojectHistoryPixel(pixel, motion);
    int2 center = int2(floor(reprojected));
    for (uint attempt = 0u; attempt < ReservoirTemporalSearchAttempts;
         ++attempt) {
        int2 offset = 0;
        if (attempt > 0u) {
            float2 random = sampleStream(
                pixel, sampleIndex,
                ReservoirTemporalSearchStream + attempt).xy;
            offset = int2((random - 0.5) * ReservoirTemporalSearchRadius);
        }
        int2 candidatePixel = center + offset;
        PackedLightReservoir candidate;
        if (loadPreviousReservoirAt(candidatePixel, dimensions, surface,
                                    candidate) &&
            temporalReservoirMatches(candidate, surface, previousPosition)) {
            previousPixel = candidatePixel;
            return true;
        }
    }
    return false;
}

/* Reconstructs the previous owner surface needed by basic bias correction.
 * Its BSDF inputs are filtered from the immutable material atlas at the UV
 * carried by the reservoir; normals and world position are the values that
 * actually owned that previous reservoir. */
SurfaceData reservoirSurface(PackedLightReservoir reservoir)
{
    SurfaceData surface = (SurfaceData)0;
    surface.position = reservoir.surfacePosition;
    surface.shadingNormal =
        unpackOctahedralNormal(reservoir.surfaceNormal);
    surface.geometricNormal =
        unpackOctahedralNormal(reservoir.surfaceGeometricNormal);
    surface.textureCoordinate = reservoir.surfaceTextureCoordinate;
    surface.materialIndex = reservoir.surfaceMaterialIndex;
    surface.textureWindowOrigin = reservoir.surfaceTextureWindowOrigin;
    surface.textureWindowExtent = reservoir.surfaceTextureWindowExtent;
    SceneMaterial material = Materials[surface.materialIndex];
    float2 inverseAtlasDimensions = materialAtlasInverseDimensions();
    surface.baseColor = saturate(
        sampleMaterialAtlasLevelHardware(
            BaseColorAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            0u, inverseAtlasDimensions).rgb);
    surface.metalness = saturate(
        sampleMaterialAtlasLevelHardware(
            MetalnessAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            0u, inverseAtlasDimensions).r);
    surface.specularFactor = saturate(material.specularFactor);
    surface.roughness = clamp(
        sampleMaterialAtlasLevelHardware(
            RoughnessAtlas, material, surface.textureCoordinate,
            surface.textureWindowOrigin, surface.textureWindowExtent,
            0u, inverseAtlasDimensions).r, 0.045, 1.0);
    surface.emitterIndex = InvalidIndex;
    return surface;
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

/*
 * Reservoir-resampled direct lighting for the primary hit, from Bitterli,
 * Wyman, Pharr, Shirley, Lefohn, and Jarosz, "Spatiotemporal reservoir resampling
 * for real-time ray tracing with dynamic direct lighting", SIGGRAPH 2020.
 *
 * CandidateCount unshadowed local candidates, one analytic-environment
 * candidate, and one environment-overlap BRDF candidate are resampled into one
 * reservoir, then the reprojected previous reservoir is combined with weight
 * targetPdf * W * M re-evaluated at this surface. The normalization evaluates
 * the selected sample at both owning surfaces, which is the basic pairwise-MIS
 * correction required when those targets differ. Current-frame spatial reuse
 * and final visibility are deliberately deferred to SpatialShade.
 *
 * With no usable history this is a fresh balance-heuristic mixed estimator;
 * temporal reuse changes its effective sample count rather than its integral.
 */
float3 resampleDirectTemporal(uint2 pixel, uint2 dimensions,
                             uint sampleIndex,
                             SurfaceData surface, float3 viewDirection,
                             float3 previousPosition, float2 motion,
                             uint instanceMask, bool enableTemporalReuse,
                             out PackedLightReservoir stored)
{
    stored = (PackedLightReservoir)0;
    stored.emitterIndex = InvalidIndex;
    stored.surfacePosition = surface.position;
    stored.surfaceNormal = packOctahedralNormal(surface.shadingNormal);
    stored.surfaceTextureCoordinate = surface.textureCoordinate;
    stored.surfaceGeometricNormal =
        packOctahedralNormal(surface.geometricNormal);
    stored.surfaceMaterialIndex = surface.materialIndex;
    stored.surfaceTextureWindowOrigin = surface.textureWindowOrigin;
    stored.surfaceTextureWindowExtent = surface.textureWindowExtent;
    if (CandidateCount == 0u) {
        return 0.0;
    }

    EmitterSample selected = (EmitterSample)0;
    selected.emitterIndex = InvalidIndex;
    selected.positionSample = 0u;
    selected.valid = false;
    float weightSum = 0.0;
    uint localSampleCount = EmitterCount > 0u ? CandidateCount : 0u;
    const uint environmentSampleCount = 1u;
    const uint brdfSampleCount = 1u;
    uint initialSampleCount = localSampleCount + environmentSampleCount +
        brdfSampleCount;
    float inverseInitialSampleCount = 1.0 / float(initialSampleCount);
    float localStrategyWeight =
        float(localSampleCount) * inverseInitialSampleCount;
    float environmentStrategyWeight =
        float(environmentSampleCount) * inverseInitialSampleCount;
    float brdfStrategyWeight =
        float(brdfSampleCount) * inverseInitialSampleCount;
    int lightGridCell = lightGridCellForSurface(
        pixel, sampleIndex, surface.position);
    for (uint candidate = 0u; candidate < localSampleCount; ++candidate) {
        float3 stream;
        float acceptance;
        if (candidate == 0u) {
            uint dimension = PathDimensionsPerBounce * 0u;
            stream = float3(
                sampleBlueNoise(pixel, sampleIndex, dimension + 2u),
                sampleBlueNoise(pixel, sampleIndex, dimension + 3u),
                sampleBlueNoise(pixel, sampleIndex, dimension + 4u));
            acceptance = 0.0;
        } else {
            float4 hashed = sampleStream(pixel, sampleIndex, candidate);
            stream = hashed.xyz;
            acceptance = hashed.w;
        }
        /* RTXDI stratifies local-light selection across the initial candidate
         * count. The full set still covers the complete power distribution,
         * while duplicates and candidate-set clumping are reduced. */
        stream.x = (stream.x + float(candidate)) / float(CandidateCount);
        LightSelection lightSelection =
            selectEmitterForCell(stream.x, lightGridCell);
        EmitterSample candidateSample;
        candidateSample.emitterIndex = lightSelection.emitterIndex;
        candidateSample.positionSample = packPositionSample(stream.yz);
        candidateSample.valid = true;
        EmitterEvaluation evaluation =
            evaluateDirectSample(surface, viewDirection, candidateSample);
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
                                     acceptance, selected, weightSum);
    }

    /* NVIDIA's higher-quality initial pass places the infinite/environment and
     * BRDF strategies in the same reservoir as local lights. The project sky
     * is analytic rather than a texture, so its one environment candidate is
     * sampled directly in solid angle and stores a reusable world direction. */
    float4 environmentRandom = sampleStream(
        pixel, sampleIndex, ReservoirEnvironmentStream);
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
        pixel, sampleIndex, ReservoirBrdfStream);
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

    /* RTXDI's initial pass finalizes all direct-light candidates into one
     * current-frame proposal before temporal reuse. M therefore counts history
     * domains, not how many candidates were tested inside this proposal. This
     * also keeps CandidateCount from changing the meaning of the history cap. */
    weightSum *= inverseInitialSampleCount;
    uint sampleCount = ReservoirInitialSampleCount;
    /* NVIDIA's initial-visibility option tests only the RIS-selected sample,
     * then discards its identity and weight while retaining M when occluded.
     * Visibility inside every candidate weight is a different proposal and
     * causes unstable selection at shadow boundaries. */
    EmitterEvaluation initialEvaluation =
        evaluateDirectSample(surface, viewDirection, selected);
    if (!initialEvaluation.valid ||
        !traceDirectVisibility(surface, initialEvaluation, instanceMask)) {
        selected.valid = false;
        weightSum = 0.0;
    }
    uint currentCount = sampleCount;
    PackedLightReservoir previous = (PackedLightReservoir)0;
    uint previousCount = 0u;
    bool reusedPrevious = false;
    bool selectedPrevious = false;
    int2 temporalPixel;
    if (enableTemporalReuse && findTemporalReservoir(
            pixel, dimensions, sampleIndex, surface, previousPosition, motion,
            temporalPixel) &&
        loadPreviousReservoirAt(temporalPixel, dimensions, surface,
                                previous)) {
        previousCount = min(previous.sampleCount, ReservoirSampleLimit);
        reusedPrevious = previousCount > 0u;
        if (reusedPrevious) {
            EmitterSample previousSample;
            previousSample.emitterIndex = previous.emitterIndex;
            previousSample.positionSample = previous.positionSample;
            previousSample.valid = previous.unbiasedWeight > 0.0 &&
                directSampleIndexValid(previous.emitterIndex);
            EmitterEvaluation previousEvaluation =
                evaluateDirectSample(surface, viewDirection, previousSample);
            float previousWeight = previousEvaluation.valid ?
                previousEvaluation.targetPdf * previous.unbiasedWeight *
                    float(previousCount) : 0.0;
            float totalWeight = weightSum + previousWeight;
            float acceptance = sampleStream(
                pixel, sampleIndex, ReservoirAcceptanceStream).x;
            if (previousWeight > 0.0 &&
                acceptance * totalWeight < previousWeight) {
                selected = previousSample;
                selectedPrevious = true;
            }
            weightSum = totalWeight;
            sampleCount += previousCount;
        }
    }

    stored.sampleCount = sampleCount;
    if (!selected.valid) {
        return 0.0;
    }
    EmitterEvaluation finalEvaluation =
        evaluateDirectSample(surface, viewDirection, selected);
    if (!finalEvaluation.valid || !(finalEvaluation.targetPdf > 0.0)) {
        return 0.0;
    }
    float normalizationNumerator = finalEvaluation.targetPdf;
    float normalizationDenominator =
        finalEvaluation.targetPdf * float(currentCount);
    if (reusedPrevious) {
        SurfaceData previousSurface = reservoirSurface(previous);
        float3 toPreviousCamera =
            PreviousCameraPosition - previousSurface.position;
        float previousTarget = 0.0;
        if (dot(toPreviousCamera, toPreviousCamera) > 1.0e-8) {
            EmitterEvaluation evaluationAtPrevious =
                evaluatePreviousDirectSample(
                    previousSurface, normalize(toPreviousCamera), selected);
            if (evaluationAtPrevious.valid) {
                previousTarget = evaluationAtPrevious.targetPdf;
                /* RTXDI's Unbiased/Ultra path disables the temporal visibility
                 * shortcut: even a sample selected from the previous reservoir
                 * is tested at that owner with the current acceleration
                 * structure before it enters the normalization. Trusting the
                 * stored result is a faster-preset optimization that can retain
                 * stale visibility across moving geometry. */
                if (previousTarget > 0.0 &&
                    !traceDirectVisibility(previousSurface,
                                           evaluationAtPrevious,
                                           instanceMask)) {
                    previousTarget = 0.0;
                }
            }
        }
        normalizationDenominator += previousTarget * float(previousCount);
        if (selectedPrevious) {
            normalizationNumerator = previousTarget;
        }
    }
    float unbiasedWeight = normalizationNumerator > 0.0 &&
            normalizationDenominator > 0.0 ?
        weightSum * normalizationNumerator /
            (finalEvaluation.targetPdf * normalizationDenominator) : 0.0;
    if (!(unbiasedWeight > 0.0) || isnan(unbiasedWeight) ||
        isinf(unbiasedWeight)) {
        return 0.0;
    }
    stored.emitterIndex = selected.emitterIndex;
    stored.positionSample = selected.positionSample;
    stored.unbiasedWeight = unbiasedWeight;
    return enableTemporalReuse ? 0.0 :
        finalEvaluation.contribution * unbiasedWeight;
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
            indirectSampleCount = MaximumDepth >= 2u && DiffuseGiScale > 0.0 &&
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
    IndirectRadiance[pixel] = resolvedIndirectSignal.luminanceSH;
    IndirectChroma[pixel] = resolvedIndirectSignal.chroma;
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
        float3 incident = pathSample.radiance * diffuseScale;
        if (sampleOrdinal == 0u &&
            (DiagnosticGuideMask & 2u) != 0u) {
            DiffuseHitDistance[pixel] = pathSample.firstDistance;
        }
        if (any(isnan(incident)) || any(isinf(incident))) {
            continue;
        }
        IndirectSignal sampleSignal = indirectSignalFromRadiance(
            incident, pathSample.firstDirection);
        signalSum.luminanceSH += sampleSignal.luminanceSH;
        signalSum.chroma += sampleSignal.chroma;
    }

    IndirectSignal signal = scaleIndirectSignal(
        signalSum, rcp(float(sampleCount)));
    IndirectRadiance[pixel] = signal.luminanceSH;
    IndirectChroma[pixel] = signal.chroma;
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

/* Fresh indirect diffuse lighting remains in a dedicated current-frame channel
 * until final composition. This pass remodulates the raw incident estimate at
 * the primary receiver; DLSS-RR owns all temporal/spatial reconstruction. */
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
        return;
    }
    /* Publish only the current frame's genuine estimator. Directional SH is
     * a compact per-pixel storage format here, not a temporal or spatial
     * reconstruction stage. */
    IndirectSignal rawSignal;
    rawSignal.luminanceSH = IndirectRadiance[pixel];
    rawSignal.chroma = IndirectChroma[pixel];
    float3 filteredIncident = decodeIndirectSignalColor(rawSignal);
    IndirectSignal specularSignal = rawSignal;
    bool hasSpecularSignal = true;
    if (any(isnan(filteredIncident)) || any(isinf(filteredIncident))) {
        filteredIncident = 0.0;
    }
    filteredIncident = max(filteredIncident, 0.0);
    IndirectFiltered[pixel] = float4(filteredIncident, 1.0);
    float3 reconstructed = filteredIncident * centerAlbedo.rgb *
        DiffuseGiScale;
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
    NoisyRadiance[pixel] = noisy;
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

/*
 * The second RTXDI-style stage consumes the completed temporal field, reuses
 * current-frame neighbors, performs basic pairwise-MIS normalization, traces
 * final visibility, and only then publishes the history reservoir. Keeping
 * this separate from RayGeneration means every spatial lookup sees this
 * frame's temporal result instead of a prior-frame approximation.
 */
[shader("raygeneration")]
void SpatialShade()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint reservoirIndex = pixel.y * dimensions.x + pixel.x;
    PackedLightReservoir center = CurrentReservoirs[reservoirIndex];
    PackedLightReservoir output = center;
    output.emitterIndex = InvalidIndex;
    output.positionSample = 0u;
    output.unbiasedWeight = 0.0;
    if (center.sampleCount == 0u) {
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }

    SurfaceData centerSurface = reservoirSurface(center);
    float3 toCamera = CameraPosition - centerSurface.position;
    if (dot(toCamera, toCamera) <= 1.0e-8) {
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }
    float3 viewDirection = normalize(toCamera);
    EmitterSample selected;
    selected.emitterIndex = center.emitterIndex;
    selected.positionSample = center.positionSample;
    selected.valid = center.unbiasedWeight > 0.0 &&
        directSampleIndexValid(center.emitterIndex);
    EmitterEvaluation centerEvaluation =
        evaluateDirectSample(centerSurface, viewDirection, selected);
    float weightSum = centerEvaluation.valid ?
        centerEvaluation.targetPdf * center.unbiasedWeight *
            float(center.sampleCount) : 0.0;
    uint totalCount = center.sampleCount;
    int selectedDomain = -1;
    int2 neighborPixels[ReservoirSpatialDomainCount];
    uint neighborCounts[ReservoirSpatialDomainCount];
    uint neighborDomainCount = 0u;
    uint attemptCount = ReservoirSampleLimit == 0u ? 0u :
        (center.sampleCount <= ReservoirInitialSampleCount ?
            ReservoirDisocclusionSampleCount : ReservoirSpatialSampleCount);
    uint spatialSampleIndex = SampleIndex * SamplesPerPixel;
    uint neighborStart = min(uint(sampleStream(
        pixel, spatialSampleIndex, ReservoirSpatialStream).x *
        float(ReservoirNeighborOffsetCount)), ReservoirNeighborOffsetMask);

    for (uint attempt = 0u; attempt < attemptCount; ++attempt) {
        float2 offset = reservoirNeighborOffset(neighborStart + attempt);
        int2 neighborPixel = int2(pixel) +
            int2(round(ReservoirSpatialRadius * offset));
        if (all(neighborPixel == int2(pixel))) {
            continue;
        }
        bool duplicate = false;
        for (uint neighborIndex = 0u;
             neighborIndex < neighborDomainCount; ++neighborIndex) {
            duplicate = duplicate ||
                all(neighborPixel == neighborPixels[neighborIndex]);
        }
        if (duplicate) {
            continue;
        }

        PackedLightReservoir neighbor;
        if (!loadCurrentReservoirAt(neighborPixel, dimensions, centerSurface,
                                    neighbor) ||
            !currentSpatialReservoirMatches(neighbor, centerSurface)) {
            continue;
        }
        /* Match RTXDI's default discountNaiveSamples behavior: do not spread a
         * valid sample that has no temporal history into surrounding pixels. */
        if (neighbor.unbiasedWeight > 0.0 &&
            directSampleIndexValid(neighbor.emitterIndex) &&
            neighbor.sampleCount <= ReservoirNaiveSampleThreshold) {
            continue;
        }
        uint neighborCount = neighbor.sampleCount;
        EmitterSample neighborSample;
        neighborSample.emitterIndex = neighbor.emitterIndex;
        neighborSample.positionSample = neighbor.positionSample;
        neighborSample.valid = neighbor.unbiasedWeight > 0.0 &&
            directSampleIndexValid(neighbor.emitterIndex);
        EmitterEvaluation neighborEvaluation =
            evaluateDirectSample(centerSurface, viewDirection,
                                 neighborSample);
        float neighborWeight = neighborEvaluation.valid ?
            neighborEvaluation.targetPdf * neighbor.unbiasedWeight *
                float(neighborCount) : 0.0;
        float combinedWeight = weightSum + neighborWeight;
        float acceptance = sampleStream(
            pixel, spatialSampleIndex,
            ReservoirSpatialAcceptanceStream + attempt).x;
        if (neighborWeight > 0.0 &&
            acceptance * combinedWeight < neighborWeight) {
            selected = neighborSample;
            selectedDomain = int(neighborDomainCount);
        }
        weightSum = combinedWeight;
        totalCount += neighborCount;
        neighborPixels[neighborDomainCount] = neighborPixel;
        neighborCounts[neighborDomainCount] = neighborCount;
        neighborDomainCount += 1u;
    }

    output.sampleCount = totalCount;
    if (!selected.valid) {
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }
    EmitterEvaluation finalEvaluation =
        evaluateDirectSample(centerSurface, viewDirection, selected);
    if (!finalEvaluation.valid || !(finalEvaluation.targetPdf > 0.0)) {
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }

    float normalizationNumerator = finalEvaluation.targetPdf;
    float normalizationDenominator =
        finalEvaluation.targetPdf * float(center.sampleCount);
    for (uint neighborIndex = 0u;
         neighborIndex < neighborDomainCount; ++neighborIndex) {
        PackedLightReservoir neighbor = CurrentReservoirs[
            uint(neighborPixels[neighborIndex].y) * dimensions.x +
            uint(neighborPixels[neighborIndex].x)];
        SurfaceData neighborSurface = reservoirSurface(neighbor);
        float3 toNeighborCamera = CameraPosition - neighborSurface.position;
        float neighborTarget = 0.0;
        if (dot(toNeighborCamera, toNeighborCamera) > 1.0e-8) {
            EmitterEvaluation evaluationAtNeighbor = evaluateDirectSample(
                neighborSurface, normalize(toNeighborCamera), selected);
            /* NVIDIA's Unbiased/Ultra spatial mode includes conservative
             * visibility in the pairwise target. Basic mode assumes every
             * accepted neighbor can see the selected sample, which leaves
             * isolated high-weight samples at occlusion boundaries. */
            if (evaluationAtNeighbor.valid &&
                traceDirectVisibility(neighborSurface,
                                      evaluationAtNeighbor,
                                      SceneInstanceMask)) {
                neighborTarget = evaluationAtNeighbor.targetPdf;
            }
        }
        normalizationDenominator +=
            neighborTarget * float(neighborCounts[neighborIndex]);
        if (selectedDomain == int(neighborIndex)) {
            normalizationNumerator = neighborTarget;
        }
    }
    float unbiasedWeight = normalizationNumerator > 0.0 &&
            normalizationDenominator > 0.0 ?
        weightSum * normalizationNumerator /
            (finalEvaluation.targetPdf * normalizationDenominator) : 0.0;
    if (!(unbiasedWeight > 0.0) || isnan(unbiasedWeight) ||
        isinf(unbiasedWeight)) {
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }

    output.emitterIndex = selected.emitterIndex;
    output.positionSample = selected.positionSample;
    output.unbiasedWeight = unbiasedWeight;
    if (!traceDirectVisibility(centerSurface, finalEvaluation,
                               SceneInstanceMask)) {
        output.emitterIndex = InvalidIndex;
        output.positionSample = 0u;
        output.unbiasedWeight = 0.0;
        PreviousReservoirs[reservoirIndex] = output;
        return;
    }
    PreviousReservoirs[reservoirIndex] = output;

    float3 directRadiance = finalEvaluation.contribution * unbiasedWeight;
    if (any(isnan(directRadiance)) || any(isinf(directRadiance))) {
        directRadiance = 0.0;
    }
    float directLuminance = luminance(directRadiance);
    if (RadianceClamp > 0.0 && directLuminance > RadianceClamp) {
        directRadiance *= RadianceClamp / directLuminance;
    }
    float4 noisy = NoisyRadiance[pixel];
    noisy.rgb += directRadiance / float(SamplesPerPixel);
    NoisyRadiance[pixel] = noisy;
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
