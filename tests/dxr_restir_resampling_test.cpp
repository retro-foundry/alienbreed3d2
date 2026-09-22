/*
 * Reuse-correctness tests.
 *
 * The property under test is the one the whole algorithm is judged on: reusing
 * paths between pixels and across frames must not change how bright the result
 * is. A reuse scheme that is subtly wrong still converges, still looks smooth,
 * and still responds to the scene; it just settles on the wrong answer, and no
 * amount of looking at it will say so.
 *
 * So the tests integrate known functions through simulated reuse and compare
 * the mean against the closed form. The interesting cases are the ones where
 * naive implementations go wrong: neighbours with unequal confidence, and a
 * neighbour whose domain disagrees about what the sample is worth.
 */

#include "renderer_dxr/dxr_restir_resampling.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

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

struct Rng {
    uint64_t state = 0x2545f4914f6cdd1dULL;

    float next()
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>(static_cast<uint32_t>(state >> 32)) *
            (1.0f / 4294967296.0f);
    }
};

Vec3 grey(float value)
{
    return {value, value, value};
}

double integrand(double x)
{
    return 0.5 + x;
}
constexpr double integrand_integral = 1.0;  /* over [0,1] */

/* One pixel's canonical sample: a single uniform draw. */
PathReservoir canonical_sample(Rng &rng, double &drawn)
{
    const double x = static_cast<double>(rng.next());
    drawn = x;
    const Vec3 target = grey(static_cast<float>(integrand(x)));
    return make_reservoir(target, 0u, 0u, 2u, 3u, 1.0f, 1.0f,
                          {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, target,
                          1.0f);
}

/*
 * Reuse between two pixels that share an integration domain.
 *
 * With a shared domain the selected sample is worth the same in both, so the
 * balance heuristic reduces to weighting by confidence. If the normalization
 * were skipped or applied once instead of per domain, the mean would come out
 * scaled rather than merely noisier -- which is exactly the failure that looks
 * fine on screen.
 */
void reuse_preserves_brightness_test()
{
    Rng rng;
    const int trials = 300000;
    double total = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
        double unused = 0.0;
        PathReservoir mine = canonical_sample(rng, unused);
        PathReservoir theirs = canonical_sample(rng, unused);

        /* Each pixel normalizes its own canonical sample first. */
        finalize_normalization(mine, begin_normalization(mine.target_function,
                                                         mine.m));
        finalize_normalization(theirs,
                               begin_normalization(theirs.target_function,
                                                    theirs.m));

        PathReservoir combined = empty_reservoir();
        combine(combined, mine, rng.next(), mine.target_function);
        combine(combined, theirs, rng.next(), theirs.target_function);

        /* Both domains could have proposed whichever sample won, and in a
         * shared domain the selected sample is worth the same in each. */
        MisNormalization normalization =
            begin_normalization(combined.target_function, mine.m);
        add_domain(normalization, combined.target_function, theirs.m);
        finalize_normalization(combined, normalization);

        total += static_cast<double>(
            ab3d2::dxr::brdf::luminance(resolved_radiance(combined)));
    }

    check_close(total / static_cast<double>(trials), integrand_integral, 0.006,
                "reuse between two pixels preserves brightness");
}

/*
 * The same, with a neighbour carrying accumulated temporal confidence.
 *
 * Unequal M is where an estimator that ignores confidence in the normalization
 * goes visibly wrong, and it is the normal state of affairs after a few frames
 * of temporal reuse.
 */
void unequal_confidence_test()
{
    Rng rng;
    const int trials = 300000;
    const float history = 16.0f;
    double total = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
        double unused = 0.0;
        PathReservoir mine = canonical_sample(rng, unused);
        finalize_normalization(mine, begin_normalization(mine.target_function,
                                                         mine.m));

        /* A temporal neighbour: a properly normalized sample that speaks for
         * many previous frames. */
        PathReservoir theirs = canonical_sample(rng, unused);
        finalize_normalization(theirs,
                               begin_normalization(theirs.target_function,
                                                    theirs.m));
        theirs.m = history;

        PathReservoir combined = empty_reservoir();
        combine(combined, mine, rng.next(), mine.target_function);
        combine(combined, theirs, rng.next(), theirs.target_function);

        MisNormalization normalization =
            begin_normalization(combined.target_function, mine.m);
        add_domain(normalization, combined.target_function, theirs.m);
        finalize_normalization(combined, normalization);

        total += static_cast<double>(
            ab3d2::dxr::brdf::luminance(resolved_radiance(combined)));
    }

    check_close(total / static_cast<double>(trials), integrand_integral, 0.006,
                "reuse with unequal confidence preserves brightness");
}

/*
 * The Jacobian. Its job is to say that a reconnection likely from where the
 * path was found may be unlikely from where it is reused.
 */
void jacobian_test()
{
    const Vec3 reconnection = {0.0f, 0.0f, 0.0f};
    const Vec3 normal = {0.0f, 0.0f, 1.0f};
    const Vec3 receiver = {0.0f, 0.0f, 4.0f};

    check_close(shift_jacobian(receiver, receiver, reconnection, normal), 1.0,
                1.0e-5, "an identity shift has unit Jacobian");

    /* Twice as far away subtends a quarter of the solid angle. */
    const Vec3 distant = {0.0f, 0.0f, 8.0f};
    check_close(shift_jacobian(distant, receiver, reconnection, normal), 0.25,
                1.0e-4, "doubling the distance quarters the Jacobian");

    /* The shift and its reverse are reciprocal, which is what lets a paired
     * reuse compute one from the other. */
    const float forward = shift_jacobian(distant, receiver, reconnection,
                                         normal);
    const float reverse = shift_jacobian(receiver, distant, reconnection,
                                         normal);
    check_close(forward * reverse, 1.0, 1.0e-4,
                "a shift and its reverse are reciprocal");

    /* A grazing receiver sees less of the reconnection. */
    const Vec3 grazing = {8.0f, 0.0f, 0.5f};
    check(shift_jacobian(grazing, receiver, reconnection, normal) <
              shift_jacobian(receiver, receiver, reconnection, normal),
          "a grazing receiver has a smaller Jacobian");

    /* Degenerate configurations reject rather than scale by an infinity. */
    check(shift_jacobian(reconnection, receiver, reconnection, normal) == 0.0f,
          "a coincident receiver has no Jacobian");
    const Vec3 behind = {0.0f, 0.0f, -4.0f};
    check(shift_jacobian(behind, receiver, reconnection, normal) == 0.0f,
          "a receiver behind the reconnection has no Jacobian");
    check(std::isfinite(shift_jacobian(receiver, behind, reconnection,
                                       normal)),
          "a Jacobian from behind stays finite");
}

/*
 * Normalization bookkeeping. A domain that could not have produced the sample
 * must not be counted as a proposer of it, or the weight is divided among more
 * domains than really competed and the result comes out dark.
 */
void normalization_test()
{
    const Vec3 target = grey(2.0f);

    MisNormalization alone = begin_normalization(target, 1.0f);
    check_close(alone.pi_sum, 2.0, 1.0e-5,
                "one domain contributes its target times its confidence");

    MisNormalization pair = begin_normalization(target, 1.0f);
    add_domain(pair, target, 3.0f);
    check_close(pair.pi_sum, 8.0, 1.0e-5,
                "a second domain adds its own confidence");

    /* A failed shift evaluates to zero there and adds nothing. */
    MisNormalization failed = begin_normalization(target, 1.0f);
    add_domain(failed, grey(0.0f), 5.0f);
    check_close(failed.pi_sum, 2.0, 1.0e-5,
                "a domain that could not propose the sample adds nothing");

    MisNormalization poisoned = begin_normalization(target, 1.0f);
    add_domain(poisoned, grey(std::numeric_limits<float>::quiet_NaN()), 5.0f);
    check_close(poisoned.pi_sum, 2.0, 1.0e-5,
                "a non-finite domain adds nothing");

    /* Normalizing an empty reservoir must yield no radiance, not a division by
     * zero that propagates into the image. */
    PathReservoir empty = empty_reservoir();
    finalize_normalization(empty, begin_normalization(grey(0.0f), 0.0f));
    check(empty.weight_sum == 0.0f, "an empty normalization yields no weight");
    check(reservoir_finite(empty), "an empty reservoir stays finite");
}

}  // namespace

int main()
{
    reuse_preserves_brightness_test();
    unequal_confidence_test();
    jacobian_test();
    normalization_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d resampling check(s) failed\n", failures);
        return 1;
    }
    std::printf("resampling checks passed\n");
    return 0;
}
