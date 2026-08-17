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
