#include "renderer_dxr/dxr_reconstruction_math.h"

#include <cmath>
#include <cstdio>

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
