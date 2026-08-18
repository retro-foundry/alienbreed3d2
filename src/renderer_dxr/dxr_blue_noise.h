#ifndef AB3D2_DXR_BLUE_NOISE_H
#define AB3D2_DXR_BLUE_NOISE_H

#include <cstddef>
#include <cstdint>
#include <span>

namespace ab3d2::dxr::blue_noise {

constexpr uint32_t sample_count = 256u;
constexpr uint32_t dimension_count = 256u;
constexpr uint32_t tile_width = 128u;
constexpr uint32_t tile_height = 128u;
constexpr uint32_t optimized_dimension_count = 8u;
constexpr size_t sobol_offset = 0u;
constexpr size_t sobol_size = sample_count * dimension_count;
constexpr size_t scrambling_offset = sobol_offset + sobol_size;
constexpr size_t tile_size =
    tile_width * tile_height * optimized_dimension_count;
constexpr size_t ranking_offset = scrambling_offset + tile_size;
constexpr size_t package_size = ranking_offset + tile_size;

inline bool valid_package(std::span<const uint8_t> bytes)
{
    return bytes.size() == package_size;
}

inline float sample(std::span<const uint8_t> bytes, uint32_t pixel_x,
                    uint32_t pixel_y, uint32_t sample_index,
                    uint32_t dimension)
{
    dimension &= dimension_count - 1u;
    const uint32_t dimension_group =
        dimension / optimized_dimension_count;
    const uint32_t sample_cycle = sample_index / sample_count;
    const uint32_t tile_x =
        (pixel_x + dimension_group * 37u + sample_cycle * 53u) &
        (tile_width - 1u);
    const uint32_t tile_y =
        (pixel_y + dimension_group * 59u + sample_cycle * 97u) &
        (tile_height - 1u);
    const uint32_t tile_index = tile_x + tile_y * tile_width;
    const uint32_t optimized_dimension =
        dimension & (optimized_dimension_count - 1u);
    const size_t key_index = optimized_dimension +
        static_cast<size_t>(tile_index) * optimized_dimension_count;
    const uint32_t ranked_index =
        (sample_index & (sample_count - 1u)) ^
        bytes[ranking_offset + key_index];
    uint32_t value = bytes[sobol_offset + dimension +
        static_cast<size_t>(ranked_index) * dimension_count];
    value ^= bytes[scrambling_offset + key_index];
    return (static_cast<float>(value) + 0.5f) /
        static_cast<float>(sample_count);
}

}  // namespace ab3d2::dxr::blue_noise

#endif
