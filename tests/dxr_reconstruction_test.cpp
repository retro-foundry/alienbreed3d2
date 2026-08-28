#include "renderer_dxr/dxr_reconstruction_math.h"
#include "renderer_dxr/dxr_post_processing.h"
#include "renderer_dxr/dxr_tone_mapping.h"
#include "renderer_dxr/dxr_emitter_history.h"
#include "renderer_dxr/dxr_indirect_reconstruction.h"
#include "renderer_dxr/dxr_light_grid.h"
#include "renderer_dxr/dxr_restir_gi.h"
#include "renderer_dxr/dxr_temporal_metrics.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using namespace ab3d2::dxr::reconstruction;

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
    std::fprintf(stderr, "DXR reconstruction test failed: %s\n", message);
    return 1;
}

}  // namespace

int main()
{
    namespace tone = ab3d2::dxr::tone_mapping;
    namespace post = ab3d2::dxr::post_processing;
    namespace indirect = ab3d2::dxr::indirect_reconstruction;
    namespace grid = ab3d2::dxr::light_grid;
    namespace gi = ab3d2::dxr::restir_gi;
    namespace temporal = ab3d2::dxr::temporal_metrics;
    static_assert(indirect::downsample_factor == 3 &&
                  static_cast<uint32_t>(indirect::Mode::full) == 0u &&
                  static_cast<uint32_t>(indirect::Mode::temporal) == 1u &&
                  static_cast<uint32_t>(indirect::Mode::raw) == 2u &&
                  static_cast<uint32_t>(indirect::Mode::regional) == 3u &&
                  static_cast<uint32_t>(indirect::Mode::deflicker) == 4u &&
                  static_cast<uint32_t>(indirect::Mode::wavelet1) == 5u &&
                  static_cast<uint32_t>(indirect::Mode::wavelet2) == 6u &&
                  static_cast<uint32_t>(indirect::Mode::restir) == 7u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::combined) == 0u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::emission) == 1u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::direct_diffuse) == 2u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::direct_specular) == 3u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::indirect) == 4u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::smooth_specular) == 5u &&
                  static_cast<uint32_t>(
                      indirect::RadianceChannel::rough_specular) == 6u &&
                  indirect::filter_steps[0] == 1 &&
                  indirect::filter_steps[1] == 2 &&
                  indirect::filter_steps[2] == 4 &&
                  indirect::filter_radius == 1 &&
                  indirect::filter_reach == 22 &&
                  indirect::filter_kernel[0] == 1.0f &&
                  indirect::filter_kernel[1] == 0.5f &&
                  indirect::gradient_filter_steps[0] == 1 &&
                  indirect::gradient_filter_steps[6] == 64 &&
                  indirect::deflicker_neighbor_factor == 2.0f &&
                  indirect::temporal_antilag_scale == 0.2f &&
                  indirect::temporal_antilag_history_power == 10.0f &&
                  indirect::temporal_minimum_current_weight == 0.01f &&
                  indirect::temporal_gradient_confirmation_rate == 0.25f &&
                  indirect::temporal_gradient_confirmation_threshold == 0.4f &&
                  indirect::stable_indirect_sample_count == 1u &&
                  indirect::stable_indirect_sampling_phase_count == 4u &&
                  indirect::adaptive_history_maturity_tolerance == 0.5f &&
                  indirect::stable_schedule_covers_gradient_region() &&
                  indirect::maximum_path_depth == 8u &&
                  indirect::path_dimensions_per_continuation == 8u &&
                  indirect::direction_dimension_x == 6u &&
                  indirect::direction_dimension_y == 7u &&
                  indirect::polygon_bounce_stream_stride == 1024u &&
                  indirect::continuation_count(0u) == 0u &&
                  indirect::continuation_count(1u) == 0u &&
                  indirect::continuation_count(2u) == 1u &&
                  indirect::continuation_count(3u) == 2u &&
                  indirect::continuation_count(8u) == 7u &&
                  indirect::continuation_count(9u) == 7u &&
                  indirect::sh_basis_l0 == 0.282095f &&
                  indirect::sh_basis_l1 == 0.488603f &&
                  indirect::filter_support_is_continuous() &&
                  indirect::continuation_radial_power == 0.4f);
    static_assert(sizeof(gi::PackedReservoir) == 32u);
    static_assert(sizeof(indirect::PackedHistoryPixel) == 24u);
    static_assert(grid::refresh_phase_count == 16u &&
                  grid::lights_per_cell == 512u &&
                  grid::entry_count == 2097152u &&
                  grid::lights_per_cell / grid::refresh_phase_count == 32u);
    static_assert(gi::initial_candidate_count == 4u &&
                  gi::spatial_sample_count == 4u &&
                  gi::spatial_radius == 32 &&
                  !gi::spatial_reuse_requires_primary_guide_match);
    if (!near(temporal::decode_half(0x3c00u), 1.0f) ||
        !near(temporal::decode_half(0xbc00u), -1.0f) ||
        !near(temporal::decode_half(0x0001u),
              std::ldexp(1.0f, -24), 1.0e-10f)) {
        return fail("binary16 motion decoding changed");
    }
    const std::vector<uint8_t> previous_motion_rgb = {
        10u, 10u, 10u, 20u, 20u, 20u,
        30u, 30u, 30u, 40u, 40u, 40u};
    const std::vector<uint8_t> current_motion_rgb = {
        99u, 99u, 99u, 10u, 10u, 10u,
        20u, 20u, 20u, 30u, 30u, 30u};
    const std::vector<uint16_t> current_to_previous_motion = {
        0xb800u, 0u, 0xb800u, 0u};
    const temporal::Difference translated = temporal::measure_reprojected_rgb(
        current_motion_rgb, previous_motion_rgb, 4u, 1u,
        current_to_previous_motion, 2u, 1u);
    if (!near(static_cast<float>(translated.mean_absolute_component), 0.0f) ||
        translated.compared_pixels != 3u || translated.outlier_pixels != 0u) {
        return fail("current-to-previous motion did not cancel image translation");
    }
    const grid::Position quantized_grid_center = grid::quantized_center(
        {511.0f, -1.0f, 1024.0f});
    const uint16_t encoded_full_history =
        indirect::encode_history_length(24.0f, 24u, 4u);
    const uint16_t encoded_large_single_sample =
        indirect::encode_history_length(1.0f, 65536u, 4u);
    if (encoded_full_history != UINT16_MAX ||
        encoded_large_single_sample == 0u ||
        !near(indirect::decode_history_length(
                  encoded_full_history, 24u, 4u), 24.0f) ||
        !near(indirect::decode_history_length(
                  UINT16_MAX, 65536u, 4u), 65536.0f) ||
        !near(indirect::decode_history_length(
                  indirect::encode_history_length(4.0f, 0u, 4u),
                  0u, 4u), 4.0f)) {
        return fail("packed indirect history did not preserve its limits");
    }
    if (!near(quantized_grid_center.x, 0.0f) ||
        !near(quantized_grid_center.y, -512.0f) ||
        !near(quantized_grid_center.z, 1024.0f) ||
        grid::cache_needs_rebuild(
            quantized_grid_center, 7u, quantized_grid_center, 7u, true) ||
        !grid::cache_needs_rebuild(
            quantized_grid_center, 7u, quantized_grid_center, 8u, true) ||
        !grid::cache_needs_rebuild(
            quantized_grid_center, 7u, {512.0f, -512.0f, 1024.0f},
            7u, true) ||
        !grid::cache_needs_rebuild(
            quantized_grid_center, 7u, quantized_grid_center, 7u, false)) {
        return fail("ReGIR quantized cache invalidation changed");
    }
    const gi::Vec3 gi_primary = {0.0f, 0.0f, 0.0f};
    const gi::Vec3 gi_secondary = {
        1.7320508075688772f, 0.0f, 1.0f};
    const gi::Vec3 gi_direction = gi::normalize(gi_secondary);
    const gi::Vec3 gi_secondary_normal = gi::multiply(gi_direction, -1.0f);
    const gi::Vec3 gi_incident = gi::reconnect_incident(
        gi_primary, {0.0f, 0.0f, 1.0f}, gi_secondary,
        gi_secondary_normal, {3.0f, 2.0f, 1.0f});
    /* The same secondary sample remains a valid proposal for a perpendicular
     * primary face. The GPU path additionally reconstructs current geometry
     * and traces fresh visibility before accepting this cross-corner reuse. */
    const gi::Vec3 gi_cross_corner_incident = gi::reconnect_incident(
        gi_primary, {1.0f, 0.0f, 0.0f}, gi_secondary,
        gi_secondary_normal, {3.0f, 2.0f, 1.0f});
    const float gi_solid_angle_pdf =
        gi::low_frequency_solid_angle_pdf(0.5f);
    const float gi_directional_bias =
        gi::low_frequency_directional_bias(0.5f);
    const float gi_area_pdf = gi::solid_angle_pdf_to_area(
        gi_solid_angle_pdf, gi_primary, gi_secondary,
        gi_secondary_normal);
    const float gi_target = gi::target({0.5f, 0.5f, 0.5f}, gi_incident);
    const float gi_initial_weight =
        gi::initial_candidate_weight(gi_target, gi_area_pdf);
    const float gi_final_weight =
        gi::finalize_weight(gi_initial_weight, gi_target, 1u);
    if (!near(gi_incident.x, 3.0f * gi_directional_bias /
                                  (8.0f * gi::pi)) ||
        !near(gi_incident.y, 2.0f * gi_directional_bias /
                                  (8.0f * gi::pi)) ||
        !near(gi_incident.z, gi_directional_bias / (8.0f * gi::pi)) ||
        !near(gi_area_pdf, gi_solid_angle_pdf / 4.0f) ||
        !(gi_directional_bias > 1.0f) ||
        !(gi::luminance(gi_cross_corner_incident) > 0.0f) ||
        !near(gi_final_weight, 1.0f / gi_area_pdf) ||
        !near(gi::reused_candidate_weight(gi_target, gi_final_weight, 1u),
              gi_initial_weight) ||
        gi::initial_candidate_weight(gi_target, 0.0f) != 0.0f ||
        gi::reused_candidate_weight(0.0f, gi_final_weight, 1u) != 0.0f ||
        gi::finalize_weight(gi_initial_weight, 0.0f, 1u) != 0.0f) {
        return fail("ReSTIR GI area-measure reservoir contract changed");
    }
    double gi_pdf_mass = 0.0;
    double gi_biased_cosine_mass = 0.0;
    constexpr int gi_pdf_steps = 16384;
    for (int step = 0; step < gi_pdf_steps; ++step) {
        const float cosine = (static_cast<float>(step) + 0.5f) /
            static_cast<float>(gi_pdf_steps);
        const float solid_angle = 2.0f * gi::pi /
            static_cast<float>(gi_pdf_steps);
        gi_pdf_mass +=
            gi::low_frequency_solid_angle_pdf(cosine) * solid_angle;
        gi_biased_cosine_mass += (cosine / gi::pi) *
            gi::low_frequency_directional_bias(cosine) * solid_angle;
    }
    if (std::abs(gi_pdf_mass - 1.0) > 1.0e-4 ||
        std::abs(gi_biased_cosine_mass - 1.0) > 1.0e-4) {
        return fail("ReSTIR GI broad continuation PDF lost unit mass");
    }
    tone::Histogram metering_histogram = {};
    tone::add_sample(metering_histogram, 0.000001f, 10u);
    tone::add_sample(metering_histogram, 0.01f, 80u);
    tone::add_sample(metering_histogram, 10.0f, 10u);
    const tone::Metering metering = tone::meter(metering_histogram);
    const tone::State first_curve = tone::build_curve(
        metering_histogram, tone::State{}, false, 1.0f / 60.0f);
    const tone::State second_curve = tone::build_curve(
        metering_histogram, first_curve, true, 1.0f / 60.0f);
    tone::State invalid_previous_curve = first_curve;
    invalid_previous_curve.log_curve[32] =
        std::numeric_limits<float>::quiet_NaN();
    const tone::State recovered_curve = tone::build_curve(
        metering_histogram, invalid_previous_curve, true, 1.0f / 60.0f);
    /* Regression for the Q2RTX parity request on 2026-08-27. These are the
     * checked-in comparator defaults and the published minimum-distortion
     * stage contract, not a fitted AB3D2 shadow curve. */
    tone::Histogram dark_corridor_histogram = {};
    tone::add_sample(dark_corridor_histogram, 0.000004f, 10u);
    tone::add_sample(dark_corridor_histogram, 0.000062f, 80u);
    tone::add_sample(dark_corridor_histogram, 0.038598f, 10u);
    const tone::State dark_corridor_curve = tone::build_curve(
        dark_corridor_histogram, tone::State{}, false, 1.0f / 60.0f);
    bool curve_is_monotonic = true;
    for (uint32_t point = 1u; point < tone::curve_point_count; ++point) {
        curve_is_monotonic = curve_is_monotonic &&
            first_curve.log_curve[point] >=
                first_curve.log_curve[point - 1u];
    }
    const float one_bright_step = tone::adapt_luminance(
        0.001f, 0.01f, true, 0.2f);
    const float two_bright_steps = tone::adapt_luminance(
        tone::adapt_luminance(0.001f, 0.01f, true, 0.1f),
        0.01f, true, 0.1f);
    const float dark_corridor_low =
        tone::lookup(dark_corridor_curve, 0.000004f);
    const float dark_corridor_middle =
        tone::lookup(dark_corridor_curve, 0.000062f);
    const float dark_corridor_high =
        tone::lookup(dark_corridor_curve, 0.038598f);
    if (tone::histogram_index(0.0f) != 0u ||
        tone::histogram_index(1000.0f) !=
            tone::histogram_bin_count - 1u ||
        metering_histogram.total_weight >
            100u * tone::histogram_fraction_scale ||
        metering_histogram.total_weight <
            100u * (tone::histogram_fraction_scale - 1u) ||
        !near(metering.total_weight, 1.0f) ||
        !(metering.included_weight > 0.0f) ||
        !(metering.average_luminance > 0.0f) ||
        !(metering.low_luminance < metering.average_luminance) ||
        !(metering.high_luminance > metering.average_luminance) ||
        first_curve.target_luminance < tone::minimum_scene_luminance ||
        first_curve.target_luminance > tone::maximum_scene_luminance ||
        !near(first_curve.adapted_luminance,
              first_curve.target_luminance) ||
        !near(one_bright_step, two_bright_steps) ||
        tone::adapt_luminance(1.0f, 0.5f, false, 0.1f) != 0.5f ||
        tone::adapt_luminance(1.0f, 0.5f, true, -1.0f) != 1.0f ||
        !near(tone::adapt_luminance(0.001f, 0.01f, true, 1.0f),
              tone::adapt_luminance(0.001f, 0.01f, true,
                                    tone::maximum_delta_seconds)) ||
        !curve_is_monotonic ||
        !(tone::lookup(first_curve, 0.01f) > 0.0f) ||
        !(tone::lookup(first_curve, 0.01f) <
          tone::lookup(first_curve, 0.18f)) ||
        !(tone::lookup(first_curve, 0.18f) <
          tone::lookup(first_curve, 8.0f)) ||
        tone::curve_position(std::exp2(tone::maximum_log_luminance)) !=
            float(tone::histogram_bin_count) ||
        tone::lookup(first_curve, 0.0f) != 0.0f ||
        !(dark_corridor_low >= 0.0f) ||
        !(dark_corridor_low < dark_corridor_middle) ||
        !(dark_corridor_middle < dark_corridor_high) ||
        !(second_curve.adapted_luminance > 0.0f) ||
        !std::isfinite(recovered_curve.log_curve[32]) ||
        !near(tone::knee(tone::knee_start), tone::knee_start) ||
        !near(tone::knee(tone::white_point), 1.0f) ||
        tone::display_dynamic_range_stops != 7.0f ||
        tone::exposure_bias_stops != -1.0f ||
        tone::noise_floor_stops != -12.0f ||
        tone::noise_floor_blend != 0.5f ||
        tone::reinhard_blend != 0.5f ||
        tone::slope_blur_sigma != 12.0f ||
        tone::knee_start != 0.6f || tone::white_point != 10.0f) {
        return fail("post-RR adaptive tone-mapping contract changed");
    }
    const float dark_bloom_weight = post::bloom_extraction_weight(0.001f);
    const float bright_bloom_weight = post::bloom_extraction_weight(2.0f);
    const float half_code = 0.5f * post::sdr_quantization_step;
    const float minimum_blue_noise = 0.5f / 256.0f;
    const float maximum_blue_noise = 255.5f / 256.0f;
    if (post::bloom_extraction_weight(0.0f) != 0.0f ||
        post::bloom_extraction_weight(
            std::numeric_limits<float>::infinity()) != 0.0f ||
        !(dark_bloom_weight > 0.0f && dark_bloom_weight < 0.05f) ||
        !(bright_bloom_weight > 0.98f && bright_bloom_weight < 1.0f) ||
        !near(post::dither_sdr(0.5f, 0.5f), 0.5f) ||
        !(std::abs(post::dither_sdr(
              0.5f, minimum_blue_noise) - 0.5f) < half_code) ||
        !(std::abs(post::dither_sdr(
              0.5f, maximum_blue_noise) - 0.5f) < half_code) ||
        post::dither_sdr(0.0f, 0.0f) != 0.0f ||
        post::dither_sdr(1.0f, 1.0f) != 1.0f) {
        return fail("linear-HDR bloom or SDR dithering contract changed");
    }
    if (!near(post::hdr_scene_scale(800.0f), 10.0f) ||
        !near(post::hdr_scene_scale(1000.0f), 12.5f) ||
        post::hdr_scene_scale(
            std::numeric_limits<float>::infinity()) != 0.0f ||
        post::hdr_sc_rgb_luminance(0.0f, 800.0f) != 0.0f ||
        !near(post::hdr_sc_rgb_luminance(1.0f, 800.0f), 10.0f) ||
        !near(post::hdr_sc_rgb_luminance(1.5f, 800.0f), 15.0f) ||
        post::hdr_sc_rgb_luminance(
            std::numeric_limits<float>::infinity(), 800.0f) != 0.0f ||
        !near(post::hdr_saturation_channel(1.0f, 0.5f, 0.0f), 0.5f) ||
        !near(post::hdr_saturation_channel(1.0f, 0.5f, 1.0f), 1.0f) ||
        !near(post::hdr_saturation_channel(1.0f, 0.5f, 2.0f), 1.5f) ||
        post::hdr_saturation_channel(0.0f, 0.5f, 2.0f) != 0.0f) {
        return fail("Q2RTX-compatible scRGB scale or HDR saturation changed");
    }
    if (!near(indirect::guide_weight(100.0f, 100.0f, 1.0f), 1.0f) ||
        !near(indirect::guide_weight(100.0f, 105.0f, 0.75f), 0.125f) ||
        indirect::guide_weight(100.0f, 110.0f, 1.0f) > 1.0e-5f ||
        indirect::guide_weight(100.0f, 100.0f, 0.5f) != 0.0f ||
        indirect::guide_weight(0.0f, 100.0f, 1.0f) != 0.0f) {
        return fail("low-frequency indirect guide weighting changed");
    }
    const std::array<float, 4> temporal_weights =
        indirect::temporal_bilinear_weights(0.25f, 0.75f);
    const float temporal_weight_sum = temporal_weights[0] +
        temporal_weights[1] + temporal_weights[2] + temporal_weights[3];
    const indirect::TemporalBlend stable_history =
        indirect::temporal_blend(20.0f, 4.0f, 0.0f, 32u);
    const indirect::TemporalBlend changed_history =
        indirect::temporal_blend(100.0f, 4.0f, 1.0f, 32u);
    const indirect::TemporalBlend capped_history =
        indirect::temporal_blend(300.0f, 4.0f, 0.0f, 32u);
    const indirect::TemporalBlend disabled_history =
        indirect::temporal_blend(20.0f, 4.0f, 1.0f, 0u);
    const indirect::TemporalBlend eighth_four_ray_frame =
        indirect::temporal_blend(28.0f, 4.0f, 0.0f, 32u);
    const indirect::GradientConfirmation first_gradient =
        indirect::confirm_gradient(0.0f, 1.0f);
    const indirect::GradientConfirmation second_gradient =
        indirect::confirm_gradient(first_gradient.confidence, 1.0f);
    const indirect::GradientConfirmation third_gradient =
        indirect::confirm_gradient(second_gradient.confidence, 1.0f);
    const indirect::GradientConfirmation alternating_gradient =
        indirect::confirm_gradient(first_gradient.confidence, -1.0f);
    const auto adaptive_samples = [](float history, float confidence,
                                     uint32_t limit, bool valid,
                                     indirect::Mode mode =
                                         indirect::Mode::full,
                                     bool scheduled = true) {
        return indirect::adaptive_indirect_sample_count(
            4u, history, confidence, limit, valid, mode, scheduled);
    };
    const float expected_changed_length =
        100.0f * std::pow(0.8f, 10.0f) + 4.0f;
    const float expected_changed_weight =
        4.0f / expected_changed_length * 0.8f + 0.2f;
    if (!near(temporal_weights[0], 0.1875f) ||
        !near(temporal_weights[1], 0.0625f) ||
        !near(temporal_weights[2], 0.5625f) ||
        !near(temporal_weights[3], 0.1875f) ||
        !near(temporal_weight_sum, 1.0f) ||
        !near(indirect::relative_luminance_gradient(4.0f, 2.0f), 0.25f) ||
        indirect::relative_luminance_gradient(0.0f, 0.0f) != 0.0f ||
        !near(stable_history.history_length, 24.0f) ||
        !near(stable_history.current_weight, 1.0f / 6.0f) ||
        stable_history.antilag != 0.0f ||
        !near(changed_history.history_length, expected_changed_length) ||
        !near(changed_history.current_weight, expected_changed_weight) ||
        !near(changed_history.antilag, 0.2f) ||
        !near(capped_history.history_length, 32.0f) ||
        !near(capped_history.current_weight, 0.125f) ||
        disabled_history.history_length != 4.0f ||
        disabled_history.current_weight != 1.0f ||
        !near(eighth_four_ray_frame.history_length, 32.0f) ||
        !near(eighth_four_ray_frame.current_weight, 0.125f) ||
        !near(first_gradient.confidence, 0.25f) ||
        first_gradient.gradient != 0.0f ||
        !near(second_gradient.confidence, 0.4375f) ||
        !near(second_gradient.gradient, 0.0625f) ||
        !near(third_gradient.confidence, 0.578125f) ||
        !near(third_gradient.gradient, 0.296875f) ||
        !near(alternating_gradient.confidence, -0.0625f) ||
        alternating_gradient.gradient != 0.0f ||
        adaptive_samples(0.0f, 0.0f, 32u, false) != 4u ||
        adaptive_samples(28.0f, 0.0f, 32u, true) != 4u ||
        adaptive_samples(31.49f, 0.0f, 32u, true) != 4u ||
        adaptive_samples(31.5f, 0.0f, 32u, true) != 1u ||
        adaptive_samples(31.5f, 0.0f, 32u, true,
                         indirect::Mode::full, false) != 0u ||
        adaptive_samples(32.0f, 0.39f, 32u, true) != 1u ||
        adaptive_samples(32.0f, 0.4f, 32u, true) != 4u ||
        adaptive_samples(32.0f, 0.0f, 0u, true) != 4u ||
        adaptive_samples(32.0f, 0.0f, 32u, true,
                         indirect::Mode::raw) != 4u ||
        adaptive_samples(32.0f, 0.0f, 32u, true,
                         indirect::Mode::restir) != 4u ||
        indirect::adaptive_indirect_sample_count(
            1u, 0.0f, 0.0f, 32u, false,
            indirect::Mode::full, false) != 1u ||
        indirect::adaptive_indirect_sample_count(
            1u, 32.0f, 0.0f, 32u, true,
            indirect::Mode::full, false) != 0u ||
        !indirect::stable_indirect_sample_scheduled(0u, 0u, 0u) ||
        !indirect::stable_indirect_sample_scheduled(1u, 0u, 1u) ||
        !indirect::stable_indirect_sample_scheduled(0u, 1u, 2u) ||
        !indirect::stable_indirect_sample_scheduled(1u, 1u, 3u) ||
        indirect::stable_indirect_sample_scheduled(0u, 0u, 1u) ||
        adaptive_samples(std::numeric_limits<float>::quiet_NaN(),
                         0.0f, 32u, true) != 4u) {
        return fail(
            "low-frequency temporal/adaptive sampling contract changed");
    }
    if (indirect::kernel_weight(-2) != 0.0f ||
        indirect::kernel_weight(-1) != 0.5f ||
        indirect::kernel_weight(0) != 1.0f ||
        indirect::kernel_weight(1) != 0.5f ||
        indirect::kernel_weight(2) != 0.0f) {
        return fail("low-frequency indirect filter kernel changed");
    }
    if (!near(indirect::deflicker_scale(4.0f, 8.0f, 8u), 0.5f) ||
        indirect::deflicker_scale(1.0f, 8.0f, 8u) != 1.0f ||
        indirect::deflicker_scale(1.0f, 0.0f, 8u) != 0.0f ||
        indirect::deflicker_scale(1.0f, 0.0f, 0u) != 1.0f ||
        indirect::deflicker_scale(0.0f, 8.0f, 8u) != 1.0f) {
        return fail("low-frequency indirect deflicker contract changed");
    }
    const indirect::Color axial = indirect::project_signal(
        indirect::signal_from_radiance({1.0f, 0.5f, 0.25f},
                                       0.0f, 0.0f, 1.0f),
        0.0f, 0.0f, 1.0f);
    const indirect::Color tangent = indirect::project_signal(
        indirect::signal_from_radiance({1.0f, 0.5f, 0.25f},
                                       0.0f, 0.0f, 1.0f),
        1.0f, 0.0f, 0.0f);
    const indirect::Color black = indirect::project_signal(
        indirect::signal_from_radiance({0.0f, 0.0f, 0.0f},
                                       0.0f, 0.0f, 1.0f),
        0.0f, 0.0f, 1.0f);
    if (!near(axial.red, 1.5f, 2.0e-5f) ||
        !near(axial.green, 0.75f, 2.0e-5f) ||
        !near(axial.blue, 0.375f, 2.0e-5f) ||
        !near(tangent.red, 0.5f, 2.0e-5f) ||
        !near(tangent.green, 0.25f, 2.0e-5f) ||
        !near(tangent.blue, 0.125f, 2.0e-5f) ||
        black.red != 0.0f || black.green != 0.0f || black.blue != 0.0f) {
        return fail("directional low-frequency projection changed");
    }
    static_assert(grid::cell_count == 4096u);
    static_assert(grid::entry_count == 2097152u);
    static_assert(sizeof(grid::Entry) == 8u);

    const grid::Position grid_center = {100.0f, -50.0f, 25.0f};
    uint32_t center_cell = 0u;
    if (!grid::world_position_to_cell(grid_center, grid_center, center_cell) ||
        center_cell != 2184u) {
        return fail("ReGIR camera-centered cell mapping changed");
    }
    const grid::Position mapped_center =
        grid::cell_center(center_cell, grid_center);
    uint32_t round_trip_cell = 0u;
    if (!grid::world_position_to_cell(mapped_center, grid_center,
                                      round_trip_cell) ||
        round_trip_cell != center_cell) {
        return fail("ReGIR cell center did not round-trip through the grid");
    }
    const float half_grid = grid::grid_extent * 0.5f;
    const grid::Position minimum = {
        grid_center.x - half_grid,
        grid_center.y - half_grid,
        grid_center.z - half_grid,
    };
    const grid::Position outside_minimum = {
        minimum.x - 0.01f, minimum.y, minimum.z,
    };
    const grid::Position outside_maximum = {
        grid_center.x + half_grid, grid_center.y, grid_center.z,
    };
    uint32_t boundary_cell = 0u;
    if (!grid::world_position_to_cell(minimum, grid_center, boundary_cell) ||
        boundary_cell != 0u ||
        grid::world_position_to_cell(outside_minimum, grid_center,
                                     boundary_cell) ||
        grid::world_position_to_cell(outside_maximum, grid_center,
                                     boundary_cell)) {
        return fail("ReGIR grid boundary mapping is not half-open");
    }
    const float near_target = grid::volume_target(
        0.25f, 1.0f / 128.0f, grid_center, grid_center);
    const float far_target = grid::volume_target(
        0.25f, 1.0f / 128.0f,
        {grid_center.x + 2048.0f, grid_center.y, grid_center.z}, grid_center);
    if (!std::isfinite(near_target) || !(near_target > far_target) ||
        !near(grid::volume_target(0.5f, 1.0f / 128.0f,
                                 grid_center, grid_center),
              near_target * 2.0f) ||
        grid::volume_target(0.0f, 1.0f / 128.0f,
                            grid_center, grid_center) != 0.0f ||
        grid::volume_target(0.25f, 0.0f,
                            grid_center, grid_center) != 0.0f) {
        return fail("ReGIR volume target is not finite importance sampling");
    }
    if (!near(grid::finalize_inverse_selection_probability(8.0f, 2.0f, 4u),
              1.0f) ||
        grid::finalize_inverse_selection_probability(8.0f, 0.0f, 4u) != 0.0f ||
        grid::finalize_inverse_selection_probability(8.0f, 2.0f, 0u) != 0.0f) {
        return fail("ReGIR RIS inverse-proposal correction changed");
    }
    if (!near(grid::local_solid_angle_pdf(0.25f, 0.125f, 2.0f), 1.0f) ||
        !near(grid::local_solid_angle_pdf(0.25f, 0.125f, 8.0f), 0.25f) ||
        grid::local_solid_angle_pdf(0.25f, 0.0f, 2.0f) != 0.0f ||
        grid::local_solid_angle_pdf(0.25f, 0.125f, 0.0f) != 0.0f) {
        return fail("ReGIR local polygon-light PDF correction changed");
    }

    struct EmitterIdentity {
        uint32_t first_vertex;
        float selection_probability;
        float inverse_area;
        float alias_threshold;
        uint32_t alias_index;
    };
    const std::vector<EmitterIdentity> emitter_layout = {
        {0u, 0.25f, 2.0f, 0.5f, 1u},
        {3u, 0.75f, 4.0f, 1.0f, 1u},
    };
    std::vector<EmitterIdentity> changed_layout = emitter_layout;
    changed_layout[0].alias_threshold = 0.75f;
    changed_layout[0].alias_index = 0u;
    if (!ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("alias representation incorrectly invalidated emitter history");
    }
    changed_layout[1].first_vertex = 6u;
    if (ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("emitter identity change retained incompatible history");
    }
    changed_layout = emitter_layout;
    changed_layout[1].selection_probability = 0.5f;
    if (!ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("proposal-only change incorrectly invalidated emitter history");
    }
    changed_layout[1].inverse_area = 8.0f;
    if (ab3d2::dxr::emitter_history_layout_compatible<EmitterIdentity>(
            emitter_layout, changed_layout)) {
        return fail("emitter area change retained incompatible history");
    }

    const InitialReservoirDomain one_candidate =
        collapse_initial_reservoir_domain(8.0f, 1u);
    const InitialReservoirDomain four_candidates =
        collapse_initial_reservoir_domain(8.0f, 4u);
    const InitialReservoirDomain no_candidates =
        collapse_initial_reservoir_domain(8.0f, 0u);
    if (!near(one_candidate.weight_sum, 8.0f) ||
        one_candidate.sample_count != 1u ||
        !near(four_candidates.weight_sum, 2.0f) ||
        four_candidates.sample_count != 1u ||
        no_candidates.weight_sum != 0.0f ||
        no_candidates.sample_count != 0u) {
        return fail("initial candidates changed temporal reservoir ownership");
    }

    if (!near(direct_mixture_pdf(0.25f, 16u, 0.0f, 1u, 0.0f, 1u),
              4.0f / 18.0f) ||
        !near(direct_mixture_pdf(0.0f, 16u, 0.125f, 1u, 0.5f, 1u),
              0.625f / 18.0f) ||
        !near(direct_mixture_pdf(0.25f, 2u, 0.0f, 1u, 0.0f, 1u),
              0.5f / 4.0f) ||
        direct_mixture_pdf(1.0f, 0u, 1.0f, 0u, 1.0f, 0u) != 0.0f ||
        !near(finalize_initial_direct_weight(12.0f, 4u, 2.0f), 1.5f) ||
        finalize_initial_direct_weight(12.0f, 0u, 2.0f) != 0.0f ||
        finalize_initial_direct_weight(12.0f, 4u, 0.0f) != 0.0f) {
        return fail("heterogeneous direct-light mixture normalization changed");
    }

    if (!near(finalize_basic_reservoir_weight(8.0f, 2.0f, 2.0f, 8.0f),
              1.0f) ||
        !near(finalize_basic_reservoir_weight(30.0f, 2.0f, 2.0f, 16.0f),
              1.875f) ||
        !near(finalize_basic_reservoir_weight(30.0f, 2.0f, 1.0f, 16.0f),
              0.9375f) ||
        !near(finalize_basic_reservoir_weight(50.0f, 2.0f, 3.0f, 28.0f),
              2.6785714f) ||
        finalize_basic_reservoir_weight(30.0f, 0.0f, 1.0f, 16.0f) != 0.0f ||
        finalize_basic_reservoir_weight(30.0f, 2.0f, 0.0f, 16.0f) != 0.0f) {
        return fail("basic reservoir normalization is not surface-aware");
    }

    const PixelJitter first_jitter = frame_jitter(0u);
    const PixelJitter second_jitter = frame_jitter(1u);
    const PixelJitter last_phase_jitter = frame_jitter(jitter_phase_count - 1u);
    if (!near(first_jitter.x, 0.0f) ||
        !near(first_jitter.y, -1.0f / 6.0f) ||
        !near(second_jitter.x, -0.25f) ||
        !near(second_jitter.y, 1.0f / 6.0f) ||
        !near(last_phase_jitter.x, -0.484375f) ||
        !near(last_phase_jitter.y, 0.29012346f)) {
        return fail("frame jitter sequence is not deterministic");
    }

    /* Ray Reconstruction only reaches a fixed point if the sub-pixel offsets
     * repeat, so the sequence must have exactly `jitter_phase_count` phases and
     * every offset must stay inside the pixel. */
    for (uint32_t phase = 0u; phase < jitter_phase_count; ++phase) {
        const PixelJitter phase_jitter = frame_jitter(phase);
        const PixelJitter wrapped_jitter =
            frame_jitter(phase + jitter_phase_count * 7u);
        if (!near(phase_jitter.x, wrapped_jitter.x) ||
            !near(phase_jitter.y, wrapped_jitter.y)) {
            return fail("frame jitter did not repeat with the phase period");
        }
        if (phase_jitter.x < -0.5f || phase_jitter.x > 0.5f ||
            phase_jitter.y < -0.5f || phase_jitter.y > 0.5f) {
            return fail("frame jitter left the pixel footprint");
        }
        for (uint32_t earlier = 0u; earlier < phase; ++earlier) {
            const PixelJitter other = frame_jitter(earlier);
            if (near(phase_jitter.x, other.x) &&
                near(phase_jitter.y, other.y)) {
                return fail("frame jitter phases are not distinct");
            }
        }
    }

    const Vec3 dielectric = specular_albedo({0.04f, 0.04f, 0.04f}, 0.5f,
                                             1.0f);
    const Vec3 metal = specular_albedo({0.8f, 0.2f, 0.1f}, 0.25f, 0.5f);
    if (!near(dielectric, {0.0354616f, 0.0354616f, 0.0354616f}, 1.0e-5f) ||
        !near(metal, {0.784531f, 0.221111f, 0.127208f}, 1.0e-5f) ||
        !near(specular_albedo({0.0f, 0.0f, 0.0f}, 0.5f, 0.5f),
              {0.0f, 0.0f, 0.0f})) {
        return fail("NVIDIA specular-albedo approximation changed");
    }
    const Vec3 packed_f0 = {0.04f, 0.5f, 1.0f};
    const Vec3 unpacked_f0 = unpack_surface_f0(pack_surface_f0(packed_f0));
    if (!near(unpacked_f0, packed_f0, 0.5f / 1023.0f + 1.0e-6f) ||
        (pack_surface_f0({1.0f, 1.0f, 1.0f}) & 0xc0000000u) != 0u) {
        return fail("packed primary F0 contract changed");
    }

    const CameraProjection current = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        1.0f,
        2.0f,
        200u,
        100u,
    };
    const PixelPosition center = project_world(current, {0.0f, 0.0f, 10.0f});
    const PixelPosition right = project_world(current, {10.0f, 0.0f, 10.0f});
    if (!center.valid || !right.valid || !near(center.x, 100.0f) ||
        !near(center.y, 50.0f) || !near(right.x, 150.0f) ||
        !near(right.y, 50.0f) ||
        project_world(current, {0.0f, 0.0f, -1.0f}).valid) {
        return fail("world-to-pixel projection is invalid");
    }

    CameraProjection previous_camera = current;
    previous_camera.position.x = -1.0f;
    const PixelPosition camera_motion = scene_motion(
        current, previous_camera, {0.0f, 0.0f, 10.0f},
        {0.0f, 0.0f, 10.0f});
    const PixelPosition object_motion = scene_motion(
        current, current, {1.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 10.0f});
    if (!camera_motion.valid || !near(camera_motion.x, 5.0f) ||
        !near(camera_motion.y, 0.0f) || !object_motion.valid ||
        !near(object_motion.x, -5.0f) || !near(object_motion.y, 0.0f)) {
        return fail("motion is not previousPixel-currentPixel in pixel units");
    }

    /* Renderer-owned history is indexed in the previous frame's jittered pixel
     * grid. Scene motion intentionally excludes jitter for Streamline, so the
     * private reservoir and hit-distance reprojection must apply the phase
     * difference exactly once. This case crosses into the adjacent pixel and
     * would fetch stale history if the jitter terms were omitted. */
    const PixelPosition history_pixel = reproject_history_pixel(
        10u, 20u, {0.0f, 0.0f, true}, {0.49f, -0.49f},
        {-0.49f, 0.49f}, 200u, 100u);
    const PixelPosition moving_history_pixel = reproject_history_pixel(
        10u, 20u, {5.0f, -2.0f, true}, {0.25f, -0.25f},
        {-0.25f, 0.25f}, 200u, 100u);
    if (!history_pixel.valid || !near(history_pixel.x, 11.48f) ||
        !near(history_pixel.y, 19.52f) ||
        static_cast<uint32_t>(history_pixel.x) != 11u ||
        !moving_history_pixel.valid ||
        !near(moving_history_pixel.x, 16.0f) ||
        !near(moving_history_pixel.y, 18.0f) ||
        reproject_history_pixel(0u, 0u, {0.0f, 0.0f, false}, {}, {},
                                200u, 100u).valid) {
        return fail("history reprojection did not account for jitter phases");
    }

    CameraProjection previous_yaw = current;
    previous_yaw.forward = {1.0f, 0.0f, 0.0f};
    previous_yaw.right = {0.0f, 0.0f, -1.0f};
    const PixelPosition sky_current = project_direction(current,
                                                        {0.0f, 0.0f, 1.0f});
    const PixelPosition sky_previous = project_direction(previous_yaw,
                                                         {0.0f, 0.0f, 1.0f});
    if (!sky_current.valid || sky_previous.valid) {
        return fail("direction projection did not preserve sky rotation semantics");
    }
    return 0;
}
