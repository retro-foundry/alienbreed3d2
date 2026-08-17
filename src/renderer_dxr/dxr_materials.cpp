#include "dxr_materials.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace ab3d2::dxr {

namespace {

constexpr uint8_t runtime_magic[8] = {'A', 'B', '3', 'P', 'B', 'R', '2', 0};
constexpr uint32_t runtime_version = 2u;
constexpr uint32_t runtime_source_none = 0u;
constexpr uint32_t runtime_source_shared_wall = 1u;
constexpr uint32_t runtime_source_shared_floor = 2u;
constexpr uint32_t runtime_emissive_none = 0u;
constexpr uint32_t runtime_emissive_texture = 1u;
constexpr size_t runtime_header_size = 24u;
constexpr size_t runtime_record_size = 40u;
constexpr uint32_t runtime_material_limit = 4096u;
constexpr uint32_t runtime_image_extent_limit = 8192u;
constexpr uint64_t runtime_file_size_limit = UINT64_C(512) * 1024u * 1024u;

uint32_t read_u32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

float read_float(const uint8_t *bytes)
{
    const uint32_t bits = read_u32(bytes);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string path_text(const std::filesystem::path &path)
{
    return path.string().empty() ? "<unprintable material path>" : path.string();
}

bool add_size(size_t &value, size_t addition)
{
    if (value > std::numeric_limits<size_t>::max() - addition) {
        return false;
    }
    value += addition;
    return true;
}

}  // namespace

bool DxrMaterialLibrary::load(const std::filesystem::path &path,
                              std::string &error)
{
    definitions_.clear();
    bindings_.clear();
    loaded_ = false;

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "DXR PBR material package is unavailable: " + path_text(path);
        return false;
    }
    const std::streamoff end = stream.tellg();
    if (end < static_cast<std::streamoff>(runtime_header_size) ||
        static_cast<uint64_t>(end) > runtime_file_size_limit) {
        error = "DXR PBR material package has an invalid size: " + path_text(path);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), end)) {
        error = "DXR PBR material package could not be read completely: " +
            path_text(path);
        return false;
    }
    if (std::memcmp(bytes.data(), runtime_magic, sizeof(runtime_magic)) != 0 ||
        read_u32(bytes.data() + 8u) != runtime_version) {
        error = "DXR PBR material package has an unsupported format";
        return false;
    }
    const uint32_t material_count = read_u32(bytes.data() + 12u);
    const uint32_t channel_count = read_u32(bytes.data() + 16u);
    const uint32_t record_size = read_u32(bytes.data() + 20u);
    if (material_count == 0u || material_count > runtime_material_limit ||
        channel_count != static_cast<uint32_t>(DxrMaterialChannel::count) ||
        record_size != runtime_record_size ||
        static_cast<size_t>(material_count) >
            (bytes.size() - runtime_header_size) / runtime_record_size) {
        error = "DXR PBR material package header is invalid";
        return false;
    }

    size_t pixel_offset = runtime_header_size +
        static_cast<size_t>(material_count) * runtime_record_size;
    definitions_.reserve(material_count);
    for (uint32_t index = 0; index < material_count; ++index) {
        const uint8_t *record = bytes.data() + runtime_header_size +
            static_cast<size_t>(index) * runtime_record_size;
        const uint32_t source_kind = read_u32(record + 0u);
        const uint32_t source_asset_id = read_u32(record + 4u);
        const uint32_t width = read_u32(record + 8u);
        const uint32_t height = read_u32(record + 12u);
        const float normal_strength = read_float(record + 16u);
        const float emissive_factor[3] = {
            read_float(record + 20u),
            read_float(record + 24u),
            read_float(record + 28u),
        };
        const uint32_t emissive_source = read_u32(record + 32u);
        const uint32_t reserved = read_u32(record + 36u);
        if (width == 0u || height == 0u || width > runtime_image_extent_limit ||
            height > runtime_image_extent_limit ||
            !std::isfinite(normal_strength) || normal_strength <= 0.0f ||
            reserved != 0u ||
            (emissive_source != runtime_emissive_none &&
             emissive_source != runtime_emissive_texture)) {
            error = "DXR PBR material package contains an invalid material record";
            return false;
        }
        bool has_emission = false;
        for (float value : emissive_factor) {
            if (!std::isfinite(value) || value < 0.0f) {
                error = "DXR PBR material package contains invalid emissive radiance";
                return false;
            }
            has_emission = has_emission || value > 0.0f;
        }
        if ((emissive_source == runtime_emissive_none && has_emission) ||
            (emissive_source == runtime_emissive_texture && !has_emission)) {
            error = "DXR PBR material package emissive source and radiance disagree";
            return false;
        }
        const uint64_t channel_bytes64 =
            static_cast<uint64_t>(width) * height * 4u;
        if (channel_bytes64 > std::numeric_limits<size_t>::max()) {
            error = "DXR PBR material image is too large";
            return false;
        }
        const size_t channel_bytes = static_cast<size_t>(channel_bytes64);
        size_t material_end = pixel_offset;
        if (!add_size(material_end, channel_bytes * channel_count) ||
            material_end > bytes.size()) {
            error = "DXR PBR material package pixel data is truncated";
            return false;
        }

        DxrMaterialDefinition definition;
        definition.width = width;
        definition.height = height;
        definition.normal_strength = normal_strength;
        std::memcpy(definition.emissive_factor, emissive_factor,
                    sizeof(definition.emissive_factor));
        for (size_t channel = 0;
             channel < static_cast<size_t>(DxrMaterialChannel::count); ++channel) {
            const uint8_t *begin = bytes.data() + pixel_offset + channel * channel_bytes;
            definition.pixels[channel].assign(begin, begin + channel_bytes);
        }
        pixel_offset = material_end;

        if (source_kind == runtime_source_shared_wall) {
            definition.source = SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
            definition.source_asset_id = source_asset_id;
            const auto key = std::make_pair(definition.source, source_asset_id);
            if (!bindings_.emplace(key, definitions_.size()).second) {
                error = "DXR PBR material package contains a duplicate source binding";
                return false;
            }
        } else if (source_kind == runtime_source_shared_floor) {
            definition.source = SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE;
            definition.source_asset_id = source_asset_id;
            const auto key = std::make_pair(definition.source, source_asset_id);
            if (!bindings_.emplace(key, definitions_.size()).second) {
                error = "DXR PBR material package contains a duplicate source binding";
                return false;
            }
        } else if (source_kind != runtime_source_none ||
                   source_asset_id != UINT32_MAX) {
            error = "DXR PBR material package contains an unsupported source binding";
            return false;
        }
        definitions_.push_back(std::move(definition));
    }
    if (pixel_offset != bytes.size()) {
        error = "DXR PBR material package has trailing pixel data";
        return false;
    }
    loaded_ = true;
    return true;
}

bool DxrMaterialLibrary::load_from_executable(std::string &error)
{
    std::wstring executable_path(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
    if (length == 0u || length >= executable_path.size()) {
        error = "GetModuleFileNameW failed while locating DXR PBR materials";
        return false;
    }
    executable_path.resize(length);
    const std::filesystem::path package =
        std::filesystem::path(executable_path).parent_path() /
        L"renderer_dxr" / L"materials" / L"material_runtime.bin";
    return load(package, error);
}

const DxrMaterialDefinition *DxrMaterialLibrary::find(
    SceneMaterialSource source, uint32_t source_asset_id) const
{
    const auto found = bindings_.find(std::make_pair(source, source_asset_id));
    return found == bindings_.end() ? nullptr : &definitions_[found->second];
}

}  // namespace ab3d2::dxr
