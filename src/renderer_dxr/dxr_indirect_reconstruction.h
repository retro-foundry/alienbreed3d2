#ifndef AB3D2_DXR_INDIRECT_RECONSTRUCTION_H
#define AB3D2_DXR_INDIRECT_RECONSTRUCTION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::indirect_reconstruction {

enum class Mode : uint32_t {
    full = 0u,
    temporal = 1u,
    raw = 2u,
    regional = 3u,
    deflicker = 4u,
    wavelet1 = 5u,
    wavelet2 = 6u,
    restir = 7u,
};

/* Startup-only diagnostic composition. The indirect option preserves the
 * ordinary path trace and every reconstruction guide, but publishes only the
 * reconstructed secondary diffuse channel to the single DLSS-RR evaluation. */
enum class RadianceChannel : uint32_t {
    combined = 0u,
    indirect = 1u,
};

/*
 * Project-owned low-frequency diffuse reconstruction constants mirrored by
 * shaders/path_trace.hlsl. Temporal incident radiance is integrated over
 * guide-compatible 3x3 full-resolution regions. A deflicker bound and three
 * guide-aware 3x3 wavelet passes operate on that one-third-resolution signal
 * before bilateral reconstruction at the original pixel grid. Each deflicker
 * comparison therefore represents a region, not an individual Monte Carlo
 * path.
 */
inline constexpr int downsample_factor = 3;
inline constexpr std::array<int, 3> filter_steps = {1, 2, 4};
inline constexpr std::array<int, 7> gradient_filter_steps = {
    1, 2, 4, 8, 16, 32, 64};
inline constexpr std::array<float, 2> filter_kernel = {1.0f, 0.5f};
inline constexpr int filter_radius = 1;
inline constexpr int filter_reach = 1 + downsample_factor *
    (filter_steps[0] + filter_steps[1] + filter_steps[2]);
inline constexpr float deflicker_neighbor_factor = 2.0f;
inline constexpr float sh_basis_l0 = 0.282095f;
inline constexpr float sh_basis_l1 = 0.488603f;
inline constexpr float sh_irradiance_l0 = 0.886226f;
inline constexpr float sh_irradiance_l1 = 1.023326f;
inline constexpr float relative_depth_tolerance = 0.1f;
inline constexpr float normal_tolerance = 0.5f;
inline constexpr float temporal_antilag_scale = 0.2f;
inline constexpr float temporal_antilag_history_power = 10.0f;
inline constexpr float temporal_minimum_current_weight = 0.01f;
inline constexpr float temporal_gradient_confirmation_rate = 0.25f;
inline constexpr float temporal_gradient_confirmation_threshold = 0.4f;
/* Q2RTX's low-frequency path deliberately samples slightly more grazing
 * directions than an ordinary cosine hemisphere so a sparse screen block
 * covers broad transport directions. This project-owned sampler mirrors that
 * distributional property, not its implementation. */
inline constexpr float continuation_radial_power = 0.4f;

/* GPU history layout mirrored by IndirectHistoryPixel in path_trace.hlsl.
 * `normal` is the primary geometric normal: normal-map detail must not split
 * room-scale indirect reconstruction into unrelated high-frequency patches. */
struct HistoryPixel {
    float luminance_sh[4];
    float chroma[2];
    float depth;
    uint32_t normal;
    float history_length;
    float gradient_confidence;
};

static_assert(sizeof(HistoryPixel) == 40u);

struct Signal {
    float luminance_sh[4];
    float chroma[2];
};

struct Color {
    float red;
    float green;
    float blue;
};

struct TemporalBlend {
    float history_length;
    float current_weight;
    float antilag;
};

struct GradientConfirmation {
    float confidence;
    float gradient;
};

inline Signal signal_from_radiance(Color color, float direction_x,
                                   float direction_y, float direction_z)
{
    const float co = color.red - color.blue;
    const float base = color.blue + co * 0.5f;
    const float cg = color.green - base;
    const float y = std::max(base + cg * 0.5f, 0.0f);
    return {{direction_x * sh_basis_l1 * y,
             direction_y * sh_basis_l1 * y,
             direction_z * sh_basis_l1 * y,
             sh_basis_l0 * y},
            {co, cg}};
}

inline Color project_signal(const Signal &signal, float normal_x,
                            float normal_y, float normal_z)
{
    const float directional = signal.luminance_sh[0] * normal_x +
        signal.luminance_sh[1] * normal_y +
        signal.luminance_sh[2] * normal_z;
    const float y = std::max(2.0f *
        (sh_irradiance_l1 * directional +
         sh_irradiance_l0 * signal.luminance_sh[3]), 0.0f);
    const float chroma_scale = y * sh_basis_l0 /
        std::max(signal.luminance_sh[3], 1.0e-6f);
    const float co = signal.chroma[0] * chroma_scale;
    const float cg = signal.chroma[1] * chroma_scale;
    const float base = y - cg * 0.5f;
    const float green = cg + base;
    const float blue = base - co * 0.5f;
    const float red = blue + co;
    return {std::max(red, 0.0f), std::max(green, 0.0f),
            std::max(blue, 0.0f)};
}

inline float depth_weight(float center_depth, float sample_depth)
{
    if (!(center_depth > 0.0f) || !(sample_depth > 0.0f) ||
        !std::isfinite(center_depth) || !std::isfinite(sample_depth)) {
        return 0.0f;
    }
    const float relative_difference =
        std::fabs(sample_depth - center_depth) /
        std::max(center_depth, 1.0f);
    return std::max(
        0.0f, 1.0f - relative_difference / relative_depth_tolerance);
}

inline float normal_weight(float normal_dot)
{
    const float accepted = std::max(
        0.0f, std::min(1.0f,
            (normal_dot - normal_tolerance) / (1.0f - normal_tolerance)));
    return accepted * accepted;
}

inline float guide_weight(float center_depth, float sample_depth,
                          float normal_dot)
{
    return depth_weight(center_depth, sample_depth) *
        normal_weight(normal_dot);
}

inline std::array<float, 4> temporal_bilinear_weights(float fraction_x,
                                                       float fraction_y)
{
    fraction_x = std::max(0.0f, std::min(1.0f, fraction_x));
    fraction_y = std::max(0.0f, std::min(1.0f, fraction_y));
    return {(1.0f - fraction_x) * (1.0f - fraction_y),
            fraction_x * (1.0f - fraction_y),
            (1.0f - fraction_x) * fraction_y,
            fraction_x * fraction_y};
}

/* The low-frequency gradient compares broad-region current and accumulated
 * luminance. Squaring relative change keeps ordinary Monte Carlo differences
 * from shortening history while preserving a bounded response to flashes. */
inline float relative_luminance_gradient(float current_luminance,
                                         float previous_luminance)
{
    if (!(current_luminance >= 0.0f) ||
        !(previous_luminance >= 0.0f) ||
        !std::isfinite(current_luminance) ||
        !std::isfinite(previous_luminance)) {
        return 0.0f;
    }
    const float maximum = std::max(current_luminance,
                                   previous_luminance);
    if (!(maximum > 0.0f)) {
        return 0.0f;
    }
    const float relative = std::fabs(current_luminance -
                                     previous_luminance) / maximum;
    return relative * relative;
}

inline GradientConfirmation confirm_gradient(float previous_confidence,
                                             float signed_gradient)
{
    previous_confidence = std::max(-1.0f, std::min(
        1.0f, std::isfinite(previous_confidence) ?
            previous_confidence : 0.0f));
    signed_gradient = std::max(-1.0f, std::min(
        1.0f, std::isfinite(signed_gradient) ? signed_gradient : 0.0f));
    const float confidence = previous_confidence +
        (signed_gradient - previous_confidence) *
            temporal_gradient_confirmation_rate;
    const float confirmed = std::max(0.0f, std::min(1.0f,
        (std::fabs(confidence) - temporal_gradient_confirmation_threshold) /
        (1.0f - temporal_gradient_confirmation_threshold)));
    return {confidence, signed_gradient * signed_gradient * confirmed};
}

inline TemporalBlend temporal_blend(float previous_history_length,
                                    float gradient,
                                    uint32_t history_limit)
{
    if (history_limit == 0u || !(previous_history_length > 0.0f) ||
        !std::isfinite(previous_history_length)) {
        return {1.0f, 1.0f, 0.0f};
    }
    gradient = std::max(0.0f, std::min(1.0f, gradient));
    const float antilag = std::max(0.0f, std::min(
        1.0f, temporal_antilag_scale * gradient));
    const float history_length = std::min(
        previous_history_length * std::pow(
            1.0f - antilag, temporal_antilag_history_power) + 1.0f,
        static_cast<float>(history_limit));
    const float base_weight = std::max(
        temporal_minimum_current_weight,
        1.0f / std::max(history_length, 1.0f));
    const float current_weight = base_weight +
        (1.0f - base_weight) * antilag;
    return {history_length, current_weight, antilag};
}

inline float kernel_weight(int offset)
{
    const int distance = std::abs(offset);
    return distance <= filter_radius ? filter_kernel[distance] : 0.0f;
}

inline float deflicker_scale(float center_luminance,
                             float neighbor_luminance_sum,
                             uint32_t neighbor_count)
{
    if (!(center_luminance > 0.0f) || !std::isfinite(center_luminance) ||
        !(neighbor_luminance_sum >= 0.0f) ||
        !std::isfinite(neighbor_luminance_sum) || neighbor_count == 0u) {
        return 1.0f;
    }
    const float maximum_luminance = deflicker_neighbor_factor *
        neighbor_luminance_sum / static_cast<float>(neighbor_count);
    return std::max(0.0f, std::min(1.0f,
        maximum_luminance / center_luminance));
}

inline constexpr bool filter_support_is_continuous()
{
    int reach = 0;
    for (int step : filter_steps) {
        if (reach > 0 && step > reach * 2 + 1) {
            return false;
        }
        reach += filter_radius * step;
    }
    return 1 + downsample_factor * reach == filter_reach;
}

static_assert(filter_support_is_continuous());

}  // namespace ab3d2::dxr::indirect_reconstruction

#endif
