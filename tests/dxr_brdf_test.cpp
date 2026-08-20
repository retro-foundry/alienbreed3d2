#include "renderer_dxr/dxr_brdf_math.h"

#include <cmath>
#include <cstdio>

namespace {

using namespace ab3d2::dxr::brdf;

bool near(float actual, float expected, float tolerance = 1.0e-5f)
{
    return std::fabs(actual - expected) <= tolerance;
}

bool near(Vec3 actual, Vec3 expected, float tolerance = 1.0e-5f)
{
    return near(actual.x, expected.x, tolerance) &&
        near(actual.y, expected.y, tolerance) &&
        near(actual.z, expected.z, tolerance);
}

int fail(const char *message)
{
    std::fprintf(stderr, "DXR BRDF test failed: %s\n", message);
    return 1;
}

}  // namespace

int main()
{
    const Material dielectric = {{0.8f, 0.2f, 0.1f}, 0.5f, 0.0f, 1.0f};
    const Material metal = {{0.8f, 0.2f, 0.1f}, 0.5f, 1.0f, 1.0f};
    const Material source_vector = {
        {0.8f, 0.2f, 0.1f}, 184.0f / 255.0f, 0.0f, 0.35f};
    if (!near(diffuse_reflectance(dielectric), dielectric.base_color) ||
        !near(f0(dielectric), {0.04f, 0.04f, 0.04f}) ||
        !near(diffuse_reflectance(metal), {0.0f, 0.0f, 0.0f}) ||
        !near(f0(metal), metal.base_color) ||
        !near(f0(source_vector), {0.014f, 0.014f, 0.014f})) {
        return fail("metallic-roughness reflectance equations disagree");
    }
    if (!(specular_probability(dielectric) >= 0.05f &&
          specular_probability(dielectric) <= 0.95f &&
          specular_probability(metal) > specular_probability(dielectric))) {
        return fail("lobe selection probability is invalid");
    }

    const Vec3 normal = {0.0f, 0.0f, 1.0f};
    const Vec3 tangent = {1.0f, 0.0f, 0.0f};
    const Vec3 bitangent = {0.0f, 1.0f, 0.0f};
    if (!near(transform_normal({0.0f, 0.0f, 1.0f}, 1.0f, tangent,
                               bitangent, normal), normal) ||
        !near(dot(transform_normal({0.5f, 0.0f, 0.8660254f}, 2.0f,
                                   tangent, bitangent, normal), normal),
              0.6546537f, 1.0e-4f)) {
        return fail("tangent-space normal transform is invalid");
    }

    const Vec3 view = normalize({0.25f, -0.15f, 1.0f});
    uint32_t seed = 0x12345678u;
    uint32_t repeated_seed = seed;
    for (int index = 0; index < 32; ++index) {
        if (random_uint(seed) != random_uint(repeated_seed)) {
            return fail("random sequence is not reproducible");
        }
    }

    seed = 0x31415926u;
    unsigned selected_specular_count = 0u;
    unsigned accepted_count = 0u;
    constexpr unsigned sample_count = 100000u;
    for (unsigned index = 0; index < sample_count; ++index) {
        Vec3 light = {};
        Evaluation sampled = {};
        bool selected_specular = false;
        const bool accepted = sample(dielectric, view, seed, light, sampled,
                                     selected_specular);
        selected_specular_count += selected_specular;
        if (!accepted) {
            continue;
        }
        ++accepted_count;
        const Evaluation checked = evaluate(dielectric, normal, view, light);
        if (!near(sampled.pdf, checked.pdf, 1.0e-6f) ||
            !near(sampled.value, checked.value, 1.0e-5f) ||
            !finite(sampled.value) || !std::isfinite(sampled.pdf)) {
            return fail("sampled BRDF and evaluated PDF disagree");
        }
        const Vec3 throughput = sampled.value * (light.z / sampled.pdf);
        if (!finite(throughput)) {
            return fail("sampled throughput is not finite");
        }
    }
    const float observed_specular =
        static_cast<float>(selected_specular_count) / sample_count;
    if (!near(observed_specular, specular_probability(dielectric), 0.005f) ||
        accepted_count < sample_count * 9u / 10u) {
        return fail("lobe sampler frequency or acceptance is invalid");
    }

    double integrated_pdf = 0.0;
    constexpr unsigned elevation_steps = 256u;
    constexpr unsigned azimuth_steps = 512u;
    const double solid_angle = (2.0 * pi / azimuth_steps) /
        static_cast<double>(elevation_steps);
    for (unsigned elevation = 0; elevation < elevation_steps; ++elevation) {
        const float cosine = (elevation + 0.5f) / elevation_steps;
        const float sine = std::sqrt(1.0f - cosine * cosine);
        for (unsigned azimuth = 0; azimuth < azimuth_steps; ++azimuth) {
            const float angle = 2.0f * pi *
                (azimuth + 0.5f) / azimuth_steps;
            const Vec3 light = {sine * std::cos(angle), sine * std::sin(angle),
                                cosine};
            integrated_pdf += evaluate(dielectric, normal, view, light).pdf *
                solid_angle;
        }
    }
    if (!(integrated_pdf > 0.95 && integrated_pdf < 1.01)) {
        return fail("mixture PDF does not integrate to expected hemisphere mass");
    }
    return 0;
}
