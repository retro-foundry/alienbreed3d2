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
    if (!near(lambertian_value(dielectric), dielectric.base_color / pi) ||
        !near(cosine_hemisphere_pdf(0.25f), 0.25f / pi) ||
        !near(cosine_sample_throughput(dielectric, 0.25f),
              dielectric.base_color) ||
        !near(cosine_sample_throughput(metal, 0.75f), Vec3{}) ||
        !near(triangle_solid_angle_pdf(0.25f, 0.5f, 16.0f, 0.5f),
              4.0f) ||
        !near(diffuse_polygon_nee(dielectric, {10.0f, 20.0f, 30.0f},
                                  0.5f, 4.0f),
              {1.0f / pi, 0.5f / pi, 0.375f / pi}) ||
        !near(diffuse_polygon_nee(dielectric, {10.0f, 20.0f, 30.0f},
                                  0.0f, 4.0f), Vec3{}) ||
        !near(diffuse_polygon_nee(dielectric, {10.0f, 20.0f, 30.0f},
                                  0.5f, 0.0f), Vec3{})) {
        return fail("isolated diffuse polygon-light estimator changed");
    }
    for (uint32_t ordinal = 0u; ordinal < 4u; ++ordinal) {
        const float sample = stratified_unit_sample(
            ordinal, 4u, 0.5f);
        if (!(sample > static_cast<float>(ordinal) / 4.0f &&
              sample < static_cast<float>(ordinal + 1u) / 4.0f) ||
            rotate_stratum(ordinal, 4u, 3u) !=
                (ordinal + 3u) % 4u) {
            return fail("diffuse continuation strata do not partition the domain");
        }
    }
    if (!near(stratified_unit_sample(0u, 0u, 0.5f), 0.5f) ||
        !near(stratified_unit_sample(5u, 4u, 0.5f), 0.375f)) {
        return fail("diffuse continuation stratum bounds changed");
    }
    if (!(specular_probability(dielectric) >= 0.05f &&
          specular_probability(dielectric) <= 0.95f &&
          specular_probability(metal) > specular_probability(dielectric))) {
        return fail("lobe selection probability is invalid");
    }
    if (!near(direct_specular_weight(0.0f), 0.0f) ||
        !near(direct_specular_weight(0.16f), 0.0f) ||
        !near(direct_specular_weight(0.18f), 0.5f) ||
        !near(direct_specular_weight(0.20f), 1.0f) ||
        !near(direct_specular_weight(1.0f), 1.0f) ||
        !near(direct_specular_weight(0.18f) +
                  direct_emitter_hit_complement(0.18f),
              1.0f) ||
        !near(fake_specular_weight(0.20f), 0.0f) ||
        !near(fake_specular_weight(0.25f), 0.5f) ||
        !near(fake_specular_weight(0.30f), 1.0f)) {
        return fail("direct/specular-hit transition weights changed");
    }
    Material rough_dielectric = dielectric;
    rough_dielectric.roughness = 0.30f;
    Material smooth_dielectric = dielectric;
    smooth_dielectric.roughness = 0.10f;
    Material smooth_metal = metal;
    smooth_metal.roughness = 0.10f;
    const float normal_continuation_probability =
        continuation_specular_probability(smooth_dielectric, 1.0f);
    const float grazing_continuation_probability =
        continuation_specular_probability(smooth_dielectric, 0.1f);
    if (!(normal_continuation_probability >= 0.05f &&
          normal_continuation_probability <= 0.95f &&
          grazing_continuation_probability >
              normal_continuation_probability) ||
        !near(continuation_specular_probability(rough_dielectric, 0.5f),
              0.0f) ||
        !near(continuation_specular_probability(smooth_metal, 0.5f), 1.0f) ||
        !near(normal_continuation_probability *
                  (3.0f / normal_continuation_probability) +
                  (1.0f - normal_continuation_probability) *
                  (2.0f / (1.0f - normal_continuation_probability)),
              5.0f)) {
        return fail("continuation-lobe selection or inverse weight changed");
    }
    if (primary_direct_visibility_sample_count(0u) != 0u ||
        primary_direct_visibility_sample_count(1u) != 1u ||
        primary_direct_visibility_sample_count(4u) != 4u ||
        primary_direct_visibility_sample_count(16u) != 4u ||
        primary_direct_candidate_group_size(1u, 0u) != 1u ||
        primary_direct_candidate_group_size(5u, 0u) != 2u ||
        primary_direct_candidate_group_size(5u, 1u) != 1u ||
        primary_direct_candidate_group_size(16u, 3u) != 4u ||
        primary_direct_candidate_group_size(16u, 4u) != 0u) {
        return fail("primary direct visibility partition changed");
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
            !near(sampled.value, sampled.diffuse + sampled.specular,
                  1.0e-5f) ||
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

    /*
     * Toksvig. A mip-averaged normal map that has kept its full length carries
     * all of its detail and must not move roughness; one that has shortened has
     * lost detail into the average, and roughness has to widen to stand in for
     * it. Level zero resolves everything, so it is always a no-op.
     */
    constexpr float smooth_roughness = 0.25f;
    /* The spec-power round trip is not bit-exact, so this is a tolerance
     * rather than an equality. Mip zero below returns early and is exact. */
    if (!(std::fabs(adjust_roughness_toksvig(smooth_roughness, 1.0f, 1.0f) -
                    smooth_roughness) < 1.0e-3f)) {
        return fail("an unshortened normal map perturbed roughness");
    }
    if (adjust_roughness_toksvig(smooth_roughness, 0.6f, 0.0f) !=
        smooth_roughness) {
        return fail("mip level zero perturbed roughness");
    }
    const float widened = adjust_roughness_toksvig(smooth_roughness, 0.6f, 1.0f);
    if (!(widened > smooth_roughness)) {
        return fail("a shortened normal map did not widen roughness");
    }
    const float widened_more =
        adjust_roughness_toksvig(smooth_roughness, 0.3f, 1.0f);
    if (!(widened_more > widened)) {
        return fail("roughness did not track how much detail was averaged away");
    }
    if (!(widened_more <= 1.0f)) {
        return fail("Toksvig roughness left the unit range");
    }
    return 0;
}
