Texture2D<float4> InputRadiance : register(t0);
RWStructuredBuffer<uint> LuminanceHistogram : register(u0);
RWStructuredBuffer<float> ToneMapState : register(u1);
RWStructuredBuffer<uint> Diagnostics : register(u2);

cbuffer PostConstants : register(b0)
{
    uint SourceWidth;
    uint SourceHeight;
    float DeltaSeconds;
    uint ResetHistory;
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
static const float MiddleGrey = 0.18;
static const float MinimumExposure = 1.0 / 32.0;
static const float MaximumExposure = 4096.0;
static const float DisplayBlackLog = -8.0;
static const float DisplayWhiteLog = -0.0740005814; // log2(0.95)

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
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

float reinhardWhite(float exposedLuminance)
{
    const float whitePoint = 4.0;
    float mapped = exposedLuminance *
        (1.0 + exposedLuminance / (whitePoint * whitePoint)) /
        (1.0 + exposedLuminance);
    return saturate(mapped);
}

/*
 * One thread deliberately owns the 128-bin curve. The work is tiny compared
 * with a frame dispatch and the serial ordering makes the temporal state and
 * diagnostics deterministic. The adaptive branch distributes display range
 * according to a blurred square-root histogram; blending it in log space with
 * a photographic exposure curve preserves monotonicity while giving occupied
 * dark ranges more contrast than a fixed global toe.
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
            ToneMapState[emptyBin] = reinhardWhite(exp2(inputLog));
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
        clamp(MiddleGrey / averageLuminance,
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

    float importanceSum = 0.0;
    [loop]
    for (uint importanceBin = lowBin; importanceBin <= highBin;
         ++importanceBin) {
        uint left = importanceBin > 0u ? importanceBin - 1u : 0u;
        uint right = min(importanceBin + 1u, HistogramBinCount - 1u);
        float blurred = float(LuminanceHistogram[left]) * 0.25 +
            float(LuminanceHistogram[importanceBin]) * 0.5 +
            float(LuminanceHistogram[right]) * 0.25;
        importanceSum += sqrt(max(blurred / float(totalWeight), 1.0e-8));
    }

    float accumulatedImportance = 0.0;
    [loop]
    for (uint curvePoint = 0u; curvePoint < ToneCurvePointCount;
         ++curvePoint) {
        uint sourceBin = min(curvePoint, HistogramBinCount - 1u);
        if (sourceBin >= lowBin && sourceBin <= highBin) {
            uint left = sourceBin > 0u ? sourceBin - 1u : 0u;
            uint right = min(sourceBin + 1u, HistogramBinCount - 1u);
            float blurred = float(LuminanceHistogram[left]) * 0.25 +
                float(LuminanceHistogram[sourceBin]) * 0.5 +
                float(LuminanceHistogram[right]) * 0.25;
            accumulatedImportance +=
                sqrt(max(blurred / float(totalWeight), 1.0e-8));
        }
        float inputLog = lerp(
            MinimumLogLuminance, MaximumLogLuminance,
            float(curvePoint) / float(HistogramBinCount));
        float baseMapped = reinhardWhite(exp2(inputLog) * adaptedExposure);
        float targetMapped = baseMapped;
        if (sourceBin >= lowBin && sourceBin <= highBin &&
            importanceSum > 0.0) {
            float adaptivePosition =
                saturate(accumulatedImportance / importanceSum);
            float adaptiveMapped = exp2(lerp(
                DisplayBlackLog, DisplayWhiteLog, adaptivePosition));
            float baseLog = log2(max(baseMapped, exp2(DisplayBlackLog)));
            float adaptiveLog = log2(max(
                adaptiveMapped, exp2(DisplayBlackLog)));
            targetMapped = exp2(lerp(baseLog, adaptiveLog, 0.35));
        }
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
