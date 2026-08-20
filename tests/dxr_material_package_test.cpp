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
    if (library.size() != 973u) {
        std::fprintf(stderr, "material package exposed unexpected bindings\n");
        return 1;
    }
    if (library.resident_size() != 0u) {
        std::fprintf(stderr, "material package decoded PNGs eagerly\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *stone = nullptr;
    if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 0u,
                         stone, error) ||
        !stone || stone->name != "wall_00_stonewall") {
        std::fprintf(stderr, "source wall zero PBR binding is incomplete\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *lights = nullptr;
    if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 6u,
                         lights, error) ||
        !lights || lights->width == 0u || lights->height == 0u ||
        lights->name != "wall_06_technolights" ||
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
    const ab3d2::dxr::DxrMaterialDefinition *floor_light = nullptr;
    const ab3d2::dxr::DxrMaterialDefinition *floor_pbr = nullptr;
    if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE, 0x0101u,
                         floor_light, error) ||
        !library.resolve(SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE, 0x0201u,
                         floor_pbr, error) ||
        !floor_light || !floor_pbr ||
        std::fabs(floor_light->emissive_factor[0] - 200.0f) > 0.0001f ||
        floor_pbr->emissive_factor[0] != 0.0f) {
        std::fprintf(stderr, "source floor material bindings are incomplete\n");
        return 1;
    }
    for (uint32_t asset : {3u, 4u}) {
        const ab3d2::dxr::DxrMaterialDefinition *material = nullptr;
        if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, asset,
                             material, error) ||
            !material || material->emissive_factor[0] != 0.0f) {
            std::fprintf(stderr, "non-light wall material retained emission\n");
            return 1;
        }
    }
    const ab3d2::dxr::DxrMaterialDefinition *weapon = nullptr;
    if (!library.resolve_vector(3u, 0u, 0u, 2u, 0u, 63u, 0u,
                                weapon, error) ||
        !weapon || weapon->name != "weapon_03_blaster_material_000" ||
        weapon->width != 3u || weapon->height != 64u) {
        std::fprintf(stderr, "view-weapon vector PBR binding is incomplete\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *vector_glare = nullptr;
    if (!library.resolve_vector(0u, 65536u, 0u, 22u, 0u, 0u, 1u,
                                vector_glare, error) ||
        !vector_glare ||
        vector_glare->name != "vector_model_00_generator_material_008") {
        std::fprintf(stderr, "vector glare PBR identity is incomplete\n");
        return 1;
    }
    const ab3d2::dxr::DxrMaterialDefinition *non_glare = nullptr;
    error.clear();
    if (library.resolve_vector(0u, 65536u, 0u, 22u, 0u, 0u, 0u,
                               non_glare, error) ||
        non_glare) {
        std::fprintf(stderr, "vector glare accepted a non-glare binding\n");
        return 1;
    }
    error.clear();
    const ab3d2::dxr::DxrMaterialDefinition *enemy = nullptr;
    const ab3d2::dxr::DxrMaterialDefinition *glare = nullptr;
    if (!library.resolve_bitmap(0u, 0u, 0u, enemy, error) ||
        !library.resolve_bitmap(0u, 0u, 7u, glare, error) ||
        !enemy || !glare ||
        enemy->name != "billboard_00_alien2_frame_00_bitmap" ||
        glare->name != "billboard_00_alien2_frame_00_glare") {
        std::fprintf(stderr, "enemy/effect bitmap PBR bindings are incomplete\n");
        return 1;
    }
    if (library.resident_size() != 10u) {
        std::fprintf(stderr, "material package decoded unused PNGs\n");
        return 1;
    }
    const size_t resident_size = library.resident_size();
    if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 6u,
                         lights, error) ||
        library.resident_size() != resident_size) {
        std::fprintf(stderr, "resident material was decoded more than once\n");
        return 1;
    }
    return 0;
}
