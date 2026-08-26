#ifndef AB3D2_DXR_INDIRECT_RECONSTRUCTION_H
#define AB3D2_DXR_INDIRECT_RECONSTRUCTION_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::indirect_reconstruction {

/*
 * Project-owned low-frequency diffuse reconstruction constants mirrored by
 * shaders/path_trace.hlsl. Temporal incident radiance is integrated over
 * guide-compatible 3x3 full-resolution regions. A deflicker bound and three
 * guide-aware 3x3 wavelet passes operate on that one-third-resolution signal
 * before bilateral reconstruction at the original pixel grid. Each deflicker
 * comparison therefore represents a region, not an individual Monte Carlo
 * path.
 */
inline constexpr int downsample_factor = 3;
inline constexpr std::array<int, 3> filter_steps = {1, 2, 4};
inline constexpr std::array<float, 2> filter_kernel = {1.0f, 0.5f};
inline constexpr int filter_radius = 1;
inline constexpr int filter_reach = 1 + downsample_factor *
    (filter_steps[0] + filter_steps[1] + filter_steps[2]);
inline constexpr float deflicker_neighbor_factor = 2.0f;
inline constexpr float sh_basis_l0 = 0.282095f;
inline constexpr float sh_basis_l1 = 0.488603f;
inline constexpr float sh_irradiance_l0 = 0.886226f;
inline constexpr float sh_irradiance_l1 = 1.023326f;
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
    float luminance_sh[4];
    float chroma[2];
    float depth;
    uint32_t normal;
    uint32_t sample_count;
    float padding;
};

static_assert(sizeof(HistoryPixel) == 40u);

struct Signal {
    float luminance_sh[4];
    float chroma[2];
};

struct Color {
    float red;
    float green;
    float blue;
};

inline Signal signal_from_radiance(Color color, float direction_x,
                                   float direction_y, float direction_z)
{
    const float co = color.red - color.blue;
    const float base = color.blue + co * 0.5f;
    const float cg = color.green - base;
    const float y = std::max(base + cg * 0.5f, 0.0f);
    return {{direction_x * sh_basis_l1 * y,
             direction_y * sh_basis_l1 * y,
             direction_z * sh_basis_l1 * y,
             sh_basis_l0 * y},
            {co, cg}};
}

inline Color project_signal(const Signal &signal, float normal_x,
                            float normal_y, float normal_z)
{
    const float directional = signal.luminance_sh[0] * normal_x +
        signal.luminance_sh[1] * normal_y +
        signal.luminance_sh[2] * normal_z;
    const float y = std::max(2.0f *
        (sh_irradiance_l1 * directional +
         sh_irradiance_l0 * signal.luminance_sh[3]), 0.0f);
    const float chroma_scale = y * sh_basis_l0 /
        std::max(signal.luminance_sh[3], 1.0e-6f);
    const float co = signal.chroma[0] * chroma_scale;
    const float cg = signal.chroma[1] * chroma_scale;
    const float base = y - cg * 0.5f;
    const float green = cg + base;
    const float blue = base - co * 0.5f;
    const float red = blue + co;
    return {std::max(red, 0.0f), std::max(green, 0.0f),
            std::max(blue, 0.0f)};
}

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

inline float kernel_weight(int offset)
{
    const int distance = std::abs(offset);
    return distance <= filter_radius ? filter_kernel[distance] : 0.0f;
}

inline float deflicker_scale(float center_luminance,
                             float neighbor_luminance_sum,
                             uint32_t neighbor_count)
{
    if (!(center_luminance > 0.0f) || !std::isfinite(center_luminance) ||
        !(neighbor_luminance_sum >= 0.0f) ||
        !std::isfinite(neighbor_luminance_sum) || neighbor_count == 0u) {
        return 1.0f;
    }
    const float maximum_luminance = deflicker_neighbor_factor *
        neighbor_luminance_sum / static_cast<float>(neighbor_count);
    return std::max(0.0f, std::min(1.0f,
        maximum_luminance / center_luminance));
}

inline constexpr bool filter_support_is_continuous()
{
    int reach = 0;
    for (int step : filter_steps) {
        if (reach > 0 && step > reach * 2 + 1) {
            return false;
        }
        reach += filter_radius * step;
    }
    return 1 + downsample_factor * reach == filter_reach;
}

static_assert(filter_support_is_continuous());

}  // namespace ab3d2::dxr::indirect_reconstruction

#endif
