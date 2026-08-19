Texture2D<float4> NoisyRadiance : register(t0);
Texture2D<float4> DiffuseAlbedo : register(t1);
Texture2D<float4> SpecularAlbedo : register(t2);
Texture2D<float4> ShadingNormal : register(t3);
Texture2D<float4> LinearRoughness : register(t4);
Texture2D<float4> LinearDepth : register(t5);
Texture2D<float4> SceneMotion : register(t6);
Texture2D<float4> SpecularHitDistance : register(t7);
Texture2D<float4> SpecularHitDistanceHistory : register(t8);

cbuffer PresentConstants : register(b0)
{
    uint DebugView;
    float ScalarRange;
    uint SourceWidth;
    uint SourceHeight;
    uint TargetWidth;
    uint TargetHeight;
    float Exposure;
};

struct PixelInput
{
    float4 position : SV_Position;
};

PixelInput vs_main(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };
    PixelInput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    return output;
}

float3 displayLinear(float3 color)
{
    return pow(saturate(color), 1.0 / 2.2);
}

float3 hsvToRgb(float3 hsv)
{
    float3 shifted = abs(frac(hsv.xxx + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) *
                         6.0 - 3.0);
    return hsv.z * lerp(1.0.xxx, saturate(shifted - 1.0), hsv.y);
}

float4 ps_main(PixelInput input) : SV_Target
{
    float2 normalized = input.position.xy /
        max(float2(TargetWidth, TargetHeight), float2(1.0, 1.0));
    uint2 pixel = min(uint2(normalized * float2(SourceWidth, SourceHeight)),
                      uint2(SourceWidth - 1u, SourceHeight - 1u));
    if (DebugView == 1u) {
        return float4(displayLinear(DiffuseAlbedo.Load(int3(pixel, 0)).rgb),
                      1.0);
    }
    if (DebugView == 2u) {
        return float4(displayLinear(SpecularAlbedo.Load(int3(pixel, 0)).rgb),
                      1.0);
    }
    if (DebugView == 3u) {
        float4 normal = ShadingNormal.Load(int3(pixel, 0));
        return float4(normal.a > 0.0 ? normal.rgb * 0.5 + 0.5 : 0.0, 1.0);
    }
    if (DebugView == 4u) {
        float roughness = LinearRoughness.Load(int3(pixel, 0)).r;
        return float4(roughness.xxx, 1.0);
    }
    if (DebugView == 5u) {
        float depth = LinearDepth.Load(int3(pixel, 0)).r;
        return float4(saturate(depth / ScalarRange).xxx, 1.0);
    }
    if (DebugView == 6u) {
        float2 motion = SceneMotion.Load(int3(pixel, 0)).rg;
        if (any(abs(motion) >= 65503.0)) {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        float magnitude = length(motion);
        float hue = frac(atan2(motion.y, motion.x) / (2.0 * 3.14159265358979323846) +
                         1.0);
        return float4(hsvToRgb(float3(hue, magnitude > 0.0 ? 1.0 : 0.0,
                                      saturate(magnitude / ScalarRange))), 1.0);
    }
    if (DebugView == 7u) {
        float distance = SpecularHitDistance.Load(int3(pixel, 0)).r;
        return float4(saturate(distance / ScalarRange).xxx, 1.0);
    }
    if (DebugView == 8u) {
        float distance = SpecularHitDistanceHistory.Load(int3(pixel, 0)).r;
        return float4(saturate(distance / ScalarRange).xxx, 1.0);
    }
    float3 hdr = max(NoisyRadiance.Load(int3(pixel, 0)).rgb, 0.0);
    float3 exposed = hdr * Exposure;
    /* Krzysztof Narkowicz ACES filmic approximation. */
    float3 mapped = saturate((exposed * (2.51 * exposed + 0.03)) /
                             (exposed * (2.43 * exposed + 0.59) + 0.14));
    mapped = pow(mapped, 1.0 / 2.2);
    return float4(mapped, 1.0);
}
