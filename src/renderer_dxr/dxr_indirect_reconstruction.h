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

/* Startup-only diagnostic composition. Every option preserves the ordinary
 * path trace and reconstruction guides while isolating one contribution at
 * the final noisy-HDR boundary before the single DLSS-RR evaluation. */
enum class RadianceChannel : uint32_t {
    combined = 0u,
    emission = 1u,
    direct_diffuse = 2u,
    direct_specular = 3u,
    indirect = 4u,
    smooth_specular = 5u,
    rough_specular = 6u,
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
/* Production reconstruction treats the configured indirect SPP as a burst
 * ceiling. Guide-valid pixels keep that ceiling until their effective history
 * reaches the temporal cap, then a rotating 2x2 phase spends one fresh path
 * while the other pixels retain validated reprojected history. A persistent
 * lighting gradient restores the ceiling everywhere. Half a sample of
 * tolerance prevents bilinear reprojection round-off from holding an otherwise
 * full history in burst mode forever. */
inline constexpr uint32_t stable_indirect_sample_count = 1u;
inline constexpr uint32_t stable_indirect_sampling_phase_count = 4u;
inline constexpr float adaptive_history_maturity_tolerance = 0.5f;

inline constexpr uint32_t stable_indirect_sampling_phase(
    uint32_t pixel_x, uint32_t pixel_y)
{
    return (pixel_x & 1u) | ((pixel_y & 1u) << 1u);
}

inline constexpr bool stable_indirect_sample_scheduled(
    uint32_t pixel_x, uint32_t pixel_y, uint32_t sample_index)
{
    return stable_indirect_sampling_phase(pixel_x, pixel_y) ==
        sample_index % stable_indirect_sampling_phase_count;
}

inline constexpr bool stable_schedule_covers_gradient_region()
{
    for (uint32_t origin_y = 0u; origin_y < 2u; ++origin_y) {
        for (uint32_t origin_x = 0u; origin_x < 2u; ++origin_x) {
            for (uint32_t phase = 0u;
                 phase < stable_indirect_sampling_phase_count; ++phase) {
                bool covered = false;
                for (uint32_t y = 0u;
                     y < static_cast<uint32_t>(downsample_factor); ++y) {
                    for (uint32_t x = 0u;
                         x < static_cast<uint32_t>(downsample_factor); ++x) {
                        covered = covered ||
                            stable_indirect_sampling_phase(
                                origin_x + x, origin_y + y) == phase;
                    }
                }
                if (!covered) {
                    return false;
                }
            }
        }
    }
    return true;
}

static_assert(stable_schedule_covers_gradient_region());
/* Runtime path-depth and dimension contracts mirrored by path_trace.hlsl.
 * Depth counts the primary surface. The first continuation consumes dimensions
 * 6/7 from group zero; every later continuation advances by one eight-value
 * group. Polygon RIS reserves 1024 streams per reached surface, matching the
 * public candidate-count ceiling. */
inline constexpr uint32_t maximum_path_depth = 8u;
inline constexpr uint32_t path_dimensions_per_continuation = 8u;
inline constexpr uint32_t direction_dimension_x = 6u;
inline constexpr uint32_t direction_dimension_y = 7u;
inline constexpr uint32_t polygon_bounce_stream_stride = 1024u;

inline constexpr uint32_t continuation_count(uint32_t path_depth)
{
    const uint32_t bounded = path_depth < maximum_path_depth ?
        path_depth : maximum_path_depth;
    return bounded > 0u ? bounded - 1u : 0u;
}

/* GPU history layout mirrored by PackedIndirectHistoryPixel in
 * path_trace.hlsl. Six signed signal coefficients use binary16, history uses a
 * limit-relative UNORM16, and confidence uses binary16. Depth remains FP32 and
 * `normal` remains the exact packed primary geometric normal. */
struct PackedHistoryPixel {
    uint32_t luminance_sh_01;
    uint32_t luminance_sh_23;
    uint32_t chroma;
    float depth;
    uint32_t normal;
    uint32_t history_and_confidence;
};

static_assert(sizeof(PackedHistoryPixel) == 24u);

inline uint16_t encode_history_length(float history_length,
                                      uint32_t history_limit,
                                      uint32_t indirect_sample_count)
{
    const float scale = static_cast<float>(history_limit > 0u ?
        history_limit : std::max(indirect_sample_count, 1u));
    const float normalized = std::isfinite(history_length) ?
        std::clamp(history_length / scale, 0.0f, 1.0f) : 0.0f;
    return static_cast<uint16_t>(std::lround(normalized * 65535.0f));
}

inline float decode_history_length(uint16_t encoded_history,
                                   uint32_t history_limit,
                                   uint32_t indirect_sample_count)
{
    const float scale = static_cast<float>(history_limit > 0u ?
        history_limit : std::max(indirect_sample_count, 1u));
    return static_cast<float>(encoded_history) * (scale / 65535.0f);
}

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

inline uint32_t adaptive_indirect_sample_count(
    uint32_t configured_maximum, float previous_history_length,
    float previous_gradient_confidence, uint32_t history_limit,
    bool history_valid, Mode mode, bool stable_sample_scheduled)
{
    configured_maximum = std::max(
        configured_maximum, stable_indirect_sample_count);
    if (mode == Mode::raw || mode == Mode::restir ||
        history_limit == 0u || !history_valid ||
        !(previous_history_length > 0.0f) ||
        !std::isfinite(previous_history_length) ||
        !std::isfinite(previous_gradient_confidence)) {
        return configured_maximum;
    }
    const float mature_history = std::max(
        1.0f, static_cast<float>(history_limit) -
            adaptive_history_maturity_tolerance);
    if (previous_history_length < mature_history ||
        std::fabs(previous_gradient_confidence) >=
            temporal_gradient_confirmation_threshold) {
        return configured_maximum;
    }
    return stable_sample_scheduled ? stable_indirect_sample_count : 0u;
}

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
                                    float current_sample_count,
                                    float gradient,
                                    uint32_t history_limit)
{
    current_sample_count = std::max(
        1.0f, std::isfinite(current_sample_count) ?
            current_sample_count : 1.0f);
    if (history_limit == 0u || !(previous_history_length > 0.0f) ||
        !std::isfinite(previous_history_length)) {
        return {current_sample_count, 1.0f, 0.0f};
    }
    gradient = std::max(0.0f, std::min(1.0f, gradient));
    const float antilag = std::max(0.0f, std::min(
        1.0f, temporal_antilag_scale * gradient));
    const float retained_history = previous_history_length * std::pow(
        1.0f - antilag, temporal_antilag_history_power);
    const float history_length = std::min(
        retained_history + current_sample_count,
        static_cast<float>(history_limit));
    const float base_weight = std::max(
        temporal_minimum_current_weight,
        std::min(1.0f, current_sample_count /
            std::max(history_length, current_sample_count)));
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
