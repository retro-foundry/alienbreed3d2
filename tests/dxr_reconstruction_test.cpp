#include "renderer_dxr/dxr_reconstruction_math.h"
#include "renderer_dxr/dxr_auto_exposure.h"
#include "renderer_dxr/dxr_emitter_history.h"
#include "renderer_dxr/dxr_indirect_reconstruction.h"
#include "renderer_dxr/dxr_light_grid.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace ab3d2::dxr::reconstruction;

bool near(float actual, float expected, float tolerance = 1.0e-5f)
{
    return std::fabs(actual - expected) <= tolerance;
}

bool near(Vec3 actual, Vec3 expected, float tolerance = 1.0e-5f)
{
    return near(actual.x, expected.x, tolerance) &&
        near(actual.y, expected.y, tolerance) &&
        near(actual.z, expected.z, tolerance);
}

int fail(const char *message)
{
    std::fprintf(stderr, "DXR reconstruction test failed: %s\n", message);
    return 1;
}

}  // namespace

int main()
{
    namespace exposure = ab3d2::dxr::auto_exposure;
    namespace indirect = ab3d2::dxr::indirect_reconstruction;
    namespace grid = ab3d2::dxr::light_grid;
    static_assert(indirect::filter_steps[0] == 1 &&
                  indirect::filter_steps[1] == 3 &&
                  indirect::filter_steps[2] == 6 &&
                  indirect::filter_steps[3] == 12 &&
                  indirect::filter_reach == 22 &&
                  indirect::continuation_radial_power == 0.4f);
    if (!near(exposure::target(std::log(0.18f) * 8.0f, 8u), 1.0f) ||
        exposure::target(std::log(0.0001f), 1u) !=
            exposure::maximum_exposure ||
        exposure::target(0.0f, 0u) != 1.0f ||
        !near(exposure::adapt(1.0f, 101.0f, true), 6.0f) ||
        exposure::adapt(1.0f, 101.0f, false) != 101.0f ||
        !near(exposure::tone_map_luminance(0.0f), 0.0f) ||
        !near(exposure::tone_map_luminance(
                  exposure::tone_minimum_luminance), 1.0f / 128.0f) ||
        !near(exposure::tone_map_luminance(
                  exposure::tone_white_point), 1.0f)) {
        return fail("automatic exposure contract changed");
    }
    if (!near(indirect::guide_weight(100.0f, 100.0f, 1.0f), 1.0f) ||
        !near(indirect::guide_weight(100.0f, 105.0f, 0.75f), 0.125f) ||
        indirect::guide_weight(100.0f, 110.0f, 1.0f) > 1.0e-5f ||
        indirect::guide_weight(100.0f, 100.0f, 0.5f) != 0.0f ||
        indirect::guide_weight(0.0f, 100.0f, 1.0f) != 0.0f) {
        return fail("low-frequency indirect guide weighting changed");
    }
    static_assert(grid::cell_count == 4096u);
    static_assert(grid::entry_count == 2097152u);
    static_assert(sizeof(grid::Entry) == 8u);

    const grid::Position grid_center = {100.0f, -50.0f, 25.0f};
    uint32_t center_cell = 0u;
    if (!grid::world_position_to_cell(grid_center, grid_center, center_cell) ||
        center_cell != 2184u) {
        return fail("ReGIR camera-centered cell mapping changed");
    }
    const grid::Position mapped_center =
        grid::cell_center(center_cell, grid_center);
    uint32_t round_trip_cell = 0u;
    if (!grid::world_position_to_cell(mapped_center, grid_center,
                                      round_trip_cell) ||
        round_trip_cell != center_cell) {
        return fail("ReGIR cell center did not round-trip through the grid");
    }
    const float half_grid = grid::grid_extent * 0.5f;
    const grid::Position minimum = {
        grid_center.x - half_grid,
        grid_center.y - half_grid,
        grid_center.z - half_grid,
    };
    const grid::Position outside_minimum = {
        minimum.x - 0.01f, minimum.y, minimum.z,
    };
    const grid::Position outside_maximum = {
        grid_center.x + half_grid, grid_center.y, grid_center.z,
    };
    uint32_t boundary_cell = 0u;
    if (!grid::world_position_to_cell(minimum, grid_center, boundary_cell) ||
        boundary_cell != 0u ||
        grid::world_position_to_cell(outside_minimum, grid_center,
                                     boundary_cell) ||
        grid::world_position_to_cell(outside_maximum, grid_center,
                                     boundary_cell)) {
        return fail("ReGIR grid boundary mapping is not half-open");
    }
    const float near_target = grid::volume_target(
        0.25f, 1.0f / 128.0f, grid_center, grid_center);
    const float far_target = grid::volume_target(
        0.25f, 1.0f / 128.0f,
        {grid_center.x + 2048.0f, grid_center.y, grid_center.z}, grid_center);
    if (!std::isfinite(near_target) || !(near_target > far_target) ||
        !near(grid::volume_target(0.5f, 1.0f / 128.0f,
                                 grid_center, grid_center),
              near_target * 2.0f) ||
        grid::volume_target(0.0f, 1.0f / 128.0f,
                            grid_center, grid_center) != 0.0f ||
        grid::volume_target(0.25f, 0.0f,
                            grid_center, grid_center) != 0.0f) {
        return fail("ReGIR volume target is not finite importance sampling");
    }
    if (!near(grid::finalize_inverse_selection_probability(8.0f, 2.0f, 4u),
              1.0f) ||
        grid::finalize_inverse_selection_probability(8.0f, 0.0f, 4u) != 0.0f ||
        grid::finalize_inverse_selection_probability(8.0f, 2.0f, 0u) != 0.0f) {
        return fail("ReGIR RIS inverse-proposal correction changed");
    }
    if (!near(grid::local_solid_angle_pdf(0.25f, 0.125f, 2.0f), 1.0f) ||
        !near(grid::local_solid_angle_pdf(0.25f, 0.125f, 8.0f), 0.25f) ||
        grid::local_solid_angle_pdf(0.25f, 0.0f, 2.0f) != 0.0f ||
        grid::local_solid_angle_pdf(0.25f, 0.125f, 0.0f) != 0.0f) {
        return fail("ReGIR local polygon-light PDF correction changed");
    }

    struct EmitterIdentity {
        uint32_t first_vertex;
        float selection_probability;
        float inverse_area;
        float alias_threshold;
        uint32_t alias_index;
    };
    const std::vector<EmitterIdentity> emitter_layout = {
        {0u, 0.25f, 2.0f, 0.5f, 1u},
        {3u, 0.75f, 4.0f, 1.0f, 1u},
    };
    std::vector<EmitterIdentity> changed_layout = emitter_layout;
    changed_layout[0].alias_threshold = 0.75f;
    changed_layout[0].alias_index = 0u;
    if (!ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("alias representation incorrectly invalidated emitter history");
    }
    changed_layout[1].first_vertex = 6u;
    if (ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("emitter identity change retained incompatible history");
    }
    changed_layout = emitter_layout;
    changed_layout[1].selection_probability = 0.5f;
    if (!ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("proposal-only change incorrectly invalidated emitter history");
    }
    changed_layout[1].inverse_area = 8.0f;
    if (ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("emitter area change retained incompatible history");
    }

    const InitialReservoirDomain one_candidate =
        collapse_initial_reservoir_domain(8.0f, 1u);
    const InitialReservoirDomain four_candidates =
        collapse_initial_reservoir_domain(8.0f, 4u);
    const InitialReservoirDomain no_candidates =
        collapse_initial_reservoir_domain(8.0f, 0u);
    if (!near(one_candidate.weight_sum, 8.0f) ||
        one_candidate.sample_count != 1u ||
        !near(four_candidates.weight_sum, 2.0f) ||
        four_candidates.sample_count != 1u ||
        no_candidates.weight_sum != 0.0f ||
        no_candidates.sample_count != 0u) {
        return fail("initial candidates changed temporal reservoir ownership");
    }

    if (!near(direct_mixture_pdf(0.25f, 16u, 0.0f, 1u, 0.0f, 1u),
              4.0f / 18.0f) ||
        !near(direct_mixture_pdf(0.0f, 16u, 0.125f, 1u, 0.5f, 1u),
              0.625f / 18.0f) ||
        !near(direct_mixture_pdf(0.25f, 2u, 0.0f, 1u, 0.0f, 1u),
              0.5f / 4.0f) ||
        direct_mixture_pdf(1.0f, 0u, 1.0f, 0u, 1.0f, 0u) != 0.0f ||
        !near(finalize_initial_direct_weight(12.0f, 4u, 2.0f), 1.5f) ||
        finalize_initial_direct_weight(12.0f, 0u, 2.0f) != 0.0f ||
        finalize_initial_direct_weight(12.0f, 4u, 0.0f) != 0.0f) {
        return fail("heterogeneous direct-light mixture normalization changed");
    }

    if (!near(finalize_basic_reservoir_weight(8.0f, 2.0f, 2.0f, 8.0f),
              1.0f) ||
        !near(finalize_basic_reservoir_weight(30.0f, 2.0f, 2.0f, 16.0f),
              1.875f) ||
        !near(finalize_basic_reservoir_weight(30.0f, 2.0f, 1.0f, 16.0f),
              0.9375f) ||
        !near(finalize_basic_reservoir_weight(50.0f, 2.0f, 3.0f, 28.0f),
              2.6785714f) ||
        finalize_basic_reservoir_weight(30.0f, 0.0f, 1.0f, 16.0f) != 0.0f ||
        finalize_basic_reservoir_weight(30.0f, 2.0f, 0.0f, 16.0f) != 0.0f) {
        return fail("basic reservoir normalization is not surface-aware");
    }

    const PixelJitter first_jitter = frame_jitter(0u);
    const PixelJitter second_jitter = frame_jitter(1u);
    const PixelJitter last_phase_jitter = frame_jitter(jitter_phase_count - 1u);
    if (!near(first_jitter.x, 0.0f) ||
        !near(first_jitter.y, -1.0f / 6.0f) ||
        !near(second_jitter.x, -0.25f) ||
        !near(second_jitter.y, 1.0f / 6.0f) ||
        !near(last_phase_jitter.x, -0.484375f) ||
        !near(last_phase_jitter.y, 0.29012346f)) {
        return fail("frame jitter sequence is not deterministic");
    }

    /* Ray Reconstruction only reaches a fixed point if the sub-pixel offsets
     * repeat, so the sequence must have exactly `jitter_phase_count` phases and
     * every offset must stay inside the pixel. */
    for (uint32_t phase = 0u; phase < jitter_phase_count; ++phase) {
        const PixelJitter phase_jitter = frame_jitter(phase);
        const PixelJitter wrapped_jitter =
            frame_jitter(phase + jitter_phase_count * 7u);
        if (!near(phase_jitter.x, wrapped_jitter.x) ||
            !near(phase_jitter.y, wrapped_jitter.y)) {
            return fail("frame jitter did not repeat with the phase period");
        }
        if (phase_jitter.x < -0.5f || phase_jitter.x > 0.5f ||
            phase_jitter.y < -0.5f || phase_jitter.y > 0.5f) {
            return fail("frame jitter left the pixel footprint");
        }
        for (uint32_t earlier = 0u; earlier < phase; ++earlier) {
            const PixelJitter other = frame_jitter(earlier);
            if (near(phase_jitter.x, other.x) &&
                near(phase_jitter.y, other.y)) {
                return fail("frame jitter phases are not distinct");
            }
        }
    }

    const Vec3 dielectric = specular_albedo({0.04f, 0.04f, 0.04f}, 0.5f,
                                             1.0f);
    const Vec3 metal = specular_albedo({0.8f, 0.2f, 0.1f}, 0.25f, 0.5f);
    if (!near(dielectric, {0.0354616f, 0.0354616f, 0.0354616f}, 1.0e-5f) ||
        !near(metal, {0.784531f, 0.221111f, 0.127208f}, 1.0e-5f) ||
        !near(specular_albedo({0.0f, 0.0f, 0.0f}, 0.5f, 0.5f),
              {0.0f, 0.0f, 0.0f})) {
        return fail("NVIDIA specular-albedo approximation changed");
    }

    const CameraProjection current = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        1.0f,
        2.0f,
        200u,
        100u,
    };
    const PixelPosition center = project_world(current, {0.0f, 0.0f, 10.0f});
    const PixelPosition right = project_world(current, {10.0f, 0.0f, 10.0f});
    if (!center.valid || !right.valid || !near(center.x, 100.0f) ||
        !near(center.y, 50.0f) || !near(right.x, 150.0f) ||
        !near(right.y, 50.0f) ||
        project_world(current, {0.0f, 0.0f, -1.0f}).valid) {
        return fail("world-to-pixel projection is invalid");
    }

    CameraProjection previous_camera = current;
    previous_camera.position.x = -1.0f;
    const PixelPosition camera_motion = scene_motion(
        current, previous_camera, {0.0f, 0.0f, 10.0f},
        {0.0f, 0.0f, 10.0f});
    const PixelPosition object_motion = scene_motion(
        current, current, {1.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 10.0f});
    if (!camera_motion.valid || !near(camera_motion.x, 5.0f) ||
        !near(camera_motion.y, 0.0f) || !object_motion.valid ||
        !near(object_motion.x, -5.0f) || !near(object_motion.y, 0.0f)) {
        return fail("motion is not previousPixel-currentPixel in pixel units");
    }

    /* Renderer-owned history is indexed in the previous frame's jittered pixel
     * grid. Scene motion intentionally excludes jitter for Streamline, so the
     * private reservoir and hit-distance reprojection must apply the phase
     * difference exactly once. This case crosses into the adjacent pixel and
     * would fetch stale history if the jitter terms were omitted. */
    const PixelPosition history_pixel = reproject_history_pixel(
        10u, 20u, {0.0f, 0.0f, true}, {0.49f, -0.49f},
        {-0.49f, 0.49f}, 200u, 100u);
    const PixelPosition moving_history_pixel = reproject_history_pixel(
        10u, 20u, {5.0f, -2.0f, true}, {0.25f, -0.25f},
        {-0.25f, 0.25f}, 200u, 100u);
    if (!history_pixel.valid || !near(history_pixel.x, 11.48f) ||
        !near(history_pixel.y, 19.52f) ||
        static_cast<uint32_t>(history_pixel.x) != 11u ||
        !moving_history_pixel.valid ||
        !near(moving_history_pixel.x, 16.0f) ||
        !near(moving_history_pixel.y, 18.0f) ||
        reproject_history_pixel(0u, 0u, {0.0f, 0.0f, false}, {}, {},
                                200u, 100u).valid) {
        return fail("history reprojection did not account for jitter phases");
    }

    CameraProjection previous_yaw = current;
    previous_yaw.forward = {1.0f, 0.0f, 0.0f};
    previous_yaw.right = {0.0f, 0.0f, -1.0f};
    const PixelPosition sky_current = project_direction(current,
                                                        {0.0f, 0.0f, 1.0f});
    const PixelPosition sky_previous = project_direction(previous_yaw,
                                                         {0.0f, 0.0f, 1.0f});
    if (!sky_current.valid || sky_previous.valid) {
        return fail("direction projection did not preserve sky rotation semantics");
    }
    return 0;
}
