#ifndef AB3D2_DXR_TONE_MAPPING_H
#define AB3D2_DXR_TONE_MAPPING_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::tone_mapping {

/* CPU mirror of shaders/post_process.hlsl. The constants and stage contract
 * reproduce the observable Q2RTX tone-mapping defaults requested on
 * 2026-08-27. Curve construction is independently expressed from the
 * minimum-contrast-distortion equations in Eilertsen, Mantiuk, and Unger,
 * Real-time noise-aware tone mapping (ACM TOG 34(6), 2015). */
inline constexpr uint32_t histogram_bin_count = 128u;
inline constexpr uint32_t curve_point_count = histogram_bin_count + 1u;
inline constexpr uint32_t histogram_fraction_scale = 128u;
inline constexpr float minimum_log_luminance = -24.0f;
inline constexpr float maximum_log_luminance = 8.0f;
inline constexpr float display_dynamic_range_stops = 7.0f;
inline constexpr float exposure_bias_stops = -1.0f;
inline constexpr float minimum_scene_luminance = 0.0002f;
inline constexpr float maximum_scene_luminance = 1.0f;
inline constexpr float noise_floor_stops = -12.0f;
inline constexpr float noise_floor_blend = 0.5f;
inline constexpr float reinhard_blend = 0.5f;
inline constexpr float slope_blur_sigma = 12.0f;
inline constexpr int32_t slope_blur_radius = 13;
inline constexpr float knee_start = 0.6f;
inline constexpr float white_point = 10.0f;
inline constexpr float exposure_speed_down = 1.0f;
inline constexpr float exposure_speed_up = 2.0f;
inline constexpr float maximum_delta_seconds = 0.25f;

struct Histogram {
    std::array<uint32_t, histogram_bin_count> bins = {};
    uint32_t total_weight = 0u;
};

struct Metering {
    float average_luminance = 0.0f;
    float low_luminance = 0.0f;
    float high_luminance = 0.0f;
    float included_weight = 0.0f;
    float total_weight = 0.0f;
    uint32_t low_bin = 0u;
    uint32_t high_bin = histogram_bin_count - 1u;
};

struct State {
    std::array<float, curve_point_count> log_curve = {};
    float adapted_luminance = 1.0f;
    float target_luminance = 1.0f;
    float average_luminance = 0.0f;
    float low_luminance = 0.0f;
    float high_luminance = 0.0f;
};

inline float histogram_position(float luminance)
{
    const float value_log = std::log2(std::clamp(
        luminance, std::exp2(minimum_log_luminance),
        std::exp2(maximum_log_luminance)));
    const float normalized = std::clamp(
        (value_log - minimum_log_luminance) /
            (maximum_log_luminance - minimum_log_luminance),
        0.0f, 1.0f);
    return std::min(normalized * static_cast<float>(histogram_bin_count),
                    static_cast<float>(histogram_bin_count - 1u));
}

inline float curve_position(float luminance)
{
    const float value_log = std::log2(std::clamp(
        luminance, std::exp2(minimum_log_luminance),
        std::exp2(maximum_log_luminance)));
    const float normalized = std::clamp(
        (value_log - minimum_log_luminance) /
            (maximum_log_luminance - minimum_log_luminance),
        0.0f, 1.0f);
    return normalized * static_cast<float>(histogram_bin_count);
}

inline uint32_t histogram_index(float luminance)
{
    return static_cast<uint32_t>(histogram_position(luminance));
}

inline float histogram_log_luminance(uint32_t index)
{
    return minimum_log_luminance +
        (maximum_log_luminance - minimum_log_luminance) *
            static_cast<float>(std::min(index, histogram_bin_count - 1u)) /
            static_cast<float>(histogram_bin_count);
}

inline float histogram_luminance(uint32_t index)
{
    return std::exp2(histogram_log_luminance(index));
}

inline void add_sample(Histogram &histogram, float luminance,
                       uint32_t weight = 1u)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance) || weight == 0u) {
        return;
    }
    const float position = histogram_position(luminance);
    const uint32_t left = static_cast<uint32_t>(position);
    const uint32_t right = std::min(left + 1u, histogram_bin_count - 1u);
    const float fraction = position - static_cast<float>(left);
    const uint32_t right_weight = weight * static_cast<uint32_t>(
        static_cast<float>(histogram_fraction_scale) * fraction);
    const uint32_t left_weight = weight * static_cast<uint32_t>(
        static_cast<float>(histogram_fraction_scale) * (1.0f - fraction));
    histogram.bins[left] += left_weight;
    histogram.bins[right] += right_weight;
    histogram.total_weight += left_weight + right_weight;
}

inline std::array<float, histogram_bin_count> normalized_histogram(
    const Histogram &histogram)
{
    std::array<float, histogram_bin_count> result = {};
    float sum = 0.0f;
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        result[index] = 1.0f +
            static_cast<float>(histogram.bins[index]) /
                static_cast<float>(histogram_fraction_scale);
        sum += result[index];
    }
    for (float &value : result) {
        value /= sum;
    }
    return result;
}

inline Metering meter(const Histogram &histogram)
{
    Metering result = {};
    const auto distribution = normalized_histogram(histogram);
    constexpr float diagnostic_low = 0.02f;
    constexpr float diagnostic_high = 0.99f;
    constexpr float exposure_low = 0.70f;
    constexpr float exposure_high = 0.90f;
    float cumulative = 0.0f;
    double weighted_log_sum = 0.0;
    bool low_found = false;
    bool high_found = false;
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        const float next = cumulative + distribution[index];
        if (!low_found && next > diagnostic_low) {
            result.low_bin = index;
            low_found = true;
        }
        if (!high_found && next >= diagnostic_high) {
            result.high_bin = index;
            high_found = true;
        }
        if (exposure_low <= next && cumulative <= exposure_high) {
            weighted_log_sum += static_cast<double>(distribution[index]) *
                histogram_log_luminance(index);
            result.included_weight += distribution[index];
        }
        cumulative = next;
    }
    result.total_weight = cumulative;
    if (result.included_weight > 0.0f) {
        result.average_luminance = static_cast<float>(std::exp2(
            weighted_log_sum /
            static_cast<double>(result.included_weight)));
    }
    result.low_luminance = histogram_luminance(result.low_bin);
    result.high_luminance = histogram_luminance(result.high_bin);
    return result;
}

inline float target_luminance(const Metering &metering)
{
    return std::clamp(metering.average_luminance,
                      minimum_scene_luminance, maximum_scene_luminance);
}

inline float adapt_luminance(float previous, float target,
                             bool history_valid, float delta_seconds)
{
    if (!history_valid || !(previous > 0.0f) ||
        !std::isfinite(previous)) {
        return target;
    }
    const float elapsed = std::clamp(
        std::isfinite(delta_seconds) ? delta_seconds : 0.0f,
        0.0f, maximum_delta_seconds);
    const float previous_log = std::log2(previous);
    const float target_log = std::log2(std::max(target, 1.0e-8f));
    const float rate = previous_log < target_log ?
        exposure_speed_up : exposure_speed_down;
    return std::exp2(target_log + (previous_log - target_log) *
        std::exp(-rate * elapsed));
}

inline float smooth_step(float edge0, float edge1, float value)
{
    const float position = std::clamp(
        (value - edge0) / std::max(edge1 - edge0, 1.0e-8f),
        0.0f, 1.0f);
    return position * position * (3.0f - 2.0f * position);
}

inline State build_curve(const Histogram &histogram, const State &previous,
                         bool history_valid, float delta_seconds)
{
    State result = {};
    const Metering metering = meter(histogram);
    result.target_luminance = target_luminance(metering);
    result.adapted_luminance = adapt_luminance(
        previous.adapted_luminance, result.target_luminance,
        history_valid, delta_seconds);
    result.average_luminance = metering.average_luminance;
    result.low_luminance = metering.low_luminance;
    result.high_luminance = metering.high_luminance;

    auto distribution = normalized_histogram(histogram);
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        if (histogram_log_luminance(index) < noise_floor_stops) {
            distribution[index] = 0.0f;
        }
    }

    constexpr float bin_width =
        (maximum_log_luminance - minimum_log_luminance) /
        static_cast<float>(histogram_bin_count);
    constexpr float output_range_bins =
        display_dynamic_range_stops / bin_width;
    std::array<float, histogram_bin_count> inverse = {};
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        inverse[index] = distribution[index] > 0.0f ?
            1.0f / distribution[index] : 0.0f;
    }

    float threshold = 1.0e-16f;
    float active_count = 0.0f;
    float inverse_sum = 0.0f;
    for (uint32_t iteration = 0u; iteration < 16u; ++iteration) {
        active_count = 0.0f;
        inverse_sum = 0.0f;
        for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
            if (distribution[index] >= threshold) {
                active_count += 1.0f;
                inverse_sum += inverse[index];
            }
        }
        threshold = inverse_sum > 0.0f ?
            (active_count - output_range_bins) / inverse_sum : 0.0f;
    }

    std::array<float, histogram_bin_count> slopes = {};
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        if (distribution[index] >= threshold && inverse_sum > 0.0f) {
            slopes[index] = 1.0f + inverse[index] *
                (output_range_bins - active_count) / inverse_sum;
        }
    }

    std::array<float, slope_blur_radius + 1> gaussian = {};
    float gaussian_sum = 0.0f;
    for (int32_t offset = 0; offset <= slope_blur_radius; ++offset) {
        gaussian[static_cast<size_t>(offset)] = std::exp(
            -static_cast<float>(offset * offset) /
            (2.0f * slope_blur_sigma * slope_blur_sigma));
        gaussian_sum += gaussian[static_cast<size_t>(offset)] *
            (offset == 0 ? 1.0f : 2.0f);
    }
    for (float &weight : gaussian) {
        weight /= gaussian_sum;
    }

    std::array<float, histogram_bin_count> filtered = {};
    for (int32_t index = 0;
         index < static_cast<int32_t>(histogram_bin_count); ++index) {
        for (int32_t offset = -slope_blur_radius;
             offset <= slope_blur_radius; ++offset) {
            const int32_t source = std::clamp(
                index + offset, 0,
                static_cast<int32_t>(histogram_bin_count) - 1);
            filtered[static_cast<size_t>(index)] +=
                slopes[static_cast<size_t>(source)] *
                gaussian[static_cast<size_t>(std::abs(offset))];
        }
    }

    std::array<float, curve_point_count> target_curve = {};
    float prefix = 0.0f;
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        target_curve[index] = prefix * bin_width -
            display_dynamic_range_stops;
        prefix += filtered[index];
    }
    target_curve[histogram_bin_count] = 0.0f;

    const float noise_position = std::clamp(
        (noise_floor_stops - minimum_log_luminance) /
            (maximum_log_luminance - minimum_log_luminance) *
            static_cast<float>(histogram_bin_count),
        1.0f, static_cast<float>(histogram_bin_count - 1u));
    const uint32_t noise_index = static_cast<uint32_t>(noise_position);
    const float curve_at_noise = target_curve[noise_index - 1u];
    const float input_at_noise = histogram_log_luminance(noise_index - 1u);
    const float target_log = std::log2(result.target_luminance);
    const float correction = std::abs(target_log) > 1.0e-8f ?
        -(curve_at_noise - input_at_noise) / target_log : 1.0f;
    for (uint32_t index = 0u; index < noise_index; ++index) {
        const float auto_exposed = histogram_log_luminance(index) -
            target_log * correction;
        const float transition = noise_floor_blend +
            (1.0f - noise_floor_blend) * smooth_step(
                noise_position * 0.5f, noise_position,
                static_cast<float>(index));
        target_curve[index] = auto_exposed +
            (target_curve[index] - auto_exposed) * transition;
    }

    const float elapsed = std::clamp(
        std::isfinite(delta_seconds) ? delta_seconds : 0.0f,
        0.0f, maximum_delta_seconds);
    for (uint32_t point = 0u; point < curve_point_count; ++point) {
        const float old_value = previous.log_curve[point];
        const float target = target_curve[point];
        const float rate = old_value < target ?
            exposure_speed_up : exposure_speed_down;
        result.log_curve[point] = !history_valid ||
                !std::isfinite(old_value) ? target :
            target + (old_value - target) * std::exp(-rate * elapsed);
    }
    return result;
}

inline float lookup_log_curve(const State &state, float luminance)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance)) {
        return state.log_curve[0];
    }
    const float position = curve_position(luminance);
    const uint32_t left = std::min(
        static_cast<uint32_t>(position), curve_point_count - 2u);
    const float fraction = position - static_cast<float>(left);
    return state.log_curve[left] +
        (state.log_curve[left + 1u] - state.log_curve[left]) * fraction;
}

inline float adaptive_luminance(const State &state, float luminance)
{
    return luminance > 0.0f ?
        std::exp2(lookup_log_curve(state, luminance) +
                  exposure_bias_stops) : 0.0f;
}

inline float reinhard_luminance(float luminance, float adapted_luminance,
                                float selected_white_point = white_point)
{
    if (!(luminance > 0.0f) || !(adapted_luminance > 0.0f)) {
        return 0.0f;
    }
    const float scaled = std::exp2(exposure_bias_stops - 2.0f) *
        luminance / adapted_luminance;
    const float white_squared = selected_white_point * selected_white_point;
    return scaled * (1.0f + scaled / white_squared) / (1.0f + scaled);
}

inline float knee(float value)
{
    if (value < knee_start) {
        return value;
    }
    const float coefficient =
        (knee_start * (knee_start - 2.0f) + white_point) /
        (white_point - 1.0f);
    const float numerator = coefficient * value -
        knee_start * knee_start;
    const float denominator = value + coefficient - 2.0f * knee_start;
    return numerator / std::max(denominator, 1.0e-6f);
}

inline float lookup(const State &state, float luminance)
{
    const float adaptive = knee(adaptive_luminance(state, luminance));
    const float automatic = reinhard_luminance(
        luminance, state.adapted_luminance);
    return std::clamp(adaptive +
        (automatic - adaptive) * reinhard_blend, 0.0f, 1.0f);
}

}  // namespace ab3d2::dxr::tone_mapping

#endif
