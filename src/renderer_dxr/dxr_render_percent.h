#ifndef AB3D2_DXR_RENDER_PERCENT_H
#define AB3D2_DXR_RENDER_PERCENT_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "renderer_ray_tracing_options.h"

/*
 * rtx_render_percent: the share of the window's pixels the path tracer renders,
 * and how Ray Reconstruction gets from there to the window.
 *
 * RR does not take any input to any output. At a given output each DLSS mode
 * accepts render sizes from its own ratio up to the output itself, and the
 * lightest, Ultra Performance, only its exact one third. So a render size down
 * to half the window on each axis (25% of the pixels) reconstructs straight to
 * the window. Between that and one third, nothing can: RR reconstructs to twice
 * the render size and the presentation's linear upscale covers the rest. From
 * one third down, Ultra Performance reconstructs to three times it the same way.
 */
namespace ab3d2::dxr::render_percent {

inline constexpr float minimum = 5.0f;
inline constexpr float maximum = 100.0f;

/* Output-to-render ratio on each axis of each mode's own render size. */
inline constexpr double quality_ratio = 1.5;
inline constexpr double balanced_ratio = 1.724;
inline constexpr double performance_ratio = 2.0;
inline constexpr double ultra_performance_ratio = 3.0;

struct Plan {
    RendererRayReconstructionMode mode = RENDERER_RAY_RECONSTRUCTION_OFF;
    /* What the path tracer renders. Zero means RR's own size for `mode` at
     * the reconstruction extent, which Ultra Performance needs because it
     * accepts exactly one render size. */
    uint32_t render_width = 0u;
    uint32_t render_height = 0u;
    /* What RR reconstructs to; the presentation's linear upscale covers the
     * rest of the window. */
    uint32_t reconstruction_width = 0u;
    uint32_t reconstruction_height = 0u;
};

inline bool valid(float percent)
{
    return std::isfinite(percent) && percent >= minimum && percent <= maximum;
}

/* Output-to-render ratio on each axis for a share of the pixels. */
inline double axis_ratio(float percent)
{
    return 1.0 / std::sqrt(static_cast<double>(percent) / 100.0);
}

/*
 * The lightest mode whose own ratio is at least the one asked for, so the
 * requested size lies between that mode's own render size and its output and
 * RR accepts it. Past Performance's reach the mode is the one that
 * reconstructs to the reduced extent: Performance below one third's worth of
 * scaling, Ultra Performance from there.
 */
inline RendererRayReconstructionMode mode_for(float percent)
{
    const double ratio = axis_ratio(percent);
    constexpr double slack = 1.0e-6;
    if (ratio <= quality_ratio + slack) {
        return RENDERER_RAY_RECONSTRUCTION_QUALITY;
    }
    if (ratio <= balanced_ratio + slack) {
        return RENDERER_RAY_RECONSTRUCTION_BALANCED;
    }
    /* Within a percent of one third Ultra Performance takes it, straight to
     * the window at its own size: a percentage cannot be typed to land on one
     * ninth exactly, and one short would otherwise mean a stretch. */
    if (ratio < ultra_performance_ratio * 0.99) {
        return RENDERER_RAY_RECONSTRUCTION_PERFORMANCE;
    }
    return RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE;
}

inline Plan plan(float percent, uint32_t window_width, uint32_t window_height)
{
    Plan result;
    result.mode = mode_for(percent);
    const double scale = 1.0 / axis_ratio(percent);
    const auto scaled = [scale](uint32_t extent) {
        return std::max<uint32_t>(1u, static_cast<uint32_t>(
            std::lround(static_cast<double>(extent) * scale)));
    };
    const uint32_t render_width = std::min(scaled(window_width), window_width);
    const uint32_t render_height =
        std::min(scaled(window_height), window_height);
    const double ratio = axis_ratio(percent);
    if (ratio <= performance_ratio + 1.0e-6) {
        /* Within reach: straight to the window. */
        result.render_width = render_width;
        result.render_height = render_height;
        result.reconstruction_width = window_width;
        result.reconstruction_height = window_height;
    } else if (result.mode == RENDERER_RAY_RECONSTRUCTION_PERFORMANCE) {
        /* Performance's own size at twice the render size is the render size
         * exactly, so RR accepts it. */
        result.render_width = render_width;
        result.render_height = render_height;
        result.reconstruction_width = std::min(window_width, render_width * 2u);
        result.reconstruction_height =
            std::min(window_height, render_height * 2u);
    } else {
        /* Ultra Performance renders round(extent / 3) and nothing else, so
         * choose the extent and let RR name the size. */
        result.reconstruction_width =
            std::min(window_width, render_width * 3u + 1u);
        result.reconstruction_height =
            std::min(window_height, render_height * 3u + 1u);
    }
    return result;
}

}  // namespace ab3d2::dxr::render_percent

#endif
