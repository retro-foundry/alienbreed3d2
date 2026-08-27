Texture2D<float4> InputRadiance : register(t0);
Texture2D<float4> BloomInput : register(t1);
Texture2D<float4> BloomLow : register(t2);
RWStructuredBuffer<uint> LuminanceHistogram : register(u0);
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
static const uint ExposureStateIndex = ToneCurvePointCount;
static const uint TargetExposureStateIndex = ExposureStateIndex + 1u;
static const uint AverageLuminanceStateIndex = ExposureStateIndex + 2u;
static const uint LowLuminanceStateIndex = ExposureStateIndex + 3u;
static const uint HighLuminanceStateIndex = ExposureStateIndex + 4u;
static const float MinimumLogLuminance = -18.0;
static const float MaximumLogLuminance = 8.0;
static const float MeteringKey = 0.014;
static const float ToneToeLuminance = 0.02;
static const float MinimumExposure = 0.125;
static const float MaximumExposure = 4096.0;
static const float BloomSoftThreshold = 0.02;
static const float BloomStrength = 0.08;
static const float BloomUpsampleWeight = 0.5;
static const uint BloomExtract = 0u;
static const uint BloomDownsample = 1u;
static const uint BloomBlurHorizontal = 2u;
static const uint BloomBlurVertical = 3u;
static const uint BloomUpsample = 4u;
static const uint BloomComposite = 5u;

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

uint histogramIndex(float logLuminance)
{
    float normalized = saturate(
        (logLuminance - MinimumLogLuminance) /
        (MaximumLogLuminance - MinimumLogLuminance));
    return min(uint(normalized * float(HistogramBinCount)),
               HistogramBinCount - 1u);
}

float histogramLogLuminance(uint index)
{
    return lerp(MinimumLogLuminance, MaximumLogLuminance,
                (float(min(index, HistogramBinCount - 1u)) + 0.5) /
                    float(HistogramBinCount));
}

/*
 * The histogram consumes the post-reconstruction image. Isolated luminance
 * impulses receive less weight than values supported by their four immediate
 * neighbours, preventing one reconstructed firefly from steering the whole
 * frame. A mild centre weighting keeps the view direction important without
 * excluding the hallway and doorway at the image edge.
 */
[numthreads(8, 8, 1)]
void histogram_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= SourceWidth || pixel.y >= SourceHeight) {
        return;
    }
    float centerLuminance = luminance(max(
        InputRadiance.Load(int3(pixel, 0)).rgb, 0.0));
    if (!(centerLuminance > 0.0) || !isfinite(centerLuminance)) {
        return;
    }
    float centerLog = log2(clamp(centerLuminance,
                                 exp2(MinimumLogLuminance),
                                 exp2(MaximumLogLuminance)));
    int2 offsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    float neighbourLogSum = 0.0;
    float neighbourCount = 0.0;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex) {
        int2 neighbourPixel = clamp(
            int2(pixel) + offsets[sampleIndex], int2(0, 0),
            int2(SourceWidth - 1u, SourceHeight - 1u));
        float neighbourLuminance = luminance(max(
            InputRadiance.Load(int3(neighbourPixel, 0)).rgb, 0.0));
        if (neighbourLuminance > 0.0 && isfinite(neighbourLuminance)) {
            neighbourLogSum += log2(clamp(
                neighbourLuminance, exp2(MinimumLogLuminance),
                exp2(MaximumLogLuminance)));
            neighbourCount += 1.0;
        }
    }
    float logDifference = neighbourCount > 0.0 ?
        abs(centerLog - neighbourLogSum / neighbourCount) : 4.0;
    float consistency = exp2(-min(logDifference, 8.0));
    float2 normalizedPosition =
        (float2(pixel) + 0.5) / float2(SourceWidth, SourceHeight);
    float2 centered = normalizedPosition * 2.0 - 1.0;
    float centreWeight = lerp(0.75, 1.25,
        saturate(1.0 - dot(centered, centered) * 0.5));
    uint fixedWeight = max(1u, uint(round(
        16.0 * centreWeight * lerp(0.25, 1.0, consistency))));
    InterlockedAdd(LuminanceHistogram[histogramIndex(centerLog)], fixedWeight);
}

float toneMapLuminance(float exposedLuminance)
{
    if (!(exposedLuminance > 0.0) || !isfinite(exposedLuminance)) {
        return 0.0;
    }
    float toe = exposedLuminance * exposedLuminance /
        (exposedLuminance + ToneToeLuminance);
    return saturate(toe / (1.0 + toe));
}

/*
 * One thread deliberately owns the 128-bin curve. The work is tiny compared
 * with a frame dispatch and the serial ordering makes the temporal state and
 * diagnostics deterministic. The noise-weighted histogram adapts exposure;
 * the project-calibrated quadratic toe prevents post-reconstruction near-black
 * transport from being promoted to visible grey, while the rational shoulder
 * retains highlight separation without clipping.
 */
[numthreads(1, 1, 1)]
void curve_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (any(dispatchThreadId != 0u)) {
        return;
    }
    uint totalWeight = 0u;
    [loop]
    for (uint bin = 0u; bin < HistogramBinCount; ++bin) {
        totalWeight += LuminanceHistogram[bin];
    }

    if (totalWeight == 0u) {
        [loop]
        for (uint emptyBin = 0u; emptyBin < ToneCurvePointCount; ++emptyBin) {
            float inputLog = lerp(
                MinimumLogLuminance, MaximumLogLuminance,
                float(emptyBin) / float(HistogramBinCount));
            ToneMapState[emptyBin] = toneMapLuminance(exp2(inputLog));
        }
        ToneMapState[ExposureStateIndex] = 1.0;
        ToneMapState[TargetExposureStateIndex] = 1.0;
        ToneMapState[AverageLuminanceStateIndex] = 0.0;
        ToneMapState[LowLuminanceStateIndex] = 0.0;
        ToneMapState[HighLuminanceStateIndex] = 0.0;
        Diagnostics[5] = asuint(1.0);
        Diagnostics[6] = asuint(1.0);
        Diagnostics[7] = 0u;
        Diagnostics[8] = 0u;
        Diagnostics[9] = 0u;
        Diagnostics[10] = 0u;
        return;
    }

    uint lowRank = totalWeight * 2u / 100u;
    uint highRank = max(lowRank + 1u, totalWeight * 99u / 100u);
    uint meterLowRank = totalWeight * 10u / 100u;
    uint meterHighRank = max(meterLowRank + 1u, totalWeight * 90u / 100u);
    uint cumulative = 0u;
    uint lowBin = 0u;
    uint highBin = HistogramBinCount - 1u;
    uint meterWeight = 0u;
    float weightedLogSum = 0.0;
    bool lowFound = false;
    bool highFound = false;
    [loop]
    for (uint scanBin = 0u; scanBin < HistogramBinCount; ++scanBin) {
        uint next = cumulative + LuminanceHistogram[scanBin];
        if (!lowFound && next > lowRank) {
            lowBin = scanBin;
            lowFound = true;
        }
        if (!highFound && next >= highRank) {
            highBin = scanBin;
            highFound = true;
        }
        uint includedBegin = max(cumulative, meterLowRank);
        uint includedEnd = min(next, meterHighRank);
        if (includedEnd > includedBegin) {
            uint included = includedEnd - includedBegin;
            weightedLogSum += float(included) *
                histogramLogLuminance(scanBin);
            meterWeight += included;
        }
        cumulative = next;
    }
    float averageLuminance = meterWeight > 0u ?
        exp2(weightedLogSum / float(meterWeight)) : 0.0;
    float targetExposure = averageLuminance > 0.0 ?
        clamp(MeteringKey / averageLuminance,
              MinimumExposure, MaximumExposure) : 1.0;
    float previousExposure = ToneMapState[ExposureStateIndex];
    bool reset = ResetHistory != 0u || !(previousExposure > 0.0) ||
        !isfinite(previousExposure);
    float elapsed = clamp(isfinite(DeltaSeconds) ? DeltaSeconds : 0.0,
                          0.0, 0.25);
    float exposureRate = targetExposure > previousExposure ? 1.0 : 4.0;
    float exposureWeight = reset ? 1.0 :
        1.0 - exp(-exposureRate * elapsed);
    float adaptedExposure = reset ? targetExposure : lerp(
        previousExposure, targetExposure, exposureWeight);

    [loop]
    for (uint curvePoint = 0u; curvePoint < ToneCurvePointCount;
         ++curvePoint) {
        float inputLog = lerp(
            MinimumLogLuminance, MaximumLogLuminance,
            float(curvePoint) / float(HistogramBinCount));
        float targetMapped = toneMapLuminance(
            exp2(inputLog) * adaptedExposure);
        float previousMapped = ToneMapState[curvePoint];
        float curveRate = targetMapped > previousMapped ? 2.0 : 6.0;
        float curveWeight = reset || !isfinite(previousMapped) ? 1.0 :
            1.0 - exp(-curveRate * elapsed);
        ToneMapState[curvePoint] = reset || !isfinite(previousMapped) ?
            targetMapped : saturate(lerp(
                previousMapped, targetMapped, curveWeight));
    }

    float lowLuminance = exp2(histogramLogLuminance(lowBin));
    float highLuminance = exp2(histogramLogLuminance(highBin));
    ToneMapState[ExposureStateIndex] = adaptedExposure;
    ToneMapState[TargetExposureStateIndex] = targetExposure;
    ToneMapState[AverageLuminanceStateIndex] = averageLuminance;
    ToneMapState[LowLuminanceStateIndex] = lowLuminance;
    ToneMapState[HighLuminanceStateIndex] = highLuminance;
    Diagnostics[5] = asuint(targetExposure);
    Diagnostics[6] = asuint(adaptedExposure);
    Diagnostics[7] = asuint(averageLuminance);
    Diagnostics[8] = asuint(lowLuminance);
    Diagnostics[9] = asuint(highLuminance);
    Diagnostics[10] = totalWeight;
}
