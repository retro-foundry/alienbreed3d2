#ifndef AB3D2_DXR_RESTIR_GI_H
#define AB3D2_DXR_RESTIR_GI_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::restir_gi {

/* GPU layout mirrored by PackedGIReservoir in path_trace.hlsl. A secondary
 * surface is identified by triangle and barycentrics so its current position
 * and normal can be reconstructed after geometry moves. `weight` is the
 * finalized basic-resampling inverse density; this deliberately does not
 * claim the more expensive unbiased visibility correction. */
struct PackedReservoir {
    uint32_t primitive_index;
    uint32_t sample_count;
    float weight;
    float sample_radiance[3];
    float barycentrics[2];
};

static_assert(sizeof(PackedReservoir) == 32u);

inline constexpr uint32_t invalid_primitive = 0xffffffffu;
inline constexpr uint32_t initial_candidate_count = 4u;
inline constexpr uint32_t spatial_sample_count = 4u;
inline constexpr int spatial_radius = 32;
/* A neighboring primary normal/depth is not a validity condition for a stored
 * secondary-surface sample. The sample is reconstructed in current geometry,
 * retargeted at the new primary, and freshly visibility tested, which permits
 * useful paths to cross a geometric corner without copying irradiance across
 * it. Screen-space radius still bounds the proposal domain. */
inline constexpr bool spatial_reuse_requires_primary_guide_match = false;
inline constexpr float continuation_radial_power = 0.4f;
inline constexpr float pi = 3.14159265358979323846f;

struct Vec3 {
    float x;
    float y;
    float z;
};

inline Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 multiply(Vec3 value, float scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float length_squared(Vec3 value)
{
    return dot(value, value);
}

inline Vec3 normalize(Vec3 value)
{
    const float squared = length_squared(value);
    if (!(squared > 0.0f) || !std::isfinite(squared)) {
        return {};
    }
    return multiply(value, 1.0f / std::sqrt(squared));
}

inline float low_frequency_solid_angle_pdf(float cosine)
{
    cosine = std::clamp(cosine, 0.0f, 1.0f);
    if (!(cosine > 0.0f)) {
        return 0.0f;
    }
    const float radial = std::sqrt(std::max(1.0f - cosine * cosine, 0.0f));
    const float exponent = 1.0f / continuation_radial_power - 2.0f;
    return cosine * std::pow(radial, exponent) /
        (2.0f * pi * continuation_radial_power);
}

inline float low_frequency_directional_bias(float cosine)
{
    cosine = std::clamp(cosine, 0.0f, 1.0f);
    const float cosine_pdf = cosine / pi;
    return cosine_pdf > 0.0f ?
        low_frequency_solid_angle_pdf(cosine) / cosine_pdf : 0.0f;
}

/* The reservoir's common domain is secondary surface area. Converting the
 * continuation's solid-angle density to area makes a stored sample valid when
 * reconnected to another primary point and exposes the geometric Jacobian. */
inline float solid_angle_pdf_to_area(float solid_angle_pdf,
                                     Vec3 primary_position,
                                     Vec3 secondary_position,
                                     Vec3 secondary_normal)
{
    const Vec3 offset = subtract(secondary_position, primary_position);
    const float distance_squared = length_squared(offset);
    if (!(solid_angle_pdf > 0.0f) || !(distance_squared > 0.0f) ||
        !std::isfinite(solid_angle_pdf) ||
        !std::isfinite(distance_squared)) {
        return 0.0f;
    }
    const Vec3 direction = multiply(offset, 1.0f / std::sqrt(distance_squared));
    const float secondary_cosine =
        std::max(dot(secondary_normal, multiply(direction, -1.0f)), 0.0f);
    return solid_angle_pdf * secondary_cosine / distance_squared;
}

/* Incident diffuse radiance at the primary point before primary albedo. The
 * secondary sample stores outgoing radiance, while the two cosines and
 * inverse-square term are the area-form reconnection geometry. */
inline Vec3 reconnect_incident(Vec3 primary_position, Vec3 primary_normal,
                               Vec3 secondary_position,
                               Vec3 secondary_normal,
                               Vec3 secondary_radiance)
{
    const Vec3 offset = subtract(secondary_position, primary_position);
    const float distance_squared = length_squared(offset);
    if (!(distance_squared > 0.0f) || !std::isfinite(distance_squared)) {
        return {};
    }
    const Vec3 direction = multiply(offset, 1.0f / std::sqrt(distance_squared));
    const float primary_cosine = std::max(dot(primary_normal, direction), 0.0f);
    const float secondary_cosine =
        std::max(dot(secondary_normal, multiply(direction, -1.0f)), 0.0f);
    const float directional_bias =
        low_frequency_directional_bias(primary_cosine);
    const float geometry = directional_bias * primary_cosine *
        secondary_cosine / (pi * distance_squared);
    return multiply(secondary_radiance, geometry);
}

inline float luminance(Vec3 value)
{
    return std::max(value.x * 0.2126f + value.y * 0.7152f +
                    value.z * 0.0722f, 0.0f);
}

inline float target(Vec3 primary_albedo, Vec3 incident)
{
    return luminance({primary_albedo.x * incident.x,
                      primary_albedo.y * incident.y,
                      primary_albedo.z * incident.z});
}

inline float initial_candidate_weight(float candidate_target,
                                      float area_pdf)
{
    if (!(candidate_target > 0.0f) || !(area_pdf > 0.0f) ||
        !std::isfinite(candidate_target) || !std::isfinite(area_pdf)) {
        return 0.0f;
    }
    return candidate_target / area_pdf;
}

inline float reused_candidate_weight(float candidate_target,
                                     float source_weight,
                                     uint32_t source_sample_count)
{
    if (!(candidate_target > 0.0f) || !(source_weight > 0.0f) ||
        source_sample_count == 0u || !std::isfinite(candidate_target) ||
        !std::isfinite(source_weight)) {
        return 0.0f;
    }
    return candidate_target * source_weight *
        static_cast<float>(source_sample_count);
}

inline float finalize_weight(float weight_sum, float selected_target,
                             uint32_t total_sample_count)
{
    if (!(weight_sum > 0.0f) || !(selected_target > 0.0f) ||
        total_sample_count == 0u || !std::isfinite(weight_sum) ||
        !std::isfinite(selected_target)) {
        return 0.0f;
    }
    return weight_sum /
        (selected_target * static_cast<float>(total_sample_count));
}

}  // namespace ab3d2::dxr::restir_gi

#endif
