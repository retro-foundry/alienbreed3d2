#ifndef AB3D2_DXR_RECONSTRUCTION_MATH_H
#define AB3D2_DXR_RECONSTRUCTION_MATH_H

#include "dxr_brdf_math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::reconstruction {

using brdf::Vec3;

/*
 * NVIDIA Streamline v2.12.0 ProgrammingGuideDLSS_RR.md section 4.2.1.
 * `linear_roughness` is squared here because the approximation consumes the
 * GGX alpha used by the renderer's metallic-roughness BRDF.
 */
inline Vec3 specular_albedo(Vec3 specular_color, float linear_roughness,
                            float normal_view)
{
    const float no_v = std::fabs(normal_view);
    const float alpha = linear_roughness * linear_roughness;
    const float no_v2 = no_v * no_v;
    const float no_v3 = no_v * no_v2;
    const float alpha2 = alpha * alpha;
    const float alpha3 = alpha * alpha2;

    const float m1_x = 0.99044f - 1.28514f * no_v;
    const float m1_y = 1.29678f - 0.755907f * no_v;
    const float m2_x = 1.0f + 2.92338f * no_v + 59.4188f * no_v3;
    const float m2_y = 20.3225f - 27.0302f * no_v + 222.592f * no_v3;
    const float m2_z = 121.563f + 626.13f * no_v + 316.627f * no_v3;
    float bias = (m1_x + m1_y * alpha) /
        (m2_x + m2_y * alpha + m2_z * alpha3);

    const float m3_x = 0.0365463f + 3.32707f * no_v;
    const float m3_y = 9.0632f - 9.04756f * no_v;
    const float m4_x = 1.0f + 3.59685f * no_v2 - 1.36772f * no_v3;
    const float m4_y = 9.04401f - 16.3174f * no_v2 + 9.22949f * no_v3;
    const float m4_z = 5.56589f + 19.7886f * no_v2 - 20.2123f * no_v3;
    const float scale = (m3_x + m3_y * alpha) /
        (m4_x + m4_y * alpha + m4_z * alpha3);

    bias *= std::clamp(specular_color.y * 50.0f, 0.0f, 1.0f);
    return specular_color * std::max(scale, 0.0f) +
        Vec3{std::max(bias, 0.0f), std::max(bias, 0.0f),
             std::max(bias, 0.0f)};
}

/*
 * Depth range shared by the linear-depth guide, the Streamline camera constants,
 * and the ray extent in shaders/path_trace.hlsl, which mirrors the far plane as
 * `SceneFarPlane`. The values follow the Amiga-sized world scale rather than a
 * conventional metre-based range.
 */
constexpr float scene_near_plane = 0.05f;
constexpr float scene_far_plane = 8192.0f;

struct CameraProjection {
    Vec3 position;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float tan_half_fov_y;
    float aspect;
    uint32_t width;
    uint32_t height;
};

struct PixelPosition {
    float x;
    float y;
    bool valid;
};

struct PixelJitter {
    float x;
    float y;
};

inline float radical_inverse(uint32_t index, uint32_t base)
{
    float inverse = 1.0f / static_cast<float>(base);
    float place = inverse;
    float result = 0.0f;
    while (index != 0u) {
        result += static_cast<float>(index % base) * place;
        index /= base;
        place *= inverse;
    }
    return result;
}

/*
 * Number of distinct sub-pixel offsets before the jitter sequence repeats.
 * An unbounded Halton index keeps producing new sub-pixel positions forever, so
 * the upscaler's accumulation never closes a cycle and the reconstructed image
 * has no fixed point to settle onto. A fixed phase count gives the sequence a
 * period, which is what lets a static camera converge.
 */
constexpr uint32_t jitter_phase_count = 32u;

inline PixelJitter frame_jitter(uint32_t sample_index)
{
    const uint32_t sample = (sample_index % jitter_phase_count) + 1u;
    return {radical_inverse(sample, 2u) - 0.5f,
            radical_inverse(sample, 3u) - 0.5f};
}

inline PixelPosition project_world(const CameraProjection &camera,
                                   Vec3 world_position)
{
    const Vec3 relative = world_position - camera.position;
    const float view_depth = brdf::dot(relative, camera.forward);
    if (!(view_depth > 1.0e-6f) || !(camera.tan_half_fov_y > 0.0f) ||
        !(camera.aspect > 0.0f) || camera.width == 0u || camera.height == 0u) {
        return {0.0f, 0.0f, false};
    }
    const float ndc_x = brdf::dot(relative, camera.right) /
        (view_depth * camera.aspect * camera.tan_half_fov_y);
    const float ndc_y = brdf::dot(relative, camera.up) /
        (view_depth * camera.tan_half_fov_y);
    return {
        (ndc_x * 0.5f + 0.5f) * static_cast<float>(camera.width),
        (0.5f - ndc_y * 0.5f) * static_cast<float>(camera.height),
        std::isfinite(ndc_x) && std::isfinite(ndc_y),
    };
}

inline PixelPosition project_direction(const CameraProjection &camera,
                                       Vec3 world_direction)
{
    CameraProjection direction_camera = camera;
    direction_camera.position = {0.0f, 0.0f, 0.0f};
    return project_world(direction_camera, world_direction);
}

inline PixelPosition scene_motion(const CameraProjection &current_camera,
                                  const CameraProjection &previous_camera,
                                  Vec3 current_world_position,
                                  Vec3 previous_world_position)
{
    const PixelPosition current = project_world(current_camera,
                                                current_world_position);
    const PixelPosition previous = project_world(previous_camera,
                                                 previous_world_position);
    return {previous.x - current.x, previous.y - current.y,
            current.valid && previous.valid};
}

}  // namespace ab3d2::dxr::reconstruction

#endif
