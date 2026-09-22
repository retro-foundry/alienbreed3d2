/*
 * Deterministic replay tests.
 *
 * The property being defended is narrow and easy to lose: a path's random
 * numbers must depend on which decision is being made, never on how many
 * decisions preceded it. Every one of these tests is a way that property gets
 * broken in practice, and the one that matters most simulates two paths that
 * diverge in the middle and checks that the later bounces still line up.
 */

#include "renderer_dxr/dxr_path_dimensions.h"

#include <cstdio>
#include <set>
#include <vector>

using namespace ab3d2::dxr::path_dimensions;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/*
 * A path tracer that consumes dimensions by address rather than in sequence.
 * `sample_nee` decides whether this walk performs next event estimation, which
 * is exactly the branch a replayed path is likely to take differently.
 */
std::vector<float> walk(PathRng rng, uint32_t bounces, bool sample_nee,
                        bool roll_roulette)
{
    std::vector<float> bsdf_samples;
    for (uint32_t bounce = 0u; bounce < bounces; ++bounce) {
        (void)uniform(rng, bounce, bsdf_component);
        const std::array<float, 2> direction = bsdf_direction(rng, bounce);
        bsdf_samples.push_back(direction[0]);
        bsdf_samples.push_back(direction[1]);

        if (sample_nee) {
            (void)uniform(rng, bounce, nee_selector);
            (void)nee_position(rng, bounce);
        }
        if (roll_roulette) {
            (void)uniform(rng, bounce, russian_roulette);
        }
    }
    return bsdf_samples;
}

/*
 * The core replay guarantee. Two walks over the same path that take entirely
 * different branches must still draw identical BSDF directions at every bounce.
 * Under a sequentially consumed stream these diverge at the first skipped draw.
 */
void divergent_branches_do_not_shift_dimensions_test()
{
    const PathRng rng = {0x9e3779b9u, 12u};

    const std::vector<float> full = walk(rng, 6u, true, true);
    const std::vector<float> no_nee = walk(rng, 6u, false, true);
    const std::vector<float> no_roulette = walk(rng, 6u, true, false);
    const std::vector<float> neither = walk(rng, 6u, false, false);

    check(full == no_nee,
          "skipping next event estimation does not move later dimensions");
    check(full == no_roulette,
          "skipping Russian roulette does not move later dimensions");
    check(full == neither,
          "skipping both does not move later dimensions");

    /* A shorter walk must be a prefix of a longer one, so a path that
     * terminates early still agrees with one that continues. */
    const std::vector<float> shorter = walk(rng, 3u, true, true);
    check(std::vector<float>(full.begin(), full.begin() +
                             static_cast<long>(shorter.size())) == shorter,
          "an early-terminating path agrees with a longer one");
}

/* Replay has to be reproducible across calls, or nothing above means anything. */
void replay_is_reproducible_test()
{
    const PathRng rng = {4242u, 7u};
    for (uint32_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
        const float first = uniform(rng, bounce, bsdf_component);
        const float second = uniform(rng, bounce, bsdf_component);
        check(first == second, "the same decision replays to the same number");
    }

    /* A different path must not replay the same numbers, or reuse would be
     * drawing one sample and calling it many. */
    const PathRng other = {4243u, 7u};
    bool any_difference = false;
    for (uint32_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
        if (uniform(rng, bounce, bsdf_u) != uniform(other, bounce, bsdf_u)) {
            any_difference = true;
        }
    }
    check(any_difference, "a different seed replays different numbers");

    const PathRng later = {4242u, 8u};
    any_difference = false;
    for (uint32_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
        if (uniform(rng, bounce, bsdf_u) != uniform(later, bounce, bsdf_u)) {
            any_difference = true;
        }
    }
    check(any_difference, "a later frame replays different numbers");
}

/* No two decisions may share an address, at any bounce. */
void addresses_are_injective_test()
{
    const Dimension all[] = {bsdf_component, bsdf_u,     bsdf_v,
                             nee_selector,   nee_u,      nee_v,
                             russian_roulette, reserved};
    std::set<uint32_t> seen;
    for (uint32_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
        for (const Dimension dimension : all) {
            const uint32_t slot = address(bounce, dimension);
            check(seen.insert(slot).second,
                  "each decision has its own stream address");
        }
    }
    check(seen.size() == maximum_bounces * per_bounce,
          "the addressable dimensions are exactly the reserved ones");
}

/*
 * Values have to be usable as uniforms. A draw of exactly one breaks an index
 * computed as floor(u * count), which is how a light selector reaches one past
 * the end of a table.
 */
void uniforms_are_in_range_test()
{
    bool in_range = true;
    bool saw_low = false;
    bool saw_high = false;
    for (uint32_t seed = 0u; seed < 512u; ++seed) {
        const PathRng rng = {seed * 2654435761u, seed};
        for (uint32_t bounce = 0u; bounce < maximum_bounces; ++bounce) {
            const std::array<float, 2> pair = bsdf_direction(rng, bounce);
            for (const float value : pair) {
                if (!(value >= 0.0f) || !(value < 1.0f)) {
                    in_range = false;
                }
                saw_low = saw_low || value < 0.25f;
                saw_high = saw_high || value > 0.75f;
            }
        }
    }
    check(in_range, "every replayed uniform lies in [0, 1)");
    check(saw_low && saw_high, "replayed uniforms cover the unit interval");
}

/*
 * The two numbers of a paired decision must differ from one another. Drawing
 * both from the same hash word would pin every sampled direction to a diagonal.
 */
void paired_dimensions_are_independent_test()
{
    int identical = 0;
    for (uint32_t seed = 0u; seed < 512u; ++seed) {
        const PathRng rng = {seed * 40503u + 7u, 3u};
        const std::array<float, 2> pair = bsdf_direction(rng, 0u);
        if (pair[0] == pair[1]) {
            ++identical;
        }
    }
    check(identical == 0, "a paired decision draws two distinct numbers");
}

}  // namespace

int main()
{
    divergent_branches_do_not_shift_dimensions_test();
    replay_is_reproducible_test();
    addresses_are_injective_test();
    uniforms_are_in_range_test();
    paired_dimensions_are_independent_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d path dimension check(s) failed\n", failures);
        return 1;
    }
    std::printf("path dimension checks passed\n");
    return 0;
}
