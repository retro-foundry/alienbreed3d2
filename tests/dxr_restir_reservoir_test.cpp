/*
 * Reservoir resampling tests.
 *
 * The thing worth testing here is not that the code runs but that the estimator
 * it implements is unbiased. Incorrect resampling weights produce images that
 * look entirely reasonable while being persistently too bright or too dark, and
 * that is not something a screenshot will reveal. So these tests integrate
 * functions whose answers are known in closed form and check the mean of the
 * estimator against them, then pin the invariants that keep a reservoir buffer
 * from degenerating: proportional selection, confidence accounting, and the
 * refusal to admit a non-finite weight.
 */

#include "renderer_dxr/dxr_restir_reservoir.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using ab3d2::dxr::brdf::Vec3;
using namespace ab3d2::dxr::restir;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_close(double value, double expected, double tolerance,
                 const char *what)
{
    if (!(std::fabs(value - expected) <= tolerance)) {
        std::fprintf(stderr, "FAIL: %s (got %.6f, expected %.6f)\n", what,
                     value, expected);
        ++failures;
    }
}

/* A small deterministic generator, so a failure is always reproducible. */
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;

    float next()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint32_t bits = static_cast<uint32_t>(state >> 32);
        return static_cast<float>(bits) * (1.0f / 4294967296.0f);
    }
};

Vec3 grey(float value)
{
    return {value, value, value};
}

/* The integrand, with a known integral over [0,1]. */
double integrand(double x)
{
    return x * x;
}
constexpr double integrand_integral = 1.0 / 3.0;

/*
 * Resampled importance sampling has to integrate correctly when the candidates
 * arrive from a distribution that is not the target.
 *
 * Candidates are drawn from p(x) = 2x rather than uniformly, so the initial
 * contribution weight 1/p actually has to do something; the target function is
 * the integrand. If the resampling weight, the selection rule or the
 * normalization is wrong, the mean lands somewhere other than 1/3.
 */
void unbiased_with_nonuniform_source_test()
{
    Rng rng;
    const int trials = 200000;
    const int candidates = 8;
    double total = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
        PathReservoir reservoir = empty_reservoir();
        for (int i = 0; i < candidates; ++i) {
            /* Inverse CDF of p(x) = 2x. */
            const double u = static_cast<double>(rng.next());
            const double x = std::sqrt(u);
            const double pdf = 2.0 * x;
            const Vec3 target = grey(static_cast<float>(integrand(x)));

            PathReservoir candidate = make_reservoir(
                target, 0u, 0u, 2u, 3u, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, target, static_cast<float>(pdf));
            combine(reservoir, candidate, rng.next(), target);
        }
        /* Normalize by the selected target and the candidate count, which is
         * the uniform-MIS form of the contribution weight. */
        const double selected =
            static_cast<double>(ab3d2::dxr::brdf::luminance(
                reservoir.target_function));
        finalize_resampling(reservoir, 1.0f,
                            static_cast<float>(selected * reservoir.m));
        total += static_cast<double>(
            ab3d2::dxr::brdf::luminance(resolved_radiance(reservoir)));
    }

    const double mean = total / static_cast<double>(trials);
    check_close(mean, integrand_integral, 0.004,
                "RIS integrates a non-uniform source without bias");
}

/*
 * Combining reservoirs must not change the expected value either, which is the
 * property temporal and spatial reuse rest on entirely.
 *
 * Two independently streamed reservoirs are merged, and the merged estimator
 * has to integrate to the same answer as one of them alone.
 */
void reservoir_merge_is_unbiased_test()
{
    Rng rng;
    const int trials = 200000;
    double total = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
        PathReservoir left = empty_reservoir();
        PathReservoir right = empty_reservoir();

        for (int i = 0; i < 4; ++i) {
            const double x = static_cast<double>(rng.next());
            const Vec3 target = grey(static_cast<float>(integrand(x)));
            PathReservoir candidate = make_reservoir(
                target, 0u, 0u, 2u, 3u, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, target, 1.0f);
            combine(left, candidate, rng.next(), target);
        }
        for (int i = 0; i < 4; ++i) {
            const double x = static_cast<double>(rng.next());
            const Vec3 target = grey(static_cast<float>(integrand(x)));
            PathReservoir candidate = make_reservoir(
                target, 0u, 0u, 2u, 3u, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f},
                {0.0f, 0.0f, 1.0f}, target, 1.0f);
            combine(right, candidate, rng.next(), target);
        }

        /* Each side is normalized before merging, so both carry a contribution
         * weight rather than a raw running sum. */
        finalize_resampling(left, 1.0f,
                            ab3d2::dxr::brdf::luminance(left.target_function) *
                                left.m);
        finalize_resampling(right, 1.0f,
                            ab3d2::dxr::brdf::luminance(right.target_function) *
                                right.m);

        PathReservoir merged = empty_reservoir();
        combine(merged, left, rng.next(), left.target_function);
        combine(merged, right, rng.next(), right.target_function);
        finalize_resampling(merged, 1.0f,
                            ab3d2::dxr::brdf::luminance(
                                merged.target_function) * merged.m);

        total += static_cast<double>(
            ab3d2::dxr::brdf::luminance(resolved_radiance(merged)));
    }

    const double mean = total / static_cast<double>(trials);
    check_close(mean, integrand_integral, 0.004,
                "merging reservoirs preserves the expected value");
}

/*
 * A candidate must be selected in proportion to its resampling weight. This is
 * the one property that makes the reservoir a reservoir, and an off-by-one in
 * the comparison silently turns it into "keep the last sample".
 */
void proportional_selection_test()
{
    Rng rng;
    const int trials = 200000;
    int chose_heavy = 0;

    for (int trial = 0; trial < trials; ++trial) {
        PathReservoir reservoir = empty_reservoir();
        const Vec3 light = grey(1.0f);
        const Vec3 heavy = grey(3.0f);

        PathReservoir a = make_reservoir(light, 1u, 0u, 2u, 3u, 1.0f, 1.0f,
                                         {0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 1.0f}, light, 1.0f);
        PathReservoir b = make_reservoir(heavy, 2u, 0u, 2u, 3u, 1.0f, 1.0f,
                                         {0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 1.0f}, heavy, 1.0f);
        combine(reservoir, a, rng.next(), light);
        combine(reservoir, b, rng.next(), heavy);
        if (reservoir.random_seed == 2u) {
            ++chose_heavy;
        }
    }

    const double fraction = static_cast<double>(chose_heavy) /
        static_cast<double>(trials);
    check_close(fraction, 0.75, 0.01,
                "a candidate is selected in proportion to its weight");
}

/*
 * Confidence accounting. M is what a reservoir claims about how many candidates
 * it speaks for, and the cap is what stops one surviving path from claiming an
 * ever-growing share of a pixel.
 */
void confidence_test()
{
    PathReservoir reservoir = empty_reservoir();
    check(!reservoir_valid(reservoir), "an empty reservoir holds no sample");

    const Vec3 target = grey(1.0f);
    for (int i = 0; i < 10; ++i) {
        PathReservoir candidate = make_reservoir(
            target, 0u, 0u, 2u, 3u, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, target, 1.0f);
        combine(reservoir, candidate, 0.5f, target);
    }
    check(reservoir_valid(reservoir), "a streamed reservoir holds a sample");
    check_close(reservoir.m, 10.0, 1.0e-5, "M counts every candidate seen");

    cap_confidence(reservoir, 4.0f);
    check_close(reservoir.m, 4.0, 1.0e-5, "the confidence cap bounds M");
    cap_confidence(reservoir, 20.0f);
    check_close(reservoir.m, 4.0, 1.0e-5, "the cap never raises M");
}

/*
 * A non-finite weight must never enter a reservoir. One NaN in the running sum
 * makes every later comparison false, so the reservoir stops selecting anything
 * and the pixel is dead for as long as the buffer survives.
 */
void non_finite_rejection_test()
{
    const Vec3 good = grey(1.0f);
    PathReservoir reservoir = empty_reservoir();
    PathReservoir sane = make_reservoir(good, 7u, 0u, 2u, 3u, 1.0f, 1.0f,
                                        {0.0f, 0.0f, 0.0f},
                                        {0.0f, 0.0f, 1.0f}, good, 1.0f);
    combine(reservoir, sane, 0.5f, good);

    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    PathReservoir poison = sane;
    poison.random_seed = 99u;
    check(!resample(reservoir, poison, 0.5f, grey(nan), 1.0f, 1.0f),
          "a NaN target function is never selected");
    check(!resample(reservoir, poison, 0.5f, grey(infinity), 1.0f, 1.0f),
          "an infinite target function is never selected");
    check(!resample(reservoir, poison, 0.5f, good, nan, 1.0f),
          "a NaN normalization is never selected");
    check(reservoir.random_seed == 7u,
          "the sane sample survives every poisoned candidate");
    check(reservoir_finite(reservoir),
          "the reservoir stays finite after poisoned candidates");

    /* An impossible sample density yields no contribution rather than an
     * infinite one. */
    PathReservoir degenerate = make_reservoir(
        good, 0u, 0u, 2u, 3u, 1.0f, 1.0f, {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}, good, 0.0f);
    check(degenerate.weight_sum == 0.0f,
          "a zero sample density gives no contribution weight");

    /* Normalizing by nothing yields zero, not a division by zero. */
    PathReservoir empty = empty_reservoir();
    finalize_resampling(empty, 1.0f, 0.0f);
    check(empty.weight_sum == 0.0f, "an empty denominator yields zero weight");
    check(ab3d2::dxr::brdf::luminance(resolved_radiance(empty)) == 0.0f,
          "an invalid reservoir resolves to no radiance");
}

}  // namespace

int main()
{
    unbiased_with_nonuniform_source_test();
    reservoir_merge_is_unbiased_test();
    proportional_selection_test();
    confidence_test();
    non_finite_rejection_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d reservoir check(s) failed\n", failures);
        return 1;
    }
    std::printf("reservoir checks passed\n");
    return 0;
}
