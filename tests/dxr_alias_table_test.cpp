#include "renderer_dxr/dxr_alias_table.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using namespace ab3d2::dxr::alias_table;

int fail(const char *message)
{
    std::fprintf(stderr, "DXR alias table test failed: %s\n", message);
    return 1;
}

/*
 * Checks that the table realises the requested distribution exactly, that one
 * uniform sweep only ever returns reachable indices, and that the empirical
 * histogram agrees with the requested weights.
 */
bool matches_distribution(const std::vector<float> &weights,
                          const char *&reason)
{
    std::vector<Entry> entries;
    if (!build(weights, entries)) {
        reason = "build rejected a usable distribution";
        return false;
    }
    if (entries.size() != weights.size()) {
        reason = "build produced the wrong entry count";
        return false;
    }
    double total = 0.0;
    for (const float weight : weights) {
        total += static_cast<double>(weight);
    }
    double realised_total = 0.0;
    for (size_t index = 0; index < weights.size(); ++index) {
        const double expected = static_cast<double>(weights[index]) / total;
        const double realised =
            realised_probability(entries, static_cast<uint32_t>(index));
        realised_total += realised;
        if (std::fabs(realised - expected) > 1.0e-6) {
            reason = "realised probability does not match the weight";
            return false;
        }
        if (std::fabs(static_cast<double>(entries[index].probability) -
                      expected) > 1.0e-6) {
            reason = "reported source pdf does not match the weight";
            return false;
        }
        if (entries[index].alias >= weights.size()) {
            reason = "alias index is out of range";
            return false;
        }
        if (!(entries[index].threshold >= 0.0f) ||
            !(entries[index].threshold <= 1.0f)) {
            reason = "threshold left the unit interval";
            return false;
        }
    }
    if (std::fabs(realised_total - 1.0) > 1.0e-6) {
        reason = "realised probabilities do not sum to one";
        return false;
    }

    /* A deterministic uniform sweep, so the histogram is reproducible. */
    constexpr uint32_t sweep = 200000u;
    std::vector<uint32_t> hits(weights.size(), 0u);
    for (uint32_t step = 0; step < sweep; ++step) {
        const float uniform =
            (static_cast<float>(step) + 0.5f) / static_cast<float>(sweep);
        const uint32_t index = sample(entries, uniform);
        if (index >= weights.size()) {
            reason = "sample returned an out-of-range index";
            return false;
        }
        ++hits[index];
    }
    for (size_t index = 0; index < weights.size(); ++index) {
        const double expected = static_cast<double>(weights[index]) / total;
        const double observed = static_cast<double>(hits[index]) / sweep;
        if (std::fabs(observed - expected) > 2.0e-3) {
            reason = "sampled histogram does not match the weight";
            return false;
        }
        if (weights[index] == 0.0f && hits[index] != 0u) {
            reason = "a zero-weight index was selected";
            return false;
        }
    }
    return true;
}

}  // namespace

int main()
{
    const std::vector<std::vector<float>> distributions = {
        {1.0f},
        {1.0f, 1.0f},
        {3.0f, 1.0f},
        {0.5f, 0.25f, 0.125f, 0.125f},
        {1.0f, 0.0f, 2.0f, 0.0f, 7.0f},
        {1.0e-4f, 1.0e4f, 1.0f},
        {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f},
    };
    for (const std::vector<float> &weights : distributions) {
        const char *reason = "";
        if (!matches_distribution(weights, reason)) {
            return fail(reason);
        }
    }

    /* Emitter counts are content-driven, so a wide sweep of sizes has to hold. */
    for (size_t count = 1; count <= 64u; ++count) {
        std::vector<float> weights(count);
        for (size_t index = 0; index < count; ++index) {
            weights[index] = static_cast<float>((index * 37u) % 11u) + 0.25f;
        }
        const char *reason = "";
        if (!matches_distribution(weights, reason)) {
            return fail(reason);
        }
    }

    std::vector<Entry> entries;
    const std::vector<float> empty;
    const std::vector<float> zero = {0.0f, 0.0f};
    const std::vector<float> negative = {1.0f, -1.0f};
    const std::vector<float> infinite = {1.0f,
                                         std::numeric_limits<float>::infinity()};
    const std::vector<float> not_a_number = {
        1.0f, std::numeric_limits<float>::quiet_NaN()};
    if (build(empty, entries) || build(zero, entries) ||
        build(negative, entries) || build(infinite, entries) ||
        build(not_a_number, entries)) {
        return fail("build accepted an unusable distribution");
    }

    /* Sampling must stay in range at the closed ends of the uniform interval. */
    const std::vector<float> pair = {1.0f, 3.0f};
    if (!build(pair, entries)) {
        return fail("build rejected a two-emitter distribution");
    }
    if (sample(entries, 0.0f) >= pair.size() ||
        sample(entries, 1.0f) >= pair.size() ||
        sample(entries, 0.9999999f) >= pair.size()) {
        return fail("sample left the index range at an interval end");
    }
    return 0;
}
