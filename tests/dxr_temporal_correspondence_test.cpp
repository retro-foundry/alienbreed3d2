/*
 * The temporal-correspondence tests that have to pass before any resampling is
 * allowed to depend on reprojection.
 *
 * Each one isolates a single reason a temporal system reads the wrong history:
 * a still camera whose jitter moves, a camera that pans over a still world, an
 * object that moves under a still camera, and a cut that invalidates
 * everything. They are deliberately cheap and headless, because the failure
 * they catch is a sign error or a jitter term applied twice, and that is far
 * easier to see in arithmetic than in a reconstructed image.
 */

#include "renderer_dxr/dxr_temporal_correspondence.h"

#include <cstdio>
#include <cmath>

using ab3d2::dxr::brdf::Vec3;
using ab3d2::dxr::reconstruction::CameraProjection;
using ab3d2::dxr::reconstruction::PixelJitter;
using ab3d2::dxr::reconstruction::PixelPosition;
using namespace ab3d2::dxr::temporal;

namespace {

constexpr uint32_t render_width = 320u;
constexpr uint32_t render_height = 180u;

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_close(float value, float expected, float tolerance,
                 const char *what)
{
    if (!(std::fabs(value - expected) <= tolerance)) {
        std::fprintf(stderr, "FAIL: %s (got %.6f, expected %.6f)\n", what,
                     value, expected);
        ++failures;
    }
}

/* A camera looking down +Z with +X right and +Y up. */
CameraProjection camera_at(Vec3 position)
{
    CameraProjection camera = {};
    camera.position = position;
    camera.forward = {0.0f, 0.0f, 1.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.tan_half_fov_y = 0.5f;
    camera.aspect = static_cast<float>(render_width) /
        static_cast<float>(render_height);
    camera.width = render_width;
    camera.height = render_height;
    return camera;
}

/* The world point the ray through a given pixel and jitter would reach on a
 * plane at `depth`, so a test can start from a pixel and stay self-consistent. */
Vec3 world_through_pixel(const CameraProjection &camera, float pixel_x,
                         float pixel_y, float depth)
{
    const float ndc_x = (pixel_x / static_cast<float>(camera.width)) * 2.0f -
        1.0f;
    const float ndc_y = 1.0f - (pixel_y / static_cast<float>(camera.height)) *
        2.0f;
    const Vec3 offset =
        camera.right * (ndc_x * camera.aspect * camera.tan_half_fov_y * depth) +
        camera.up * (ndc_y * camera.tan_half_fov_y * depth) +
        camera.forward * depth;
    return camera.position + offset;
}

/*
 * A perfectly static camera and scene, with only the subpixel jitter changing.
 *
 * Nothing in the world moved, so the motion vector must be exactly zero. If it
 * is not, every temporal consumer is being told a still wall is sliding, and
 * ReSTIR history will crawl across it in step with the sampling lattice.
 */
void static_camera_jitter_test()
{
    const CameraProjection camera = camera_at({0.0f, 0.0f, 0.0f});
    const PixelJitter previous_jitter = {0.25f, -0.25f};
    const PixelJitter current_jitter = {-0.25f, 0.25f};

    for (uint32_t y = 0u; y < render_height; y += 37u) {
        for (uint32_t x = 0u; x < render_width; x += 41u) {
            const Vec3 surface = world_through_pixel(
                camera, static_cast<float>(x) + 0.5f + current_jitter.x,
                static_cast<float>(y) + 0.5f + current_jitter.y, 100.0f);
            const MotionVector motion =
                surface_motion(camera, camera, surface, surface);
            check(motion.valid, "static surface projected");
            check(motion.x == 0.0f && motion.y == 0.0f,
                  "a static camera with moving jitter reports zero motion");

            /* The history lookup still shifts, because the previous frame's
             * grid was sampled through its own jitter. That shift is subpixel
             * and must never grow into the geometric motion above. */
            const PixelPosition previous = current_to_previous_pixel(
                x, y, motion, current_jitter, previous_jitter);
            check(previous.valid, "history lookup produced a position");
            check_close(previous.x,
                        static_cast<float>(x) + 0.5f +
                            (current_jitter.x - previous_jitter.x),
                        1.0e-5f, "history lookup removes the previous jitter");
            check(std::fabs(previous.x - (static_cast<float>(x) + 0.5f)) < 1.0f,
                  "the jitter shift stays subpixel");

            const PixelPosition round_trip = previous_to_current_pixel(
                previous.x, previous.y, motion, current_jitter,
                previous_jitter);
            check_close(round_trip.x, static_cast<float>(x) + 0.5f, 1.0e-4f,
                        "reprojection round trip returns the current pixel");
            check_close(round_trip.y, static_cast<float>(y) + 0.5f, 1.0e-4f,
                        "reprojection round trip returns the current row");
        }
    }
}

/*
 * A slow constant pan across a still world.
 *
 * History has to follow the surfaces rather than the pixels, so the motion must
 * point back along the pan by the amount the projection actually moved, and the
 * sign must be the one the convention promises.
 */
void camera_pan_test()
{
    const CameraProjection previous_camera = camera_at({0.0f, 0.0f, 0.0f});
    const CameraProjection current_camera = camera_at({2.0f, 0.0f, 0.0f});
    const PixelJitter jitter = {0.1f, -0.1f};

    const float depth = 100.0f;
    const Vec3 surface = world_through_pixel(current_camera, 160.5f, 90.5f,
                                             depth);
    const MotionVector motion = surface_motion(current_camera, previous_camera,
                                               surface, surface);
    check(motion.valid, "panned surface projected in both frames");

    /* The camera moved +X, so a fixed surface slid left across the image and
     * its history lies to the RIGHT of where it is now. */
    check(motion.x > 0.0f,
          "panning right leaves history to the right of the current pixel");
    check_close(motion.y, 0.0f, 1.0e-3f, "a horizontal pan induces no vertical motion");

    /* Half the horizontal field at this depth spans
     * aspect * tan_half_fov_y * depth world units across half the width. */
    const float half_extent = current_camera.aspect *
        current_camera.tan_half_fov_y * depth;
    const float expected =
        (2.0f / half_extent) * (static_cast<float>(render_width) * 0.5f);
    check_close(motion.x, expected, 0.05f,
                "pan motion matches the projected displacement");

    const PixelPosition previous = current_to_previous_pixel(160u, 90u, motion,
                                                             jitter, jitter);
    check(pixel_inside(previous, render_width, render_height),
          "panned history stays on screen");
    /* With the same jitter in both frames nothing but the motion may shift. */
    check_close(previous.x, 160.5f + motion.x, 1.0e-4f,
                "equal jitter contributes nothing to the lookup");
}

/*
 * One object moving laterally under a completely static camera.
 *
 * This is what separates dense motion from camera-only motion. The background
 * must report zero while the moving surface reports its own displacement, and
 * the background behind the object must not inherit the object's motion.
 */
void moving_object_test()
{
    const CameraProjection camera = camera_at({0.0f, 0.0f, 0.0f});
    const float depth = 100.0f;

    const Vec3 background = world_through_pixel(camera, 60.5f, 90.5f, depth);
    const MotionVector background_motion =
        surface_motion(camera, camera, background, background);
    check(background_motion.x == 0.0f && background_motion.y == 0.0f,
          "static background reports no motion under a static camera");

    /* The object is at the same place on screen now, having arrived from the
     * left, so only its own previous world position differs. */
    const Vec3 object_now = world_through_pixel(camera, 160.5f, 90.5f, depth);
    const Vec3 object_before = object_now - Vec3{6.0f, 0.0f, 0.0f};
    const MotionVector object_motion =
        surface_motion(camera, camera, object_now, object_before);
    check(object_motion.valid, "moving object projected in both frames");
    check(object_motion.x < 0.0f,
          "an object moving right leaves history to its left");
    check_close(object_motion.y, 0.0f, 1.0e-3f,
                "lateral object motion induces no vertical motion");
    check(std::fabs(object_motion.x) > 1.0f,
          "object motion is dense rather than camera-only");

    /* Newly revealed background must not be handed the object's history. */
    TemporalSurfaceData revealed = {};
    revealed.valid = true;
    revealed.depth = depth;
    revealed.geometric_normal = {0.0f, 0.0f, -1.0f};
    revealed.shading_normal = revealed.geometric_normal;
    revealed.material_id = 7u;
    revealed.instance_id = 0u;  /* the world */
    revealed.primitive_id = 11u;

    TemporalSurfaceData departed = revealed;
    departed.instance_id = 42u;  /* the object that was covering it */
    check(!surface_compatible(revealed, departed),
          "disoccluded background rejects the departed object's history");

    TemporalSurfaceData same = revealed;
    check(surface_compatible(revealed, same),
          "an unchanged surface keeps its history");
}

/*
 * A hard camera cut, and the surface checks that have to survive one.
 *
 * After a cut there is no correspondence to find, so the invalid motion has to
 * propagate rather than degrade into a plausible-looking lookup.
 */
void camera_cut_test()
{
    const CameraProjection previous_camera = camera_at({0.0f, 0.0f, 0.0f});
    /* The new view faces the opposite way, so the old surface is behind it. */
    CameraProjection current_camera = camera_at({0.0f, 0.0f, 0.0f});
    current_camera.forward = {0.0f, 0.0f, -1.0f};
    current_camera.right = {-1.0f, 0.0f, 0.0f};

    const Vec3 surface = world_through_pixel(previous_camera, 160.5f, 90.5f,
                                             100.0f);
    const MotionVector motion = surface_motion(current_camera, previous_camera,
                                               surface, surface);
    check(!motion.valid, "a surface behind the cut camera has no correspondence");
    check(!motion_valid(motion), "invalid motion is reported as invalid");

    const PixelPosition previous = current_to_previous_pixel(
        160u, 90u, motion, {0.0f, 0.0f}, {0.0f, 0.0f});
    check(!previous.valid,
          "an invalid motion never yields a history coordinate");

    /* Off-screen reprojection is rejected even when the motion itself is fine. */
    const MotionVector far_motion = {-1000.0f, 0.0f, true};
    const PixelPosition off_screen = current_to_previous_pixel(
        10u, 90u, far_motion, {0.0f, 0.0f}, {0.0f, 0.0f});
    check(!pixel_inside(off_screen, render_width, render_height),
          "history outside the viewport is rejected");

    /* A cut usually lands on a different object at a similar depth, which is
     * exactly the case a depth-only test would wave through. */
    TemporalSurfaceData current = {};
    current.valid = true;
    current.depth = 100.0f;
    current.geometric_normal = {0.0f, 0.0f, -1.0f};
    current.shading_normal = current.geometric_normal;
    current.material_id = 3u;
    current.instance_id = 5u;
    current.primitive_id = 1u;

    TemporalSurfaceData elsewhere = current;
    elsewhere.instance_id = 9u;
    check(!surface_compatible(current, elsewhere),
          "matching depth does not license reuse across instances");

    TemporalSurfaceData turned = current;
    turned.geometric_normal = {0.0f, 1.0f, 0.0f};
    check(!surface_compatible(current, turned),
          "a reoriented surface rejects history");

    TemporalSurfaceData nearer = current;
    nearer.depth = 50.0f;
    check(!surface_compatible(current, nearer),
          "a depth discontinuity rejects history");

    TemporalSurfaceData missing = current;
    missing.valid = false;
    check(!surface_compatible(current, missing),
          "a pixel with no previous surface rejects history");
}

}  // namespace

int main()
{
    static_camera_jitter_test();
    camera_pan_test();
    moving_object_test();
    camera_cut_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d temporal correspondence check(s) failed\n",
                     failures);
        return 1;
    }
    std::printf("temporal correspondence checks passed\n");
    return 0;
}
