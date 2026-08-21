#ifndef AB3D2_DXR_LIGHT_GRID_H
#define AB3D2_DXR_LIGHT_GRID_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ab3d2::dxr::light_grid {

/*
 * Project-owned regular-grid ReGIR configuration. The grid is rebuilt around
 * the current camera every frame. It covers 8192 world units on each axis;
 * surfaces outside it deliberately use the complete global emitter proposal.
 * Each cell contains 512
 * independently presampled RIS entries built from eight candidates. There is
 * no emitter-count cap: the global alias table is the complete build proposal,
 * and each stored inverse probability corrects the selected entry. The 16 MiB
 * grid is rebuilt around the current camera every frame.
 */
inline constexpr uint32_t cells_per_axis = 16u;
inline constexpr uint32_t cell_count =
    cells_per_axis * cells_per_axis * cells_per_axis;
inline constexpr uint32_t lights_per_cell = 512u;
inline constexpr uint32_t build_samples = 8u;
inline constexpr float cell_size = 512.0f;
inline constexpr float grid_extent = cell_size * cells_per_axis;
inline constexpr uint32_t entry_count = cell_count * lights_per_cell;

struct Entry {
    uint32_t emitter_index;
    float inverse_selection_probability;
};

static_assert(sizeof(Entry) == 8u);
static_assert(entry_count == 2097152u);

struct Position {
    float x;
    float y;
    float z;
};

inline bool world_position_to_cell(Position position, Position center,
                                   uint32_t &cell_index)
{
    const float half_extent = grid_extent * 0.5f;
    const float origin_x = center.x - half_extent;
    const float origin_y = center.y - half_extent;
    const float origin_z = center.z - half_extent;
    const int32_t x = static_cast<int32_t>(
        std::floor((position.x - origin_x) / cell_size));
    const int32_t y = static_cast<int32_t>(
        std::floor((position.y - origin_y) / cell_size));
    const int32_t z = static_cast<int32_t>(
        std::floor((position.z - origin_z) / cell_size));
    if (x < 0 || y < 0 || z < 0 ||
        x >= static_cast<int32_t>(cells_per_axis) ||
        y >= static_cast<int32_t>(cells_per_axis) ||
        z >= static_cast<int32_t>(cells_per_axis)) {
        cell_index = 0u;
        return false;
    }
    cell_index = static_cast<uint32_t>(x) +
        (static_cast<uint32_t>(y) + static_cast<uint32_t>(z) *
             cells_per_axis) * cells_per_axis;
    return true;
}

inline Position cell_center(uint32_t cell_index, Position center)
{
    const uint32_t x = cell_index % cells_per_axis;
    const uint32_t yz = cell_index / cells_per_axis;
    const uint32_t y = yz % cells_per_axis;
    const uint32_t z = yz / cells_per_axis;
    const float half_extent = grid_extent * 0.5f;
    return {
        center.x - half_extent + (static_cast<float>(x) + 0.5f) * cell_size,
        center.y - half_extent + (static_cast<float>(y) + 0.5f) * cell_size,
        center.z - half_extent + (static_cast<float>(z) + 0.5f) * cell_size,
    };
}

/*
 * Volume target for the grid build. The global selection probability already
 * contains triangle area and the conservative emitted-radiance bound. The
 * remaining factor is an average inverse-square falloff over the cell.
 *
 * A project-derived RMS receiver distance and solid-angle cap remain finite
 * when a light intersects a cell and approach ordinary inverse-square falloff
 * at distance. This affects proposal variance only; the stored exact
 * categorical probability leaves the estimator target unchanged.
 */
inline float volume_target(float global_selection_probability,
                           float inverse_area,
                           Position emitter_center, Position grid_cell_center)
{
    if (!(global_selection_probability > 0.0f) || !(inverse_area > 0.0f)) {
        return 0.0f;
    }
    const float x = emitter_center.x - grid_cell_center.x;
    const float y = emitter_center.y - grid_cell_center.y;
    const float z = emitter_center.z - grid_cell_center.z;
    const float distance_squared = x * x + y * y + z * z;
    /* A full cell diagonal bounds the cell plus the +/- half-cell lookup
     * jitter. For a uniform spherical receiver volume, E[r^2] = 3 R^2 / 5. */
    const float volume_radius = cell_size * std::sqrt(3.0f);
    const float rms_distance_squared = distance_squared +
        0.6f * volume_radius * volume_radius;
    const float area = 1.0f / inverse_area;
    constexpr float two_pi = 6.28318530717958647692f;
    const float approximate_solid_angle = std::min(
        area / rms_distance_squared, two_pi);
    /* selection_probability / area is the conservative luminance proxy up to
     * the common normalization of the global alias table. */
    return approximate_solid_angle *
        (global_selection_probability / area);
}

inline float finalize_inverse_selection_probability(
    float weight_sum, float selected_target, uint32_t sample_count)
{
    return weight_sum > 0.0f && selected_target > 0.0f && sample_count > 0u ?
        weight_sum / (static_cast<float>(sample_count) * selected_target) :
        0.0f;
}

}  // namespace ab3d2::dxr::light_grid

#endif
