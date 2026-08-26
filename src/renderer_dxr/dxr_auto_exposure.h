#ifndef AB3D2_DXR_AUTO_EXPOSURE_H
#define AB3D2_DXR_AUTO_EXPOSURE_H

#include <algorithm>
#include <cmath>

namespace ab3d2::dxr::auto_exposure {

/* Project-owned sparse log-average exposure contract mirrored by the DXR
 * shader. It samples only primary surfaces, limits individual fireflies before
 * taking the logarithm, and maps the resulting scene luminance toward middle
 * grey. The broad bounds keep genuinely dark AB3D2 corridors visible without
 * turning a directly lit room into an unbounded exposure spike. */
inline constexpr unsigned sample_columns = 32u;
inline constexpr unsigned sample_rows = 18u;
inline constexpr float minimum_luminance = 0.001f;
inline constexpr float maximum_luminance = 16.0f;
inline constexpr float middle_grey = 0.18f;
inline constexpr float minimum_exposure = 0.25f;
inline constexpr float maximum_exposure = 128.0f;
inline constexpr float adaptation_weight = 0.05f;
inline constexpr float tone_minimum_luminance = 0.0002f;
inline constexpr float tone_white_point = 10.0f;
inline constexpr float tone_dynamic_range_stops = 7.0f;

inline float target(float log_luminance_sum, unsigned sample_count)
{
    if (sample_count == 0u || !std::isfinite(log_luminance_sum)) {
        return 1.0f;
    }
    const float average_luminance = std::exp(
        log_luminance_sum / static_cast<float>(sample_count));
    return std::clamp(middle_grey / average_luminance,
                      minimum_exposure, maximum_exposure);
}

inline float adapt(float previous, float target_exposure, bool history_valid)
{
    if (!history_valid || !std::isfinite(previous) || !(previous > 0.0f)) {
        return target_exposure;
    }
    return previous + (target_exposure - previous) * adaptation_weight;
}

/* Luminance-only project-owned contrast curve. Positive values below the
 * reliable scene floor fade into black; the supported scene interval maps to
 * a seven-stop display interval. Applying its ratio to RGB preserves hue. */
inline float tone_map_luminance(float luminance)
{
    if (!(luminance > 0.0f) || !std::isfinite(luminance)) {
        return 0.0f;
    }
    const float display_floor =
        std::exp2(-tone_dynamic_range_stops);
    if (luminance < tone_minimum_luminance) {
        return display_floor * luminance / tone_minimum_luminance;
    }
    const float scene_stops = std::log2(
        tone_white_point / tone_minimum_luminance);
    const float position = std::clamp(
        std::log2(luminance / tone_minimum_luminance) / scene_stops,
        0.0f, 1.0f);
    return std::exp2(-tone_dynamic_range_stops * (1.0f - position));
}

}  // namespace ab3d2::dxr::auto_exposure

#endif
