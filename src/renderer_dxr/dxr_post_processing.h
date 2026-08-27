#ifndef AB3D2_DXR_POST_PROCESSING_H
#define AB3D2_DXR_POST_PROCESSING_H

#include <algorithm>
#include <cmath>

namespace ab3d2::dxr::post_processing {

/* CPU mirror of the scalar bloom extraction and SDR quantization contracts in
 * shaders/post_process.hlsl and shaders/present.hlsl. The production filters
 * operate on linear FP16 RGB; these helpers pin their response and bounds. */
inline constexpr float bloom_soft_threshold = 0.02f;
inline constexpr float bloom_strength = 0.08f;
inline constexpr float bloom_upsample_weight = 0.5f;
inline constexpr float sdr_quantization_step = 1.0f / 255.0f;
inline constexpr float sc_rgb_reference_white_nits = 80.0f;
inline constexpr float hdr_paper_white_curve_level = 0.75f;

inline float bloom_extraction_weight(float luminance)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance)) {
        return 0.0f;
    }
    return luminance / std::max(
        luminance + bloom_soft_threshold, 1.0e-6f);
}

inline float dither_sdr(float encoded, float blue_noise_sample)
{
    if (!std::isfinite(encoded) || !std::isfinite(blue_noise_sample)) {
        return 0.0f;
    }
    return std::clamp(
        encoded + (blue_noise_sample - 0.5f) * sdr_quantization_step,
        0.0f, 1.0f);
}

inline float hdr_mapped_nits(float mapped_luminance, float peak_nits,
                             float paper_white_nits)
{
    const float peak = std::max(peak_nits, sc_rgb_reference_white_nits);
    const float paper_white = std::clamp(
        paper_white_nits, sc_rgb_reference_white_nits, peak);
    const float mapped = std::clamp(
        std::isfinite(mapped_luminance) ? mapped_luminance : 0.0f,
        0.0f, 1.0f);
    if (mapped <= hdr_paper_white_curve_level) {
        return mapped * (paper_white / hdr_paper_white_curve_level);
    }
    if (peak <= paper_white) {
        return paper_white;
    }
    const float t = (mapped - hdr_paper_white_curve_level) /
        (1.0f - hdr_paper_white_curve_level);
    const float slope = (paper_white / hdr_paper_white_curve_level) *
        (1.0f - hdr_paper_white_curve_level) / (peak - paper_white);
    const float shoulder = slope * t + (3.0f - 2.0f * slope) * t * t +
        (slope - 2.0f) * t * t * t;
    return paper_white + (peak - paper_white) *
        std::clamp(shoulder, 0.0f, 1.0f);
}

inline float hdr_sc_rgb_luminance(float mapped_luminance, float peak_nits,
                                  float paper_white_nits)
{
    return hdr_mapped_nits(mapped_luminance, peak_nits, paper_white_nits) /
        sc_rgb_reference_white_nits;
}

}  // namespace ab3d2::dxr::post_processing

#endif
