#ifndef AB3D2_DXR_MATERIALS_H
#define AB3D2_DXR_MATERIALS_H

#include "scene_frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
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
    SceneMaterialSource source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
    uint32_t source_asset_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float normal_strength = 1.0f;
    float emissive_factor[3] = {};
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> pixels;
};

class DxrMaterialLibrary final {
public:
    bool load(const std::filesystem::path &path, std::string &error);
    bool load_from_executable(std::string &error);

    const DxrMaterialDefinition *find(SceneMaterialSource source,
                                      uint32_t source_asset_id) const;
    size_t size() const { return definitions_.size(); }
    bool loaded() const { return loaded_; }

private:
    bool loaded_ = false;
    std::vector<DxrMaterialDefinition> definitions_;
    std::map<std::pair<SceneMaterialSource, uint32_t>, size_t> bindings_;
};

}  // namespace ab3d2::dxr

#endif
