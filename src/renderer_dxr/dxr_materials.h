#ifndef AB3D2_DXR_MATERIALS_H
#define AB3D2_DXR_MATERIALS_H

#include "scene_frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ab3d2::dxr {

enum class DxrMaterialChannel : size_t {
    base_color = 0,
    normal = 1,
    metalness = 2,
    roughness = 3,
    emissive = 4,
    count = 5,
};

struct DxrMaterialDefinition {
    std::string name;
    SceneMaterialSource source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    uint32_t source_asset_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float normal_strength = 1.0f;
    float specular_factor = 1.0f;
    float emissive_factor[3] = {};
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> pixels;
};

class DxrMaterialLibrary final {
public:
    bool load(const std::filesystem::path &path, std::string &error);
    bool load_from_executable(std::string &error);

    bool resolve(SceneMaterialSource source, uint32_t source_asset_id,
                 const DxrMaterialDefinition *&definition,
                 std::string &error);
    bool resolve_vector(
        uint32_t source_asset_id, uint32_t source_map_offset,
        uint8_t minimum_u, uint8_t maximum_u,
        uint8_t minimum_v, uint8_t maximum_v, uint8_t glare,
        const DxrMaterialDefinition *&definition, std::string &error);
    bool resolve_bitmap(
        uint32_t source_asset_id, uint32_t frame_index,
        uint32_t source_mode, const DxrMaterialDefinition *&definition,
        std::string &error);
    size_t size() const { return definitions_.size(); }
    size_t resident_size() const { return resident_size_; }
    bool loaded() const { return loaded_; }

private:
    struct ChannelPayload {
        uint64_t offset = 0;
        uint32_t size = 0;
    };

    bool resolve_index(size_t index,
                       const DxrMaterialDefinition *&definition,
                       std::string &error);

    bool loaded_ = false;
    size_t resident_size_ = 0;
    std::filesystem::path package_path_;
    std::vector<DxrMaterialDefinition> definitions_;
    std::vector<std::array<ChannelPayload,
                           static_cast<size_t>(DxrMaterialChannel::count)>>
        payloads_;
    std::map<std::pair<SceneMaterialSource, uint32_t>, size_t> bindings_;
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, uint8_t,
                        uint8_t, uint8_t, uint8_t>, size_t> vector_bindings_;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, size_t>
        bitmap_bindings_;
};

}  // namespace ab3d2::dxr

#endif
