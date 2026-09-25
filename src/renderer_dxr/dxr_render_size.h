#ifndef AB3D2_DXR_RENDER_SIZE_H
#define AB3D2_DXR_RENDER_SIZE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

/*
 * Render and reconstruction extents for the rtx_dlss modes that cannot take
 * DLSS-RR's own size to the window.
 *
 * RR does not take any input to any output. At a given output Quality,
 * Balanced and Performance accept render sizes from half the output on each
 * axis up to the output, and Ultra Performance exactly a third of it. A mode
 * that renders less than a quarter of the window's pixels therefore has RR
 * reconstruct to less than the window, and the presentation's linear upscale
 * covers the rest.
 */
namespace ab3d2::dxr::render_size {

struct Extents {
    uint32_t render_width = 0u;
    uint32_t render_height = 0u;
    uint32_t reconstruction_width = 0u;
    uint32_t reconstruction_height = 0u;
};

/*
 * rtx_dlss=high-performance: a fifth of the window's pixels, between
 * Performance's quarter and Ultra Performance's ninth. RR reconstructs to
 * twice the render size, Performance's own ratio, so the size it is handed is
 * exactly the one Performance renders at that output.
 */
inline constexpr double high_performance_pixel_share = 0.2;

inline Extents high_performance(uint32_t window_width, uint32_t window_height)
{
    const double scale = std::sqrt(high_performance_pixel_share);
    const auto render = [scale](uint32_t extent) {
        return std::max<uint32_t>(1u, static_cast<uint32_t>(
            std::lround(static_cast<double>(extent) * scale)));
    };
    Extents extents;
    extents.render_width = render(window_width);
    extents.render_height = render(window_height);
    extents.reconstruction_width =
        std::min(window_width, extents.render_width * 2u);
    extents.reconstruction_height =
        std::min(window_height, extents.render_height * 2u);
    return extents;
}

/*
 * rtx_dlss=extreme-performance: Ultra Performance's third, reconstructed to
 * two thirds of the window. Two ninths of it on each axis, about 5% of the
 * pixels.
 */
inline uint32_t extreme_performance_reconstruction(uint32_t window_extent)
{
    return std::max<uint32_t>(1u, static_cast<uint32_t>(
        (static_cast<uint64_t>(window_extent) * 2u + 2u) / 3u));
}

}  // namespace ab3d2::dxr::render_size

#endif
