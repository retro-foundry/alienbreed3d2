#ifndef AB3D2_DXR_RESTIR_RECONNECTION_H
#define AB3D2_DXR_RESTIR_RECONNECTION_H

#include "dxr_brdf_math.h"

#include <cmath>
#include <cstdint>

/*
 * When a shifted path may stop being replayed and reconnect to the stored
 * suffix.
 *
 * The hybrid shift replays a path's glossy prefix from the new surface and then
 * jumps straight to the original path's remainder. Reconnecting is much cheaper
 * than replaying, but it is only sound where the displacement between the two
 * paths cannot much change what the remainder contributes. Deciding that on
 * roughness alone gets glossy transport wrong in both directions: a merely
 * "rough enough" vertex at a grazing angle or a great distance still carries a
 * narrow effective lobe, and a moderately glossy one seen close up does not.
 *
 * ReSTIR PT Enhanced (Lin, Kettunen and Wyman 2026) measures the scattered
 * ray's FOOTPRINT instead -- how much area the sampled density spreads over,
 * which grows with distance and with rougher scattering, and is zero for a
 * delta lobe. Because the footprint is what changes when a path is reconnected,
 * bounding it bounds the resulting deviation in contribution directly.
 *
 * This reproduces the criterion and its equations from that paper's RTXDI
 * implementation rather than substituting a roughness cutoff for them; the
 * fixed-threshold mode is retained only so the two can be compared.
 *
 * Three conditions must hold together, and they concern a PAIR of consecutive
 * vertices, because a reconnection replaces the segment between them:
 *
 *   1. the receiving vertex is spread out enough to accept the connection,
 *      measured by the inverse footprint of the incoming segment;
 *   2. the preceding vertex scattered broadly enough, measured by its sampling
 *      density against a pdf threshold, and was not a delta bounce;
 *   3. the preceding segment was long enough that the reconnection does not
 *      pivot around a nearly coincident pair.
 */

namespace ab3d2::dxr::restir {

using brdf::Vec3;

constexpr float pi = 3.14159265358979323846f;

enum class ReconnectionMode {
    /* Footprint criterion of ReSTIR PT Enhanced. */
    footprint,
    /* Roughness and distance cutoffs, for comparison against the above. */
    fixed_threshold,
};

/* One scattering event, as the criterion needs to see it. */
struct ScatterEvent {
    /* Solid-angle density the direction was sampled with. */
    float pdf = 0.0f;
    /* Distance the scattered ray travelled to the next vertex. */
    float ray_distance = 0.0f;
    /* Surface the ray left, and the direction it was viewed from. */
    Vec3 normal = {0.0f, 0.0f, 1.0f};
    Vec3 view_direction = {0.0f, 0.0f, 1.0f};
    /* Linear roughness, used only by the fixed-threshold mode. */
    float roughness = 1.0f;
    /* A perfect mirror or refraction, which has no footprint at all. */
    bool is_delta = false;
};

struct ReconnectionThresholds {
    /* Scale on the primary ray's footprint. Larger reconnects sooner. */
    float minimum_connection_footprint = 1.0f;
    /* Acts as an adaptive roughness floor via pdf <= 1 / roughness^2. */
    float minimum_pdf_roughness = 0.4f;
    /* Fixed-threshold mode only. */
    float roughness_threshold = 0.2f;
    float distance_threshold = 0.0f;
};

/* The thresholds the criterion actually compares against, derived per pixel. */
struct FootprintLimits {
    float footprint = 0.0f;
    float pdf = 0.0f;
};

/*
 * The primary ray's footprint, which every other footprint is measured relative
 * to. Expressing the threshold as a ratio is what keeps the criterion
 * resolution- and distance-independent instead of tied to world units.
 */
inline float primary_ray_footprint(Vec3 primary_hit_position,
                                   Vec3 camera_position, Vec3 surface_normal,
                                   Vec3 view_direction)
{
    const Vec3 displacement = primary_hit_position - camera_position;
    const float cosine = std::fabs(brdf::dot(surface_normal, view_direction));
    if (!(cosine > 0.0f)) {
        return 0.0f;
    }
    return brdf::dot(displacement, displacement) * 4.0f * pi / cosine;
}

inline FootprintLimits footprint_limits(float primary_footprint,
                                        const ReconnectionThresholds &thresholds)
{
    FootprintLimits limits = {};
    const float connection = thresholds.minimum_connection_footprint;
    limits.footprint = primary_footprint * connection * connection;
    const float roughness = thresholds.minimum_pdf_roughness;
    limits.pdf = (roughness > 0.0f) ? 1.0f / (roughness * roughness) : 0.0f;
    return limits;
}

/*
 * The footprint of one scattered ray: the area its sampling density spreads
 * over by the time it reaches the next vertex.
 *
 * A delta bounce has none. That is not a special case bolted on, it is the
 * reason the criterion refuses to reconnect through a mirror: a footprint of
 * zero can never exceed a positive threshold.
 */
inline float ray_footprint(Vec3 surface_normal, Vec3 view_direction,
                           float ray_distance, float pdf, bool is_delta)
{
    if (is_delta) {
        return 0.0f;
    }
    if (!(pdf > 0.0f) || !(ray_distance > 0.0f)) {
        return 0.0f;
    }
    const float cosine = std::fabs(brdf::dot(surface_normal, view_direction));
    if (!(cosine > 0.0f)) {
        return 0.0f;
    }
    const float geometry_factor = cosine / (ray_distance * ray_distance);
    const float footprint = 1.0f / (geometry_factor * pdf);
    return std::isfinite(footprint) ? footprint : 0.0f;
}

inline float ray_footprint(const ScatterEvent &event)
{
    return ray_footprint(event.normal, event.view_direction,
                         event.ray_distance, event.pdf, event.is_delta);
}

/*
 * The inverse footprint: the incoming segment measured with the RECEIVING
 * vertex's normal.
 *
 * Reconnection has to be well behaved in both directions, because resampling
 * evaluates the shift forwards and its reverse. Measuring only the outgoing
 * footprint would accept pairs whose reverse shift is badly conditioned.
 */
inline float inverse_ray_footprint(Vec3 receiving_normal,
                                   const ScatterEvent &incoming)
{
    return ray_footprint(receiving_normal, incoming.view_direction,
                         incoming.ray_distance, incoming.pdf,
                         incoming.is_delta);
}

/*
 * Whether the segment from `previous` to `current` may be replaced by a
 * reconnection.
 *
 * `receiving_normal` is the normal at the vertex the connection would arrive
 * at, and `previous` is the scattering event that produced the segment leading
 * into it.
 */
inline bool reconnectible(ReconnectionMode mode, Vec3 receiving_normal,
                          const ScatterEvent &previous,
                          const ScatterEvent &incoming,
                          const FootprintLimits &limits,
                          const ReconnectionThresholds &thresholds)
{
    /* A delta bounce is never reconnectible, in either mode. Its outgoing
     * direction is determined, so a displaced path cannot reproduce it. */
    if (previous.is_delta || incoming.is_delta) {
        return false;
    }

    if (mode == ReconnectionMode::fixed_threshold) {
        return previous.roughness > thresholds.roughness_threshold &&
            incoming.roughness > thresholds.roughness_threshold &&
            incoming.ray_distance > thresholds.distance_threshold;
    }

    /* The receiving vertex accepts the connection. */
    const float inverse = inverse_ray_footprint(receiving_normal, incoming);
    if (!(inverse > limits.footprint)) {
        return false;
    }
    /* The preceding vertex scattered broadly. The pdf threshold behaves as an
     * adaptive roughness floor and catches the corner cases where the footprint
     * on its own is a poor description of the lobe. */
    if (!(previous.pdf <= limits.pdf)) {
        return false;
    }
    /* The preceding segment was long enough to be worth pivoting around. */
    if (!(ray_footprint(previous) > limits.footprint)) {
        return false;
    }
    return true;
}

/*
 * The distance and orientation part of the shift Jacobian at a reconnection.
 * The remaining BSDF terms are applied by the shift itself.
 */
inline float partial_jacobian(float distance, Vec3 ray_direction,
                              Vec3 sample_normal)
{
    const float cosine = std::fmin(
        std::fmax(brdf::dot(sample_normal, ray_direction * -1.0f), 0.0f), 1.0f);
    if (!(cosine > 0.0f)) {
        return 0.0f;
    }
    const float jacobian = distance * distance / cosine;
    return std::isfinite(jacobian) ? jacobian : 0.0f;
}

}  // namespace ab3d2::dxr::restir

#endif
