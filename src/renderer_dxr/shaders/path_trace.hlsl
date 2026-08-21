static const float Pi = 3.14159265358979323846;
static const float RayEpsilon = 0.05;
static const uint InvalidIndex = 0xffffffffu;
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
static const float SpecularHitDistanceBlend = 0.2;
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
/* Temporal reuse is rejected unless the reprojected surface matches this closely:
 * a world-space distance within this fraction of the view depth, so the tolerance
 * scales with the Amiga-sized world instead of assuming a unit system, and a
 * shading-normal agreement of at least this cosine. Tight values prevent
 * reservoirs from leaking across adjacent differently-oriented surfaces. */
static const float ReservoirPositionTolerance = 0.005;
static const float ReservoirNormalTolerance = 0.966;
/*
 * Sample-index offset used to draw the temporal acceptance test from an
 * optimized blue-noise dimension without reusing the value that selected this
 * frame's first candidate. Half the 256-sample block keeps the two values far
 * apart in the sequence while both stay blue in screen space.
 */
static const uint ReservoirAcceptanceOffset = 128u;
/* Clamp the reservoir's unbiased contribution weight to prevent extreme outliers
 * from persisting across frames and drifting as correlated temporal noise. */
static const float ReservoirMaxWeight = 20.0;

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

uint2 materialTexel(SceneMaterial material, float2 textureCoordinate)
{
    float2 wrapped = frac(textureCoordinate);
    return uint2(material.atlasX, material.atlasY) +
        min(uint2(wrapped * float2(material.width, material.height)),
            uint2(material.width - 1u, material.height - 1u));
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
    float3 tangent;
    float3 bitangent;
    triangleFrame(firstVertex, incomingDirection, surface.geometricNormal,
                  tangent, bitangent);
    SceneMaterial material = Materials[surface.materialIndex];
    uint2 texel = materialTexel(material, surface.textureCoordinate);
    surface.baseColor = saturate(BaseColorAtlas.Load(int3(texel, 0)).rgb);
    float3 tangentNormal = NormalAtlas.Load(int3(texel, 0)).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalStrength;
    tangentNormal = normalize(float3(tangentNormal.xy,
                                     max(tangentNormal.z, 1.0e-4)));
    surface.shadingNormal = normalize(tangent * tangentNormal.x +
        bitangent * tangentNormal.y + surface.geometricNormal * tangentNormal.z);
    if (dot(surface.shadingNormal, surface.geometricNormal) <= 0.0) {
        surface.shadingNormal = surface.geometricNormal;
    }
    surface.metalness =
        saturate(MetalnessAtlas.Load(int3(texel, 0)).r);
    surface.specularFactor = saturate(material.specularFactor);
    surface.roughness = clamp(
        RoughnessAtlas.Load(int3(texel, 0)).r, 0.045, 1.0);
    surface.authoredShade = first.emissiveScale * firstWeight +
        second.emissiveScale * payload.barycentrics.x +
        third.emissiveScale * payload.barycentrics.y;
    surface.emission = EmissiveAtlas.Load(int3(texel, 0)).rgb *
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

/*
 * Point-samples the previous frame's specular hit-distance guide at the pixel the
 * scene motion vector reprojects to. Nearest sampling is deliberate: a bilinear
 * tap would blend hit distances across depth discontinuities.
 */
bool loadSpecularHitDistanceHistory(uint2 pixel, float2 motion,
                                    uint2 dimensions,
                                    out float hitDistance)
{
    hitDistance = 0.0;
    if (HistoryValid == 0u || any(abs(motion) > 65500.0)) {
        return false;
    }
    float2 previous = reprojectHistoryPixel(pixel, motion);
    if (any(previous < 0.0) || any(previous >= float2(dimensions))) {
        return false;
    }
    hitDistance = SpecularHitDistanceHistory[uint2(previous)];
    return hitDistance > 0.0;
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

float3 sampleEnvironmentLighting(SurfaceData surface, float3 viewDirection,
                                 float2 sampleValue, uint instanceMask)
{
    float3 lightDirection = cosineHemisphere(surface.shadingNormal,
                                              sampleValue);
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    float lightPdf = normalLight / Pi;
    if (lightPdf <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0 ||
        !traceVisibility(surface.position +
                             surface.geometricNormal * RayEpsilon,
                         lightDirection, SceneFarPlane, instanceMask)) {
        return 0.0;
    }
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    float weight = powerHeuristic(lightPdf, bsdf.pdf);
    return bsdf.value * environmentRadiance(lightDirection) *
        (normalLight * weight / lightPdf);
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

/* An emitter sample's full identity: which triangle, and the two canonical
 * randoms that place the point on it. Both survive reuse at another pixel. */
struct EmitterSample
{
    uint emitterIndex;
    float2 positionSample;
    bool valid;
};

struct EmitterEvaluation
{
    /* The unshadowed, MIS-weighted integrand in solid-angle measure. Folding the
     * power heuristic into the value, and therefore into the target function, is
     * what keeps resampling and multiple importance sampling compatible: the
     * reservoir estimates the light strategy's MIS-weighted share of the
     * integral, and the BSDF strategy independently estimates its own. */
    float3 contribution;
    float targetPdf;
    float sourcePdf;
    float3 lightDirection;
    float lightDistance;
    bool valid;
};

EmitterEvaluation evaluateEmitterSample(SurfaceData surface,
                                        float3 viewDirection,
                                        EmitterSample lightSample)
{
    EmitterEvaluation evaluation;
    evaluation.contribution = 0.0;
    evaluation.targetPdf = 0.0;
    evaluation.sourcePdf = 0.0;
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
    float root = sqrt(lightSample.positionSample.x);
    float secondRandom = lightSample.positionSample.y;
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
    uint2 lightTexel = materialTexel(lightMaterial, lightUv);
    float lightEmissiveScale = first.emissiveScale * barycentrics.x +
        second.emissiveScale * barycentrics.y +
        third.emissiveScale * barycentrics.z;
    float3 emittedRadiance =
        EmissiveAtlas.Load(int3(lightTexel, 0)).rgb *
        lightMaterial.emissiveFactor * lightEmissiveScale;
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    float misWeight = powerHeuristic(lightPdf, bsdf.pdf);
    evaluation.contribution =
        bsdf.value * emittedRadiance * (normalLight * misWeight);
    evaluation.targetPdf = luminance(evaluation.contribution);
    evaluation.sourcePdf = lightPdf;
    evaluation.lightDirection = lightDirection;
    evaluation.lightDistance = distance;
    evaluation.valid = true;
    return evaluation;
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

/*
 * One-sample emitter lighting, still used at secondary hits where the throughput
 * has already attenuated the variance and no reservoir is carried. With the
 * MIS weight folded into `contribution`, dividing by the source pdf reproduces
 * the estimator this replaced.
 */
float3 sampleEmitterLighting(SurfaceData surface, float3 viewDirection,
                             float selection, float2 positionSample,
                             uint instanceMask)
{
    if (EmitterCount == 0u) {
        return 0.0;
    }
    EmitterSample lightSample;
    lightSample.emitterIndex = selectEmitter(selection);
    lightSample.positionSample = positionSample;
    lightSample.valid = true;
    EmitterEvaluation evaluation =
        evaluateEmitterSample(surface, viewDirection, lightSample);
    if (!evaluation.valid || !(evaluation.targetPdf > 0.0) ||
        !traceEmitterVisibility(surface, evaluation, instanceMask)) {
        return 0.0;
    }
    return evaluation.contribution / evaluation.sourcePdf;
}

/*
 * Reads the reservoir the scene motion vector reprojects to, and rejects it
 * unless it describes the same surface. The comparison is against the current
 * surface's *previous* world position, which is exactly what the motion vector
 * is derived from, so geometry that translates between frames still reuses its
 * history instead of being treated as a disocclusion.
 */
bool loadPreviousReservoir(uint2 pixel, uint2 dimensions, SurfaceData surface,
                           float3 previousPosition, float2 motion,
                           out PackedLightReservoir previous)
{
    previous = (PackedLightReservoir)0;
    if (HistoryValid == 0u || any(abs(motion) > 65500.0)) {
        return false;
    }
    float2 reprojected = reprojectHistoryPixel(pixel, motion);
    if (any(reprojected < 0.0) || any(reprojected >= float2(dimensions))) {
        return false;
    }
    uint2 previousPixel = uint2(reprojected);
    previous = PreviousReservoirs[previousPixel.y * dimensions.x +
                                  previousPixel.x];
    if (previous.sampleCount == 0u || !(previous.unbiasedWeight > 0.0)) {
        return false;
    }
    float viewDepth = max(dot(surface.position - CameraPosition, CameraForward),
                          RayEpsilon);
    if (length(previousPosition - previous.surfacePosition) >
        ReservoirPositionTolerance * viewDepth) {
        return false;
    }
    return dot(surface.shadingNormal,
               unpackOctahedralNormal(previous.surfaceNormal)) >=
        ReservoirNormalTolerance;
}

/*
 * Reservoir-resampled direct lighting for the primary hit, from Bitterli,
 * Wyman, Pharr, Shirley, Lefohn, and Jarosz, "Spatiotemporal reservoir resampling
 * for real-time ray tracing with dynamic direct lighting", SIGGRAPH 2020.
 *
 * `CandidateCount` emitter candidates are resampled into one reservoir with no
 * shadow rays (their Algorithm 2), the reprojected previous reservoir is combined
 * into it with weight `targetPdf * W * M` re-evaluated at this surface and
 * normalised by the total sample count (their Algorithm 4), and one shadow ray is
 * traced for whichever sample survives.
 *
 * Re-tracing that ray every frame is what keeps stale visibility out of the
 * result without the per-sample visibility checks the unbiased combination
 * weights would need. The `1 / total M` form is the paper's Algorithm 4 and is
 * biased in the presence of that reuse; the choice is deliberate.
 *
 * With `CandidateCount` of one and no usable history this reduces exactly to the
 * single-sample estimator it replaces: the unbiased weight becomes the
 * reciprocal of the source pdf.
 */
float3 resampleEmitterLighting(uint2 pixel, uint2 dimensions,
                               uint sampleIndex,
                               SurfaceData surface, float3 viewDirection,
                               float3 previousPosition, float2 motion,
                               uint instanceMask,
                               out PackedLightReservoir stored)
{
    stored = (PackedLightReservoir)0;
    stored.surfacePosition = surface.position;
    stored.surfaceNormal = packOctahedralNormal(surface.shadingNormal);
    if (EmitterCount == 0u || CandidateCount == 0u) {
        return 0.0;
    }

    EmitterSample selected = (EmitterSample)0;
    float selectedTarget = 0.0;
    float weightSum = 0.0;
    uint sampleCount = 0u;
    for (uint candidate = 0u; candidate < CandidateCount; ++candidate) {
        /*
         * The first candidate keeps the blue-noise dimensions this bounce already
         * reserves for emitter sampling, and the rest come from the hash stream.
         *
         * This split is measured, not stylistic. Taking every candidate from the
         * hash raised the reconstructed image's residual frame-to-frame delta even
         * though it lowered the path-traced input's variance, and the regression
         * was already fully present at a single candidate. Screen-space blue noise
         * is what makes the surviving error cheap for Ray Reconstruction to
         * filter, so the dimensions the pinned tables actually optimize stay on
         * the sample most likely to survive resampling. The hash covers the
         * remaining candidates, which need far more dimensions than the tables
         * provide and would otherwise reuse the same eight ranking and scrambling
         * channels through a tile translation.
         *
         * The first candidate needs no acceptance random: it is always selected,
         * because at that point the running weight sum is its own weight.
         */
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
        EmitterSample candidateSample;
        candidateSample.emitterIndex = selectEmitter(stream.x);
        /* Quantized here so the weight a reservoir stores always describes
         * exactly the sample it stores. */
        candidateSample.positionSample = quantizePositionSample(stream.yz);
        candidateSample.valid = true;
        EmitterEvaluation evaluation =
            evaluateEmitterSample(surface, viewDirection, candidateSample);
        float weight = evaluation.valid && evaluation.sourcePdf > 0.0 ?
            evaluation.targetPdf / evaluation.sourcePdf : 0.0;
        weightSum += weight;
        sampleCount += 1u;
        if (weight > 0.0 && acceptance * weightSum < weight) {
            selected = candidateSample;
            selectedTarget = evaluation.targetPdf;
        }
    }

    PackedLightReservoir previous;
    if (loadPreviousReservoir(pixel, dimensions, surface, previousPosition,
                              motion, previous)) {
        EmitterSample previousSample;
        previousSample.emitterIndex = previous.emitterIndex;
        previousSample.positionSample =
            unpackPositionSample(previous.positionSample);
        previousSample.valid = true;
        EmitterEvaluation previousEvaluation =
            evaluateEmitterSample(surface, viewDirection, previousSample);
        /* Capping the carried sample count keeps the reservoir responsive:
         * without it a pixel's history would dominate every new candidate and
         * lighting changes would never take effect. A limit of zero disables
         * temporal reuse outright, which is the measured default. */
        uint previousCount = min(previous.sampleCount, ReservoirSampleLimit);
        float previousWeight = previousEvaluation.valid ?
            previousEvaluation.targetPdf * previous.unbiasedWeight *
                float(previousCount) : 0.0;
        float totalWeight = weightSum + previousWeight;
        /*
         * The temporal acceptance test decides whether a pixel keeps its history
         * or takes a fresh candidate, so it is what determines where stale
         * samples sit on screen. Drawing it from the hash lets neighbouring
         * pixels hold their history in clumps, which is the correlated
         * low-frequency error a denoiser cannot remove. Drawing it from an
         * optimized blue-noise dimension spreads the switch events across the
         * screen instead, which is precisely what the sampler exists to do.
         */
        float acceptance = sampleBlueNoise(
            pixel, sampleIndex + ReservoirAcceptanceOffset,
            PathDimensionsPerBounce * 0u + 2u);
        if (previousWeight > 0.0 && acceptance * totalWeight < previousWeight) {
            selected = previousSample;
            selectedTarget = previousEvaluation.targetPdf;
        }
        weightSum = totalWeight;
        sampleCount += previousCount;
    }

    float unbiasedWeight = sampleCount > 0u && selectedTarget > 0.0 ?
        min(weightSum / (float(sampleCount) * selectedTarget),
            ReservoirMaxWeight) : 0.0;
    stored.emitterIndex = selected.emitterIndex;
    stored.positionSample = packPositionSample(selected.positionSample);
    stored.unbiasedWeight = unbiasedWeight;
    stored.sampleCount = sampleCount;
    if (!selected.valid || !(unbiasedWeight > 0.0)) {
        return 0.0;
    }
    EmitterEvaluation finalEvaluation =
        evaluateEmitterSample(surface, viewDirection, selected);
    if (!finalEvaluation.valid || !(finalEvaluation.targetPdf > 0.0) ||
        !traceEmitterVisibility(surface, finalEvaluation, instanceMask)) {
        return 0.0;
    }
    return finalEvaluation.contribution * unbiasedWeight;
}

float emitterPdfForHit(SurfaceData surface, float3 previousPosition)
{
    if (surface.emitterIndex == InvalidIndex ||
        surface.emitterIndex >= EmitterCount) {
        return 0.0;
    }
    EmissiveTriangle emitter = Emitters[surface.emitterIndex];
    float3 difference = surface.position - previousPosition;
    float distanceSquared = dot(difference, difference);
    float3 direction = normalize(difference);
    uint firstVertex = emitter.firstVertex;
    float3 lightNormal = normalize(cross(
        Vertices[firstVertex + 1u].position - Vertices[firstVertex].position,
        Vertices[firstVertex + 2u].position - Vertices[firstVertex].position));
    float lightCosine = abs(dot(lightNormal, -direction));
    return lightCosine > 1.0e-6 ?
        emitter.selectionProbability * emitter.inverseArea *
            distanceSquared / lightCosine : 0.0;
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

/* The primary hit's guide outputs the path integrator also needs: the motion
 * vector, for reprojecting the reservoir and the specular hit distance, and the
 * reprojected hit-distance estimate itself. */
struct PrimaryGuides
{
    float2 motion;
    float historyHitDistance;
    bool historyHitDistanceValid;
};

PrimaryGuides writeSurfaceGuides(uint2 pixel, SurfacePayload payload,
                                 SurfaceData surface, float3 viewDirection,
                                 uint2 dimensions)
{
    PrimaryGuides guides;
    float3 diffuseReflectance = surface.baseColor * (1.0 - surface.metalness);
    float3 specularColor = surfaceF0(surface);
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    DiffuseAlbedo[pixel] = float4(diffuseReflectance, 1.0);
    SpecularAlbedo[pixel] = float4(reconstructionSpecularAlbedo(
        specularColor, surface.roughness, normalView), 1.0);
    ShadingNormal[pixel] = float4(surface.shadingNormal, 1.0);
    LinearRoughness[pixel] = surface.roughness;
    LinearDepth[pixel] = max(0.0, dot(surface.position - CameraPosition,
                                      CameraForward));
    guides.motion = surfaceMotion(payload, surface, float2(dimensions));
    SceneMotion[pixel] = guides.motion;
    guides.historyHitDistanceValid = loadSpecularHitDistanceHistory(
        pixel, guides.motion, dimensions, guides.historyHitDistance);
    SpecularHitDistance[pixel] =
        guides.historyHitDistanceValid ? guides.historyHitDistance : 0.0;
    DiffuseHitDistance[pixel] = 0.0;
    return guides;
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

    /* The camera-attached weapon and room share the same nearest-hit query.
     * This gives the source-scale geometry ordinary world depth while keeping
     * its radiance and reconstruction guides in the primary DXR pass. */
    RayDesc primaryRay;
    primaryRay.Origin = CameraPosition;
    primaryRay.Direction = direction;
    primaryRay.TMin = RayEpsilon;
    primaryRay.TMax = SceneFarPlane;
    /*
     * The additive effects in front of the primary surface are resolved once
     * here rather than per sample: their radiance is a deterministic property
     * of the segment, so re-tracing them for every sample would cost traversals
     * to reach the same sum.
     */
    SegmentTraversal primarySegment = traceSegment(primaryRay);
    SurfacePayload primaryPayload = primarySegment.payload;
    uint primaryPrimitive = primaryPayload.hit != 0u ?
        Vertices[primaryPayload.primitiveIndex * 3u].primitive : InvalidIndex;
    bool primaryViewWeaponHit = primaryPrimitive == ViewWeaponPrimitive;

    /*
     * A pixel owns one temporal reservoir, not one per SPP sample. Ordinal zero
     * advances that chain using the primary guides; later ordinals deliberately
     * remain fresh independent estimates. Do not let their historyless
     * reservoirs overwrite the temporal result when SamplesPerPixel is above
     * one.
     */
    PackedLightReservoir reservoir = (PackedLightReservoir)0;
    float3 accumulatedRadiance = 0.0;

    for (uint sampleOrdinal = 0u; sampleOrdinal < SamplesPerPixel;
         ++sampleOrdinal) {
    uint effectiveSampleIndex = SampleIndex * SamplesPerPixel + sampleOrdinal;
    float3 radiance = 0.0;
    float3 throughput = 1.0;
    float previousBsdfPdf = 0.0;
    float3 previousPosition = 0.0;
    float3 previousNormal = 0.0;
    bool firstBounceSpecular = false;
    PrimaryGuides primaryGuides;
    primaryGuides.motion = InvalidMotion.xx;
    primaryGuides.historyHitDistance = 0.0;
    primaryGuides.historyHitDistanceValid = false;
    RayDesc ray = primaryRay;

    for (uint depth = 0u; depth < MaximumDepth; ++depth) {
        SurfacePayload payload;
        float segmentDistance = 0.0;
        if (depth == 0u) {
            /* The primary segment's additive radiance is added once, outside
             * this loop, for the reason given where it is traced. */
            payload = primaryPayload;
            segmentDistance = primarySegment.distance;
        } else {
            SegmentTraversal segment = traceSegment(ray);
            payload = segment.payload;
            segmentDistance = segment.distance;
            radiance += throughput * segment.additiveRadiance;
        }
        if (depth == 1u && firstBounceSpecular && sampleOrdinal == 0u) {
            /* A specular ray that escapes the scene reflects something
             * effectively infinitely far away, which is the far plane rather
             * than a zero distance at the shading point. */
            float sampledHitDistance =
                payload.hit != 0u ? segmentDistance : SceneFarPlane;
            SpecularHitDistance[pixel] =
                primaryGuides.historyHitDistanceValid ?
                lerp(primaryGuides.historyHitDistance, sampledHitDistance,
                     SpecularHitDistanceBlend) :
                sampledHitDistance;
        }
        if (depth == 1u && !firstBounceSpecular && sampleOrdinal == 0u) {
            float sampledHitDistance =
                payload.hit != 0u ? segmentDistance : SceneFarPlane;
            DiffuseHitDistance[pixel] = sampledHitDistance;
        }
        if (payload.hit == 0u) {
            if (depth == 0u && sampleOrdinal == 0u) {
                writeMissGuides(pixel, unjitteredDirection,
                                float2(dimensions));
            }
            float weight = 1.0;
            if (depth == 1u) {
                float lightPdf =
                    saturate(dot(previousNormal, ray.Direction)) / Pi;
                weight = powerHeuristic(previousBsdfPdf, lightPdf);
            }
            radiance += throughput * environmentRadiance(ray.Direction) * weight;
            break;
        }

        SurfaceData surface = loadSurface(payload, ray.Direction);
        float3 viewDirection = -ray.Direction;
        if (depth == 0u && sampleOrdinal == 0u) {
            primaryGuides = writeSurfaceGuides(pixel, payload, surface,
                                              viewDirection, dimensions);
        }
        if (any(surface.emission > 0.0)) {
            float weight = depth == 0u ? 1.0 : powerHeuristic(
                previousBsdfPdf,
                emitterPdfForHit(surface, previousPosition));
            radiance += throughput * surface.emission * weight;
        }
        /*
         * The authored zone lighting enters only here, on a bounce, so a
         * directly visible surface shows the traced lighting alone and picks the
         * authored level up as fill from whatever surrounds it. No MIS weight
         * applies: this radiance is not in any emitter's sampling distribution,
         * so a BSDF-sampled path is the only estimator that ever sees it.
         */
        if (depth > 0u) {
            radiance += throughput * authoredAmbientRadiance(surface);
        }
        uint sampleDimension = depth * PathDimensionsPerBounce;
        float2 environmentSample = float2(
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 0u),
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 1u));
        float emitterSelection =
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 2u);
        float2 emitterSample = float2(
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 3u),
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 4u));
        if (depth == 0u) {
            radiance += throughput * sampleEnvironmentLighting(
                surface, viewDirection, environmentSample, SceneInstanceMask);
        }
        if (depth == 0u) {
            /* The reservoir's first candidate consumes the same emitter
             * dimensions this bounce already reserves, so every bounce keeps its
             * fixed dimension layout and no branch shifts the sequence. */
            PackedLightReservoir sampleReservoir;
            radiance += throughput * resampleEmitterLighting(
                pixel, dimensions, effectiveSampleIndex, surface, viewDirection,
                previousSurfacePosition(payload), primaryGuides.motion,
                SceneInstanceMask, sampleReservoir);
            if (sampleOrdinal == 0u) {
                reservoir = sampleReservoir;
            }
        } else {
            radiance += throughput * sampleEmitterLighting(
                surface, viewDirection, emitterSelection, emitterSample,
                SceneInstanceMask);
        }

        if (depth + 1u >= MaximumDepth) {
            break;
        }
        float3 bounceDirection;
        BsdfEvaluation bsdf;
        bool sampledSpecular;
        float chooseBsdf =
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 5u);
        float2 bsdfSample = float2(
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 6u),
            sampleBlueNoise(pixel, effectiveSampleIndex, sampleDimension + 7u));
        if (!sampleBsdf(surface, viewDirection, chooseBsdf, bsdfSample,
                        bounceDirection, bsdf, sampledSpecular)) {
            break;
        }
        if (depth == 0u) {
            firstBounceSpecular = sampledSpecular;
        }
        float normalBounce = saturate(dot(surface.shadingNormal, bounceDirection));
        throughput *= bsdf.value * (normalBounce / bsdf.pdf);
        if (luminance(throughput) <= 1.0e-6 ||
            any(isnan(throughput)) || any(isinf(throughput))) {
            break;
        }
        previousBsdfPdf = bsdf.pdf;
        previousPosition = surface.position;
        previousNormal = surface.shadingNormal;
        ray.Origin = surface.position +
            surface.geometricNormal * RayEpsilon;
        ray.Direction = bounceDirection;
        ray.TMin = RayEpsilon;
        ray.TMax = SceneFarPlane;
    }

    if (any(isnan(radiance)) || any(isinf(radiance))) {
        radiance = 0.0;
    }
    float radianceLuminance = luminance(radiance);
    if (radianceLuminance > RadianceClamp) {
        radiance *= RadianceClamp / radianceLuminance;
    }
    accumulatedRadiance += radiance;
    }  /* end sampleOrdinal loop */

    /*
     * The primary segment's additive layers land on the resolved estimate,
     * where every sample would have contributed the same value. They are
     * deliberately outside the per-sample radiance clamp: an additive texel is
     * bounded by one and the layer limit bounds how many can stack, so the sum
     * cannot reach the clamp, and clamping the pixel's traced lighting against
     * a total that includes an effect in front of it would dim the room instead
     * of the outlier the clamp exists for.
     */
    float3 resolvedRadiance = max(accumulatedRadiance /
        float(SamplesPerPixel), 0.0) + primarySegment.additiveRadiance;
    NoisyRadiance[pixel] = float4(resolvedRadiance, 1.0);
    if (primaryViewWeaponHit) {
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
    /*
     * Additive effects never become the primary surface, so the counters above
     * cannot see them. This one counts the pixels a primary ray crossed an
     * additive layer on, which is the only evidence the hidden smoke has that
     * glares, additive bitmaps and `predoglare` faces reach the image at all.
     */
    if (primarySegment.additiveLayers != 0u) {
        InterlockedAdd(Diagnostics[4], 1u);
    }
    CurrentReservoirs[pixel.y * dimensions.x + pixel.x] = reservoir;
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
            materialTexel(material, textureCoordinate), 0)).a < 0.5) {
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
