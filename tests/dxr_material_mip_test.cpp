#include "renderer_dxr/dxr_material_mip.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<uint8_t> solid_row(
    std::initializer_list<uint8_t> red_values)
{
    std::vector<uint8_t> pixels;
    for (uint8_t red : red_values) {
        pixels.insert(pixels.end(), {red, red, red, 255u});
    }
    return pixels;
}

}  // namespace

int main()
{
    using namespace ab3d2::dxr::material_mip;

    expect(level_count(4u, 4u) == 3u,
           "4x4 has level-zero, 2x2, and 1x1");
    expect(level_y_offset(4u, 0u) == 0u &&
               level_y_offset(4u, 1u) == 4u &&
               level_y_offset(4u, 2u) == 6u,
           "software levels are stacked directly below level zero");
    expect(packed_height(4u, 4u) == 7u,
           "4x4 pyramid occupies seven atlas rows");
    expect(level_count(256u, 256u) == 9u &&
               packed_height(256u, 256u) == 511u,
           "256x256 floor/ceiling tile produces its complete packed pyramid");
    expect(level_count(3u, 5u) == 3u && packed_height(3u, 5u) == 8u,
           "non-power-of-two chains terminate and pack correctly");
    expect(wrapped_level_y_offset(4u, 0u) == 0u &&
               wrapped_level_y_offset(4u, 1u) == 6u &&
               wrapped_level_y_offset(4u, 2u) == 10u &&
               wrapped_packed_height(4u, 4u) == 13u,
           "wrapped 4x4 levels reserve one repeat texel on every edge");
    expect(wrapped_packed_height(256u, 256u) == 529u &&
               wrapped_packed_height(3u, 5u) == 14u,
           "wrapped atlas height covers power-of-two and uneven chains");

    const FilterFootprint isotropic = filter_footprint(
        4.0f, 4.0f, 1.0f, 9u);
    expect(isotropic.sample_count == 1u &&
               std::abs(isotropic.mip_level - 2.0f) < 1.0e-6f,
           "isotropic minification selects one trilinear sample");
    const FilterFootprint grazing = filter_footprint(
        8.0f, 1.0f, 1.0f, 9u);
    expect(grazing.sample_count == maximum_filter_taps &&
               grazing.mip_level == 0.0f,
           "grazing footprint preserves its narrow-axis detail");
    const FilterFootprint bounded = filter_footprint(
        32.0f, 1.0f, 1.0f, 9u);
    expect(bounded.sample_count == maximum_filter_taps &&
               std::abs(bounded.mip_level - 2.0f) < 1.0e-6f,
           "extreme anisotropy is bounded and raises per-tap LOD");
    const FilterFootprint reconstructed = filter_footprint(
        1.5f, 1.5f, 2.0f / 3.0f, 9u);
    expect(reconstructed.sample_count == 1u &&
               reconstructed.mip_level == 0.0f,
           "RR selects detail for the displayed rather than input pixel");

    Levels levels;
    std::string error;
    expect(generate(Semantic::linear, solid_row({0u, 10u, 20u}),
                    3u, 1u, levels, error),
           "linear non-power-of-two row generates");
    expect(levels.size() == 2u && levels[1].size() == 4u &&
               levels[1][0] == 10u,
           "3-to-1 reduction includes every source texel");

    std::vector<uint8_t> checker = solid_row({0u, 255u, 0u, 255u});
    expect(generate(Semantic::srgb, checker, 2u, 2u, levels, error),
           "sRGB checker mip generates");
    expect(levels.size() == 2u && levels[1][0] >= 187u &&
               levels[1][0] <= 188u,
           "sRGB colors average in linear light rather than encoded space");

    const std::vector<uint8_t> normals = {
        255u, 128u, 128u, 255u,
        128u, 128u, 255u, 255u,
    };
    expect(generate(Semantic::normal, normals, 2u, 1u, levels, error),
           "normal-map mip generates");
    expect(levels.size() == 2u && levels[1][0] >= 217u &&
               levels[1][0] <= 219u && levels[1][1] >= 127u &&
               levels[1][1] <= 129u && levels[1][2] >= 217u &&
               levels[1][2] <= 219u,
           "averaged tangent normals are renormalized");

    expect(!generate(Semantic::linear, std::vector<uint8_t>(3u),
                     1u, 1u, levels, error) && !error.empty(),
           "invalid RGBA8 payload is rejected");

    if (failures != 0) {
        std::cerr << failures << " material-mip test(s) failed\n";
        return 1;
    }
    std::cout << "DXR material mip tests passed\n";
    return 0;
}
