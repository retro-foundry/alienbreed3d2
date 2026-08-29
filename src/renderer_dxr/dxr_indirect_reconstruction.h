#ifndef AB3D2_DXR_INDIRECT_RECONSTRUCTION_H
#define AB3D2_DXR_INDIRECT_RECONSTRUCTION_H

#include <cstdint>

namespace ab3d2::dxr::indirect_reconstruction {

/* Startup-only diagnostic composition. Every option preserves the ordinary
 * path trace and reconstruction guides while isolating one contribution at
 * the final noisy-HDR boundary before the single DLSS-RR evaluation. */
enum class RadianceChannel : uint32_t {
    combined = 0u,
    emission = 1u,
    direct_diffuse = 2u,
    direct_specular = 3u,
    indirect = 4u,
    smooth_specular = 5u,
    rough_specular = 6u,
};

/* Runtime path-depth and dimension contracts mirrored by path_trace.hlsl.
 * Depth counts the primary surface. The first continuation consumes dimensions
 * 6/7 from group zero; every later continuation advances by one eight-value
 * group. Polygon RIS reserves 1024 streams per reached surface, matching the
 * public candidate-count ceiling. */
inline constexpr uint32_t maximum_path_depth = 8u;
inline constexpr uint32_t path_dimensions_per_continuation = 8u;
inline constexpr uint32_t direction_dimension_x = 6u;
inline constexpr uint32_t direction_dimension_y = 7u;
inline constexpr uint32_t polygon_bounce_stream_stride = 1024u;

inline constexpr uint32_t continuation_count(uint32_t path_depth)
{
    const uint32_t bounded = path_depth < maximum_path_depth ?
        path_depth : maximum_path_depth;
    return bounded > 0u ? bounded - 1u : 0u;
}

}  // namespace ab3d2::dxr::indirect_reconstruction

#endif
