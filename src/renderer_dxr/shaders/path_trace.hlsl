static const float Pi = 3.14159265358979323846;
static const float RayEpsilon = 0.05;
static const uint InvalidIndex = 0xffffffffu;

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
StructuredBuffer<EmissiveTriangle> Emitters : register(t7);
RWTexture2D<float4> NoisyRadiance : register(u0);

cbuffer FrameConstants : register(b0)
{
    float3 CameraPosition;
    float TanHalfFovY;
    float3 CameraForward;
    float Aspect;
    float3 CameraRight;
    uint FrameIndex;
    float3 CameraUp;
    uint MaximumDepth;
    uint AtlasWidth;
    uint AtlasHeight;
    uint TriangleCount;
    uint EmitterCount;
};

uint randomUint(inout uint state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomUnit(inout uint state)
{
    return (randomUint(state) & 0x00ffffffu) * (1.0 / 16777216.0);
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

float3 cosineHemisphere(float3 normal, inout uint seed)
{
    float first = randomUnit(seed);
    float second = randomUnit(seed);
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
    surface.emission = surface.baseColor * material.emissiveFactor;
    return surface;
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
                              inout uint seed)
{
    float3 stretchedView = normalize(
        float3(alpha * viewDirection.x, alpha * viewDirection.y,
               viewDirection.z));
    float lensSquared = dot(stretchedView.xy, stretchedView.xy);
    float3 firstTangent = lensSquared > 0.0 ?
        float3(-stretchedView.y, stretchedView.x, 0.0) / sqrt(lensSquared) :
        float3(1.0, 0.0, 0.0);
    float3 secondTangent = cross(stretchedView, firstTangent);
    float radius = sqrt(randomUnit(seed));
    float angle = 2.0 * Pi * randomUnit(seed);
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

bool sampleBsdf(SurfaceData surface, float3 viewDirection, inout uint seed,
                out float3 lightDirection, out BsdfEvaluation evaluation)
{
    float3 diffuseReflectance = surface.baseColor * (1.0 - surface.metalness);
    float3 f0 = lerp(0.04.xxx, surface.baseColor, surface.metalness);
    float chooseSpecular = specularProbability(diffuseReflectance, f0);
    if (randomUnit(seed) < chooseSpecular) {
        float3 tangent;
        float3 bitangent;
        coordinateSystem(surface.shadingNormal, tangent, bitangent);
        float3 localView = float3(dot(viewDirection, tangent),
                                  dot(viewDirection, bitangent),
                                  dot(viewDirection, surface.shadingNormal));
        float alpha = surface.roughness * surface.roughness;
        float3 localHalf = sampleGgxVisibleNormal(localView, alpha, seed);
        float3 halfVector = normalize(tangent * localHalf.x +
            bitangent * localHalf.y + surface.shadingNormal * localHalf.z);
        lightDirection = reflect(-viewDirection, halfVector);
    } else {
        lightDirection = cosineHemisphere(surface.shadingNormal, seed);
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
                                 inout uint seed)
{
    float3 lightDirection = cosineHemisphere(surface.shadingNormal, seed);
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
                             inout uint seed)
{
    if (EmitterCount == 0u) {
        return 0.0;
    }
    float selection = randomUnit(seed);
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
    float root = sqrt(randomUnit(seed));
    float secondRandom = randomUnit(seed);
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
        BaseColorAtlas.Load(int3(lightTexel, 0)).rgb *
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

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint seed = pixel.x * 1973u + pixel.y * 9277u +
        FrameIndex * 26699u + 911u;
    float2 jitter = float2(randomUnit(seed), randomUnit(seed));
    float2 screen = (float2(pixel) + jitter) / float2(dimensions);
    float2 ndc = float2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    float3 direction = normalize(CameraForward +
        CameraRight * (ndc.x * Aspect * TanHalfFovY) +
        CameraUp * (ndc.y * TanHalfFovY));

    float3 radiance = 0.0;
    float3 throughput = 1.0;
    float previousBsdfPdf = 0.0;
    float3 previousPosition = 0.0;
    float3 previousNormal = 0.0;
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
        if (payload.hit == 0u) {
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
        if (any(surface.emission > 0.0)) {
            float weight = depth == 0u ? 1.0 : powerHeuristic(
                previousBsdfPdf,
                emitterPdfForHit(surface, previousPosition));
            radiance += throughput * surface.emission * weight;
        }
        radiance += throughput *
            sampleEnvironmentLighting(surface, viewDirection, seed);
        radiance += throughput * sampleEmitterLighting(surface, viewDirection, seed);

        if (depth + 1u >= MaximumDepth) {
            break;
        }
        float3 bounceDirection;
        BsdfEvaluation bsdf;
        if (!sampleBsdf(surface, viewDirection, seed, bounceDirection, bsdf)) {
            break;
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
    payload.primitiveIndex = PrimitiveIndex();
    payload.hit = 1u;
}
