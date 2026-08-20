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
    if (library.size() != 978u) {
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
        weapon->width != 3u || weapon->height != 64u ||
        std::fabs(weapon->specular_factor - 0.35f) > 0.0001f) {
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
    std::vector<ab3d2::dxr::DxrBitmapMaterialBinding> enemy_frames;
    const size_t before_enemy_frames = library.resident_size();
    if (!library.resolve_bitmap_asset_mode(0u, 0u, enemy_frames, error) ||
        enemy_frames.empty()) {
        std::fprintf(stderr, "bitmap animation-frame enumeration failed\n");
        return 1;
    }
    bool found_enemy_frame_zero = false;
    for (const auto &binding : enemy_frames) {
        found_enemy_frame_zero = found_enemy_frame_zero ||
            (binding.frame_index == 0u && binding.definition == enemy);
        if (binding.source_asset_id != 0u || binding.source_mode != 0u ||
            !binding.definition) {
            std::fprintf(stderr, "bitmap animation enumeration crossed a mode\n");
            return 1;
        }
    }
    if (!found_enemy_frame_zero ||
        library.resident_size() != before_enemy_frames + enemy_frames.size() - 1u) {
        std::fprintf(stderr, "bitmap animation enumeration decoded the wrong set\n");
        return 1;
    }
    const size_t resident_size = library.resident_size();
    if (!library.resolve(SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE, 6u,
                         lights, error) ||
        library.resident_size() != resident_size) {
        std::fprintf(stderr, "resident material was decoded more than once\n");
        return 1;
    }
    /*
     * Object 21's glare animation steps asset eight through frames 14 to 19,
     * and the whole run of 20 has to be bound. The exporter used to enter that
     * animation at ODefT_DefaultAnimLen_w instead of row zero, which bound
     * frame 19 alone; the renderer then failed outright on the first glare that
     * reached frame 15, because a missing binding is not something a world
     * billboard can fall back from.
     */
    std::vector<ab3d2::dxr::DxrBitmapMaterialBinding> glare_frames;
    if (!library.resolve_bitmap_asset_mode(8u, 7u, glare_frames, error)) {
        std::fprintf(stderr, "glare animation-frame enumeration failed\n");
        return 1;
    }
    uint32_t glare_frame_mask = 0u;
    for (const auto &binding : glare_frames) {
        if (binding.source_asset_id != 8u || binding.source_mode != 7u ||
            binding.frame_index >= 32u || !binding.definition) {
            std::fprintf(stderr, "glare animation enumeration crossed a mode\n");
            return 1;
        }
        glare_frame_mask |= 1u << binding.frame_index;
    }
    const ab3d2::dxr::DxrMaterialDefinition *glare_frame_15 = nullptr;
    if (glare_frame_mask != 0x000fffffu ||
        !library.resolve_bitmap(8u, 15u, 7u, glare_frame_15, error) ||
        !glare_frame_15 ||
        glare_frame_15->name != "billboard_08_glare_frame_15_glare") {
        std::fprintf(stderr, "glare animation is missing a reachable frame\n");
        return 1;
    }
    return 0;
}
