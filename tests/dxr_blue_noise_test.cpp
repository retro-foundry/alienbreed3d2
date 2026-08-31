#include "renderer_dxr/dxr_blue_noise.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace {

using ab3d2::dxr::blue_noise::sample;

int fail(const char *message)
{
    std::fprintf(stderr, "DXR blue-noise test failed: %s\n", message);
    return 1;
}

double correlation(const std::vector<double> &first,
                   const std::vector<double> &second)
{
    double first_mean = 0.0;
    double second_mean = 0.0;
    for (size_t index = 0; index < first.size(); ++index) {
        first_mean += first[index];
        second_mean += second[index];
    }
    first_mean /= static_cast<double>(first.size());
    second_mean /= static_cast<double>(second.size());
    double covariance = 0.0;
    double first_variance = 0.0;
    double second_variance = 0.0;
    for (size_t index = 0; index < first.size(); ++index) {
        const double first_delta = first[index] - first_mean;
        const double second_delta = second[index] - second_mean;
        covariance += first_delta * second_delta;
        first_variance += first_delta * first_delta;
        second_variance += second_delta * second_delta;
    }
    return covariance / std::sqrt(first_variance * second_variance);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        return fail("expected the sampler package path");
    }
    std::ifstream stream(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    const std::span<const uint8_t> package(bytes);
    if (stream.bad() || !ab3d2::dxr::blue_noise::valid_package(package)) {
        return fail("sampler package size or read is invalid");
    }

    std::array<bool, 256> observed = {};
    for (uint32_t frame = 0; frame < 256u; ++frame) {
        const float value = sample(package, 23u, 19u, frame, 0u);
        if (!(value > 0.0f && value < 1.0f)) {
            return fail("sample is outside the open unit interval");
        }
        const uint32_t quantized = static_cast<uint32_t>(value * 256.0f);
        if (quantized >= observed.size() || observed[quantized]) {
            return fail("per-pixel Sobol sequence is not a 256-value permutation");
        }
        observed[quantized] = true;
    }
    std::array<bool, 256> temporal_block_values = {};
    for (uint32_t frame = 0u; frame < 64u; ++frame) {
        for (uint32_t ordinal = 0u; ordinal < 4u; ++ordinal) {
            const uint32_t sample_index = frame * 4u + ordinal;
            const float value = sample(
                package, 23u, 19u, sample_index, 6u);
            const uint32_t quantized = static_cast<uint32_t>(value * 256.0f);
            if (quantized >= temporal_block_values.size() ||
                temporal_block_values[quantized]) {
                return fail("64-frame GI diagnostic repeats a direction sample");
            }
            temporal_block_values[quantized] = true;
        }
    }
    std::vector<double> path_samples;
    std::vector<double> guide_samples;
    std::vector<double> next_cycle_samples;
    path_samples.reserve(128u * 128u);
    guide_samples.reserve(128u * 128u);
    next_cycle_samples.reserve(128u * 128u);
    for (uint32_t y = 0; y < 128u; ++y) {
        for (uint32_t x = 0; x < 128u; ++x) {
            path_samples.push_back(sample(package, x, y, 0u, 0u));
            guide_samples.push_back(sample(package, x, y, 0u, 24u));
            next_cycle_samples.push_back(sample(package, x, y, 256u, 0u));
        }
    }
    if (std::fabs(correlation(path_samples, guide_samples)) > 0.03) {
        return fail("path and specular-guide dimensions are correlated");
    }
    if (std::fabs(correlation(path_samples, next_cycle_samples)) > 0.03) {
        return fail("the 256-sample screen-space pattern repeats");
    }

    if (std::fabs(sample(package, 1u, 2u, 3u, 4u) -
                  0.337890625f) > 1.0e-8f) {
        return fail("pinned sampler data or addressing changed");
    }
    return 0;
}
