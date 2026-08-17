struct SceneVertex
{
    float3 position;
    float2 textureCoordinate;
    uint materialIndex;
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

struct RadiancePayload
{
    float3 radiance;
    uint seed;
    uint depth;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<SceneVertex> Vertices : register(t1);
StructuredBuffer<SceneMaterial> Materials : register(t2);
Texture2D<float4> AlbedoAtlas : register(t3);
Texture2D<float4> NormalAtlas : register(t4);
Texture2D<float4> MetalnessAtlas : register(t5);
Texture2D<float4> RoughnessAtlas : register(t6);
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
    uint Reserved;
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

float3 environmentRadiance(float3 direction)
{
    float horizon = saturate(direction.y * 0.5 + 0.5);
    return lerp(float3(0.025, 0.035, 0.055), float3(0.8, 0.95, 1.2),
                horizon * horizon);
}

float3 cosineHemisphere(float3 normal, inout uint seed)
{
    float first = randomUnit(seed);
    float second = randomUnit(seed);
    float radius = sqrt(first);
    float angle = 6.28318530718 * second;
    float3 helper = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) :
                                            float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(helper, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * (radius * cos(angle)) +
                     bitangent * (radius * sin(angle)) +
                     normal * sqrt(max(0.0, 1.0 - first)));
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dimensions = DispatchRaysDimensions().xy;
    uint seed = pixel.x * 1973u + pixel.y * 9277u + FrameIndex * 26699u + 911u;
    float2 jitter = float2(randomUnit(seed), randomUnit(seed));
    float2 screen = (float2(pixel) + jitter) / float2(dimensions);
    float2 ndc = float2(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0);
    float3 direction = normalize(CameraForward +
        CameraRight * (ndc.x * Aspect * TanHalfFovY) +
        CameraUp * (ndc.y * TanHalfFovY));

    RayDesc ray;
    ray.Origin = CameraPosition;
    ray.Direction = direction;
    ray.TMin = 0.05;
    ray.TMax = 8192.0;
    RadiancePayload payload;
    payload.radiance = 0.0;
    payload.seed = seed;
    payload.depth = 0u;
    TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    NoisyRadiance[pixel] = float4(payload.radiance, 1.0);
}

[shader("miss")]
void Miss(inout RadiancePayload payload)
{
    payload.radiance = environmentRadiance(WorldRayDirection());
}

[shader("closesthit")]
void ClosestHit(inout RadiancePayload payload,
                BuiltInTriangleIntersectionAttributes attributes)
{
    uint baseIndex = PrimitiveIndex() * 3u;
    SceneVertex first = Vertices[baseIndex];
    SceneVertex second = Vertices[baseIndex + 1u];
    SceneVertex third = Vertices[baseIndex + 2u];
    float thirdWeight = 1.0 - attributes.barycentrics.x - attributes.barycentrics.y;
    float2 textureCoordinate = first.textureCoordinate * thirdWeight +
        second.textureCoordinate * attributes.barycentrics.x +
        third.textureCoordinate * attributes.barycentrics.y;
    SceneMaterial material = Materials[first.materialIndex];
    float2 wrapped = frac(textureCoordinate);
    uint2 texel = uint2(material.atlasX, material.atlasY) +
        min(uint2(wrapped * float2(material.width, material.height)),
            uint2(material.width - 1u, material.height - 1u));
    float3 baseColor = saturate(AlbedoAtlas.Load(int3(texel, 0)).rgb);
    float3 normal = normalize(cross(second.position - first.position,
                                    third.position - first.position));
    if (dot(normal, WorldRayDirection()) > 0.0) {
        normal = -normal;
    }
    /* Phase 5 primary-visibility diagnostic: one fresh cosine-weighted
       Lambertian environment sample exposes geometry/material/UV errors even
       in sealed rooms, before authored emitters and traced light visibility
       make the Phase 7 path integrator self-sufficient. */
    float3 bounceDirection = cosineHemisphere(normal, payload.seed);
    payload.radiance = baseColor * environmentRadiance(bounceDirection);
}
