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
    /*
     * The source Gouraud shade response at this vertex, one being the brightest
     * source row. It scales authored emission, and on world geometry it is also
     * the ambience secondary rays gather through `authoredAmbientRadiance`.
     * Direct incident lighting is traced either way.
     * newanims.s:brightanim moves it for zones whose CurrentPointBrights words
     * carry an Anim_BrightTable index, which is how an authored emissive panel
     * pulses.
     */
    float emissiveScale;
};

struct SceneMaterial
{
    uint atlasX;
    uint atlasY;
    uint width;
    uint height;
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
    /* The interpolated source Gouraud shade response, before `emissiveFactor`
     * turns it into emission. Only world flats and strips carry a real one. */
    float authoredShade;
    uint materialIndex;
    uint emitterIndex;
    uint primitive;
    uint textureWindowOrigin;
    uint textureWindowExtent;
};

struct BsdfEvaluation
{
    float3 value;
    float pdf;
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

RWTexture2D<float4> NoisyRadiance : register(u0);
RWTexture2D<float4> DiffuseAlbedo : register(u1);
RWTexture2D<float4> SpecularAlbedo : register(u2);
RWTexture2D<float4> ShadingNormal : register(u3);
RWTexture2D<float> LinearRoughness : register(u4);
RWTexture2D<float> LinearDepth : register(u5);
RWTexture2D<float2> SceneMotion : register(u6);
RWTexture2D<float> SpecularHitDistance : register(u7);
RWTexture2D<float> DiffuseHitDistance : register(u8);
RWTexture2D<float> SpecularHitDistanceHistory : register(u9);
RWStructuredBuffer<PackedLightReservoir> CurrentReservoirs : register(u10);
/* Read-only this frame, but declared as a UAV so both reservoir buffers can stay
 * in the unordered-access state and the frame needs no state transitions. */
RWStructuredBuffer<PackedLightReservoir> PreviousReservoirs : register(u11);
/* Primary-ray view-weapon coverage and fresh-radiance checksum. The hidden GPU
 * smoke reads this after the dispatch; it is never used to shade the image. */
RWStructuredBuffer<uint> Diagnostics : register(u12);
RWStructuredBuffer<LightGridEntry> LightGrid : register(u13);

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
    uint AtlasWidth;
    uint AtlasHeight;
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
};

static const uint BlueNoiseSampleCount = 256u;
static const uint BlueNoiseDimensionCount = 256u;
static const uint BlueNoiseTileWidth = 128u;
static const uint BlueNoiseOptimizedDimensions = 8u;
static const uint BlueNoiseSobolOffset = 0u;
static const uint BlueNoiseScramblingOffset = 65536u;
static const uint BlueNoiseRankingOffset = 196608u;
static const uint PathDimensionsPerBounce = 8u;
/*
 * The primary lobe choice is stochastic, so only some frames produce a
 * specular continuation ray for a given pixel. Writing zero on the other
 * frames makes Ray Reconstruction resize its specular filter footprint per
 * pixel per frame. The guide therefore carries a reprojected running estimate
 * that only ever moves towards a real measurement. This filters a guide, not
 * radiance: the path-traced estimate in NoisyRadiance is untouched.
 */
/*
 * Radiance the source's authored zone lighting contributes as ambience, in
 * multiples of the outgoing radiance a surface has when the source rasterizer
 * draws it at its brightest shade row.
 *
 * `hires.s:goursides` and its wall equivalents shade a texel by walking the
 * palette shade rows, and row zero draws the texel at its own display value.
 * Whatever else the source's authored lighting is, that fixes what "fully lit"
 * means in it: the surface leaves exactly its albedo. A Lambertian surface
 * leaves `albedo * E / Pi`, so row zero corresponds to `E = Pi`, and
 * `baseColor * emissiveScale` is the authored lighting restated as outgoing
 * radiance in this renderer's units. A scale of one therefore reproduces the
 * source's own brightness rather than picking a level, which is why nothing
 * here is fitted.
 *
 * Primary rays ignore it, so a directly visible surface only ever receives this
 * through a bounce, at roughly the product of the two albedos - about a tenth of
 * the authored level for typical AB3D2 art. A room lit by the emissive floor
 * panel at offset 0x0101 sits an order of magnitude above that - its 200
 * radiance reaches the surrounding geometry at around one - so this reads as a
 * fill: it lifts what the path tracer leaves black without competing with the
 * traced lighting. In a zone with no emissive panel at all it becomes the only
 * thing in the room, which is the point.
 */
static const float AuthoredAmbientScale = 1.0;
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
static const uint ReservoirTemporalSearchStream = 0x10000u;
static const uint ReservoirSpatialStream = 0x10100u;
static const uint ReservoirAcceptanceStream = 0x10200u;
static const uint ReservoirSpatialAcceptanceStream = 0x10300u;
static const uint ReservoirEnvironmentStream = 0x10400u;
static const uint ReservoirBrdfStream = 0x10500u;
static const uint SecondaryDirectStream = 0x10600u;
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

/*
 * The source's authored zone lighting, restated as radiance this surface leaves
 * in every direction. See `AuthoredAmbientScale` for where the unit comes from.
 *
 * Only `DxrScenePrimitive::world` carries an authored shade: the flats and wall
 * strips whose `CurrentPointBrights` word the source rasterizer shades from.
 * Billboards, vector models and the view weapon all write one into the vertex
 * buffer so their own emissive materials survive, because `doapoly`'s Gouraud
 * modulation was deliberately dropped from PBR entities. Reading it here would
 * make every entity a full-brightness ambient emitter, so entities are left to
 * gather this from the world around them like any other incident light.
 *
 * Metalness is not factored out. Base colour is the specular tint for a metal
 * rather than a diffuse albedo, but a rough metal under ambient light does
 * return roughly its base colour, so the same product answers for both and a
 * `1 - metalness` factor would only turn metal-panelled rooms black.
 */
float3 authoredAmbientRadiance(SurfaceData surface)
{
    if (surface.primitive != WorldSurfacePrimitive) {
        return 0.0;
    }
    return surface.baseColor * (surface.authoredShade * AuthoredAmbientScale);
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

struct MaterialSampleFootprint
{
    uint2 texel00;
    uint2 texel10;
    uint2 texel01;
    uint2 texel11;
    float2 blend;
};

MaterialSampleFootprint materialSampleFootprint(
    SceneMaterial material, float2 textureCoordinate,
    uint packedWindowOrigin, uint packedWindowExtent)
{
    MaterialTextureWindow window = materialTextureWindow(
        material, packedWindowOrigin, packedWindowExtent);
    float2 dimensions = float2(window.extent);
    float2 position = frac(textureCoordinate) * dimensions - 0.5;
    int2 lower = int2(floor(position));
    int2 size = int2(window.extent);
    int2 lowerWrapped = (lower + size) % size;
    int2 upperWrapped = (lower + 1 + size) % size;
    uint2 origin = uint2(material.atlasX, material.atlasY) + window.origin;
    MaterialSampleFootprint footprint;
    footprint.texel00 = origin + uint2(lowerWrapped.x, lowerWrapped.y);
    footprint.texel10 = origin + uint2(upperWrapped.x, lowerWrapped.y);
    footprint.texel01 = origin + uint2(lowerWrapped.x, upperWrapped.y);
    footprint.texel11 = origin + uint2(upperWrapped.x, upperWrapped.y);
    footprint.blend = frac(position);
    return footprint;
}

float4 sampleMaterialAtlas(Texture2D<float4> atlas,
                           MaterialSampleFootprint footprint)
{
    float4 upper = lerp(atlas.Load(int3(footprint.texel00, 0)),
                        atlas.Load(int3(footprint.texel10, 0)),
                        footprint.blend.x);
    float4 lower = lerp(atlas.Load(int3(footprint.texel01, 0)),
                        atlas.Load(int3(footprint.texel11, 0)),
                        footprint.blend.x);
    return lerp(upper, lower, footprint.blend.y);
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
    MaterialSampleFootprint footprint =
        materialSampleFootprint(material, surface.textureCoordinate,
                                surface.textureWindowOrigin,
                                surface.textureWindowExtent);
    surface.baseColor = saturate(
        sampleMaterialAtlas(BaseColorAtlas, footprint).rgb);
    float3 tangentNormal =
        sampleMaterialAtlas(NormalAtlas, footprint).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalStrength;
    tangentNormal = normalize(float3(tangentNormal.xy,
                                     max(tangentNormal.z, 1.0e-4)));
    surface.shadingNormal = normalize(tangent * tangentNormal.x +
        bitangent * tangentNormal.y + surface.geometricNormal * tangentNormal.z);
    if (dot(surface.shadingNormal, surface.geometricNormal) <= 0.0) {
        surface.shadingNormal = surface.geometricNormal;
    }
    surface.metalness = saturate(
        sampleMaterialAtlas(MetalnessAtlas, footprint).r);
    surface.specularFactor = saturate(material.specularFactor);
    surface.roughness = clamp(
        sampleMaterialAtlas(RoughnessAtlas, footprint).r, 0.045, 1.0);
    surface.authoredShade = first.emissiveScale * firstWeight +
        second.emissiveScale * payload.barycentrics.x +
        third.emissiveScale * payload.barycentrics.y;
    surface.emission = sampleMaterialAtlas(EmissiveAtlas, footprint).rgb *
        material.emissiveFactor * surface.authoredShade;
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
                     float2 dimensions)
{
    if (HistoryValid == 0u) {
        return InvalidMotion.xx;
    }
    float2 currentPixel;
    float2 previousPixel;
    bool currentValid = projectWorldToPixel(
        surface.position, CameraPosition, CameraForward, CameraRight, CameraUp,
        TanHalfFovY, Aspect, dimensions, currentPixel);
    bool previousValid = projectWorldToPixel(
        previousSurfacePosition(payload), PreviousCameraPosition,
        PreviousCameraForward, PreviousCameraRight, PreviousCameraUp,
        PreviousTanHalfFovY, PreviousAspect, dimensions, previousPixel);
    if (!currentValid || !previousValid) {
        return InvalidMotion.xx;
    }
    return clamp(previousPixel - currentPixel, -65500.0, 65500.0);
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
    return clamp(previousPixel - currentPixel, -65500.0, 65500.0);
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
    result.value = diffuse + specular;
    result.pdf = lerp(diffusePdf, specularPdf, chooseSpecular);
    return result;
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
    MaterialSampleFootprint lightFootprint =
        materialSampleFootprint(lightMaterial, lightUv,
                                first.textureWindowOrigin,
                                first.textureWindowExtent);
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    float3 emittedRadiance =
        sampleMaterialAtlas(EmissiveAtlas, lightFootprint).rgb *
        lightMaterial.emissiveFactor * lightEmissiveScale;
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    evaluation.contribution =
        bsdf.value * emittedRadiance * normalLight;
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = lightPdf;
    evaluation.brdfPdf = bsdf.pdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = distance;
    evaluation.valid = true;
    return evaluation;
}

EmitterEvaluation evaluateEnvironmentSample(SurfaceData surface,
                                             float3 viewDirection,
                                             EmitterSample lightSample)
{
    EmitterEvaluation evaluation;
    evaluation.contribution = 0.0;
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
    evaluation.contribution = bsdf.value * environmentRadiance(lightDirection) *
        normalLight;
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

struct LightSelection
{
    uint emitterIndex;
    /* RIS correction for selecting this emitter, excluding the independent
     * uniform point sampled on its triangle. */
    float inverseProbability;
};

float3 lightGridCellCenter(uint cellIndex)
{
    uint3 position;
    position.x = cellIndex % LightGridCellsPerAxis;
    uint yz = cellIndex / LightGridCellsPerAxis;
    position.y = yz % LightGridCellsPerAxis;
    position.z = yz / LightGridCellsPerAxis;
    float3 origin = CameraPosition - LightGridExtent * 0.5;
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
    uint lightSlot = dispatchIndex.x * LightGridLightsPerCell +
        dispatchIndex.y;
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
    if (ReservoirSampleLimit == 0u) {
        return -1;
    }
    float3 jitter = sampleStream(
        pixel, sampleIndex, LightGridLookupStream).xyz - 0.5;
    float3 samplingPosition =
        surfacePosition + jitter * LightGridCellSize;
    float3 origin = CameraPosition - LightGridExtent * 0.5;
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
    MaterialSampleFootprint footprint =
        materialSampleFootprint(material, surface.textureCoordinate,
                                surface.textureWindowOrigin,
                                surface.textureWindowExtent);
    surface.baseColor = saturate(
        sampleMaterialAtlas(BaseColorAtlas, footprint).rgb);
    surface.metalness = saturate(
        sampleMaterialAtlas(MetalnessAtlas, footprint).r);
    surface.specularFactor = saturate(material.specularFactor);
    surface.roughness = clamp(
        sampleMaterialAtlas(RoughnessAtlas, footprint).r, 0.045, 1.0);
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
    ShadingNormal[pixel] = float4(-unjitteredDirection, 1.0);
    LinearRoughness[pixel] = 1.0;
    LinearDepth[pixel] = SceneFarPlane;
    SceneMotion[pixel] = environmentMotion(unjitteredDirection, dimensions);
    SpecularHitDistance[pixel] = 0.0;
    DiffuseHitDistance[pixel] = 0.0;
}

void writeFlatSurfaceGuides(uint2 pixel, SurfacePayload payload,
                            SurfaceData surface, uint2 dimensions)
{
    /* The reset renderer has no specular or indirect signal. Preserve only the
     * primary surface data Ray Reconstruction needs to reproject the flat base
     * colour; in particular, do not trace the former mirror guide ray. */
    DiffuseAlbedo[pixel] = float4(surface.baseColor, 1.0);
    SpecularAlbedo[pixel] = 0.0;
    ShadingNormal[pixel] = float4(surface.shadingNormal, 1.0);
    LinearRoughness[pixel] = 1.0;
    LinearDepth[pixel] = max(0.0, dot(surface.position - CameraPosition,
                                      CameraForward));
    SceneMotion[pixel] = surfaceMotion(payload, surface, float2(dimensions));
    SpecularHitDistance[pixel] = 0.0;
    DiffuseHitDistance[pixel] = 0.0;
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    float2 jitter = float2(JitterX, JitterY);
    float2 screen = (float2(pixel) + 0.5 + jitter) / float2(dimensions);
    float2 ndc = float2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    float3 direction = normalize(CameraForward +
        CameraRight * (ndc.x * Aspect * TanHalfFovY) +
        CameraUp * (ndc.y * TanHalfFovY));
    float2 unjitteredScreen =
        (float2(pixel) + 0.5) / float2(dimensions);
    float2 unjitteredNdc = float2(unjitteredScreen.x * 2.0 - 1.0,
                                  1.0 - unjitteredScreen.y * 2.0);
    float3 unjitteredDirection = normalize(CameraForward +
        CameraRight * (unjitteredNdc.x * Aspect * TanHalfFovY) +
        CameraUp * (unjitteredNdc.y * TanHalfFovY));

    /* The reset baseline performs primary visibility only. `traceSegment`
     * continues the same camera ray through source additive layers so they do
     * not become opaque, but their emission is deliberately discarded. */
    RayDesc primaryRay;
    primaryRay.Origin = CameraPosition;
    primaryRay.Direction = direction;
    primaryRay.TMin = RayEpsilon;
    primaryRay.TMax = SceneFarPlane;
    SegmentTraversal primarySegment = traceSegment(primaryRay);
    SurfacePayload primaryPayload = primarySegment.payload;
    uint primaryPrimitive = primaryPayload.hit != 0u ?
        Vertices[primaryPayload.primitiveIndex * 3u].primitive : InvalidIndex;
    float3 resolvedRadiance = 0.0;
    if (primaryPayload.hit == 0u) {
        writeMissGuides(pixel, unjitteredDirection, float2(dimensions));
    } else {
        SurfaceData surface = loadSurface(primaryPayload, primaryRay.Direction);
        writeFlatSurfaceGuides(pixel, primaryPayload, surface, dimensions);
        resolvedRadiance = surface.baseColor;
    }
    NoisyRadiance[pixel] = float4(resolvedRadiance, 1.0);
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
    /* Keep source-effect geometry observable to the existing smoke diagnostic
     * even though the reset renderer intentionally discards its emission. */
    if (primarySegment.additiveLayers != 0u) {
        InterlockedAdd(Diagnostics[4], 1u);
    }
    PackedLightReservoir emptyReservoir = (PackedLightReservoir)0;
    emptyReservoir.emitterIndex = InvalidIndex;
    CurrentReservoirs[pixel.y * dimensions.x + pixel.x] = emptyReservoir;
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
    if (directLuminance > RadianceClamp) {
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
