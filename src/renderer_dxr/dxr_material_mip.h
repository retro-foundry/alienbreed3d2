#ifndef AB3D2_DXR_MATERIAL_MIP_H
#define AB3D2_DXR_MATERIAL_MIP_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ab3d2::dxr::material_mip {

enum class Semantic {
    srgb,
    normal,
    linear,
};

using Levels = std::vector<std::vector<uint8_t>>;

/* The atlas stores each material's mip pyramid inside hardware level zero, so
 * the ray shader performs its own bounded anisotropic line filter. Eight taps
 * retain long grazing-angle detail without multiplying all five PBR channel
 * reads without limit. */
inline constexpr uint32_t maximum_filter_taps = 8u;

struct FilterFootprint {
    float mip_level;
    uint32_t sample_count;
};

inline FilterFootprint filter_footprint(float major_texels,
                                        float minor_texels,
                                        float render_to_output_scale,
                                        uint32_t mip_count)
{
    if (mip_count <= 1u || !std::isfinite(major_texels) ||
        !std::isfinite(minor_texels) ||
        !std::isfinite(render_to_output_scale) ||
        render_to_output_scale <= 0.0f) {
        return {0.0f, 1u};
    }
    const float minor = std::max(
        std::min(major_texels, minor_texels) * render_to_output_scale, 1.0f);
    const float major = std::max(
        std::max(major_texels, minor_texels) * render_to_output_scale, minor);
    const float bounded_anisotropy = std::clamp(
        major / minor, 1.0f, static_cast<float>(maximum_filter_taps));
    const uint32_t samples =
        static_cast<uint32_t>(std::ceil(bounded_anisotropy));
    const float per_sample = std::max(
        minor, major / static_cast<float>(samples));
    return {
        std::clamp(std::log2(per_sample), 0.0f,
                   static_cast<float>(mip_count - 1u)),
        samples,
    };
}

inline constexpr uint32_t level_extent(uint32_t extent, uint32_t level)
{
    return std::max(1u, extent >> level);
}

inline constexpr uint32_t level_count(uint32_t width, uint32_t height)
{
    if (width == 0u || height == 0u) {
        return 0u;
    }
    uint32_t count = 1u;
    while (width > 1u || height > 1u) {
        width = std::max(1u, width >> 1u);
        height = std::max(1u, height >> 1u);
        ++count;
    }
    return count;
}

inline constexpr uint32_t level_y_offset(uint32_t base_height,
                                         uint32_t level)
{
    uint32_t offset = 0u;
    for (uint32_t index = 0u; index < level; ++index) {
        offset += level_extent(base_height, index);
    }
    return offset;
}

inline constexpr uint32_t packed_height(uint32_t width, uint32_t height)
{
    const uint32_t count = level_count(width, height);
    return count == 0u ? 0u : level_y_offset(height, count);
}

inline float srgb_to_linear(uint8_t encoded)
{
    const float value = static_cast<float>(encoded) / 255.0f;
    return value <= 0.04045f ? value / 12.92f :
        std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline uint8_t linear_to_srgb(float linear)
{
    const float bounded = std::clamp(linear, 0.0f, 1.0f);
    const float encoded = bounded <= 0.0031308f ? bounded * 12.92f :
        1.055f * std::pow(bounded, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::lround(
        std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

inline uint8_t linear_byte(float value)
{
    return static_cast<uint8_t>(std::lround(
        std::clamp(value, 0.0f, 255.0f)));
}

inline bool generate(Semantic semantic, const std::vector<uint8_t> &base,
                     uint32_t width, uint32_t height, Levels &levels,
                     std::string &error)
{
    levels.clear();
    if (width == 0u || height == 0u ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / height / 4u ||
        base.size() != static_cast<size_t>(width) * height * 4u) {
        error = "material mip generation received an invalid RGBA8 image";
        return false;
    }
    levels.push_back(base);
    uint32_t source_width = width;
    uint32_t source_height = height;
    while (source_width > 1u || source_height > 1u) {
        const uint32_t destination_width = std::max(1u, source_width >> 1u);
        const uint32_t destination_height = std::max(1u, source_height >> 1u);
        const std::vector<uint8_t> &source = levels.back();
        std::vector<uint8_t> destination(
            static_cast<size_t>(destination_width) * destination_height * 4u);
        for (uint32_t y = 0u; y < destination_height; ++y) {
            const uint32_t source_y0 = y * source_height / destination_height;
            const uint32_t source_y1 =
                (y + 1u) * source_height / destination_height;
            for (uint32_t x = 0u; x < destination_width; ++x) {
                const uint32_t source_x0 =
                    x * source_width / destination_width;
                const uint32_t source_x1 =
                    (x + 1u) * source_width / destination_width;
                const uint32_t sample_count =
                    (source_x1 - source_x0) * (source_y1 - source_y0);
                float sum[4] = {};
                for (uint32_t source_y = source_y0;
                     source_y < source_y1; ++source_y) {
                    for (uint32_t source_x = source_x0;
                         source_x < source_x1; ++source_x) {
                        const size_t source_offset =
                            (static_cast<size_t>(source_y) * source_width +
                             source_x) * 4u;
                        if (semantic == Semantic::srgb) {
                            sum[0] += srgb_to_linear(source[source_offset + 0u]);
                            sum[1] += srgb_to_linear(source[source_offset + 1u]);
                            sum[2] += srgb_to_linear(source[source_offset + 2u]);
                        } else if (semantic == Semantic::normal) {
                            sum[0] += source[source_offset + 0u] /
                                    127.5f - 1.0f;
                            sum[1] += source[source_offset + 1u] /
                                    127.5f - 1.0f;
                            sum[2] += source[source_offset + 2u] /
                                    127.5f - 1.0f;
                        } else {
                            sum[0] += source[source_offset + 0u];
                            sum[1] += source[source_offset + 1u];
                            sum[2] += source[source_offset + 2u];
                        }
                        sum[3] += source[source_offset + 3u];
                    }
                }
                const float inverse_count = 1.0f /
                    static_cast<float>(sample_count);
                const size_t destination_offset =
                    (static_cast<size_t>(y) * destination_width + x) * 4u;
                if (semantic == Semantic::srgb) {
                    destination[destination_offset + 0u] =
                        linear_to_srgb(sum[0] * inverse_count);
                    destination[destination_offset + 1u] =
                        linear_to_srgb(sum[1] * inverse_count);
                    destination[destination_offset + 2u] =
                        linear_to_srgb(sum[2] * inverse_count);
                } else if (semantic == Semantic::normal) {
                    float normal_x = sum[0] * inverse_count;
                    float normal_y = sum[1] * inverse_count;
                    float normal_z = sum[2] * inverse_count;
                    const float length_squared = normal_x * normal_x +
                        normal_y * normal_y + normal_z * normal_z;
                    if (length_squared > 1.0e-12f) {
                        const float inverse_length =
                            1.0f / std::sqrt(length_squared);
                        normal_x *= inverse_length;
                        normal_y *= inverse_length;
                        normal_z *= inverse_length;
                    } else {
                        normal_x = 0.0f;
                        normal_y = 0.0f;
                        normal_z = 1.0f;
                    }
                    destination[destination_offset + 0u] = linear_byte(
                        (normal_x * 0.5f + 0.5f) * 255.0f);
                    destination[destination_offset + 1u] = linear_byte(
                        (normal_y * 0.5f + 0.5f) * 255.0f);
                    destination[destination_offset + 2u] = linear_byte(
                        (normal_z * 0.5f + 0.5f) * 255.0f);
                } else {
                    destination[destination_offset + 0u] =
                        linear_byte(sum[0] * inverse_count);
                    destination[destination_offset + 1u] =
                        linear_byte(sum[1] * inverse_count);
                    destination[destination_offset + 2u] =
                        linear_byte(sum[2] * inverse_count);
                }
                destination[destination_offset + 3u] =
                    linear_byte(sum[3] * inverse_count);
            }
        }
        levels.push_back(std::move(destination));
        source_width = destination_width;
        source_height = destination_height;
    }
    error.clear();
    return true;
}

}  // namespace ab3d2::dxr::material_mip

#endif
