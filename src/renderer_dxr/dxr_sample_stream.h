#ifndef AB3D2_DXR_SAMPLE_STREAM_H
#define AB3D2_DXR_SAMPLE_STREAM_H

#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::sample_stream {

/*
 * The `pcg4d` integer hash from Mark Jarzynski and Marc Olano, "Hash Functions
 * for GPU Rendering", Journal of Computer Graphics Techniques 9(3), 2020.
 *
 * The pinned Heitz tables optimize eight dimensions, so they distribute error as
 * a blue noise only for the primary-hit decisions that consume those dimensions.
 * Reservoir resampling needs three dimensions per candidate across tens of
 * candidates; taking those from the same tables would reuse the same eight
 * ranking and scrambling channels through a tile translation and hand
 * neighbouring pixels correlated candidates. This stream covers the candidate
 * loop instead, seeded by pixel, frame, and candidate so it is stateless and
 * reproducible.
 *
 * Mirrored by `sampleStream` in shaders/path_trace.hlsl.
 */
inline std::array<uint32_t, 4> pcg4d(std::array<uint32_t, 4> value)
{
    for (uint32_t &component : value) {
        component = component * 1664525u + 1013904223u;
    }
    value[0] += value[1] * value[3];
    value[1] += value[2] * value[0];
    value[2] += value[0] * value[1];
    value[3] += value[1] * value[2];
    for (uint32_t &component : value) {
        component ^= component >> 16u;
    }
    value[0] += value[1] * value[3];
    value[1] += value[2] * value[0];
    value[2] += value[0] * value[1];
    value[3] += value[1] * value[2];
    return value;
}

/*
 * Converts a hashed word to a uniform in [0, 1). The 24-bit mantissa shift keeps
 * every result strictly below one, which the alias-table bucket index relies on.
 */
inline float uniform(uint32_t word)
{
    return static_cast<float>(word >> 8u) * (1.0f / 16777216.0f);
}

/* Four uniforms for one candidate: emitter selection, two barycentric
 * coordinates, and the reservoir acceptance test. */
inline std::array<float, 4> uniforms(uint32_t pixel_x, uint32_t pixel_y,
                                     uint32_t sample_index,
                                     uint32_t sequence_index)
{
    const std::array<uint32_t, 4> hashed =
        pcg4d({pixel_x, pixel_y, sample_index, sequence_index});
    return {uniform(hashed[0]), uniform(hashed[1]), uniform(hashed[2]),
            uniform(hashed[3])};
}

inline float stratified_candidate(float selection, uint32_t candidate,
                                  uint32_t candidate_count)
{
    return candidate_count == 0u ? 0.0f :
        (selection + static_cast<float>(candidate)) /
            static_cast<float>(candidate_count);
}

constexpr uint32_t neighbor_offset_count = 256u;

inline uint32_t reverse_low_byte(uint32_t value)
{
    value &= 0xffu;
    value = ((value & 0x55u) << 1u) | ((value >> 1u) & 0x55u);
    value = ((value & 0x33u) << 2u) | ((value >> 2u) & 0x33u);
    value = ((value & 0x0fu) << 4u) | ((value >> 4u) & 0x0fu);
    return value;
}

/* Fixed low-discrepancy disk mirrored by reservoirNeighborOffset in HLSL. */
inline std::array<float, 2> spatial_neighbor_offset(uint32_t sequence_index)
{
    const uint32_t index = sequence_index & (neighbor_offset_count - 1u);
    const uint32_t radius_index = reverse_low_byte(index);
    const float radius = std::sqrt(
        (static_cast<float>(radius_index) + 0.5f) /
        static_cast<float>(neighbor_offset_count));
    constexpr float golden_angle = 2.39996322972865332f;
    const float angle = (static_cast<float>(index) + 0.5f) * golden_angle;
    return {radius * std::cos(angle), radius * std::sin(angle)};
}

}  // namespace ab3d2::dxr::sample_stream

#endif
