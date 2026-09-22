#ifndef AB3D2_DXR_RESTIR_RESAMPLING_H
#define AB3D2_DXR_RESTIR_RESAMPLING_H

#include "dxr_brdf_math.h"
#include "dxr_restir_reservoir.h"

#include <cmath>

/*
 * The two corrections that make reuse free of charge: the shift Jacobian, and
 * the multiple-importance normalization across the domains a sample could have
 * come from.
 *
 * Reuse moves a path between pixels, and a pixel is its own integration domain.
 * Two things therefore have to be paid for. First, the shift changes the
 * density of the sample, by the ratio of the solid angles the reconnection
 * subtends from the two receivers -- that is the Jacobian. Second, the same
 * path could have been proposed by several reservoirs, and counting it once per
 * proposer without weighting would count it several times over.
 *
 * Both are reproduced from Generalized Resampled Importance Sampling (Lin et
 * al., SIGGRAPH 2022) and its RTXDI implementation. Getting either wrong does
 * not produce an obviously broken image: it produces one that is quietly and
 * consistently the wrong brightness, which is the single most important thing
 * for these tests to be able to detect.
 */

namespace ab3d2::dxr::restir {

using brdf::Vec3;

/*
 * The Jacobian of the shift that moves a reconnection from one receiver to
 * another, equation (11) of the ReSTIR GI paper.
 *
 * It is the ratio of the solid angles the reconnection vertex subtends from the
 * two receivers: what was a likely direction from where the path was found may
 * be an unlikely one from where it is being reused, and the weight has to say
 * so. A degenerate configuration returns zero, which rejects the shift rather
 * than scaling it by an infinity.
 */
inline float shift_jacobian(Vec3 receiver_position,
                            Vec3 neighbor_receiver_position,
                            Vec3 reconnection_position,
                            Vec3 reconnection_normal)
{
    const Vec3 to_receiver = receiver_position - reconnection_position;
    const Vec3 to_neighbor = neighbor_receiver_position - reconnection_position;

    const float new_distance_squared = brdf::dot(to_receiver, to_receiver);
    const float original_distance_squared =
        brdf::dot(to_neighbor, to_neighbor);
    if (!(new_distance_squared > 0.0f) || !(original_distance_squared > 0.0f)) {
        return 0.0f;
    }

    const float new_cosine = std::fmin(
        std::fmax(brdf::dot(reconnection_normal,
                            to_receiver * (1.0f /
                                           std::sqrt(new_distance_squared))),
                  0.0f), 1.0f);
    const float original_cosine = std::fmin(
        std::fmax(brdf::dot(reconnection_normal,
                            to_neighbor *
                                (1.0f /
                                 std::sqrt(original_distance_squared))),
                  0.0f), 1.0f);

    const float jacobian = (new_cosine * original_distance_squared) /
        (original_cosine * new_distance_squared);
    return std::isfinite(jacobian) ? jacobian : 0.0f;
}

/*
 * The generalized balance heuristic denominator.
 *
 * Every domain that could have proposed the selected sample contributes the
 * target function of THAT SAMPLE evaluated in ITS domain, weighted by how many
 * candidates that domain speaks for. Evaluating the selected sample in the
 * neighbour's domain is what requires the inverse shift, and it is the step
 * that is easiest to skip and hardest to notice having skipped: without it the
 * estimator still produces an image, just not the right one.
 */
struct MisNormalization {
    float pi_sum = 0.0f;
};

/*
 * Opens the normalization with the receiving pixel's own contribution. The
 * canonical sample's domain is always one of the proposers.
 */
inline MisNormalization begin_normalization(Vec3 selected_target_function,
                                            float canonical_m)
{
    MisNormalization normalization = {};
    normalization.pi_sum =
        brdf::luminance(selected_target_function) * canonical_m;
    return normalization;
}

/*
 * Adds one neighbouring domain.
 *
 * `target_function_in_domain` is the SELECTED sample's target function
 * evaluated in that neighbour's domain, not the neighbour's own target
 * function. A shift that fails there contributes nothing, which is correct: a
 * domain that could not have produced the sample is not a proposer of it.
 */
inline void add_domain(MisNormalization &normalization,
                       Vec3 target_function_in_domain, float domain_m)
{
    const float contribution =
        brdf::luminance(target_function_in_domain) * domain_m;
    if (std::isfinite(contribution) && contribution > 0.0f) {
        normalization.pi_sum += contribution;
    }
}

/*
 * Normalizes the reservoir, turning the accumulated resampling weight into the
 * unbiased contribution weight.
 *
 * Expressed with the reference's numerator and denominator rather than the
 * algebraically simpler `weight_sum / pi_sum`, so that the correspondence with
 * the published formulation stays visible.
 */
inline void finalize_normalization(PathReservoir &reservoir,
                                   const MisNormalization &normalization)
{
    const float selected = brdf::luminance(reservoir.target_function);
    finalize_resampling(reservoir, selected, normalization.pi_sum * selected);
}

}  // namespace ab3d2::dxr::restir

#endif
