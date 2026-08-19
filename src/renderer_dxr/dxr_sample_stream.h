#ifndef AB3D2_DXR_SAMPLE_STREAM_H
#define AB3D2_DXR_SAMPLE_STREAM_H

#include <array>
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

}  // namespace ab3d2::dxr::sample_stream

#endif
