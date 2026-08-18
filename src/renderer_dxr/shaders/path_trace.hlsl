static const float Pi = 3.14159265358979323846;
static const float RayEpsilon = 0.05;
static const uint InvalidIndex = 0xffffffffu;
static const float InvalidMotion = 65504.0;

struct SceneVertex
{
    float3 position;
    float2 textureCoordinate;
    uint materialIndex;
    uint emitterIndex;
};

struct SceneMaterial
{
    uint atlasX;
    uint atlasY;
    uint width;
    uint height;
    float normalStrength;
    float3 emissiveFactor;
};

struct EmissiveTriangle
{
    uint firstVertex;
    float selectionCdf;
    float selectionProbability;
    float inverseArea;
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
    float3 emission;
    uint materialIndex;
    uint emitterIndex;
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
    uint FrameConstantsPadding;
};

static const uint BlueNoiseSampleCount = 256u;
static const uint BlueNoiseDimensionCount = 256u;
static const uint BlueNoiseTileWidth = 128u;
static const uint BlueNoiseOptimizedDimensions = 8u;
static const uint BlueNoiseSobolOffset = 0u;
static const uint BlueNoiseScramblingOffset = 65536u;
static const uint BlueNoiseRankingOffset = 196608u;
static const uint PathDimensionsPerBounce = 8u;

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
    surface.roughness = clamp(
        RoughnessAtlas.Load(int3(texel, 0)).r, 0.045, 1.0);
    surface.emission = EmissiveAtlas.Load(int3(texel, 0)).rgb *
        material.emissiveFactor;
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

float3 fresnelSchlick(float cosine, float3 reflectance)
{
    float factor = pow(1.0 - saturate(cosine), 5.0);
    return reflectance + (1.0 - reflectance) * factor;
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
    float3 f0 = lerp(0.04.xxx, surface.baseColor, surface.metalness);
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
        max(4.0 * normalView, 1.0e-7);
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
    float radius = sqrt(sampleValue.x);
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
    float3 f0 = lerp(0.04.xxx, surface.baseColor, surface.metalness);
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

bool traceVisibility(float3 origin, float3 direction, float maximumDistance)
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
             0xff, 0, 0, 1, ray, payload);
    return payload.visible != 0u;
}

float3 sampleEnvironmentLighting(SurfaceData surface, float3 viewDirection,
                                 float2 sampleValue)
{
    float3 lightDirection = cosineHemisphere(surface.shadingNormal,
                                              sampleValue);
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    float lightPdf = normalLight / Pi;
    if (lightPdf <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0 ||
        !traceVisibility(surface.position +
                             surface.geometricNormal * RayEpsilon,
                         lightDirection, 8192.0)) {
        return 0.0;
    }
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    float weight = powerHeuristic(lightPdf, bsdf.pdf);
    return bsdf.value * environmentRadiance(lightDirection) *
        (normalLight * weight / lightPdf);
}

float3 sampleEmitterLighting(SurfaceData surface, float3 viewDirection,
                             float selection, float2 positionSample)
{
    if (EmitterCount == 0u) {
        return 0.0;
    }
    uint emitterIndex = EmitterCount - 1u;
    for (uint index = 0u; index < EmitterCount; ++index) {
        if (selection <= Emitters[index].selectionCdf) {
            emitterIndex = index;
            break;
        }
    }
    EmissiveTriangle emitter = Emitters[emitterIndex];
    SceneVertex first = Vertices[emitter.firstVertex + 0u];
    SceneVertex second = Vertices[emitter.firstVertex + 1u];
    SceneVertex third = Vertices[emitter.firstVertex + 2u];
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
        return 0.0;
    }
    float distance = sqrt(distanceSquared);
    float3 lightDirection = toLight / distance;
    float normalLight = saturate(dot(surface.shadingNormal, lightDirection));
    if (normalLight <= 0.0 ||
        dot(surface.geometricNormal, lightDirection) <= 0.0) {
        return 0.0;
    }
    float3 lightNormal = normalize(cross(second.position - first.position,
                                         third.position - first.position));
    float lightCosine = abs(dot(lightNormal, -lightDirection));
    if (lightCosine <= 1.0e-6) {
        return 0.0;
    }
    float lightPdf = emitter.selectionProbability * emitter.inverseArea *
        distanceSquared / lightCosine;
    if (lightPdf <= 0.0 ||
        !traceVisibility(surface.position +
                             surface.geometricNormal * RayEpsilon,
                         lightDirection, distance - RayEpsilon)) {
        return 0.0;
    }
    SceneMaterial lightMaterial = Materials[first.materialIndex];
    uint2 lightTexel = materialTexel(lightMaterial, lightUv);
    float3 emittedRadiance =
        EmissiveAtlas.Load(int3(lightTexel, 0)).rgb *
        lightMaterial.emissiveFactor;
    BsdfEvaluation bsdf = evaluateBsdf(surface, viewDirection, lightDirection);
    float weight = powerHeuristic(lightPdf, bsdf.pdf);
    return bsdf.value * emittedRadiance * (normalLight * weight / lightPdf);
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
    DiffuseAlbedo[pixel] = 0.0;
    SpecularAlbedo[pixel] = 0.0;
    ShadingNormal[pixel] = 0.0;
    LinearRoughness[pixel] = 0.0;
    LinearDepth[pixel] = 0.0;
    SceneMotion[pixel] = environmentMotion(unjitteredDirection, dimensions);
    SpecularHitDistance[pixel] = 0.0;
}

void writeSurfaceGuides(uint2 pixel, SurfacePayload payload,
                        SurfaceData surface, float3 viewDirection,
                        float2 dimensions)
{
    float3 diffuseReflectance = surface.baseColor * (1.0 - surface.metalness);
    float3 specularColor = lerp(0.04.xxx, surface.baseColor,
                                surface.metalness);
    float normalView = saturate(dot(surface.shadingNormal, viewDirection));
    DiffuseAlbedo[pixel] = float4(diffuseReflectance, 1.0);
    SpecularAlbedo[pixel] = float4(reconstructionSpecularAlbedo(
        specularColor, surface.roughness, normalView), 1.0);
    ShadingNormal[pixel] = float4(surface.shadingNormal, 1.0);
    LinearRoughness[pixel] = surface.roughness;
    LinearDepth[pixel] = max(0.0, dot(surface.position - CameraPosition,
                                      CameraForward));
    SceneMotion[pixel] = surfaceMotion(payload, surface, dimensions);
    SpecularHitDistance[pixel] = 0.0;
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

    float3 radiance = 0.0;
    float3 throughput = 1.0;
    float previousBsdfPdf = 0.0;
    float3 previousPosition = 0.0;
    float3 previousNormal = 0.0;
    bool firstBounceSpecular = false;
    RayDesc ray;
    ray.Origin = CameraPosition;
    ray.Direction = direction;
    ray.TMin = RayEpsilon;
    ray.TMax = 8192.0;

    for (uint depth = 0u; depth < MaximumDepth; ++depth) {
        SurfacePayload payload;
        payload.rayDistance = 0.0;
        payload.barycentrics = 0.0;
        payload.primitiveIndex = InvalidIndex;
        payload.hit = 0u;
        TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
        if (depth == 1u && firstBounceSpecular) {
            SpecularHitDistance[pixel] =
                payload.hit != 0u ? payload.rayDistance : 0.0;
        }
        if (payload.hit == 0u) {
            if (depth == 0u) {
                writeMissGuides(pixel, unjitteredDirection,
                                float2(dimensions));
            }
            float weight = 1.0;
            if (depth > 0u) {
                float lightPdf =
                    saturate(dot(previousNormal, ray.Direction)) / Pi;
                weight = powerHeuristic(previousBsdfPdf, lightPdf);
            }
            radiance += throughput * environmentRadiance(ray.Direction) * weight;
            break;
        }

        SurfaceData surface = loadSurface(payload, ray.Direction);
        float3 viewDirection = -ray.Direction;
        if (depth == 0u) {
            writeSurfaceGuides(pixel, payload, surface, viewDirection,
                               float2(dimensions));
        }
        if (any(surface.emission > 0.0)) {
            float weight = depth == 0u ? 1.0 : powerHeuristic(
                previousBsdfPdf,
                emitterPdfForHit(surface, previousPosition));
            radiance += throughput * surface.emission * weight;
        }
        uint sampleDimension = depth * PathDimensionsPerBounce;
        float2 environmentSample = float2(
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 0u),
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 1u));
        float emitterSelection =
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 2u);
        float2 emitterSample = float2(
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 3u),
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 4u));
        radiance += throughput * sampleEnvironmentLighting(
            surface, viewDirection, environmentSample);
        radiance += throughput * sampleEmitterLighting(
            surface, viewDirection, emitterSelection, emitterSample);

        if (depth + 1u >= MaximumDepth) {
            break;
        }
        float3 bounceDirection;
        BsdfEvaluation bsdf;
        bool sampledSpecular;
        float chooseBsdf =
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 5u);
        float2 bsdfSample = float2(
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 6u),
            sampleBlueNoise(pixel, SampleIndex, sampleDimension + 7u));
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
        ray.TMax = 8192.0;
    }

    if (any(isnan(radiance)) || any(isinf(radiance))) {
        radiance = 0.0;
    }
    NoisyRadiance[pixel] = float4(max(radiance, 0.0), 1.0);
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
