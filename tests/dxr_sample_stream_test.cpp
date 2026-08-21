#include "renderer_dxr/dxr_sample_stream.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace ab3d2::dxr::sample_stream;

int fail(const char *message)
{
    std::fprintf(stderr, "DXR sample stream test failed: %s\n", message);
    return 1;
}

}  // namespace

int main()
{
    /*
     * Determinism, and a pin against accidental change. These are this
     * implementation's own outputs, not values published with the paper: the
     * paper specifies the construction, and this fixes our transcription of it so
     * the HLSL mirror can be compared against the same numbers.
     */
    const std::array<uint32_t, 4> hashed = pcg4d({7u, 11u, 13u, 17u});
    if (pcg4d({7u, 11u, 13u, 17u}) != hashed) {
        return fail("pcg4d is not deterministic");
    }
    if (pcg4d({7u, 11u, 13u, 18u}) == hashed ||
        pcg4d({7u, 11u, 14u, 17u}) == hashed ||
        pcg4d({8u, 11u, 13u, 17u}) == hashed) {
        return fail("pcg4d ignored an input component");
    }
    const std::array<uint32_t, 4> pinned = {1888506481u, 2638939206u,
                                            2567560720u, 1326896239u};
    if (hashed != pinned) {
        std::fprintf(stderr, "pcg4d(7,11,13,17) = %u %u %u %u\n", hashed[0],
                     hashed[1], hashed[2], hashed[3]);
        return fail("pcg4d transcription changed");
    }

    /* Every uniform must land in [0, 1). The alias-table bucket index truncates
     * `uniform * count`, so a result of exactly one would index out of range. */
    if (!(uniform(0u) >= 0.0f) || !(uniform(0xffffffffu) < 1.0f)) {
        return fail("uniform left the half-open unit interval");
    }

    for (uint32_t candidate = 0u; candidate < 4u; ++candidate) {
        const float selection = stratified_candidate(0.25f, candidate, 4u);
        if (!(selection >= 0.0f) || !(selection < 1.0f) ||
            static_cast<uint32_t>(selection * 4.0f) != candidate) {
            return fail("initial emitter candidate did not occupy its stratum");
        }
    }
    if (stratified_candidate(0.5f, 0u, 0u) != 0.0f) {
        return fail("empty candidate stratification was not rejected");
    }

    double offset_x_sum = 0.0;
    double offset_y_sum = 0.0;
    double radius_squared_sum = 0.0;
    for (uint32_t index = 0u; index < neighbor_offset_count; ++index) {
        const std::array<float, 2> offset = spatial_neighbor_offset(index);
        const double radius_squared =
            static_cast<double>(offset[0]) * offset[0] +
            static_cast<double>(offset[1]) * offset[1];
        if (!(radius_squared > 0.0) || !(radius_squared < 1.0)) {
            return fail("spatial neighbor offset left the unit disk");
        }
        offset_x_sum += offset[0];
        offset_y_sum += offset[1];
        radius_squared_sum += radius_squared;
    }
    const double offset_count = static_cast<double>(neighbor_offset_count);
    if (std::fabs(offset_x_sum / offset_count) > 0.05 ||
        std::fabs(offset_y_sum / offset_count) > 0.05 ||
        std::fabs(radius_squared_sum / offset_count - 0.5) > 1.0e-5) {
        return fail("spatial neighbor disk is not low-discrepancy");
    }

    /*
     * Uniformity across the candidate dimension for a fixed pixel, which is the
     * axis the resampling loop actually walks.
     */
    constexpr uint32_t bucket_count = 16u;
    constexpr uint32_t candidate_count = 4096u;
    for (uint32_t component = 0; component < 4u; ++component) {
        std::vector<uint32_t> buckets(bucket_count, 0u);
        for (uint32_t candidate = 0; candidate < candidate_count; ++candidate) {
            const std::array<float, 4> values =
                uniforms(37u, 91u, 5u, candidate);
            if (!(values[component] >= 0.0f) || !(values[component] < 1.0f)) {
                return fail("candidate uniform left the unit interval");
            }
            ++buckets[static_cast<size_t>(
                values[component] * static_cast<float>(bucket_count))];
        }
        const double expected =
            static_cast<double>(candidate_count) / bucket_count;
        double chi_square = 0.0;
        for (const uint32_t count : buckets) {
            const double difference = static_cast<double>(count) - expected;
            chi_square += difference * difference / expected;
        }
        /* 15 degrees of freedom; the 0.999 critical value is about 37.7. */
        if (chi_square > 37.7) {
            return fail("candidate uniforms are not uniformly distributed");
        }
    }

    /*
     * Neighbouring pixels must not share a stream, otherwise whole regions would
     * pick the same emitter and the resampling would produce structured blotches
     * instead of noise.
     */
    double correlation_sum = 0.0;
    double left_sum = 0.0;
    double right_sum = 0.0;
    constexpr uint32_t pixel_span = 128u;
    uint32_t pair_count = 0u;
    for (uint32_t y = 0; y < pixel_span; ++y) {
        for (uint32_t x = 0; x + 1u < pixel_span; ++x) {
            const float left = uniforms(x, y, 3u, 0u)[0] - 0.5f;
            const float right = uniforms(x + 1u, y, 3u, 0u)[0] - 0.5f;
            correlation_sum += static_cast<double>(left) * right;
            left_sum += static_cast<double>(left) * left;
            right_sum += static_cast<double>(right) * right;
            ++pair_count;
        }
    }
    if (pair_count == 0u || left_sum <= 0.0 || right_sum <= 0.0) {
        return fail("neighbour correlation sweep collected no samples");
    }
    const double correlation =
        correlation_sum / std::sqrt(left_sum * right_sum);
    if (std::fabs(correlation) > 0.02) {
        return fail("horizontally adjacent pixels share a correlated stream");
    }

    /* The same pixel across consecutive frames must also decorrelate, or the
     * candidate set would freeze and the reservoir would stop learning. */
    correlation_sum = 0.0;
    left_sum = 0.0;
    right_sum = 0.0;
    for (uint32_t frame = 0; frame < 4096u; ++frame) {
        const float current = uniforms(19u, 23u, frame, 0u)[0] - 0.5f;
        const float next = uniforms(19u, 23u, frame + 1u, 0u)[0] - 0.5f;
        correlation_sum += static_cast<double>(current) * next;
        left_sum += static_cast<double>(current) * current;
        right_sum += static_cast<double>(next) * next;
    }
    if (left_sum <= 0.0 || right_sum <= 0.0) {
        return fail("frame correlation sweep collected no samples");
    }
    if (std::fabs(correlation_sum / std::sqrt(left_sum * right_sum)) > 0.05) {
        return fail("consecutive frames share a correlated stream");
    }
    return 0;
}
