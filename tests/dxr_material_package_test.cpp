#include "renderer_dxr/dxr_materials.h"

#include <cmath>
#include <cstdio>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: dxr_material_package_test <material_runtime.bin>\n");
        return 1;
    }
    ab3d2::dxr::DxrMaterialLibrary library;
    std::string error;
    if (!library.load(argv[1], error)) {
        std::fprintf(stderr, "material package load failed: %s\n", error.c_str());
        return 1;
    }
    if (library.size() != 14u ||
        library.find(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 0u) != nullptr) {
        std::fprintf(stderr, "material package exposed unexpected bindings\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *lights =
        library.find(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 6u);
    if (!lights || lights->width == 0u || lights->height == 0u ||
        std::fabs(lights->normal_strength - 1.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[0] - 200.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[1] - 200.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[2] - 200.0f) > 0.0001f) {
        std::fprintf(stderr, "technolights runtime material is incomplete\n");
        return 1;
    }
    const size_t expected_pixels =
        static_cast<size_t>(lights->width) * lights->height * 4u;
    for (const auto &channel : lights->pixels) {
        if (channel.size() != expected_pixels) {
            std::fprintf(stderr, "technolights channel extent mismatch\n");
            return 1;
        }
    }
    bool saw_dark = false;
    bool saw_light = false;
    const auto &emissive = lights->pixels[static_cast<size_t>(
        ab3d2::dxr::DxrMaterialChannel::emissive)];
    for (size_t offset = 0; offset < emissive.size(); offset += 4u) {
        const bool lit = emissive[offset] != 0u || emissive[offset + 1u] != 0u ||
            emissive[offset + 2u] != 0u;
        saw_light = saw_light || lit;
        saw_dark = saw_dark || !lit;
    }
    if (!saw_dark || !saw_light) {
        std::fprintf(stderr, "technolights does not contain an explicit light mask\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *floor_light =
        library.find(SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE, 0x0101u);
    const ab3d2::dxr::DxrMaterialDefinition *floor_pbr =
        library.find(SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE, 0x0201u);
    if (!floor_light || !floor_pbr ||
        std::fabs(floor_light->emissive_factor[0] - 200.0f) > 0.0001f ||
        floor_pbr->emissive_factor[0] != 0.0f) {
        std::fprintf(stderr, "source floor material bindings are incomplete\n");
        return 1;
    }
    for (uint32_t asset : {3u, 4u}) {
        const ab3d2::dxr::DxrMaterialDefinition *material =
            library.find(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, asset);
        if (!material || material->emissive_factor[0] != 0.0f) {
            std::fprintf(stderr, "non-light wall material retained emission\n");
            return 1;
        }
    }
    return 0;
}
