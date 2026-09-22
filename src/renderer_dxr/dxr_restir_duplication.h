#ifndef AB3D2_DXR_RESTIR_DUPLICATION_H
#define AB3D2_DXR_RESTIR_DUPLICATION_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/*
 * Detecting when a reservoir's confidence is a lie, and shortening its history
 * when it is.
 *
 * Temporal reuse raises a reservoir's represented sample count M every frame it
 * survives, and spatial reuse copies whichever path currently looks best into
 * its neighbours. Do both for long enough and a neighbourhood fills with
 * descendants of a single canonical path, all claiming a large M. Their
 * combined confidence says "many independent samples agree"; the truth is that
 * one sample has been counted many times.
 *
 * That is not merely inefficient. High confidence is exactly what makes a
 * reservoir slow to adapt, so an impoverished region stops responding to
 * changing light and keeps any firefly it has adopted, and a downstream
 * denoiser or DLSS Ray Reconstruction reads the resulting stability as signal.
 *
 * ReSTIR PT Enhanced (Lin, Kettunen and Wyman 2026) measures duplication
 * directly: count how many pixels in a neighbourhood carry the same sample
 * ancestry, and lower the temporal confidence cap toward one as that fraction
 * rises. The window, the neighbour count and the response curve are reproduced
 * from that paper's RTXDI implementation rather than replaced by an invented
 * weighting.
 */

namespace ab3d2::dxr::restir {

/* Half-width of the counting window: a 17x17 neighbourhood. */
constexpr int32_t duplication_radius = 8;

/* Neighbours in that window, excluding the pixel itself. */
constexpr float duplication_neighbor_count = 288.0f;

/* The count is stored in eight bits. */
constexpr uint32_t duplication_maximum_count = 255u;

/*
 * How many neighbours of `pixel` carry the same sample ancestry.
 *
 * Ancestry zero means "no sample", and never counts as a match; otherwise a
 * region of empty pixels would look maximally impoverished and have its history
 * cut for having no history at all.
 */
inline uint32_t duplication_count(const std::vector<uint32_t> &ancestry,
                                  int32_t width, int32_t height, int32_t x,
                                  int32_t y)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 0u;
    }
    const uint32_t own = ancestry[static_cast<size_t>(y) *
                                  static_cast<size_t>(width) +
                                  static_cast<size_t>(x)];
    if (own == 0u) {
        return 0u;
    }
    uint32_t count = 0u;
    for (int32_t dy = -duplication_radius; dy <= duplication_radius; ++dy) {
        for (int32_t dx = -duplication_radius; dx <= duplication_radius; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int32_t nx = x + dx;
            const int32_t ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                continue;
            }
            const uint32_t neighbor =
                ancestry[static_cast<size_t>(ny) * static_cast<size_t>(width) +
                         static_cast<size_t>(nx)];
            if (neighbor == own) {
                ++count;
            }
        }
    }
    return std::min(count, duplication_maximum_count);
}

/* The fraction of the neighbourhood that shares this pixel's ancestry. */
inline float impoverishment(uint32_t count)
{
    return std::min(static_cast<float>(count) / duplication_neighbor_count,
                    1.0f);
}

/*
 * The temporal confidence cap this pixel is allowed, given its duplication.
 *
 * `strength` shapes an exponent rather than scaling the result directly, which
 * is what lets one control span "only cut history where duplication is severe"
 * and "cut it as soon as duplication appears at all". Zero disables the
 * reduction entirely, leaving the configured cap in place.
 */
inline float reduced_history_length(float maximum_history, uint32_t count,
                                    float strength)
{
    if (!(strength > 0.0f) || !(maximum_history > 1.0f)) {
        return std::max(maximum_history, 1.0f);
    }
    const float ratio = impoverishment(count);
    const float power_factor =
        0.1f * std::pow(2.0f, 6.0f * (1.0f - strength) - 3.0f);
    const float t = std::pow(ratio, power_factor);
    const float reduced = maximum_history + (1.0f - maximum_history) * t;
    if (!std::isfinite(reduced)) {
        return std::max(maximum_history, 1.0f);
    }
    return std::max(1.0f, std::min(reduced, maximum_history));
}

}  // namespace ab3d2::dxr::restir

#endif
