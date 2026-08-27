#ifndef AB3D2_DXR_TONE_MAPPING_H
#define AB3D2_DXR_TONE_MAPPING_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::tone_mapping {

/* CPU mirror of shaders/post_process.hlsl. This keeps the histogram ranks,
 * exposure time constants, and curve lookup contract regression-testable even
 * though the production implementation runs after Ray Reconstruction on the
 * GPU. */
inline constexpr uint32_t histogram_bin_count = 128u;
inline constexpr uint32_t curve_point_count = histogram_bin_count + 1u;
inline constexpr float minimum_log_luminance = -18.0f;
inline constexpr float maximum_log_luminance = 8.0f;
/* AB3D2's traced radiance deliberately uses a small scene-linear scale. This
 * key and toe were calibrated against the saved Level A corridor before the
 * meter moved after Ray Reconstruction. Keeping the key below the toe makes
 * the metered scene value visibly dark instead of promoting reconstruction
 * residue to photographic middle grey. */
inline constexpr float metering_key = 0.014f;
inline constexpr float tone_toe_luminance = 0.02f;
inline constexpr float minimum_exposure = 0.125f;
inline constexpr float maximum_exposure = 4096.0f;
inline constexpr float maximum_delta_seconds = 0.25f;

struct Histogram {
    std::array<uint32_t, histogram_bin_count> bins = {};
    uint32_t total_weight = 0u;
};

struct Metering {
    float average_luminance = 0.0f;
    float low_luminance = 0.0f;
    float high_luminance = 0.0f;
    uint32_t included_weight = 0u;
    uint32_t total_weight = 0u;
    uint32_t low_bin = 0u;
    uint32_t high_bin = histogram_bin_count - 1u;
};

struct State {
    std::array<float, curve_point_count> curve = {};
    float exposure = 1.0f;
    float target_exposure = 1.0f;
    float average_luminance = 0.0f;
    float low_luminance = 0.0f;
    float high_luminance = 0.0f;
};

inline uint32_t histogram_index(float luminance)
{
    const float value_log = std::log2(std::clamp(
        luminance, std::exp2(minimum_log_luminance),
        std::exp2(maximum_log_luminance)));
    const float normalized = std::clamp(
        (value_log - minimum_log_luminance) /
            (maximum_log_luminance - minimum_log_luminance),
        0.0f, 1.0f);
    return std::min(static_cast<uint32_t>(
                        normalized * histogram_bin_count),
                    histogram_bin_count - 1u);
}

inline float histogram_luminance(uint32_t index)
{
    const float position =
        (static_cast<float>(std::min(index, histogram_bin_count - 1u)) +
         0.5f) /
        static_cast<float>(histogram_bin_count);
    return std::exp2(minimum_log_luminance +
                     (maximum_log_luminance - minimum_log_luminance) *
                         position);
}

inline void add_sample(Histogram &histogram, float luminance,
                       uint32_t weight = 1u)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance) || weight == 0u) {
        return;
    }
    histogram.bins[histogram_index(luminance)] += weight;
    histogram.total_weight += weight;
}

inline Metering meter(const Histogram &histogram)
{
    Metering result = {};
    result.total_weight = histogram.total_weight;
    if (histogram.total_weight == 0u) {
        return result;
    }
    const uint32_t low_rank = histogram.total_weight * 2u / 100u;
    const uint32_t high_rank =
        std::max(low_rank + 1u, histogram.total_weight * 99u / 100u);
    const uint32_t meter_low_rank = histogram.total_weight * 10u / 100u;
    const uint32_t meter_high_rank =
        std::max(meter_low_rank + 1u,
                 histogram.total_weight * 90u / 100u);
    uint32_t cumulative = 0u;
    double weighted_log_sum = 0.0;
    bool low_found = false;
    bool high_found = false;
    for (uint32_t index = 0u; index < histogram_bin_count; ++index) {
        const uint32_t next = cumulative + histogram.bins[index];
        if (!low_found && next > low_rank) {
            result.low_bin = index;
            low_found = true;
        }
        if (!high_found && next >= high_rank) {
            result.high_bin = index;
            high_found = true;
        }
        const uint32_t included_begin =
            std::max(cumulative, meter_low_rank);
        const uint32_t included_end = std::min(next, meter_high_rank);
        if (included_end > included_begin) {
            const uint32_t included = included_end - included_begin;
            weighted_log_sum += static_cast<double>(included) *
                std::log2(histogram_luminance(index));
            result.included_weight += included;
        }
        cumulative = next;
    }
    if (result.included_weight != 0u) {
        result.average_luminance = static_cast<float>(std::exp2(
            weighted_log_sum /
            static_cast<double>(result.included_weight)));
    }
    result.low_luminance = histogram_luminance(result.low_bin);
    result.high_luminance = histogram_luminance(result.high_bin);
    return result;
}

inline float target_exposure(const Metering &metering)
{
    return metering.average_luminance > 0.0f &&
                   std::isfinite(metering.average_luminance) ?
        std::clamp(metering_key / metering.average_luminance,
                   minimum_exposure, maximum_exposure) :
        1.0f;
}

inline float adapt_exposure(float previous, float target,
                            bool history_valid, float delta_seconds)
{
    if (!history_valid || !(previous > 0.0f) ||
        !std::isfinite(previous)) {
        return target;
    }
    const float elapsed = std::clamp(
        std::isfinite(delta_seconds) ? delta_seconds : 0.0f,
        0.0f, maximum_delta_seconds);
    const float rate = target > previous ? 1.0f : 4.0f;
    return previous + (target - previous) *
        (1.0f - std::exp(-rate * elapsed));
}

/* Quadratic toe sends residual reconstructed transport smoothly to exact
 * black. The rational shoulder retains a broad midrange and approaches white
 * without clipping. */
inline float tone_map_luminance(float exposed_luminance)
{
    if (!(exposed_luminance > 0.0f) ||
        !std::isfinite(exposed_luminance)) {
        return 0.0f;
    }
    const float toe = exposed_luminance * exposed_luminance /
        (exposed_luminance + tone_toe_luminance);
    return std::clamp(toe / (1.0f + toe), 0.0f, 1.0f);
}

inline State build_curve(const Histogram &histogram, const State &previous,
                         bool history_valid, float delta_seconds)
{
    State result = {};
    const Metering metering = meter(histogram);
    if (metering.total_weight == 0u) {
        for (uint32_t point = 0u; point < curve_point_count; ++point) {
            const float input_log = minimum_log_luminance +
                (maximum_log_luminance - minimum_log_luminance) *
                    static_cast<float>(point) /
                    static_cast<float>(histogram_bin_count);
            result.curve[point] = tone_map_luminance(std::exp2(input_log));
        }
        return result;
    }
    result.target_exposure = target_exposure(metering);
    result.exposure = adapt_exposure(
        previous.exposure, result.target_exposure, history_valid,
        delta_seconds);
    result.average_luminance = metering.average_luminance;
    result.low_luminance = metering.low_luminance;
    result.high_luminance = metering.high_luminance;

    const float elapsed = std::clamp(
        std::isfinite(delta_seconds) ? delta_seconds : 0.0f,
        0.0f, maximum_delta_seconds);
    for (uint32_t point = 0u; point < curve_point_count; ++point) {
        const float input_log = minimum_log_luminance +
            (maximum_log_luminance - minimum_log_luminance) *
                static_cast<float>(point) /
                static_cast<float>(histogram_bin_count);
        const float target = tone_map_luminance(
            std::exp2(input_log) * result.exposure);
        const float old_value = previous.curve[point];
        const float rate = target > old_value ? 2.0f : 6.0f;
        if (!history_valid || !std::isfinite(old_value)) {
            result.curve[point] = target;
        } else {
            const float weight = 1.0f - std::exp(-rate * elapsed);
            result.curve[point] = std::clamp(
                old_value + (target - old_value) * weight, 0.0f, 1.0f);
        }
    }
    return result;
}

inline float lookup(const State &state, float exposed_luminance)
{
    if (!(exposed_luminance > 0.0f) ||
        !std::isfinite(exposed_luminance)) {
        return 0.0f;
    }
    const float input_log = std::log2(std::clamp(
        exposed_luminance, std::exp2(minimum_log_luminance),
        std::exp2(maximum_log_luminance)));
    const float position = std::clamp(
        (input_log - minimum_log_luminance) /
            (maximum_log_luminance - minimum_log_luminance),
        0.0f, 1.0f) * static_cast<float>(histogram_bin_count);
    const uint32_t left = std::min(
        static_cast<uint32_t>(position), curve_point_count - 2u);
    const float fraction = position - static_cast<float>(left);
    return state.curve[left] +
        (state.curve[left + 1u] - state.curve[left]) * fraction;
}

}  // namespace ab3d2::dxr::tone_mapping

#endif
