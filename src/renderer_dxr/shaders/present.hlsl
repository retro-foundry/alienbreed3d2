Texture2D<float4> NoisyRadiance : register(t0);
Texture2D<float4> DiffuseAlbedo : register(t1);
Texture2D<float4> SpecularAlbedo : register(t2);
Texture2D<float4> ShadingNormal : register(t3);
Texture2D<float4> LinearRoughness : register(t4);
Texture2D<float4> LinearDepth : register(t5);
Texture2D<float4> SceneMotion : register(t6);
Texture2D<float4> SpecularHitDistance : register(t7);
Texture2D<float4> DiffuseHitDistance : register(t8);
Texture2D<float4> SpecularHitDistanceHistory : register(t9);
Texture2D<float4> IndirectRadiance : register(t10);
StructuredBuffer<float> ToneMapState : register(t11);
ByteAddressBuffer BlueNoiseSampler : register(t12);

cbuffer PresentConstants : register(b0)
{
    uint DebugView;
    float ScalarRange;
    uint SourceWidth;
    uint SourceHeight;
    uint TargetWidth;
    uint TargetHeight;
    float Exposure;
    uint FrameIndex;
};

static const uint ToneCurvePointCount = 129u;
static const float MinimumLogLuminance = -18.0;
static const float MaximumLogLuminance = 8.0;
static const uint BlueNoiseSampleCount = 256u;
static const uint BlueNoiseDimensionCount = 256u;
static const uint BlueNoiseTileWidth = 128u;
static const uint BlueNoiseOptimizedDimensions = 8u;
static const uint BlueNoiseSobolOffset = 0u;
static const uint BlueNoiseScramblingOffset = 65536u;
static const uint BlueNoiseRankingOffset = 196608u;

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

float3 linearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return select(color <= 0.0031308, low, high);
}

float3 displayLinear(float3 color)
{
    return linearToSrgb(saturate(color));
}

uint blueNoiseByte(uint byteOffset)
{
    uint word = BlueNoiseSampler.Load(byteOffset & ~3u);
    return (word >> ((byteOffset & 3u) * 8u)) & 0xffu;
}

/* Use the renderer's pinned Heitz et al. Owen-scrambled Sobol package for the
 * final 8-bit SDR quantization step. Independent optimized dimensions avoid
 * correlated RGB band edges; every channel remains strictly within half of
 * one UNORM code so exact black and white still quantize to their endpoints. */
float sampleBlueNoise(uint2 pixel, uint sampleIndex, uint dimension)
{
    dimension &= BlueNoiseDimensionCount - 1u;
    uint dimensionGroup = dimension / BlueNoiseOptimizedDimensions;
    uint sampleCycle = sampleIndex / BlueNoiseSampleCount;
    uint2 tilePixel = (pixel + uint2(
        dimensionGroup * 37u + sampleCycle * 53u,
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

float3 ditherSdr(float3 encoded, uint2 pixel)
{
    float3 noise = float3(
        sampleBlueNoise(pixel, FrameIndex, 0u),
        sampleBlueNoise(pixel, FrameIndex, 1u),
        sampleBlueNoise(pixel, FrameIndex, 2u));
    return saturate(encoded + (noise - 0.5) / 255.0);
}

float adaptiveToneMapLuminance(float exposedLuminance)
{
    if (!(exposedLuminance > 0.0) || !isfinite(exposedLuminance)) {
        return 0.0;
    }
    float inputLog = log2(clamp(
        exposedLuminance, exp2(MinimumLogLuminance),
        exp2(MaximumLogLuminance)));
    float curvePosition = saturate(
        (inputLog - MinimumLogLuminance) /
        (MaximumLogLuminance - MinimumLogLuminance)) *
        float(ToneCurvePointCount - 1u);
    uint leftPoint = min(uint(curvePosition), ToneCurvePointCount - 2u);
    return lerp(ToneMapState[leftPoint], ToneMapState[leftPoint + 1u],
                curvePosition - float(leftPoint));
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
        float distance = DiffuseHitDistance.Load(int3(pixel, 0)).r;
        return float4(saturate(distance / ScalarRange).xxx, 1.0);
    }
    if (DebugView == 9u) {
        float distance = SpecularHitDistanceHistory.Load(int3(pixel, 0)).r;
        return float4(saturate(distance / ScalarRange).xxx, 1.0);
    }
    if (DebugView == 10u) {
        return float4(displayLinear(
            IndirectRadiance.Load(int3(pixel, 0)).rgb), 1.0);
    }
    float3 hdr = max(NoisyRadiance.Load(int3(pixel, 0)).rgb, 0.0);
    float3 exposed = hdr * Exposure;
    float exposedLuminance = dot(exposed, float3(0.2126, 0.7152, 0.0722));
    float mappedLuminance = adaptiveToneMapLuminance(exposedLuminance);
    float3 mapped = exposedLuminance > 0.0 ?
        exposed * (mappedLuminance / exposedLuminance) : 0.0;
    /* Preserve hue when a saturated HDR color extends outside the display
     * gamut instead of clipping each component independently. */
    float maximumComponent = max(mapped.r, max(mapped.g, mapped.b));
    mapped /= max(maximumComponent, 1.0);
    uint2 targetPixel = min(uint2(input.position.xy),
        uint2(max(TargetWidth, 1u) - 1u, max(TargetHeight, 1u) - 1u));
    return float4(ditherSdr(linearToSrgb(max(mapped, 0.0)), targetPixel),
                  1.0);
}
