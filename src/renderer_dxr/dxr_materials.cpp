#include "dxr_materials.h"

#include <windows.h>

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>

namespace ab3d2::dxr {

namespace {

constexpr uint8_t runtime_magic[8] = {'A', 'B', '3', 'P', 'B', 'R', '3', 0};
constexpr uint32_t runtime_version = 3u;
constexpr uint32_t runtime_source_none = 0u;
constexpr uint32_t runtime_source_shared_wall = 1u;
constexpr uint32_t runtime_source_shared_floor = 2u;
constexpr uint32_t runtime_source_vector = 3u;
constexpr uint32_t runtime_source_bitmap = 4u;
constexpr uint32_t runtime_alpha_mask = 0x03u;
constexpr uint32_t runtime_alpha_opaque = 0u;
constexpr uint32_t runtime_alpha_tested = 1u;
constexpr uint32_t runtime_alpha_additive = 2u;
constexpr uint32_t runtime_flag_emissive_texture = 1u << 8u;
constexpr uint32_t runtime_flag_two_sided = 1u << 9u;
constexpr uint32_t runtime_flag_vector_glare = 1u << 10u;
constexpr uint32_t runtime_known_flags =
    runtime_alpha_mask | runtime_flag_emissive_texture |
    runtime_flag_two_sided | runtime_flag_vector_glare;
constexpr size_t runtime_header_size = 24u;
constexpr size_t runtime_record_size = 140u;
constexpr size_t runtime_name_size = 96u;
constexpr uint32_t runtime_material_limit = 8192u;
constexpr uint32_t runtime_image_extent_limit = 8192u;
constexpr uint64_t runtime_file_size_limit = UINT64_C(16) * 1024u * 1024u;

constexpr const char *channel_suffixes[] = {
    "_base_color.png",
    "_normal.png",
    "_metalness.png",
    "_roughness.png",
    "_emissive.png",
};

static_assert(std::size(channel_suffixes) ==
              static_cast<size_t>(DxrMaterialChannel::count));

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

bool valid_material_name(const std::string &name)
{
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_';
    });
}

bool load_channel_png(const std::filesystem::path &path,
                      uint32_t expected_width, uint32_t expected_height,
                      std::vector<uint8_t> &pixels, std::string &error)
{
    int width = 0;
    int height = 0;
    int source_channels = 0;
    const std::string narrow_path = path.string();
    stbi_uc *decoded = stbi_load(
        narrow_path.c_str(), &width, &height, &source_channels, 4);
    if (!decoded) {
        error = "DXR PBR PNG could not be decoded: " + path_text(path) +
            "; " + (stbi_failure_reason() ? stbi_failure_reason() :
                       "unknown PNG error");
        return false;
    }
    const uint64_t byte_count64 =
        static_cast<uint64_t>(expected_width) * expected_height * 4u;
    if (width <= 0 || height <= 0 ||
        static_cast<uint32_t>(width) != expected_width ||
        static_cast<uint32_t>(height) != expected_height ||
        byte_count64 > std::numeric_limits<size_t>::max()) {
        stbi_image_free(decoded);
        error = "DXR PBR PNG extent disagrees with its catalog record: " +
            path_text(path);
        return false;
    }
    const size_t byte_count = static_cast<size_t>(byte_count64);
    pixels.assign(decoded, decoded + byte_count);
    stbi_image_free(decoded);
    return true;
}

}  // namespace

bool DxrMaterialLibrary::load(const std::filesystem::path &path,
                              std::string &error)
{
    definitions_.clear();
    bindings_.clear();
    vector_bindings_.clear();
    bitmap_bindings_.clear();
    loaded_ = false;

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "DXR PBR material catalog is unavailable: " + path_text(path);
        return false;
    }
    const std::streamoff end = stream.tellg();
    if (end < static_cast<std::streamoff>(runtime_header_size) ||
        static_cast<uint64_t>(end) > runtime_file_size_limit) {
        error = "DXR PBR material catalog has an invalid size: " + path_text(path);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char *>(bytes.data()), end)) {
        error = "DXR PBR material catalog could not be read completely: " +
            path_text(path);
        return false;
    }
    if (std::memcmp(bytes.data(), runtime_magic, sizeof(runtime_magic)) != 0 ||
        read_u32(bytes.data() + 8u) != runtime_version) {
        error = "DXR PBR material catalog has an unsupported format";
        return false;
    }
    const uint32_t material_count = read_u32(bytes.data() + 12u);
    const uint32_t channel_count = read_u32(bytes.data() + 16u);
    const uint32_t record_size = read_u32(bytes.data() + 20u);
    if (material_count == 0u || material_count > runtime_material_limit ||
        channel_count != static_cast<uint32_t>(DxrMaterialChannel::count) ||
        record_size != runtime_record_size ||
        static_cast<size_t>(material_count) >
            (bytes.size() - runtime_header_size) / runtime_record_size ||
        runtime_header_size + static_cast<size_t>(material_count) *
                runtime_record_size != bytes.size()) {
        error = "DXR PBR material catalog header/record extent is invalid";
        return false;
    }

    definitions_.reserve(material_count);
    std::set<std::string> names;
    const std::filesystem::path directory = path.parent_path();
    for (uint32_t index = 0; index < material_count; ++index) {
        const uint8_t *record = bytes.data() + runtime_header_size +
            static_cast<size_t>(index) * runtime_record_size;
        const uint32_t source_kind = read_u32(record + 0u);
        const uint32_t source_asset_id = read_u32(record + 4u);
        const uint32_t detail0 = read_u32(record + 8u);
        const uint32_t detail1 = read_u32(record + 12u);
        const uint32_t width = read_u32(record + 16u);
        const uint32_t height = read_u32(record + 20u);
        const float normal_strength = read_float(record + 24u);
        const float emissive_factor[3] = {
            read_float(record + 28u),
            read_float(record + 32u),
            read_float(record + 36u),
        };
        const uint32_t flags = read_u32(record + 40u);
        const char *name_bytes = reinterpret_cast<const char *>(record + 44u);
        const void *terminator = std::memchr(name_bytes, 0, runtime_name_size);
        if (!terminator) {
            error = "DXR PBR material catalog contains an unterminated name";
            return false;
        }
        const size_t name_size = static_cast<const char *>(terminator) - name_bytes;
        const std::string name(name_bytes, name_size);
        const uint32_t alpha_mode = flags & runtime_alpha_mask;
        if (width == 0u || height == 0u ||
            width > runtime_image_extent_limit ||
            height > runtime_image_extent_limit ||
            !std::isfinite(normal_strength) || normal_strength <= 0.0f ||
            (flags & ~runtime_known_flags) != 0u ||
            (alpha_mode != runtime_alpha_opaque &&
             alpha_mode != runtime_alpha_tested &&
             alpha_mode != runtime_alpha_additive) ||
            !valid_material_name(name) || !names.emplace(name).second) {
            error = "DXR PBR material catalog contains an invalid material record";
            return false;
        }
        bool has_emission = false;
        for (float value : emissive_factor) {
            if (!std::isfinite(value) || value < 0.0f) {
                error = "DXR PBR material catalog contains invalid emissive radiance";
                return false;
            }
            has_emission = has_emission || value > 0.0f;
        }
        if (has_emission !=
            ((flags & runtime_flag_emissive_texture) != 0u)) {
            error = "DXR PBR material catalog emissive flags/radiance disagree";
            return false;
        }

        DxrMaterialDefinition definition;
        definition.name = name;
        definition.width = width;
        definition.height = height;
        definition.normal_strength = normal_strength;
        std::memcpy(definition.emissive_factor, emissive_factor,
                    sizeof(definition.emissive_factor));
        for (size_t channel = 0;
             channel < static_cast<size_t>(DxrMaterialChannel::count); ++channel) {
            const std::filesystem::path png =
                directory / (name + channel_suffixes[channel]);
            if (!load_channel_png(png, width, height,
                                  definition.pixels[channel], error)) {
                return false;
            }
        }

        const size_t definition_index = definitions_.size();
        if (source_kind == runtime_source_shared_wall ||
            source_kind == runtime_source_shared_floor) {
            if (detail0 != 0u || detail1 != 0u ||
                (flags & runtime_flag_vector_glare) != 0u) {
                error = "DXR PBR world material contains unexpected binding detail";
                return false;
            }
            definition.source = source_kind == runtime_source_shared_wall ?
                SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE :
                SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE;
            definition.source_asset_id = source_asset_id;
            if (!bindings_.emplace(
                    std::make_pair(definition.source, source_asset_id),
                    definition_index).second) {
                error = "DXR PBR material catalog contains a duplicate world binding";
                return false;
            }
        } else if (source_kind == runtime_source_vector) {
            const uint8_t minimum_u = static_cast<uint8_t>(detail1);
            const uint8_t maximum_u = static_cast<uint8_t>(detail1 >> 8u);
            const uint8_t minimum_v = static_cast<uint8_t>(detail1 >> 16u);
            const uint8_t maximum_v = static_cast<uint8_t>(detail1 >> 24u);
            const uint8_t glare =
                (flags & runtime_flag_vector_glare) != 0u ? 1u : 0u;
            if (minimum_u > maximum_u || minimum_v > maximum_v ||
                !vector_bindings_.emplace(
                    std::make_tuple(source_asset_id, detail0,
                                    minimum_u, maximum_u,
                                    minimum_v, maximum_v, glare),
                    definition_index).second) {
                error = "DXR PBR material catalog contains an invalid/duplicate vector binding";
                return false;
            }
        } else if (source_kind == runtime_source_bitmap) {
            if (detail0 >= 32u || detail1 > 7u ||
                (flags & runtime_flag_vector_glare) != 0u ||
                !bitmap_bindings_.emplace(
                    std::make_tuple(source_asset_id, detail0, detail1),
                    definition_index).second) {
                error = "DXR PBR material catalog contains an invalid bitmap binding";
                return false;
            }
        } else if (source_kind != runtime_source_none ||
                   source_asset_id != UINT32_MAX || detail0 != 0u ||
                   detail1 != 0u ||
                   (flags & runtime_flag_vector_glare) != 0u) {
            error = "DXR PBR material catalog contains an unsupported binding";
            return false;
        }
        definitions_.push_back(std::move(definition));
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

const DxrMaterialDefinition *DxrMaterialLibrary::find_vector(
    uint32_t source_asset_id, uint32_t source_map_offset,
    uint8_t minimum_u, uint8_t maximum_u,
    uint8_t minimum_v, uint8_t maximum_v, uint8_t glare) const
{
    const auto key = std::make_tuple(
        source_asset_id, source_map_offset, minimum_u, maximum_u,
        minimum_v, maximum_v, glare);
    const auto found = vector_bindings_.find(key);
    return found == vector_bindings_.end() ?
        nullptr : &definitions_[found->second];
}

const DxrMaterialDefinition *DxrMaterialLibrary::find_bitmap(
    uint32_t source_asset_id, uint32_t frame_index,
    uint32_t source_mode) const
{
    const auto found = bitmap_bindings_.find(
        std::make_tuple(source_asset_id, frame_index, source_mode));
    return found == bitmap_bindings_.end() ?
        nullptr : &definitions_[found->second];
}

}  // namespace ab3d2::dxr
