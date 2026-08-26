#ifndef AB3D2_DXR_AUTO_EXPOSURE_H
#define AB3D2_DXR_AUTO_EXPOSURE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ab3d2::dxr::auto_exposure {

/* Project-owned sparse histogram exposure contract mirrored by the DXR shader.
 * It samples only primary surfaces, gives the central region of the image a
 * modest second vote, and meters the 10th--98th percentile interval. Exact
 * black is absent from the histogram instead of being promoted to a synthetic
 * grey sample; the low percentile rejects the darkest surviving outliers and
 * the high percentile rejects isolated emissive/firefly values. */
inline constexpr unsigned sample_columns = 32u;
inline constexpr unsigned sample_rows = 18u;
inline constexpr unsigned histogram_bin_count = 64u;
inline constexpr float minimum_luminance = 0.00001f;
inline constexpr float maximum_luminance = 64.0f;
inline constexpr unsigned low_percentile_numerator = 10u;
inline constexpr unsigned high_percentile_numerator = 98u;
inline constexpr unsigned percentile_denominator = 100u;
/* Native AB3D2 ray radiance uses a small numeric scale. This scene-linear key was
 * calibrated against the saved Level A corridor: it places the weighted
 * histogram average in the lower display range, preserving the authored dark
 * corridor while leaving headroom for its directly lit doorway. */
inline constexpr float metering_key = 0.014f;
inline constexpr float minimum_exposure = 0.125f;
inline constexpr float maximum_exposure = 4096.0f;
/* Entering darkness increases exposure slowly; encountering a bright source
 * reduces it quickly. Rates are exponential time constants in seconds, not
 * frame weights, so presentation refresh rate cannot change adaptation speed. */
inline constexpr float dark_adaptation_rate = 1.0f;
inline constexpr float light_adaptation_rate = 4.0f;
inline constexpr float maximum_delta_seconds = 0.25f;
inline constexpr float tone_toe_luminance = 0.02f;

struct Histogram {
    std::array<unsigned, histogram_bin_count> bins = {};
    unsigned total_weight = 0u;
};

struct Metering {
    float average_luminance = 0.0f;
    float low_percentile_luminance = 0.0f;
    float high_percentile_luminance = 0.0f;
    unsigned included_weight = 0u;
    unsigned total_weight = 0u;
};

inline unsigned sample_weight(float normalized_x, float normalized_y)
{
    const float x = normalized_x * 2.0f - 1.0f;
    const float y = normalized_y * 2.0f - 1.0f;
    return x * x + y * y <= 0.25f ? 2u : 1u;
}

inline unsigned histogram_index(float luminance)
{
    const float minimum_log = std::log2(minimum_luminance);
    const float maximum_log = std::log2(maximum_luminance);
    const float value_log = std::log2(std::clamp(
        luminance, minimum_luminance, maximum_luminance));
    const float normalized = std::clamp(
        (value_log - minimum_log) / (maximum_log - minimum_log),
        0.0f, 1.0f);
    return std::min(
        static_cast<unsigned>(normalized * histogram_bin_count),
        histogram_bin_count - 1u);
}

inline float histogram_luminance(unsigned index)
{
    const float minimum_log = std::log2(minimum_luminance);
    const float maximum_log = std::log2(maximum_luminance);
    const float position =
        (static_cast<float>(std::min(index, histogram_bin_count - 1u)) +
         0.5f) /
        static_cast<float>(histogram_bin_count);
    return std::exp2(
        minimum_log + (maximum_log - minimum_log) * position);
}

inline void add_sample(Histogram &histogram, float luminance,
                       unsigned weight = 1u)
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
    const unsigned low_rank =
        histogram.total_weight * low_percentile_numerator /
        percentile_denominator;
    const unsigned unclamped_high_rank =
        histogram.total_weight * high_percentile_numerator /
        percentile_denominator;
    const unsigned high_rank = std::min(
        histogram.total_weight,
        std::max(low_rank + 1u, unclamped_high_rank));
    unsigned cumulative = 0u;
    double weighted_log_sum = 0.0;
    for (unsigned index = 0u; index < histogram_bin_count; ++index) {
        const unsigned next = cumulative + histogram.bins[index];
        const unsigned included_begin = std::max(cumulative, low_rank);
        const unsigned included_end = std::min(next, high_rank);
        if (included_end > included_begin) {
            const unsigned included = included_end - included_begin;
            const float value = histogram_luminance(index);
            if (result.included_weight == 0u) {
                result.low_percentile_luminance = value;
            }
            result.high_percentile_luminance = value;
            weighted_log_sum +=
                static_cast<double>(included) * std::log2(value);
            result.included_weight += included;
        }
        cumulative = next;
    }
    if (result.included_weight != 0u) {
        result.average_luminance = static_cast<float>(std::exp2(
            weighted_log_sum / static_cast<double>(result.included_weight)));
    }
    return result;
}

inline float target(const Metering &metering)
{
    if (metering.included_weight == 0u ||
        !(metering.average_luminance > 0.0f) ||
        !std::isfinite(metering.average_luminance)) {
        return 1.0f;
    }
    return std::clamp(metering_key / metering.average_luminance,
                      minimum_exposure, maximum_exposure);
}

inline float adapt(float previous, float target_exposure, bool history_valid,
                   float delta_seconds)
{
    if (!history_valid || !std::isfinite(previous) || !(previous > 0.0f)) {
        return target_exposure;
    }
    if (!std::isfinite(delta_seconds) || !(delta_seconds > 0.0f)) {
        return previous;
    }
    const float elapsed = std::min(delta_seconds, maximum_delta_seconds);
    const float rate = target_exposure > previous ?
        dark_adaptation_rate : light_adaptation_rate;
    const float weight = 1.0f - std::exp(-rate * elapsed);
    return previous + (target_exposure - previous) * weight;
}

/* Project-owned luminance curve. A quadratic toe sends residual path noise and
 * genuinely unlit geometry smoothly to black. Above the toe it approaches a
 * near-linear midsection, then an asymptotic rational shoulder approaches one
 * without clipping highlights. Applying the luminance ratio to RGB preserves
 * hue; the shader performs a final hue-preserving gamut compression before
 * sRGB encoding. */
inline float tone_map_luminance(float luminance)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance)) {
        return 0.0f;
    }
    const float toe = luminance * luminance /
        (luminance + tone_toe_luminance);
    return toe / (1.0f + toe);
}

}  // namespace ab3d2::dxr::auto_exposure

#endif
