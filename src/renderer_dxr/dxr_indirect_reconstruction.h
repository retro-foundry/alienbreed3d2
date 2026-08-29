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

/* The short GI history stores one exact global primitive identity and a capped
 * effective frame count in a single R32_UINT texel. Sixteen million global
 * triangles are far beyond the current scene, but the runtime still fails
 * explicitly instead of allowing the packed identity to alias. Mirrored by
 * shaders/path_trace.hlsl. */
inline constexpr uint32_t temporal_window_default = 4u;
inline constexpr uint32_t temporal_window_maximum = 4u;
inline constexpr float history_motion_limit = 0.5f;
inline constexpr uint32_t temporal_window_mask = 0xffu;
inline constexpr uint32_t temporal_lighting_changed_flag = 0x80000000u;
inline constexpr uint32_t history_primitive_bits = 24u;
inline constexpr uint32_t history_primitive_mask =
    (1u << history_primitive_bits) - 1u;
inline constexpr uint32_t history_count_shift = history_primitive_bits;

inline constexpr bool temporal_window_valid(uint32_t frames)
{
    return frames == 1u || frames == 2u || frames == 4u;
}

inline constexpr uint32_t temporal_configuration(uint32_t frames,
                                                 bool lighting_changed)
{
    return frames | (lighting_changed ? temporal_lighting_changed_flag : 0u);
}

inline constexpr bool pack_history_metadata(uint32_t primitive_index,
                                            uint32_t effective_count,
                                            uint32_t &packed)
{
    if (effective_count == 0u) {
        packed = 0u;
        return true;
    }
    if (primitive_index > history_primitive_mask ||
        effective_count > temporal_window_maximum) {
        packed = 0u;
        return false;
    }
    packed = primitive_index | (effective_count << history_count_shift);
    return true;
}

inline constexpr uint32_t history_primitive_index(uint32_t packed)
{
    return packed & history_primitive_mask;
}

inline constexpr uint32_t history_effective_count(uint32_t packed)
{
    return packed >> history_count_shift;
}

inline constexpr uint32_t retained_history_count(uint32_t previous_count,
                                                 uint32_t temporal_window)
{
    return temporal_window > 1u && previous_count < temporal_window ?
        previous_count : temporal_window > 1u ? temporal_window - 1u : 0u;
}

inline constexpr float current_frame_weight(uint32_t previous_count,
                                            uint32_t temporal_window)
{
    return 1.0f / static_cast<float>(
        retained_history_count(previous_count, temporal_window) + 1u);
}

inline constexpr uint32_t continuation_count(uint32_t path_depth)
{
    const uint32_t bounded = path_depth < maximum_path_depth ?
        path_depth : maximum_path_depth;
    return bounded > 0u ? bounded - 1u : 0u;
}

}  // namespace ab3d2::dxr::indirect_reconstruction

#endif
