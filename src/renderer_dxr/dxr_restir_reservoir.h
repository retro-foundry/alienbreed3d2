#ifndef AB3D2_DXR_RESTIR_RESERVOIR_H
#define AB3D2_DXR_RESTIR_RESERVOIR_H

#include "dxr_brdf_math.h"

#include <cmath>
#include <cstdint>

/*
 * One resampled light path, and the generalized RIS arithmetic that combines
 * them.
 *
 * A reservoir does not hold a path's accumulated radiance. It holds ONE path
 * selected from every candidate the reservoir has seen, together with the
 * statistics needed to say how much that one path is worth: the running
 * resampling weight sum, the effective number of candidates it stands for, and
 * enough state to rebuild the path from a different pixel's point of view.
 *
 * The layout and the weighting follow ReSTIR PT Enhanced (Lin, Kettunen and
 * Wyman 2026) and its RTXDI runtime, whose `RTXDI_PTReservoir`,
 * `RTXDI_InternalSimplePTResample`, `RTXDI_CombinePTReservoirs` and
 * `RTXDI_FinalizePTResampling` this mirrors. The equations come from
 * Generalized Resampled Importance Sampling (Lin et al., SIGGRAPH 2022) and are
 * reproduced rather than reinvented: a plausible-looking substitute for the
 * resampling weight stays plausible-looking while quietly biasing the image.
 *
 * Selection is scalar because a reservoir can only choose one path, so the
 * resampling weight uses the luminance of the target function while the full
 * RGB target function is carried alongside it. Final radiance is the product of
 * `weight_sum` and `target_function`.
 */

namespace ab3d2::dxr::restir {

using brdf::Vec3;

/*
 * The confidence cap. M is the number of candidates a reservoir speaks for, and
 * letting it grow without bound lets one path keep speaking for a pixel long
 * after the lighting that justified it has gone.
 */
constexpr float reservoir_maximum_m = 65535.0f;

struct PathReservoir {
    /* Reconnection vertex: where a shifted path rejoins this one. */
    Vec3 translated_world_position = {0.0f, 0.0f, 0.0f};
    Vec3 world_normal = {0.0f, 0.0f, 0.0f};
    /* Radiance arriving from the reconnection vertex along the stored path. */
    Vec3 radiance = {0.0f, 0.0f, 0.0f};
    /* RGB target function of the selected path at this pixel. */
    Vec3 target_function = {0.0f, 0.0f, 0.0f};

    /*
     * Overloaded exactly as the reference does: the running sum of resampling
     * weights while candidates stream in, then the unbiased contribution weight
     * once `finalize_resampling` has normalized it.
     */
    float weight_sum = 0.0f;
    /* Effective candidate count this reservoir represents. */
    float m = 0.0f;

    /* Reconnection data needed to reweight a shifted path. */
    float partial_jacobian = 0.0f;
    float rc_wi_pdf = 0.0f;

    /* Path shape. */
    uint32_t rc_vertex_length = 0u;
    uint32_t path_length = 0u;

    /*
     * Deterministic replay state. A shifted path is regenerated from a
     * different surface by replaying these exact stochastic dimensions, so the
     * seed and index are the path, not merely a record of it.
     */
    uint32_t random_seed = 0u;
    uint32_t random_index = 0u;

    /*
     * Frames the selected sample has survived, which doubles as the stagnancy
     * signal the duplication map reads.
     */
    uint32_t age = 0u;

    /*
     * Stable ancestry of the canonical sample this path descends from. Repeated
     * reuse can leave a neighbourhood full of descendants of one original path,
     * which are not the independent samples their combined M claims; the
     * duplication map counts shared ancestry to detect exactly that.
     */
    uint32_t ancestry = 0u;
};

inline PathReservoir empty_reservoir()
{
    return PathReservoir{};
}

/* A reservoir carries a usable sample only once something has streamed in. */
inline bool reservoir_valid(const PathReservoir &reservoir)
{
    return reservoir.m > 0.0f;
}

inline bool finite(float value)
{
    return std::isfinite(value);
}

/*
 * Builds the reservoir for one freshly traced path.
 *
 * `sample_pdf` is the density the path tracer actually drew the path with, so
 * the initial contribution weight is its reciprocal. Callers that have already
 * divided the radiance through by that density pass one.
 */
inline PathReservoir make_reservoir(Vec3 target_function, uint32_t random_seed,
                                    uint32_t random_index,
                                    uint32_t rc_vertex_length,
                                    uint32_t path_length,
                                    float partial_jacobian, float rc_wi_pdf,
                                    Vec3 translated_world_position,
                                    Vec3 world_normal, Vec3 radiance,
                                    float sample_pdf)
{
    PathReservoir reservoir = {};
    reservoir.translated_world_position = translated_world_position;
    reservoir.world_normal = world_normal;
    reservoir.radiance = radiance;
    reservoir.target_function = target_function;
    reservoir.weight_sum = (sample_pdf > 0.0f && finite(sample_pdf)) ?
        1.0f / sample_pdf : 0.0f;
    reservoir.m = 1.0f;
    reservoir.partial_jacobian = partial_jacobian;
    reservoir.rc_wi_pdf = rc_wi_pdf;
    reservoir.rc_vertex_length = rc_vertex_length;
    reservoir.path_length = path_length;
    reservoir.random_seed = random_seed;
    reservoir.random_index = random_index;
    reservoir.age = 0u;
    return reservoir;
}

/*
 * The general resampling step: offer `candidate` to `target`, with the caller
 * supplying the target function and normalization rather than deriving them.
 *
 * Temporal and spatial reuse both need to state those separately, because a
 * shifted path's target function is evaluated in the receiving pixel's domain
 * while its normalization carries the source reservoir's weight, its candidate
 * count and the shift's Jacobian.
 *
 * A non-finite resampling weight is discarded rather than propagated. Letting
 * one through poisons `weight_sum` permanently, and a reservoir buffer that has
 * taken a NaN never recovers on its own.
 */
inline bool resample(PathReservoir &target, const PathReservoir &candidate,
                     float random, Vec3 candidate_target_function,
                     float sample_normalization, float sample_m)
{
    float ris_weight = brdf::luminance(candidate_target_function) *
        sample_normalization;
    if (!finite(ris_weight) || ris_weight < 0.0f) {
        ris_weight = 0.0f;
    }
    if (!finite(sample_m) || sample_m < 0.0f) {
        return false;
    }

    target.m += sample_m;
    target.weight_sum += ris_weight;

    const bool select = random * target.weight_sum < ris_weight;
    if (select) {
        target.translated_world_position = candidate.translated_world_position;
        target.world_normal = candidate.world_normal;
        target.radiance = candidate.radiance;
        target.target_function = candidate_target_function;
        target.partial_jacobian = candidate.partial_jacobian;
        target.rc_wi_pdf = candidate.rc_wi_pdf;
        target.rc_vertex_length = candidate.rc_vertex_length;
        target.path_length = candidate.path_length;
        target.random_seed = candidate.random_seed;
        target.random_index = candidate.random_index;
        target.age = candidate.age;
        target.ancestry = candidate.ancestry;
    }
    return select;
}

/*
 * Merges a whole reservoir in, the streaming form from the original ReSTIR
 * paper. Its normalization is the candidate's own contribution weight times the
 * candidate count it speaks for; normalization of the result is deferred to
 * `finalize_resampling` once every candidate has been seen.
 */
inline bool combine(PathReservoir &target, const PathReservoir &candidate,
                    float random, Vec3 candidate_target_function)
{
    return resample(target, candidate, random, candidate_target_function,
                    candidate.weight_sum * candidate.m, candidate.m);
}

/*
 * Turns the accumulated weight sum into the unbiased contribution weight.
 *
 * The caller supplies the MIS numerator and denominator, because what belongs
 * there differs between a plain candidate stream and a resampling step whose
 * shifts need their own weights.
 */
inline void finalize_resampling(PathReservoir &reservoir, float numerator,
                                float denominator)
{
    const float weight = (denominator > 0.0f) ?
        numerator * reservoir.weight_sum / denominator : 0.0f;
    reservoir.weight_sum = finite(weight) ? weight : 0.0f;
}

/* Clamps the represented candidate count, which is how temporal reuse bounds
 * the influence any one surviving path may accumulate. */
inline void cap_confidence(PathReservoir &reservoir, float maximum_m)
{
    const float limit = std::fmin(maximum_m, reservoir_maximum_m);
    if (reservoir.m > limit) {
        reservoir.m = limit;
    }
}

/* The radiance this reservoir contributes once resampling has finished. */
inline Vec3 resolved_radiance(const PathReservoir &reservoir)
{
    if (!reservoir_valid(reservoir) || !finite(reservoir.weight_sum)) {
        return {0.0f, 0.0f, 0.0f};
    }
    return reservoir.target_function * reservoir.weight_sum;
}

/*
 * Whether a reservoir holds any non-finite state. Resampling refuses to admit
 * one, and a buffer is checked against this rather than trusted.
 */
inline bool reservoir_finite(const PathReservoir &reservoir)
{
    return finite(reservoir.weight_sum) && finite(reservoir.m) &&
        finite(reservoir.target_function.x) &&
        finite(reservoir.target_function.y) &&
        finite(reservoir.target_function.z) && finite(reservoir.radiance.x) &&
        finite(reservoir.radiance.y) && finite(reservoir.radiance.z) &&
        finite(reservoir.partial_jacobian) && finite(reservoir.rc_wi_pdf);
}

}  // namespace ab3d2::dxr::restir

#endif
