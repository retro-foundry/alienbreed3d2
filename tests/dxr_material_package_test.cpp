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
    if (library.size() != 13u ||
        library.find(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 0u) != nullptr) {
        std::fprintf(stderr, "material package exposed unexpected bindings\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *lights =
        library.find(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 6u);
    if (!lights || lights->width == 0u || lights->height == 0u ||
        std::fabs(lights->normal_strength - 1.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[0] - 8.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[1] - 8.0f) > 0.0001f ||
        std::fabs(lights->emissive_factor[2] - 8.0f) > 0.0001f) {
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
    return 0;
}
