#ifndef AB3D2_DXR_BRDF_MATH_H
#define AB3D2_DXR_BRDF_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ab3d2::dxr::brdf {

constexpr float pi = 3.14159265358979323846f;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Material {
    Vec3 base_color;
    float roughness;
    float metalness;
    float specular_factor;
};

struct Evaluation {
    Vec3 value;
    float pdf;
};

inline Vec3 operator+(Vec3 first, Vec3 second)
{
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

inline Vec3 operator-(Vec3 first, Vec3 second)
{
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

inline Vec3 operator-(Vec3 value)
{
    return {-value.x, -value.y, -value.z};
}

inline Vec3 operator*(Vec3 first, Vec3 second)
{
    return {first.x * second.x, first.y * second.y, first.z * second.z};
}

inline Vec3 operator*(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

inline Vec3 operator*(float scalar, Vec3 value)
{
    return value * scalar;
}

inline Vec3 operator/(Vec3 value, float scalar)
{
    return value * (1.0f / scalar);
}

inline float dot(Vec3 first, Vec3 second)
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

inline Vec3 cross(Vec3 first, Vec3 second)
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x,
    };
}

inline Vec3 normalize(Vec3 value)
{
    const float length_squared = dot(value, value);
    return length_squared > 0.0f ? value / std::sqrt(length_squared) :
                                  Vec3{0.0f, 0.0f, 0.0f};
}

inline Vec3 lerp(Vec3 first, Vec3 second, float amount)
{
    return first * (1.0f - amount) + second * amount;
}

inline Vec3 diffuse_reflectance(const Material &material)
{
    return material.base_color * (1.0f - material.metalness);
}

inline Vec3 f0(const Material &material)
{
    const float dielectric = 0.04f * material.specular_factor;
    return lerp({dielectric, dielectric, dielectric}, material.base_color,
                material.metalness);
}

inline float luminance(Vec3 color)
{
    return dot(color, {0.2126f, 0.7152f, 0.0722f});
}

inline float specular_probability(const Material &material)
{
    const float diffuse_weight = luminance(diffuse_reflectance(material));
    const float specular_weight = luminance(f0(material));
    return std::clamp(specular_weight /
                          std::max(diffuse_weight + specular_weight, 1.0e-5f),
                      0.05f, 0.95f);
}

inline Vec3 fresnel_schlick(float cosine, Vec3 reflectance)
{
    const float factor = std::pow(1.0f - std::clamp(cosine, 0.0f, 1.0f), 5.0f);
    return reflectance + (Vec3{1.0f, 1.0f, 1.0f} - reflectance) * factor;
}

inline float ggx_distribution(float normal_half, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float denominator = normal_half * normal_half *
        (alpha_squared - 1.0f) + 1.0f;
    return alpha_squared /
        std::max(pi * denominator * denominator, 1.0e-8f);
}

inline float smith_g1(float normal_direction, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float cosine_squared = normal_direction * normal_direction;
    return (2.0f * normal_direction) /
        std::max(normal_direction +
                     std::sqrt(alpha_squared +
                               (1.0f - alpha_squared) * cosine_squared),
                 1.0e-8f);
}

inline Evaluation evaluate(const Material &material, Vec3 normal,
                           Vec3 view_direction, Vec3 light_direction)
{
    Evaluation result = {};
    const float normal_view = std::max(dot(normal, view_direction), 0.0f);
    const float normal_light = std::max(dot(normal, light_direction), 0.0f);
    if (normal_view <= 0.0f || normal_light <= 0.0f) {
        return result;
    }
    const Vec3 half_vector = normalize(view_direction + light_direction);
    const float normal_half = std::max(dot(normal, half_vector), 0.0f);
    const float view_half = std::max(dot(view_direction, half_vector), 0.0f);
    const float alpha = material.roughness * material.roughness;
    const Vec3 fresnel = fresnel_schlick(view_half, f0(material));
    const float distribution = ggx_distribution(normal_half, alpha);
    const float view_masking = smith_g1(normal_view, alpha);
    const float geometry = view_masking * smith_g1(normal_light, alpha);
    const Vec3 specular = fresnel *
        (distribution * geometry /
         std::max(4.0f * normal_view * normal_light, 1.0e-7f));
    const Vec3 diffuse = (Vec3{1.0f, 1.0f, 1.0f} - fresnel) *
        diffuse_reflectance(material) / pi;
    const float diffuse_pdf = normal_light / pi;
    const float specular_pdf = distribution * view_masking /
        std::max(4.0f * normal_view, 1.0e-7f);
    const float choose_specular = specular_probability(material);
    result.value = diffuse + specular;
    result.pdf = diffuse_pdf * (1.0f - choose_specular) +
        specular_pdf * choose_specular;
    return result;
}

inline uint32_t random_uint(uint32_t &state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

inline float random_unit(uint32_t &state)
{
    return static_cast<float>(random_uint(state) & 0x00ffffffu) /
        16777216.0f;
}

inline Vec3 cosine_hemisphere(uint32_t &seed)
{
    const float first = random_unit(seed);
    const float angle = 2.0f * pi * random_unit(seed);
    const float radius = std::sqrt(first);
    return {radius * std::cos(angle), radius * std::sin(angle),
            std::sqrt(std::max(0.0f, 1.0f - first))};
}

inline Vec3 sample_ggx_visible_normal(Vec3 view_direction, float alpha,
                                      uint32_t &seed)
{
    const Vec3 stretched_view = normalize(
        {alpha * view_direction.x, alpha * view_direction.y, view_direction.z});
    const float lens_squared = stretched_view.x * stretched_view.x +
        stretched_view.y * stretched_view.y;
    const Vec3 first_tangent = lens_squared > 0.0f ?
        Vec3{-stretched_view.y, stretched_view.x, 0.0f} /
            std::sqrt(lens_squared) : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 second_tangent = cross(stretched_view, first_tangent);
    const float radius = std::sqrt(random_unit(seed));
    const float angle = 2.0f * pi * random_unit(seed);
    const float first = radius * std::cos(angle);
    float second = radius * std::sin(angle);
    const float interpolation = 0.5f * (1.0f + stretched_view.z);
    second = std::sqrt(std::max(0.0f, 1.0f - first * first)) *
        (1.0f - interpolation) + second * interpolation;
    const Vec3 stretched_normal = first_tangent * first +
        second_tangent * second + stretched_view *
            std::sqrt(std::max(0.0f, 1.0f - first * first - second * second));
    return normalize({alpha * stretched_normal.x,
                      alpha * stretched_normal.y,
                      std::max(0.0f, stretched_normal.z)});
}

inline bool sample(const Material &material, Vec3 view_direction,
                   uint32_t &seed, Vec3 &light_direction,
                   Evaluation &evaluation, bool &selected_specular)
{
    selected_specular = random_unit(seed) < specular_probability(material);
    if (selected_specular) {
        const float alpha = material.roughness * material.roughness;
        const Vec3 half_vector =
            sample_ggx_visible_normal(view_direction, alpha, seed);
        light_direction = -view_direction + half_vector *
            (2.0f * dot(view_direction, half_vector));
    } else {
        light_direction = cosine_hemisphere(seed);
    }
    light_direction = normalize(light_direction);
    evaluation = evaluate(material, {0.0f, 0.0f, 1.0f}, view_direction,
                          light_direction);
    return light_direction.z > 0.0f && evaluation.pdf > 0.0f;
}

inline Vec3 transform_normal(Vec3 tangent_normal, float strength,
                             Vec3 tangent, Vec3 bitangent, Vec3 normal)
{
    tangent_normal.x *= strength;
    tangent_normal.y *= strength;
    tangent_normal.z = std::max(tangent_normal.z, 1.0e-4f);
    tangent_normal = normalize(tangent_normal);
    Vec3 transformed = normalize(tangent * tangent_normal.x +
                                 bitangent * tangent_normal.y +
                                 normal * tangent_normal.z);
    return dot(transformed, normal) > 0.0f ? transformed : normal;
}

inline bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

}  // namespace ab3d2::dxr::brdf

#endif
