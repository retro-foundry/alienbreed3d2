Texture2D<float4> InputRadiance : register(t0);
Texture2D<float4> BloomInput : register(t1);
Texture2D<float4> BloomLow : register(t2);
RWBuffer<uint> LuminanceHistogram : register(u0);
RWStructuredBuffer<float> ToneMapState : register(u1);
RWStructuredBuffer<uint> Diagnostics : register(u2);
RWTexture2D<float4> BloomOutput : register(u3);
SamplerState LinearClampSampler : register(s0);

cbuffer PostConstants : register(b0)
{
    uint SourceWidth;
    uint SourceHeight;
    uint TargetWidth;
    uint TargetHeight;
    float DeltaSeconds;
    uint ResetHistory;
    uint BloomOperation;
    uint Reserved;
};

static const uint HistogramBinCount = 128u;
static const uint ToneCurvePointCount = HistogramBinCount + 1u;
static const uint AdaptedLuminanceStateIndex = ToneCurvePointCount;
static const uint TargetLuminanceStateIndex = AdaptedLuminanceStateIndex + 1u;
static const uint AverageLuminanceStateIndex = AdaptedLuminanceStateIndex + 2u;
static const uint LowLuminanceStateIndex = AdaptedLuminanceStateIndex + 3u;
static const uint HighLuminanceStateIndex = AdaptedLuminanceStateIndex + 4u;
static const float MinimumLogLuminance = -24.0;
static const float MaximumLogLuminance = 8.0;
static const float DisplayDynamicRangeStops = 7.0;
static const float MinimumSceneLuminance = 0.0002;
static const float MaximumSceneLuminance = 1.0;
static const float NoiseFloorStops = -12.0;
static const float NoiseFloorBlend = 0.5;
static const float SlopeBlurSigma = 12.0;
static const int SlopeBlurRadius = 13;
static const float ExposureSpeedDown = 1.0;
static const float ExposureSpeedUp = 2.0;
static const float HistogramFractionScale = 128.0;
static const float BloomSoftThreshold = 0.02;
static const float BloomStrength = 0.08;
static const float BloomUpsampleWeight = 0.5;
static const uint BloomExtract = 0u;
static const uint BloomDownsample = 1u;
static const uint BloomBlurHorizontal = 2u;
static const uint BloomBlurVertical = 3u;
static const uint BloomUpsample = 4u;
static const uint BloomComposite = 5u;
groupshared uint GroupHistogram[HistogramBinCount];

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 finiteHdr(float3 color)
{
    return all(isfinite(color)) ? clamp(color, 0.0, 65504.0) : 0.0;
}

float3 extractBloom(float3 color)
{
    color = finiteHdr(color);
    float value = luminance(color);
    return color * (value / max(value + BloomSoftThreshold, 1.0e-6));
}

float3 loadBloomClamped(int2 coordinate)
{
    int2 maximum = int2(max(SourceWidth, 1u), max(SourceHeight, 1u)) - 1;
    return finiteHdr(BloomInput.Load(int3(
        clamp(coordinate, int2(0, 0), maximum), 0)).rgb);
}

float3 downsampleInput(uint2 pixel, bool extract)
{
    uint2 basePixel = pixel * 2u;
    float3 result = 0.0;
    [unroll]
    for (uint y = 0u; y < 2u; ++y) {
        [unroll]
        for (uint x = 0u; x < 2u; ++x) {
            uint2 sourcePixel = min(
                basePixel + uint2(x, y),
                uint2(max(SourceWidth, 1u) - 1u,
                      max(SourceHeight, 1u) - 1u));
            float3 sampleValue = extract ?
                InputRadiance.Load(int3(sourcePixel, 0)).rgb :
                BloomInput.Load(int3(sourcePixel, 0)).rgb;
            result += extract ? extractBloom(sampleValue) :
                                finiteHdr(sampleValue);
        }
    }
    return result * 0.25;
}

float3 blurBloom(uint2 pixel, bool horizontal)
{
    static const float weights[5] = {
        0.2270270270, 0.1945945946, 0.1216216216,
        0.0540540541, 0.0162162162
    };
    int2 axis = horizontal ? int2(1, 0) : int2(0, 1);
    float3 result = loadBloomClamped(int2(pixel)) * weights[0];
    [unroll]
    for (int offset = 1; offset <= 4; ++offset) {
        result += (loadBloomClamped(int2(pixel) + axis * offset) +
                   loadBloomClamped(int2(pixel) - axis * offset)) *
                  weights[offset];
    }
    return result;
}

float3 sampleBloomLow(uint2 pixel)
{
    float2 uv = (float2(pixel) + 0.5) /
        float2(max(TargetWidth, 1u), max(TargetHeight, 1u));
    return finiteHdr(BloomLow.SampleLevel(LinearClampSampler, uv, 0.0).rgb);
}

/*
 * Linear-HDR bloom stays entirely before histogram metering and tone mapping.
 * Three separately blurred scales give bright source energy a broad footprint;
 * each larger level is folded into the next finer level before one bounded
 * composite is written at the reconstructed image resolution.
 */
[numthreads(8, 8, 1)]
void bloom_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= TargetWidth || pixel.y >= TargetHeight) {
        return;
    }
    float3 result = 0.0;
    if (BloomOperation == BloomExtract) {
        result = downsampleInput(pixel, true);
    } else if (BloomOperation == BloomDownsample) {
        result = downsampleInput(pixel, false);
    } else if (BloomOperation == BloomBlurHorizontal ||
               BloomOperation == BloomBlurVertical) {
        result = blurBloom(pixel,
            BloomOperation == BloomBlurHorizontal);
    } else if (BloomOperation == BloomUpsample) {
        result = loadBloomClamped(int2(pixel)) +
            sampleBloomLow(pixel) * BloomUpsampleWeight;
    } else if (BloomOperation == BloomComposite) {
        float2 uv = (float2(pixel) + 0.5) /
            float2(max(TargetWidth, 1u), max(TargetHeight, 1u));
        float3 source = finiteHdr(
            InputRadiance.Load(int3(pixel, 0)).rgb);
        float3 bloom = finiteHdr(
            BloomInput.SampleLevel(LinearClampSampler, uv, 0.0).rgb);
        result = source + bloom * BloomStrength;
    }
    BloomOutput[pixel] = float4(finiteHdr(result), 1.0);
}

float histogramPosition(float logLuminance)
{
    float normalized = saturate(
        (logLuminance - MinimumLogLuminance) /
        (MaximumLogLuminance - MinimumLogLuminance));
    return min(normalized * float(HistogramBinCount),
               float(HistogramBinCount - 1u));
}

float histogramLogLuminance(uint index)
{
    return lerp(MinimumLogLuminance, MaximumLogLuminance,
                float(min(index, HistogramBinCount - 1u)) /
                    float(HistogramBinCount));
}

/*
 * Q2RTX behavior requested on 2026-08-27: ignore exact black, tent-filter log
 * luminance between adjacent bins, and give the center of the view the largest
 * metering weight. Curve construction handles the explicit noise floor.
 */
[numthreads(16, 16, 1)]
void histogram_main(uint3 dispatchThreadId : SV_DispatchThreadID,
                    uint groupIndex : SV_GroupIndex)
{
    if (groupIndex < HistogramBinCount) {
        GroupHistogram[groupIndex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x < SourceWidth && pixel.y < SourceHeight) {
        float value = luminance(max(
            InputRadiance.Load(int3(pixel, 0)).rgb, 0.0));
        if (value > 0.0 && isfinite(value)) {
            float valueLog = log2(clamp(
                value, exp2(MinimumLogLuminance),
                exp2(MaximumLogLuminance)));
            float position = histogramPosition(valueLog);
            uint left = uint(position);
            uint right = left + 1u;
            float2 uv = (float2(pixel) + 0.5) /
                float2(SourceWidth, SourceHeight);
            float spatialWeight = clamp(
                1.0 - length(uv - 0.5) * 1.5, 0.01, 1.0);
            float rightWeight = frac(position) * spatialWeight;
            float leftWeight = spatialWeight - rightWeight;
            InterlockedAdd(GroupHistogram[left],
                           uint(leftWeight * HistogramFractionScale));
            if (right < HistogramBinCount) {
                InterlockedAdd(GroupHistogram[right],
                               uint(rightWeight * HistogramFractionScale));
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (groupIndex < HistogramBinCount) {
        uint groupWeight = GroupHistogram[groupIndex];
        if (groupWeight != 0u) {
            InterlockedAdd(LuminanceHistogram[groupIndex], groupWeight);
        }
    }
}

/*
 * Independent serial evaluation of the Eilertsen/Mantiuk/Unger
 * minimum-contrast-distortion equations with Q2RTX's observable defaults.
 * One thread avoids a second shared-memory reduction implementation; 128 bins
 * once per frame are negligible beside Ray Reconstruction.
 */
[numthreads(1, 1, 1)]
void curve_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId != 0u)) {
        return;
    }
    uint integerWeight = 0u;
    float distribution[HistogramBinCount];
    float distributionSum = 0.0;
    [loop]
    for (uint bin = 0u; bin < HistogramBinCount; ++bin) {
        integerWeight += LuminanceHistogram[bin];
        distribution[bin] = 1.0 +
            float(LuminanceHistogram[bin]) / HistogramFractionScale;
        distributionSum += distribution[bin];
    }
    [loop]
    for (uint normalizeBin = 0u; normalizeBin < HistogramBinCount;
         ++normalizeBin) {
        distribution[normalizeBin] /= distributionSum;
    }

    float cumulative = 0.0;
    uint lowBin = 0u;
    uint highBin = HistogramBinCount - 1u;
    float meterWeight = 0.0;
    float weightedLogSum = 0.0;
    bool lowFound = false;
    bool highFound = false;
    [loop]
    for (uint scanBin = 0u; scanBin < HistogramBinCount; ++scanBin) {
        float next = cumulative + distribution[scanBin];
        if (!lowFound && next > 0.02) {
            lowBin = scanBin;
            lowFound = true;
        }
        if (!highFound && next >= 0.99) {
            highBin = scanBin;
            highFound = true;
        }
        if (0.70 <= next && cumulative <= 0.90) {
            weightedLogSum += distribution[scanBin] *
                histogramLogLuminance(scanBin);
            meterWeight += distribution[scanBin];
        }
        cumulative = next;
    }
    float averageLuminance = meterWeight > 0u ?
        exp2(weightedLogSum / float(meterWeight)) : 0.0;
    float targetLuminance = clamp(
        averageLuminance, MinimumSceneLuminance, MaximumSceneLuminance);
    float previousLuminance = ToneMapState[AdaptedLuminanceStateIndex];
    bool reset = ResetHistory != 0u || !(previousLuminance > 0.0) ||
        !isfinite(previousLuminance);
    float elapsed = clamp(isfinite(DeltaSeconds) ? DeltaSeconds : 0.0,
                          0.0, 0.25);
    float targetLog = log2(targetLuminance);
    float previousLog = log2(max(previousLuminance, 1.0e-8));
    float exposureRate = previousLog < targetLog ?
        ExposureSpeedUp : ExposureSpeedDown;
    float adaptedLog = reset ? targetLog : lerp(
        targetLog, previousLog, exp(-exposureRate * elapsed));
    float adaptedLuminance = exp2(adaptedLog);

    float inverseDistribution[HistogramBinCount];
    [loop]
    for (uint noiseBin = 0u; noiseBin < HistogramBinCount; ++noiseBin) {
        if (histogramLogLuminance(noiseBin) < NoiseFloorStops) {
            distribution[noiseBin] = 0.0;
        }
        inverseDistribution[noiseBin] = distribution[noiseBin] > 0.0 ?
            1.0 / distribution[noiseBin] : 0.0;
    }

    const float binWidth =
        (MaximumLogLuminance - MinimumLogLuminance) /
        float(HistogramBinCount);
    const float outputRangeBins = DisplayDynamicRangeStops / binWidth;
    float threshold = 1.0e-16;
    float activeCount = 0.0;
    float inverseSum = 0.0;
    [loop]
    for (uint thresholdPass = 0u; thresholdPass < 16u; ++thresholdPass) {
        activeCount = 0.0;
        inverseSum = 0.0;
        [loop]
        for (uint thresholdBin = 0u; thresholdBin < HistogramBinCount;
             ++thresholdBin) {
            if (distribution[thresholdBin] >= threshold) {
                activeCount += 1.0;
                inverseSum += inverseDistribution[thresholdBin];
            }
        }
        threshold = inverseSum > 0.0 ?
            (activeCount - outputRangeBins) / inverseSum : 0.0;
    }

    float slopes[HistogramBinCount];
    [loop]
    for (uint slopeBin = 0u; slopeBin < HistogramBinCount; ++slopeBin) {
        slopes[slopeBin] =
            distribution[slopeBin] >= threshold && inverseSum > 0.0 ?
            1.0 + inverseDistribution[slopeBin] *
                (outputRangeBins - activeCount) / inverseSum : 0.0;
    }

    float gaussian[SlopeBlurRadius + 1];
    float gaussianSum = 0.0;
    [loop]
    for (int gaussianIndex = 0; gaussianIndex <= SlopeBlurRadius;
         ++gaussianIndex) {
        gaussian[gaussianIndex] = exp(
            -float(gaussianIndex * gaussianIndex) /
            (2.0 * SlopeBlurSigma * SlopeBlurSigma));
        gaussianSum += gaussian[gaussianIndex] *
            (gaussianIndex == 0 ? 1.0 : 2.0);
    }
    [loop]
    for (int normalizeIndex = 0; normalizeIndex <= SlopeBlurRadius;
         ++normalizeIndex) {
        gaussian[normalizeIndex] /= gaussianSum;
    }

    float filteredSlopes[HistogramBinCount];
    [loop]
    for (int destinationBin = 0;
         destinationBin < int(HistogramBinCount); ++destinationBin) {
        float filtered = 0.0;
        [loop]
        for (int offset = -SlopeBlurRadius; offset <= SlopeBlurRadius;
             ++offset) {
            int sourceBin = clamp(destinationBin + offset, 0,
                                  int(HistogramBinCount) - 1);
            filtered += slopes[sourceBin] * gaussian[abs(offset)];
        }
        filteredSlopes[destinationBin] = filtered;
    }

    float targetCurve[ToneCurvePointCount];
    float prefix = 0.0;
    [loop]
    for (uint curveBin = 0u; curveBin < HistogramBinCount; ++curveBin) {
        targetCurve[curveBin] = prefix * binWidth -
            DisplayDynamicRangeStops;
        prefix += filteredSlopes[curveBin];
    }
    targetCurve[HistogramBinCount] = 0.0;

    float noisePosition = clamp(
        (NoiseFloorStops - MinimumLogLuminance) /
            (MaximumLogLuminance - MinimumLogLuminance) *
            float(HistogramBinCount),
        1.0, float(HistogramBinCount - 1u));
    uint noiseIndex = uint(noisePosition);
    float curveAtNoise = targetCurve[noiseIndex - 1u];
    float inputAtNoise = histogramLogLuminance(noiseIndex - 1u);
    float correction = abs(targetLog) > 1.0e-8 ?
        -(curveAtNoise - inputAtNoise) / targetLog : 1.0;
    [loop]
    for (uint darkBin = 0u; darkBin < noiseIndex; ++darkBin) {
        float automatic = histogramLogLuminance(darkBin) -
            targetLog * correction;
        float transition = lerp(
            smoothstep(noisePosition * 0.5, noisePosition, float(darkBin)),
            1.0, NoiseFloorBlend);
        targetCurve[darkBin] = lerp(automatic,
                                    targetCurve[darkBin], transition);
    }

    [loop]
    for (uint curvePoint = 0u; curvePoint < ToneCurvePointCount;
         ++curvePoint) {
        float previousCurve = ToneMapState[curvePoint];
        float curveRate = previousCurve < targetCurve[curvePoint] ?
            ExposureSpeedUp : ExposureSpeedDown;
        ToneMapState[curvePoint] = reset || !isfinite(previousCurve) ?
            targetCurve[curvePoint] : lerp(
                targetCurve[curvePoint], previousCurve,
                exp(-curveRate * elapsed));
    }

    float lowLuminance = exp2(histogramLogLuminance(lowBin));
    float highLuminance = exp2(histogramLogLuminance(highBin));
    ToneMapState[AdaptedLuminanceStateIndex] = adaptedLuminance;
    ToneMapState[TargetLuminanceStateIndex] = targetLuminance;
    ToneMapState[AverageLuminanceStateIndex] = averageLuminance;
    ToneMapState[LowLuminanceStateIndex] = lowLuminance;
    ToneMapState[HighLuminanceStateIndex] = highLuminance;
    Diagnostics[5] = asuint(exp2(-3.0) / targetLuminance);
    Diagnostics[6] = asuint(exp2(-3.0) / adaptedLuminance);
    Diagnostics[7] = asuint(averageLuminance);
    Diagnostics[8] = asuint(lowLuminance);
    Diagnostics[9] = asuint(highLuminance);
    Diagnostics[10] = integerWeight;
}
