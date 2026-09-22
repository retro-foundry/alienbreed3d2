#ifndef AB3D2_DXR_TEMPORAL_CORRESPONDENCE_H
#define AB3D2_DXR_TEMPORAL_CORRESPONDENCE_H

#include "dxr_brdf_math.h"
#include "dxr_reconstruction_math.h"

#include <cmath>
#include <cstdint>

/*
 * The single answer to "which previous-frame surface corresponds to this
 * current surface?".
 *
 * Several systems need that answer and they must not disagree about it: ReSTIR
 * reprojects reservoirs with it, the guide buffers hand it to DLSS Super
 * Resolution and Ray Reconstruction, and any future frame generation will read
 * the same vectors. When two of them interpret motion differently, one accepts
 * history the other rejects, and the result is ghosting, reservoir trails, or
 * disocclusion behaviour that differs between the lighting and the image.
 *
 * ================= CONVENTION =================
 *
 * A motion vector is measured in render-resolution pixels and points BACKWARDS
 * in time, from where a surface point is now to where the same point was:
 *
 *     previousPixel = currentPixel + motion
 *
 * Both endpoints are produced by UNJITTERED camera projections. Jitter moves
 * where a pixel is sampled; it does not move the world. A static surface under
 * a static camera therefore has motion exactly zero however the sampling
 * lattice shifts, which is what keeps ReSTIR history from crawling across a
 * still wall and what stops DLSS from reading a subpixel dither as movement.
 *
 * Motion is dense: it carries camera movement, rigid instance movement and
 * deforming geometry alike, because it is computed from a surface point's own
 * previous world position rather than from the camera alone.
 *
 * Jitter enters only when addressing a stored buffer. A history buffer is a
 * grid of samples that were each taken through their own frame's jitter, so
 * converting a projected position into a texel of that grid has to remove the
 * jitter it was captured with. `current_to_previous_pixel` does exactly that
 * and is the only place the renderer is allowed to reason about it; no shader
 * should ever guess the sign of a jitter term on its own.
 *
 * ==============================================
 */

namespace ab3d2::dxr::temporal {

using brdf::Vec3;
using reconstruction::CameraProjection;
using reconstruction::PixelJitter;
using reconstruction::PixelPosition;

/*
 * Mirrors `InvalidMotion` in shaders/path_trace.hlsl. A displacement at least
 * this large cannot reference usable history, and the value survives the FP16
 * motion buffer without rounding into infinity.
 */
constexpr float invalid_motion = 65504.0f;

struct MotionVector {
    float x;
    float y;
    /* False when either endpoint failed to project, which is the renderer's
     * "there is no correspondence" answer rather than a displacement of zero. */
    bool valid;
};

/*
 * Everything the renderer knows about one render-resolution pixel's primary
 * surface, and the only record any temporal consumer should read.
 *
 * Identity is carried alongside geometry deliberately: a depth and normal match
 * is not evidence that two frames saw the same thing, and only the instance and
 * primitive identity can distinguish a surface that stayed put from a different
 * surface that happens to lie at the same depth behind it.
 */
struct TemporalSurfaceData {
    float depth;
    Vec3 world_position;
    Vec3 geometric_normal;
    Vec3 shading_normal;
    uint32_t material_id;
    uint32_t instance_id;
    uint32_t primitive_id;
    MotionVector motion;
    /* False where the primary ray hit nothing, so no consumer mistakes a
     * cleared record for a surface at the origin. */
    bool valid;
};

/*
 * The backwards motion of one surface point, in render-resolution pixels.
 *
 * `current_world` is where the point is now and `previous_world` where the same
 * point was, which is what makes the result dense rather than camera-only: a
 * caller passes an instance's previously-posed vertex position and the object's
 * own movement falls out of the subtraction.
 */
inline MotionVector surface_motion(const CameraProjection &current_camera,
                                   const CameraProjection &previous_camera,
                                   Vec3 current_world, Vec3 previous_world)
{
    const PixelPosition now = reconstruction::project_world(current_camera,
                                                            current_world);
    const PixelPosition before = reconstruction::project_world(previous_camera,
                                                               previous_world);
    if (!now.valid || !before.valid) {
        return {invalid_motion, invalid_motion, false};
    }
    /* A displacement wider than the frame cannot address history. Clamping to
     * the render extent keeps it finite rather than letting it round up into
     * the invalid sentinel. */
    const float width = static_cast<float>(current_camera.width);
    const float height = static_cast<float>(current_camera.height);
    const float dx = std::fmin(std::fmax(before.x - now.x, -width), width);
    const float dy = std::fmin(std::fmax(before.y - now.y, -height), height);
    return {dx, dy, true};
}

inline bool motion_valid(MotionVector motion)
{
    return motion.valid && std::fabs(motion.x) < invalid_motion &&
        std::fabs(motion.y) < invalid_motion;
}

/*
 * Where a current pixel's surface sits in the PREVIOUS frame's sample grid.
 *
 * The result is continuous and measured from the grid origin, so `floor()`
 * gives the texel to read and the fraction is available for filtering. The
 * current ray passed through `pixel + 0.5 + current jitter`; adding the motion
 * reaches the previous projection, and removing the previous jitter converts
 * that projection into the grid the previous frame actually wrote.
 */
inline PixelPosition current_to_previous_pixel(uint32_t x, uint32_t y,
                                               MotionVector motion,
                                               PixelJitter current_jitter,
                                               PixelJitter previous_jitter)
{
    if (!motion_valid(motion)) {
        return {0.0f, 0.0f, false};
    }
    return {
        static_cast<float>(x) + 0.5f + motion.x +
            (current_jitter.x - previous_jitter.x),
        static_cast<float>(y) + 0.5f + motion.y +
            (current_jitter.y - previous_jitter.y),
        true,
    };
}

/*
 * The exact inverse, for the reverse direction of a paired reuse and for
 * asserting that a round trip returns where it started. `motion` is the vector
 * recorded at the corresponding CURRENT pixel, because that is where the
 * renderer stores it.
 */
inline PixelPosition previous_to_current_pixel(float previous_x,
                                               float previous_y,
                                               MotionVector motion,
                                               PixelJitter current_jitter,
                                               PixelJitter previous_jitter)
{
    if (!motion_valid(motion)) {
        return {0.0f, 0.0f, false};
    }
    return {
        previous_x - motion.x - (current_jitter.x - previous_jitter.x),
        previous_y - motion.y - (current_jitter.y - previous_jitter.y),
        true,
    };
}

inline bool pixel_inside(PixelPosition position, uint32_t width,
                         uint32_t height)
{
    return position.valid && position.x >= 0.0f && position.y >= 0.0f &&
        position.x < static_cast<float>(width) &&
        position.y < static_cast<float>(height);
}

/*
 * How far two surfaces may differ and still be treated as the same one.
 *
 * Depth is compared relatively because the world spans several thousand units
 * and a fixed tolerance would be far too loose near the far plane and far too
 * tight against a nearby wall.
 */
struct CompatibilityThresholds {
    float relative_depth = 0.02f;
    float normal_cosine = 0.9f;
};

/*
 * Whether a reprojected previous surface may supply history to a current one.
 *
 * Landing on screen is not sufficient and neither is a plausible depth: a
 * reprojection can arrive at a texel that holds a different object entirely,
 * and accepting it is how a temporal system paints one surface's lighting onto
 * another. Identity is therefore checked first and geometry only confirms it.
 */
inline bool surface_compatible(const TemporalSurfaceData &current,
                               const TemporalSurfaceData &previous,
                               const CompatibilityThresholds &thresholds = {})
{
    if (!current.valid || !previous.valid) {
        return false;
    }
    if (current.instance_id != previous.instance_id ||
        current.material_id != previous.material_id) {
        return false;
    }
    if (!(current.depth > 0.0f) || !(previous.depth > 0.0f)) {
        return false;
    }
    const float depth_difference = std::fabs(current.depth - previous.depth);
    if (depth_difference >
        thresholds.relative_depth * std::fmax(current.depth, previous.depth)) {
        return false;
    }
    /* Orientation uses the geometric normal: a normal map may swing the shading
     * normal far enough to reject a surface that never moved. */
    if (brdf::dot(current.geometric_normal, previous.geometric_normal) <
        thresholds.normal_cosine) {
        return false;
    }
    return true;
}

}  // namespace ab3d2::dxr::temporal

#endif
