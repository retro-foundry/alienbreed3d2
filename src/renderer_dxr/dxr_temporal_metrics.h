#ifndef AB3D2_DXR_TEMPORAL_METRICS_H
#define AB3D2_DXR_TEMPORAL_METRICS_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ab3d2::dxr::temporal_metrics {

struct Difference {
    double mean_absolute_component = -1.0;
    uint64_t outlier_pixels = 0u;
    uint64_t compared_pixels = 0u;
    float maximum_component = 0.0f;
};

inline float decode_half(uint16_t encoded)
{
    const uint32_t sign = static_cast<uint32_t>(encoded >> 15u);
    const uint32_t exponent = (encoded >> 10u) & 0x1fu;
    const uint32_t mantissa = encoded & 0x3ffu;
    const float magnitude = exponent == 0u ?
        std::ldexp(static_cast<float>(mantissa), -24) :
        exponent == 0x1fu ?
            (mantissa == 0u ? std::numeric_limits<float>::infinity() :
                              std::numeric_limits<float>::quiet_NaN()) :
            std::ldexp(static_cast<float>(mantissa + 0x400u),
                       static_cast<int>(exponent) - 25);
    return sign != 0u ? -magnitude : magnitude;
}

inline Difference measure_reprojected_rgb(
    const std::vector<uint8_t> &current_rgb,
    const std::vector<uint8_t> &previous_rgb,
    uint32_t output_width, uint32_t output_height,
    const std::vector<uint16_t> &motion_xy,
    uint32_t motion_width, uint32_t motion_height,
    float outlier_threshold = 16.0f)
{
    Difference result;
    const size_t output_pixels =
        static_cast<size_t>(output_width) * output_height;
    const size_t motion_pixels =
        static_cast<size_t>(motion_width) * motion_height;
    if (output_width == 0u || output_height == 0u ||
        motion_width == 0u || motion_height == 0u ||
        current_rgb.size() != output_pixels * 3u ||
        previous_rgb.size() != current_rgb.size() ||
        motion_xy.size() != motion_pixels * 2u) {
        return result;
    }

    const auto motion_at = [&](uint32_t x, uint32_t y, uint32_t component) {
        return decode_half(motion_xy[
            (static_cast<size_t>(y) * motion_width + x) * 2u + component]);
    };
    double difference_sum = 0.0;
    for (uint32_t y = 0u; y < output_height; ++y) {
        for (uint32_t x = 0u; x < output_width; ++x) {
            const float motion_x =
                (static_cast<float>(x) + 0.5f) * motion_width /
                    output_width - 0.5f;
            const float motion_y =
                (static_cast<float>(y) + 0.5f) * motion_height /
                    output_height - 0.5f;
            const float clamped_motion_x = std::clamp(
                motion_x, 0.0f, static_cast<float>(motion_width - 1u));
            const float clamped_motion_y = std::clamp(
                motion_y, 0.0f, static_cast<float>(motion_height - 1u));
            const uint32_t motion_x0 =
                static_cast<uint32_t>(std::floor(clamped_motion_x));
            const uint32_t motion_y0 =
                static_cast<uint32_t>(std::floor(clamped_motion_y));
            const uint32_t motion_x1 =
                std::min(motion_x0 + 1u, motion_width - 1u);
            const uint32_t motion_y1 =
                std::min(motion_y0 + 1u, motion_height - 1u);
            const float motion_fraction_x = clamped_motion_x - motion_x0;
            const float motion_fraction_y = clamped_motion_y - motion_y0;
            const float weights[4] = {
                (1.0f - motion_fraction_x) * (1.0f - motion_fraction_y),
                motion_fraction_x * (1.0f - motion_fraction_y),
                (1.0f - motion_fraction_x) * motion_fraction_y,
                motion_fraction_x * motion_fraction_y,
            };
            const uint32_t motion_coordinates[4][2] = {
                {motion_x0, motion_y0}, {motion_x1, motion_y0},
                {motion_x0, motion_y1}, {motion_x1, motion_y1},
            };
            float motion[2] = {};
            bool valid_motion = true;
            for (uint32_t tap = 0u; tap < 4u; ++tap) {
                if (weights[tap] == 0.0f) {
                    continue;
                }
                const float tap_x = motion_at(
                    motion_coordinates[tap][0], motion_coordinates[tap][1], 0u);
                const float tap_y = motion_at(
                    motion_coordinates[tap][0], motion_coordinates[tap][1], 1u);
                if (!std::isfinite(tap_x) || !std::isfinite(tap_y) ||
                    std::fabs(tap_x) >= static_cast<float>(motion_width) ||
                    std::fabs(tap_y) >= static_cast<float>(motion_height)) {
                    valid_motion = false;
                    break;
                }
                motion[0] += tap_x * weights[tap];
                motion[1] += tap_y * weights[tap];
            }
            if (!valid_motion) {
                continue;
            }

            const float previous_x = static_cast<float>(x) + motion[0] *
                output_width / motion_width;
            const float previous_y = static_cast<float>(y) + motion[1] *
                output_height / motion_height;
            if (previous_x < 0.0f || previous_y < 0.0f ||
                previous_x > static_cast<float>(output_width - 1u) ||
                previous_y > static_cast<float>(output_height - 1u)) {
                continue;
            }
            const uint32_t previous_x0 =
                static_cast<uint32_t>(std::floor(previous_x));
            const uint32_t previous_y0 =
                static_cast<uint32_t>(std::floor(previous_y));
            const uint32_t previous_x1 =
                std::min(previous_x0 + 1u, output_width - 1u);
            const uint32_t previous_y1 =
                std::min(previous_y0 + 1u, output_height - 1u);
            const float previous_fraction_x = previous_x - previous_x0;
            const float previous_fraction_y = previous_y - previous_y0;
            const float previous_weights[4] = {
                (1.0f - previous_fraction_x) * (1.0f - previous_fraction_y),
                previous_fraction_x * (1.0f - previous_fraction_y),
                (1.0f - previous_fraction_x) * previous_fraction_y,
                previous_fraction_x * previous_fraction_y,
            };
            const uint32_t previous_coordinates[4][2] = {
                {previous_x0, previous_y0}, {previous_x1, previous_y0},
                {previous_x0, previous_y1}, {previous_x1, previous_y1},
            };
            const size_t current_index =
                (static_cast<size_t>(y) * output_width + x) * 3u;
            float pixel_maximum = 0.0f;
            for (uint32_t component = 0u; component < 3u; ++component) {
                float previous_component = 0.0f;
                for (uint32_t tap = 0u; tap < 4u; ++tap) {
                    const size_t previous_index =
                        (static_cast<size_t>(previous_coordinates[tap][1]) *
                             output_width + previous_coordinates[tap][0]) * 3u;
                    previous_component += previous_rgb[
                        previous_index + component] * previous_weights[tap];
                }
                const float difference = std::fabs(
                    static_cast<float>(current_rgb[current_index + component]) -
                    previous_component);
                difference_sum += difference;
                pixel_maximum = std::max(pixel_maximum, difference);
            }
            result.maximum_component = std::max(
                result.maximum_component, pixel_maximum);
            result.outlier_pixels += pixel_maximum >= outlier_threshold;
            ++result.compared_pixels;
        }
    }
    if (result.compared_pixels != 0u) {
        result.mean_absolute_component = difference_sum /
            static_cast<double>(result.compared_pixels * 3u);
    }
    return result;
}

}  // namespace ab3d2::dxr::temporal_metrics

#endif
