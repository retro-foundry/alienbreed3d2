#ifndef AB3D2_DXR_ALIAS_TABLE_H
#define AB3D2_DXR_ALIAS_TABLE_H

#include <cstdint>
#include <span>
#include <vector>

namespace ab3d2::dxr::alias_table {

/*
 * Walker's alias method, built with Vose's stable partition pass, so an emitter
 * can be selected from an arbitrary discrete distribution with one uniform and
 * one table lookup.
 *
 * The renderer needs this because reservoir resampling draws tens of light
 * candidates per pixel per frame. The linear cumulative-distribution walk it
 * replaces costs O(emitter count) per candidate, which is unaffordable at that
 * candidate count, and the selection it produced was also quantised to the 256
 * distinct values the blue-noise tables return.
 */
struct Entry {
    /* Normalised probability of selecting this index. Retained as the source pdf
     * for multiple-importance sampling and for the resampling target function. */
    float probability;
    /* Probability of keeping this bucket's own index rather than its alias. */
    float threshold;
    /* Index selected when the bucket is not kept. */
    uint32_t alias;
};

/*
 * Builds the table for `weights`, which need not be normalised. Returns false
 * without touching `entries` when the distribution is unusable: no weights, a
 * negative or non-finite weight, or a total that is not positive. Zero weights
 * are allowed and become unreachable indices.
 */
inline bool build(std::span<const float> weights, std::vector<Entry> &entries)
{
    const size_t count = weights.size();
    if (count == 0u) {
        return false;
    }
    double total = 0.0;
    for (const float weight : weights) {
        if (!(weight >= 0.0f) || !(weight <= 3.4028235e38f)) {
            return false;
        }
        total += static_cast<double>(weight);
    }
    if (!(total > 0.0)) {
        return false;
    }

    /* Not named `small`/`large`: <windows.h> defines `small` as a macro. */
    std::vector<double> scaled(count);
    std::vector<uint32_t> under_full;
    std::vector<uint32_t> over_full;
    under_full.reserve(count);
    over_full.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        scaled[index] = static_cast<double>(weights[index]) *
            static_cast<double>(count) / total;
        if (scaled[index] < 1.0) {
            under_full.push_back(static_cast<uint32_t>(index));
        } else {
            over_full.push_back(static_cast<uint32_t>(index));
        }
    }

    entries.assign(count, Entry{});
    for (size_t index = 0; index < count; ++index) {
        entries[index].probability = static_cast<float>(
            static_cast<double>(weights[index]) / total);
        entries[index].threshold = 1.0f;
        entries[index].alias = static_cast<uint32_t>(index);
    }
    while (!under_full.empty() && !over_full.empty()) {
        const uint32_t light = under_full.back();
        under_full.pop_back();
        const uint32_t heavy = over_full.back();
        over_full.pop_back();
        entries[light].threshold = static_cast<float>(scaled[light]);
        entries[light].alias = heavy;
        /* Vose's stable update: adding the deficit before subtracting one keeps
         * the residual from drifting when many buckets are nearly full. */
        scaled[heavy] = (scaled[heavy] + scaled[light]) - 1.0;
        if (scaled[heavy] < 1.0) {
            under_full.push_back(heavy);
        } else {
            over_full.push_back(heavy);
        }
    }
    /* Buckets still queued are full to within rounding, so they keep the default
     * threshold of one and alias to themselves. */
    return true;
}

/*
 * Selects an index from one uniform in [0, 1). The bucket comes from the scaled
 * uniform's integer part and the keep/alias decision from its fraction, matching
 * the single-uniform form the ray shader uses.
 */
inline uint32_t sample(std::span<const Entry> entries, float uniform)
{
    const uint32_t count = static_cast<uint32_t>(entries.size());
    if (count == 0u) {
        return 0u;
    }
    const float scaled = uniform * static_cast<float>(count);
    uint32_t bucket = static_cast<uint32_t>(scaled);
    if (bucket >= count) {
        bucket = count - 1u;
    }
    const float fractional = scaled - static_cast<float>(bucket);
    return fractional < entries[bucket].threshold ? bucket :
        entries[bucket].alias;
}

/*
 * Exact selection probability the table realises for `index`, summed over the
 * bucket it owns and every bucket that aliases to it. Used by the CPU test to
 * check the construction against the requested distribution without sampling.
 */
inline double realised_probability(std::span<const Entry> entries,
                                   uint32_t index)
{
    const size_t count = entries.size();
    if (count == 0u || index >= count) {
        return 0.0;
    }
    const double bucket_mass = 1.0 / static_cast<double>(count);
    double probability =
        bucket_mass * static_cast<double>(entries[index].threshold);
    for (size_t bucket = 0; bucket < count; ++bucket) {
        if (bucket == index || entries[bucket].alias != index) {
            continue;
        }
        probability += bucket_mass *
            (1.0 - static_cast<double>(entries[bucket].threshold));
    }
    return probability;
}

}  // namespace ab3d2::dxr::alias_table

#endif
