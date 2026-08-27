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

}  // namespace ab3d2::dxr::post_processing

#endif
