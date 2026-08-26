#ifndef AB3D2_DXR_INDIRECT_RECONSTRUCTION_H
#define AB3D2_DXR_INDIRECT_RECONSTRUCTION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::indirect_reconstruction {

/*
 * Project-owned low-frequency diffuse reconstruction constants mirrored by
 * shaders/path_trace.hlsl. Four sparse 3x3 passes expand from immediate
 * neighbours to a 22-pixel full-resolution reach. This is the project-owned
 * equivalent of the broad low-frequency filter that Q2RTX applies at reduced
 * resolution, without importing its shader implementation.
 */
inline constexpr std::array<int, 4> filter_steps = {1, 3, 6, 12};
inline constexpr int filter_reach = 22;
inline constexpr float relative_depth_tolerance = 0.1f;
inline constexpr float normal_tolerance = 0.5f;
/* Q2RTX's low-frequency path deliberately samples slightly more grazing
 * directions than an ordinary cosine hemisphere so a sparse screen block
 * covers broad transport directions. This project-owned sampler mirrors that
 * distributional property, not its implementation. */
inline constexpr float continuation_radial_power = 0.4f;

/* GPU history layout mirrored by IndirectHistoryPixel in path_trace.hlsl.
 * `normal` is the primary geometric normal: normal-map detail must not split
 * room-scale indirect reconstruction into unrelated high-frequency patches. */
struct HistoryPixel {
    float incident_radiance[3];
    float depth;
    uint32_t normal;
    uint32_t sample_count;
    float padding[2];
};

static_assert(sizeof(HistoryPixel) == 32u);

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

}  // namespace ab3d2::dxr::indirect_reconstruction

#endif
