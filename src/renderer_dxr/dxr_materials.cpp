#include "dxr_materials.h"

#include <windows.h>

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace ab3d2::dxr {

namespace {

constexpr uint8_t runtime_magic[8] = {'A', 'B', '3', 'P', 'B', 'R', '7', 0};
constexpr uint32_t runtime_version = 7u;
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
constexpr uint32_t runtime_class_shift = 12u;
constexpr uint32_t runtime_class_mask = 0x0fu << runtime_class_shift;
constexpr uint32_t runtime_class_wall = 1u;
constexpr uint32_t runtime_class_floor = 2u;
constexpr uint32_t runtime_class_weapon = 3u;
constexpr uint32_t runtime_class_vector_model = 4u;
constexpr uint32_t runtime_class_enemy = 5u;
constexpr uint32_t runtime_class_billboard = 6u;
constexpr uint32_t runtime_class_effect = 7u;
constexpr uint32_t runtime_class_environment = 8u;
constexpr uint32_t runtime_class_ui = 9u;
constexpr uint32_t runtime_known_flags =
    runtime_alpha_mask | runtime_flag_emissive_texture |
    runtime_flag_two_sided | runtime_flag_vector_glare | runtime_class_mask;
constexpr size_t runtime_header_size = 24u;
constexpr size_t runtime_record_metadata_size = 144u;
constexpr size_t runtime_channel_payload_size = 12u;
constexpr size_t runtime_record_size = runtime_record_metadata_size +
    static_cast<size_t>(DxrMaterialChannel::count) * runtime_channel_payload_size;
constexpr size_t runtime_name_size = 96u;
constexpr uint32_t runtime_material_limit = 8192u;
constexpr uint32_t runtime_image_extent_limit = 8192u;
constexpr uint64_t runtime_file_size_limit = UINT64_C(1024) * 1024u * 1024u;

constexpr const char *channel_names[] = {
    "base_color",
    "normal",
    "metalness",
    "roughness",
    "emissive",
};

static_assert(std::size(channel_names) ==
              static_cast<size_t>(DxrMaterialChannel::count));

uint32_t read_u32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint64_t read_u64(const uint8_t *bytes)
{
    return static_cast<uint64_t>(read_u32(bytes)) |
        (static_cast<uint64_t>(read_u32(bytes + 4u)) << 32u);
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

bool valid_material_class(uint32_t material_class)
{
    switch (material_class) {
    case runtime_class_wall:
    case runtime_class_floor:
    case runtime_class_weapon:
    case runtime_class_vector_model:
    case runtime_class_enemy:
    case runtime_class_billboard:
    case runtime_class_effect:
    case runtime_class_environment:
    case runtime_class_ui:
        return true;
    default:
        return false;
    }
}

bool load_channel_png(const uint8_t *encoded, uint32_t encoded_size,
                      const std::string &description,
                      uint32_t expected_width, uint32_t expected_height,
                      std::vector<uint8_t> &pixels, std::string &error)
{
    if (!encoded || encoded_size == 0u ||
        encoded_size > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        error = "DXR PBR package contains an invalid PNG payload: " + description;
        return false;
    }
    int width = 0;
    int height = 0;
    int source_channels = 0;
    stbi_uc *decoded = stbi_load_from_memory(
        encoded, static_cast<int>(encoded_size),
        &width, &height, &source_channels, 4);
    if (!decoded) {
        error = "DXR PBR PNG could not be decoded: " + description +
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
            description;
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
    payloads_.clear();
    package_path_.clear();
    package_stream_.close();
    package_stream_.clear();
    resident_size_ = 0u;
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
    std::array<uint8_t, runtime_header_size> header = {};
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char *>(header.data()), header.size())) {
        error = "DXR PBR material package header could not be read: " +
            path_text(path);
        return false;
    }
    if (std::memcmp(header.data(), runtime_magic, sizeof(runtime_magic)) != 0 ||
        read_u32(header.data() + 8u) != runtime_version) {
        error = "DXR PBR material catalog has an unsupported format";
        return false;
    }
    const uint32_t material_count = read_u32(header.data() + 12u);
    const uint32_t channel_count = read_u32(header.data() + 16u);
    const uint32_t record_size = read_u32(header.data() + 20u);
    const uint64_t table_size = runtime_header_size +
        static_cast<uint64_t>(material_count) * runtime_record_size;
    if (material_count == 0u || material_count > runtime_material_limit ||
        channel_count != static_cast<uint32_t>(DxrMaterialChannel::count) ||
        record_size != runtime_record_size ||
        table_size > static_cast<uint64_t>(end) ||
        table_size > std::numeric_limits<size_t>::max()) {
        error = "DXR PBR material catalog header/record extent is invalid";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(table_size));
    std::memcpy(bytes.data(), header.data(), header.size());
    if (!stream.read(
            reinterpret_cast<char *>(bytes.data() + runtime_header_size),
            static_cast<std::streamsize>(table_size - runtime_header_size))) {
        error = "DXR PBR material catalog records could not be read: " +
            path_text(path);
        return false;
    }

    definitions_.reserve(material_count);
    payloads_.reserve(material_count);
    std::set<std::string> names;
    uint64_t expected_payload_offset = table_size;
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
        const float specular_factor = read_float(record + 40u);
        const uint32_t flags = read_u32(record + 44u);
        const char *name_bytes = reinterpret_cast<const char *>(record + 48u);
        const void *terminator = std::memchr(name_bytes, 0, runtime_name_size);
        if (!terminator) {
            error = "DXR PBR material catalog contains an unterminated name";
            return false;
        }
        const size_t name_size = static_cast<const char *>(terminator) - name_bytes;
        const std::string name(name_bytes, name_size);
        const uint32_t alpha_mode = flags & runtime_alpha_mask;
        const uint32_t material_class =
            (flags & runtime_class_mask) >> runtime_class_shift;
        if (width == 0u || height == 0u ||
            width > runtime_image_extent_limit ||
            height > runtime_image_extent_limit ||
            !std::isfinite(normal_strength) || normal_strength <= 0.0f ||
            !std::isfinite(specular_factor) || specular_factor < 0.0f ||
            specular_factor > 1.0f ||
            (flags & ~runtime_known_flags) != 0u ||
            (alpha_mode != runtime_alpha_opaque &&
             alpha_mode != runtime_alpha_tested &&
             alpha_mode != runtime_alpha_additive) ||
            !valid_material_class(material_class) || !valid_material_name(name) ||
            !names.emplace(name).second) {
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
        definition.specular_factor = specular_factor;
        std::memcpy(definition.emissive_factor, emissive_factor,
                    sizeof(definition.emissive_factor));
        std::array<ChannelPayload,
                   static_cast<size_t>(DxrMaterialChannel::count)> payloads = {};
        for (size_t channel = 0;
             channel < static_cast<size_t>(DxrMaterialChannel::count); ++channel) {
            const uint8_t *payload_record = record +
                runtime_record_metadata_size +
                channel * runtime_channel_payload_size;
            const uint64_t offset = read_u64(payload_record);
            const uint32_t size = read_u32(payload_record + 8u);
            if (size < 8u || offset != expected_payload_offset ||
                offset > static_cast<uint64_t>(end) ||
                size > static_cast<uint64_t>(end) - offset) {
                error = "DXR PBR material catalog contains an invalid PNG payload range";
                return false;
            }
            payloads[channel].offset = offset;
            payloads[channel].size = size;
            expected_payload_offset += size;
        }

        const size_t definition_index = definitions_.size();
        if (source_kind == runtime_source_shared_wall ||
            source_kind == runtime_source_shared_floor) {
            if (detail1 != 0u ||
                (flags & runtime_flag_vector_glare) != 0u ||
                (source_kind == runtime_source_shared_wall &&
                 (material_class != runtime_class_wall || detail0 == 0u ||
                  detail0 > UINT16_MAX)) ||
                (source_kind == runtime_source_shared_floor &&
                 (material_class != runtime_class_floor || detail0 != 0u))) {
                error = "DXR PBR world material contains unexpected binding detail";
                return false;
            }
            definition.source = source_kind == runtime_source_shared_wall ?
                SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE :
                SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE;
            definition.source_asset_id = source_asset_id;
            if (!bindings_.emplace(
                    std::make_tuple(definition.source, source_asset_id,
                                    detail0),
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
            if ((material_class != runtime_class_weapon &&
                 material_class != runtime_class_vector_model) ||
                minimum_u > maximum_u || minimum_v > maximum_v ||
                !vector_bindings_.emplace(
                    DxrVectorMaterialKey{source_asset_id, detail0,
                                         minimum_u, maximum_u,
                                         minimum_v, maximum_v, glare},
                    definition_index).second) {
                error = "DXR PBR material catalog contains an invalid/duplicate vector binding";
                return false;
            }
        } else if (source_kind == runtime_source_bitmap) {
            if ((material_class != runtime_class_enemy &&
                 material_class != runtime_class_billboard &&
                 material_class != runtime_class_effect) ||
                detail0 >= 32u || detail1 > 7u ||
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
                   (flags & runtime_flag_vector_glare) != 0u ||
                   (material_class != runtime_class_wall &&
                    material_class != runtime_class_environment &&
                    material_class != runtime_class_ui)) {
            error = "DXR PBR material catalog contains an unsupported binding";
            return false;
        }
        definitions_.push_back(std::move(definition));
        payloads_.push_back(payloads);
    }
    if (expected_payload_offset != static_cast<uint64_t>(end)) {
        error = "DXR PBR material package has trailing or missing PNG payload bytes";
        return false;
    }
    package_path_ = path;
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

bool DxrMaterialLibrary::resolve_index(
    size_t index, const DxrMaterialDefinition *&definition, std::string &error)
{
    definition = nullptr;
    if (!loaded_ || index >= definitions_.size() || index >= payloads_.size()) {
        error = "DXR PBR material resolution used an invalid catalog index";
        return false;
    }
    DxrMaterialDefinition &material = definitions_[index];
    if (!material.pixels[0].empty()) {
        definition = &material;
        return true;
    }

    const auto &payloads = payloads_[index];
    const uint64_t first_offset = payloads.front().offset;
    const ChannelPayload &last_payload = payloads.back();
    const uint64_t end_offset = last_payload.offset + last_payload.size;
    const uint64_t encoded_size64 = end_offset - first_offset;
    if (encoded_size64 > std::numeric_limits<size_t>::max() ||
        encoded_size64 > static_cast<uint64_t>(
            std::numeric_limits<std::streamsize>::max())) {
        error = "DXR PBR material PNG payload is too large: " + material.name;
        return false;
    }
    const size_t encoded_size = static_cast<size_t>(encoded_size64);
    std::vector<uint8_t> encoded(encoded_size);
    /*
     * One handle for the package's lifetime. Resolution is per material and
     * preparing a level resolves every one of them, so opening and closing the
     * same file ~940 times was a measurable share of the load cost.
     */
    if (!package_stream_.is_open()) {
        package_stream_.open(package_path_, std::ios::binary);
    }
    if (!package_stream_) {
        package_stream_.close();
        package_stream_.clear();
        error = "DXR PBR material package is unavailable while resolving " +
            material.name + ": " + path_text(package_path_);
        return false;
    }
    package_stream_.seekg(static_cast<std::streamoff>(first_offset),
                          std::ios::beg);
    if (!package_stream_ ||
        !package_stream_.read(reinterpret_cast<char *>(encoded.data()),
                              static_cast<std::streamsize>(encoded.size()))) {
        /* Leave no sticky failure bits behind for the next resolve. */
        package_stream_.clear();
        error = "DXR PBR material PNG payload could not be read for " +
            material.name + " from " + path_text(package_path_);
        return false;
    }

    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> decoded;
    constexpr uint8_t png_signature[8] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au,
    };
    for (size_t channel = 0;
         channel < static_cast<size_t>(DxrMaterialChannel::count); ++channel) {
        const ChannelPayload &payload = payloads[channel];
        const size_t relative_offset = static_cast<size_t>(
            payload.offset - first_offset);
        const uint8_t *png = encoded.data() + relative_offset;
        if (payload.size < sizeof(png_signature) ||
            std::memcmp(png, png_signature, sizeof(png_signature)) != 0) {
            error = "DXR PBR package payload is not a PNG: " + material.name +
                "/" + channel_names[channel];
            return false;
        }
        const std::string description = material.name + "/" +
            channel_names[channel] + " in " + path_text(package_path_);
        if (!load_channel_png(png, payload.size, description,
                              material.width, material.height,
                              decoded[channel], error)) {
            return false;
        }
    }
    material.pixels = std::move(decoded);
    ++resident_size_;
    definition = &material;
    return true;
}

bool DxrMaterialLibrary::resolve(
    SceneMaterialSource source, uint32_t source_asset_id,
    uint32_t texture_v_period,
    const DxrMaterialDefinition *&definition, std::string &error)
{
    const auto found = bindings_.find(
        std::make_tuple(source, source_asset_id, texture_v_period));
    if (found == bindings_.end()) {
        std::ostringstream message;
        message << "DXR PBR binding is missing for world material: source="
                << static_cast<unsigned>(source)
                << " asset=" << source_asset_id
                << " v_period=" << texture_v_period;
        error = message.str();
        definition = nullptr;
        return false;
    }
    return resolve_index(found->second, definition, error);
}

bool DxrMaterialLibrary::resolve_vector(
    const DxrVectorMaterialKey &key,
    const DxrMaterialDefinition *&definition, std::string &error)
{
    const auto found = vector_bindings_.find(key);
    if (found == vector_bindings_.end()) {
        std::ostringstream message;
        message << "DXR PBR binding is missing for vector material: asset="
                << key.source_asset_id << " map=" << key.source_map_offset
                << " u=" << static_cast<unsigned>(key.minimum_u)
                << ".." << static_cast<unsigned>(key.maximum_u)
                << " v=" << static_cast<unsigned>(key.minimum_v)
                << ".." << static_cast<unsigned>(key.maximum_v)
                << " glare=" << static_cast<unsigned>(key.glare);
        error = message.str();
        definition = nullptr;
        return false;
    }
    return resolve_index(found->second, definition, error);
}

bool DxrMaterialLibrary::resolve_bitmap(
    uint32_t source_asset_id, uint32_t frame_index,
    uint32_t source_mode, const DxrMaterialDefinition *&definition,
    std::string &error)
{
    const auto found = bitmap_bindings_.find(
        std::make_tuple(source_asset_id, frame_index, source_mode));
    if (found == bitmap_bindings_.end()) {
        std::ostringstream message;
        message << "DXR PBR binding is missing for bitmap material: asset="
                << source_asset_id << " frame=" << frame_index
                << " mode=" << source_mode;
        error = message.str();
        definition = nullptr;
        return false;
    }
    return resolve_index(found->second, definition, error);
}

bool DxrMaterialLibrary::resolve_vector_asset(
    uint32_t source_asset_id,
    std::vector<DxrVectorMaterialBinding> &bindings,
    std::string &error)
{
    bindings.clear();
    if (!loaded_) {
        error = "DXR PBR vector enumeration requires a loaded catalog";
        return false;
    }
    auto entry = vector_bindings_.lower_bound(
        DxrVectorMaterialKey{source_asset_id});
    while (entry != vector_bindings_.end() &&
           entry->first.source_asset_id == source_asset_id) {
        const DxrMaterialDefinition *definition = nullptr;
        if (!resolve_index(entry->second, definition, error)) {
            bindings.clear();
            return false;
        }
        DxrVectorMaterialBinding binding;
        binding.source_asset_id = entry->first.source_asset_id;
        binding.source_map_offset = entry->first.source_map_offset;
        binding.minimum_u = entry->first.minimum_u;
        binding.maximum_u = entry->first.maximum_u;
        binding.minimum_v = entry->first.minimum_v;
        binding.maximum_v = entry->first.maximum_v;
        binding.glare = entry->first.glare;
        binding.definition = definition;
        bindings.push_back(binding);
        ++entry;
    }
    if (bindings.empty()) {
        std::ostringstream message;
        message << "DXR PBR vector asset has no packaged materials: asset="
                << source_asset_id;
        error = message.str();
        return false;
    }
    return true;
}

bool DxrMaterialLibrary::enumerate_bitmap_bindings(
    uint32_t source_asset_id, const uint32_t *source_mode,
    std::vector<DxrBitmapMaterialBinding> &bindings, std::string &error)
{
    bindings.clear();
    if (!loaded_) {
        error = "DXR PBR bitmap enumeration requires a loaded catalog";
        return false;
    }
    auto entry = bitmap_bindings_.lower_bound(
        std::make_tuple(source_asset_id, 0u, 0u));
    while (entry != bitmap_bindings_.end() &&
           std::get<0>(entry->first) == source_asset_id) {
        if (source_mode && std::get<2>(entry->first) != *source_mode) {
            ++entry;
            continue;
        }
        const DxrMaterialDefinition *definition = nullptr;
        if (!resolve_index(entry->second, definition, error)) {
            bindings.clear();
            return false;
        }
        DxrBitmapMaterialBinding binding;
        binding.source_asset_id = source_asset_id;
        binding.frame_index = std::get<1>(entry->first);
        binding.source_mode = std::get<2>(entry->first);
        binding.definition = definition;
        bindings.push_back(binding);
        ++entry;
    }
    if (bindings.empty()) {
        std::ostringstream message;
        message << "DXR PBR bitmap asset has no packaged materials: asset="
                << source_asset_id;
        if (source_mode) {
            message << " mode=" << *source_mode;
        }
        error = message.str();
        return false;
    }
    return true;
}

bool DxrMaterialLibrary::resolve_bitmap_asset(
    uint32_t source_asset_id,
    std::vector<DxrBitmapMaterialBinding> &bindings, std::string &error)
{
    return enumerate_bitmap_bindings(source_asset_id, nullptr, bindings, error);
}

bool DxrMaterialLibrary::resolve_bitmap_asset_mode(
    uint32_t source_asset_id, uint32_t source_mode,
    std::vector<DxrBitmapMaterialBinding> &bindings, std::string &error)
{
    return enumerate_bitmap_bindings(source_asset_id, &source_mode, bindings,
                                     error);
}

}  // namespace ab3d2::dxr
