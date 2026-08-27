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

inline float hdr_scene_scale(float peak_nits)
{
    return std::isfinite(peak_nits) && peak_nits > 0.0f ?
        peak_nits / sc_rgb_reference_white_nits : 0.0f;
}

inline float hdr_sc_rgb_luminance(float mapped_luminance, float peak_nits)
{
    if (!std::isfinite(mapped_luminance)) {
        return 0.0f;
    }
    return std::max(mapped_luminance, 0.0f) * hdr_scene_scale(peak_nits);
}

inline float hdr_saturation_channel(float channel, float luminance,
                                    float saturation_scale)
{
    if (!std::isfinite(channel) || !std::isfinite(luminance) ||
        !std::isfinite(saturation_scale)) {
        return 0.0f;
    }
    return std::max(luminance * (1.0f - saturation_scale) +
                        channel * saturation_scale,
                    0.0f);
}

}  // namespace ab3d2::dxr::post_processing

#endif
