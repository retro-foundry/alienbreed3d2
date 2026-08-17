#include "dxr_scene.h"

#include "dxr_debug.h"
#include "scene_geometry_compile.h"
#include "source_world_material.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>

namespace ab3d2::dxr {

namespace {

constexpr uint32_t atlas_maximum_extent = 8192u;

struct MaterialKey {
    SceneMaterialSource source;
    uint32_t source_asset_id;
    uint16_t u_offset;
    uint16_t u_period;
    uint16_t v_period;
    SceneGeometryPrimitive primitive;

    auto tie() const {
        return std::tie(source, source_asset_id, u_offset, u_period, v_period,
                        primitive);
    }
    bool operator<(const MaterialKey &other) const { return tie() < other.tie(); }
};

struct MaterialImage {
    MaterialKey key = {};
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    float normal_strength = 1.0f;
    float emissive_factor[3] = {};
    float average_emissive_luminance = 0.0f;
};

float srgb_to_linear(uint8_t encoded)
{
    const float value = static_cast<float>(encoded) / 255.0f;
    return value <= 0.04045f ? value / 12.92f :
        std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float average_emissive_luminance(const MaterialImage &image)
{
    if (image.emissive_factor[0] == 0.0f &&
        image.emissive_factor[1] == 0.0f &&
        image.emissive_factor[2] == 0.0f) {
        return 0.0f;
    }
    const std::vector<uint8_t> &emissive = image.pixels[
        static_cast<size_t>(DxrMaterialChannel::emissive)];
    double luminance = 0.0;
    for (size_t offset = 0; offset < emissive.size(); offset += 4u) {
        const double red = srgb_to_linear(emissive[offset + 0u]) *
            image.emissive_factor[0];
        const double green = srgb_to_linear(emissive[offset + 1u]) *
            image.emissive_factor[1];
        const double blue = srgb_to_linear(emissive[offset + 2u]) *
            image.emissive_factor[2];
        luminance += red * 0.2126 + green * 0.7152 + blue * 0.0722;
    }
    return emissive.empty() ? 0.0f :
        static_cast<float>(luminance / static_cast<double>(emissive.size() / 4u));
}

uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    constexpr uint64_t prime = UINT64_C(1099511628211);
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= prime;
    }
    return hash;
}

uint64_t frame_geometry_hash(const SceneFrame &frame)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t command_index = 0; command_index < frame.count; ++command_index) {
        const SceneCommand &command = frame.commands[command_index];
        if (command.type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        const SceneGeometryInstance &instance = command.data.geometry_instance;
        hash = hash_bytes(hash, &instance.source_instance_id,
                          sizeof(instance.source_instance_id));
        hash = hash_bytes(hash, &instance.mesh.source_mesh_id,
                          sizeof(instance.mesh.source_mesh_id));
        hash = hash_bytes(hash, &instance.mesh.acceleration_class,
                          sizeof(instance.mesh.acceleration_class));
        hash = hash_bytes(hash, &instance.mesh.surface_count,
                          sizeof(instance.mesh.surface_count));
        for (uint32_t surface_index = 0; surface_index < instance.mesh.surface_count;
             ++surface_index) {
            const SceneMeshSurface &surface = instance.mesh.surfaces[surface_index];
            hash = hash_bytes(hash, &surface.material.source,
                              sizeof(surface.material.source));
            hash = hash_bytes(hash, &surface.material.source_asset_id,
                              sizeof(surface.material.source_asset_id));
            hash = hash_bytes(hash, &surface.geometry.vertex_count,
                              sizeof(surface.geometry.vertex_count));
            hash = hash_bytes(hash, &surface.geometry.topology,
                              sizeof(surface.geometry.topology));
            hash = hash_bytes(hash, &surface.geometry.primitive,
                              sizeof(surface.geometry.primitive));
            hash = hash_bytes(hash, &surface.geometry.texture_window,
                              sizeof(surface.geometry.texture_window));
            for (uint32_t vertex_index = 0;
                 surface.geometry.vertices &&
                 vertex_index < surface.geometry.vertex_count; ++vertex_index) {
                const SceneVertex &vertex = surface.geometry.vertices[vertex_index];
                hash = hash_bytes(hash, &vertex.position, sizeof(vertex.position));
                hash = hash_bytes(hash, &vertex.texture_u, sizeof(vertex.texture_u));
                hash = hash_bytes(hash, &vertex.texture_v, sizeof(vertex.texture_v));
            }
        }
    }
    return hash;
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties = {};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC buffer_description(UINT64 size,
                                       D3D12_RESOURCE_FLAGS flags =
                                           D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

bool create_buffer(ID3D12Device5 *device, UINT64 size, D3D12_HEAP_TYPE heap_type,
                   D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags,
                   const wchar_t *name, Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                   std::string &error)
{
    const D3D12_HEAP_PROPERTIES properties = heap_properties(heap_type);
    const D3D12_RESOURCE_DESC description = buffer_description(size, flags);
    const HRESULT result = device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateCommittedResource(scene buffer)",
                              result);
        return false;
    }
    resource->SetName(name);
    return true;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource *resource,
                                  D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource *resource)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

}  // namespace

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::vertex_address() const
{
    return vertex_buffer_ ? vertex_buffer_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::material_address() const
{
    return material_buffer_ ? material_buffer_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::emitter_address() const
{
    return emitter_buffer_ ? emitter_buffer_->GetGPUVirtualAddress() : 0;
}

void DxrScene::release_gpu()
{
    vertex_buffer_.Reset();
    material_buffer_.Reset();
    emitter_buffer_.Reset();
    upload_buffer_.Reset();
    for (auto &texture : atlas_textures_) {
        texture.Reset();
    }
    for (auto &upload : atlas_uploads_) {
        upload.Reset();
    }
    blas_scratch_.Reset();
    blas_.Reset();
    tlas_scratch_.Reset();
    tlas_.Reset();
    instance_upload_.Reset();
}

bool DxrScene::update(const SceneFrame &frame, bool &changed, std::string &error)
{
    const uint64_t hash = frame_geometry_hash(frame);
    changed = !has_hash_ || scene_hash_ != hash;
    if (!changed) {
        return true;
    }
    return compile(frame, hash, error);
}

bool DxrScene::compile(const SceneFrame &frame, uint64_t hash, std::string &error)
{
    std::vector<DxrSceneVertex> compiled_vertices;
    std::vector<MaterialImage> images;
    std::map<MaterialKey, uint32_t> material_indices;

    if (!material_library_.loaded() &&
        !material_library_.load_from_executable(error)) {
        return false;
    }

    for (size_t command_index = 0; command_index < frame.count; ++command_index) {
        const SceneCommand &command = frame.commands[command_index];
        if (command.type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        const SceneMesh &mesh = command.data.geometry_instance.mesh;
        if (!mesh.surfaces && mesh.surface_count != 0u) {
            error = "SceneFrame geometry instance has no surface array";
            return false;
        }
        for (uint32_t surface_index = 0; surface_index < mesh.surface_count;
             ++surface_index) {
            const SceneMeshSurface &surface = mesh.surfaces[surface_index];
            const SceneGeometry &geometry = surface.geometry;
            const DxrMaterialDefinition *surface_pbr = material_library_.find(
                surface.material.source, surface.material.source_asset_id);
            MaterialKey key = {
                surface.material.source,
                surface.material.source_asset_id,
                surface_pbr ? 0u : geometry.texture_window.u_offset,
                surface_pbr ? 0u : geometry.texture_window.u_period,
                surface_pbr ? 0u : geometry.texture_window.v_period,
                surface_pbr ? SCENE_GEOMETRY_PRIMITIVE_WALL : geometry.primitive,
            };
            uint32_t material_index;
            auto found = material_indices.find(key);
            if (found == material_indices.end()) {
                MaterialImage image;
                image.key = key;
                const DxrMaterialDefinition *pbr = surface_pbr;
                if (pbr) {
                    image.width = pbr->width;
                    image.height = pbr->height;
                    image.pixels = pbr->pixels;
                    image.normal_strength = pbr->normal_strength;
                    std::memcpy(image.emissive_factor, pbr->emissive_factor,
                                sizeof(image.emissive_factor));
                } else {
                    SourceWorldMaterialImage decoded = {};
                    char decode_error[512] = {};
                    if (!source_world_material_decode(&surface.material, &geometry,
                                                      &decoded, decode_error,
                                                      sizeof(decode_error))) {
                        error = "DXR source-albedo fallback failed: ";
                        error += decode_error;
                        return false;
                    }
                    if (!decoded.rgba || decoded.width == 0u || decoded.height == 0u) {
                        source_world_material_image_destroy(&decoded);
                        error = "DXR source-albedo fallback decoded an empty image";
                        return false;
                    }
                    image.width = decoded.width;
                    image.height = decoded.height;
                    const size_t byte_count = static_cast<size_t>(decoded.width) *
                        decoded.height * 4u;
                    image.pixels[static_cast<size_t>(DxrMaterialChannel::base_color)]
                        .assign(decoded.rgba, decoded.rgba + byte_count);
                    source_world_material_image_destroy(&decoded);
                    image.pixels[static_cast<size_t>(DxrMaterialChannel::normal)]
                        .resize(byte_count);
                    image.pixels[static_cast<size_t>(DxrMaterialChannel::metalness)]
                        .resize(byte_count);
                    image.pixels[static_cast<size_t>(DxrMaterialChannel::roughness)]
                        .resize(byte_count);
                    image.pixels[static_cast<size_t>(DxrMaterialChannel::emissive)]
                        .resize(byte_count);
                    for (size_t texel = 0; texel < byte_count; texel += 4u) {
                        auto &normal = image.pixels[
                            static_cast<size_t>(DxrMaterialChannel::normal)];
                        normal[texel + 0u] = 128u;
                        normal[texel + 1u] = 128u;
                        normal[texel + 2u] = 255u;
                        normal[texel + 3u] = 255u;
                        auto &metalness = image.pixels[
                            static_cast<size_t>(DxrMaterialChannel::metalness)];
                        metalness[texel + 3u] = 255u;
                        auto &roughness = image.pixels[
                            static_cast<size_t>(DxrMaterialChannel::roughness)];
                        roughness[texel + 0u] = 255u;
                        roughness[texel + 1u] = 255u;
                        roughness[texel + 2u] = 255u;
                        roughness[texel + 3u] = 255u;
                    }
                }
                image.average_emissive_luminance =
                    average_emissive_luminance(image);
                material_index = static_cast<uint32_t>(images.size());
                material_indices.emplace(key, material_index);
                images.push_back(std::move(image));

                std::ostringstream report;
                report << (pbr ? "DXR PBR material: source=" :
                                 "DXR material fallback: source=")
                       << static_cast<unsigned>(key.source)
                       << " asset=" << key.source_asset_id;
                if (!pbr) {
                    report << " uses decoded source albedo, roughness=1, "
                              "metalness=0, emissive=0";
                }
                debug_output(report.str());
            } else {
                material_index = found->second;
            }

            uint32_t *indices = nullptr;
            uint32_t index_count = 0;
            char triangulation_error[512] = {};
            if (!scene_geometry_triangle_indices(
                    &geometry, &indices, &index_count, triangulation_error,
                    sizeof(triangulation_error))) {
                error = "DXR SceneFrame triangulation failed: ";
                error += triangulation_error;
                return false;
            }
            const float u_scale = geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
                1.0f / static_cast<float>(geometry.texture_window.u_period) :
                1.0f / 64.0f;
            const float v_scale = geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
                1.0f / static_cast<float>(geometry.texture_window.v_period) :
                1.0f / 64.0f;
            if (compiled_vertices.size() >
                std::numeric_limits<uint32_t>::max() - index_count) {
                scene_geometry_triangle_indices_release(indices);
                error = "DXR SceneFrame has too many triangle vertices";
                return false;
            }
            for (uint32_t index = 0; index < index_count; ++index) {
                const SceneVertex &source = geometry.vertices[indices[index]];
                const SceneRenderPoint position =
                    scene_render_world_point(source.position);
                DxrSceneVertex vertex = {};
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.texture_coordinate[0] = source.texture_u * u_scale;
                vertex.texture_coordinate[1] = source.texture_v * v_scale;
                vertex.material_index = material_index;
                vertex.emitter_index = UINT32_MAX;
                compiled_vertices.push_back(vertex);
            }
            scene_geometry_triangle_indices_release(indices);
        }
    }

    std::vector<DxrEmissiveTriangle> compiled_emitters;
    std::vector<float> emitter_weights;
    double emitter_weight_sum = 0.0;
    for (size_t first_vertex = 0; first_vertex < compiled_vertices.size();
         first_vertex += 3u) {
        const uint32_t material_index =
            compiled_vertices[first_vertex].material_index;
        const float luminance = images[material_index].average_emissive_luminance;
        if (!(luminance > 0.0f)) {
            continue;
        }
        const float *first = compiled_vertices[first_vertex + 0u].position;
        const float *second = compiled_vertices[first_vertex + 1u].position;
        const float *third = compiled_vertices[first_vertex + 2u].position;
        const float edge_a[3] = {
            second[0] - first[0], second[1] - first[1], second[2] - first[2]};
        const float edge_b[3] = {
            third[0] - first[0], third[1] - first[1], third[2] - first[2]};
        const float cross[3] = {
            edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
            edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
            edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0],
        };
        const float area = 0.5f * std::sqrt(
            cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        const float weight = area * luminance;
        if (!(area > 1.0e-6f) || !std::isfinite(weight) || !(weight > 0.0f)) {
            continue;
        }
        DxrEmissiveTriangle emitter = {};
        emitter.first_vertex = static_cast<uint32_t>(first_vertex);
        emitter.inverse_area = 1.0f / area;
        const uint32_t emitter_index =
            static_cast<uint32_t>(compiled_emitters.size());
        for (size_t vertex = 0; vertex < 3u; ++vertex) {
            compiled_vertices[first_vertex + vertex].emitter_index = emitter_index;
        }
        compiled_emitters.push_back(emitter);
        emitter_weights.push_back(weight);
        emitter_weight_sum += weight;
    }
    double emitter_cdf = 0.0;
    for (size_t index = 0; index < compiled_emitters.size(); ++index) {
        const float probability = static_cast<float>(
            static_cast<double>(emitter_weights[index]) / emitter_weight_sum);
        emitter_cdf += probability;
        compiled_emitters[index].selection_probability = probability;
        compiled_emitters[index].selection_cdf = index + 1u ==
                compiled_emitters.size() ?
            1.0f : static_cast<float>(emitter_cdf);
    }

    std::vector<DxrSceneMaterial> compiled_materials(images.size());
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> compiled_atlases;
    uint32_t atlas_width = 0;
    uint32_t atlas_height = 0;
    if (!images.empty()) {
        bool packed = false;
        for (uint32_t candidate_width = 512u;
             candidate_width <= atlas_maximum_extent; candidate_width *= 2u) {
            uint32_t x = 0u;
            uint32_t y = 0u;
            uint32_t row_height = 0u;
            bool fits = true;
            for (MaterialImage &image : images) {
                if (image.width > candidate_width) {
                    fits = false;
                    break;
                }
                if (x + image.width > candidate_width) {
                    x = 0u;
                    y += row_height;
                    row_height = 0u;
                }
                if (y + image.height > atlas_maximum_extent) {
                    fits = false;
                    break;
                }
                image.x = x;
                image.y = y;
                x += image.width;
                row_height = std::max(row_height, image.height);
            }
            if (fits) {
                atlas_width = candidate_width;
                atlas_height = std::max(1u, y + row_height);
                packed = true;
                break;
            }
        }
        if (!packed || static_cast<size_t>(atlas_width) >
                           std::numeric_limits<size_t>::max() / atlas_height / 4u) {
            error = "DXR source-albedo atlas exceeds the 8192-pixel limit";
            return false;
        }
        const size_t atlas_bytes =
            static_cast<size_t>(atlas_width) * atlas_height * 4u;
        for (auto &atlas : compiled_atlases) {
            atlas.assign(atlas_bytes, 0u);
        }
        for (size_t image_index = 0; image_index < images.size(); ++image_index) {
            const MaterialImage &image = images[image_index];
            for (size_t channel = 0;
                 channel < static_cast<size_t>(DxrMaterialChannel::count);
                 ++channel) {
                for (uint32_t row = 0; row < image.height; ++row) {
                    std::memcpy(
                        compiled_atlases[channel].data() +
                            (static_cast<size_t>(image.y + row) * atlas_width +
                             image.x) * 4u,
                        image.pixels[channel].data() +
                            static_cast<size_t>(row) * image.width * 4u,
                        static_cast<size_t>(image.width) * 4u);
                }
            }
            DxrSceneMaterial &material = compiled_materials[image_index];
            material.atlas_x = image.x;
            material.atlas_y = image.y;
            material.width = image.width;
            material.height = image.height;
            material.normal_strength = image.normal_strength;
            std::memcpy(material.emissive, image.emissive_factor,
                        sizeof(material.emissive));
        }
    }

    vertices_ = std::move(compiled_vertices);
    materials_ = std::move(compiled_materials);
    emissive_triangles_ = std::move(compiled_emitters);
    atlas_pixels_ = std::move(compiled_atlases);
    atlas_width_ = atlas_width;
    atlas_height_ = atlas_height;
    scene_hash_ = hash;
    has_hash_ = true;
    gpu_build_pending_ = true;
    return true;
}

bool DxrScene::record_build(ID3D12Device5 *device,
                            ID3D12GraphicsCommandList4 *command_list,
                            D3D12_CPU_DESCRIPTOR_HANDLE tlas_descriptor,
                            const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
                                             static_cast<size_t>(
                                                 DxrMaterialChannel::count)>
                                &atlas_descriptors,
                            std::string &error)
{
    if (!gpu_build_pending_) {
        return true;
    }
    release_gpu();
    gpu_build_pending_ = false;
    if (vertices_.empty()) {
        return true;
    }
    const bool missing_atlas = std::any_of(
        atlas_pixels_.begin(), atlas_pixels_.end(),
        [](const std::vector<uint8_t> &pixels) { return pixels.empty(); });
    if (!device || !command_list || materials_.empty() || missing_atlas ||
        atlas_width_ == 0u || atlas_height_ == 0u) {
        error = "DXR scene build received incomplete CPU or D3D12 state";
        return false;
    }

    const UINT64 vertex_bytes =
        static_cast<UINT64>(vertices_.size()) * sizeof(vertices_[0]);
    const UINT64 material_bytes =
        static_cast<UINT64>(materials_.size()) * sizeof(materials_[0]);
    const UINT64 emitter_bytes = std::max<UINT64>(
        sizeof(DxrEmissiveTriangle),
        static_cast<UINT64>(emissive_triangles_.size()) *
            sizeof(DxrEmissiveTriangle));
    const UINT64 material_offset = (vertex_bytes + 255u) & ~UINT64_C(255);
    const UINT64 emitter_offset =
        (material_offset + material_bytes + 255u) & ~UINT64_C(255);
    const UINT64 upload_bytes = emitter_offset + emitter_bytes;
    if (!create_buffer(device, vertex_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Vertices", vertex_buffer_, error) ||
        !create_buffer(device, material_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Materials", material_buffer_, error) ||
        !create_buffer(device, emitter_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Emissive Triangles", emitter_buffer_, error) ||
        !create_buffer(device, upload_bytes, D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Upload", upload_buffer_, error)) {
        return false;
    }
    void *mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    HRESULT result = upload_buffer_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(scene upload)", result);
        return false;
    }
    std::memcpy(mapped, vertices_.data(), static_cast<size_t>(vertex_bytes));
    std::memcpy(static_cast<uint8_t *>(mapped) + material_offset,
                materials_.data(), static_cast<size_t>(material_bytes));
    std::memset(static_cast<uint8_t *>(mapped) + emitter_offset, 0,
                static_cast<size_t>(emitter_bytes));
    if (!emissive_triangles_.empty()) {
        std::memcpy(static_cast<uint8_t *>(mapped) + emitter_offset,
                    emissive_triangles_.data(),
                    emissive_triangles_.size() * sizeof(emissive_triangles_[0]));
    }
    upload_buffer_->Unmap(0, nullptr);
    command_list->CopyBufferRegion(vertex_buffer_.Get(), 0, upload_buffer_.Get(), 0,
                                   vertex_bytes);
    command_list->CopyBufferRegion(material_buffer_.Get(), 0, upload_buffer_.Get(),
                                   material_offset, material_bytes);
    command_list->CopyBufferRegion(emitter_buffer_.Get(), 0, upload_buffer_.Get(),
                                   emitter_offset, emitter_bytes);

    D3D12_RESOURCE_DESC texture_description = {};
    texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_description.Width = atlas_width_;
    texture_description.Height = atlas_height_;
    texture_description.DepthOrArraySize = 1;
    texture_description.MipLevels = 1;
    texture_description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_description.SampleDesc.Count = 1;
    texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const D3D12_HEAP_PROPERTIES default_heap =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT row_count = 0;
    UINT64 row_bytes = 0;
    UINT64 total_bytes = 0;
    device->GetCopyableFootprints(&texture_description, 0, 1, 0, &footprint,
                                  &row_count, &row_bytes, &total_bytes);
    constexpr std::array<const wchar_t *, 5> texture_names = {
        L"AB3D2 DXR Base Color Atlas",
        L"AB3D2 DXR Normal Atlas",
        L"AB3D2 DXR Metalness Atlas",
        L"AB3D2 DXR Roughness Atlas",
        L"AB3D2 DXR Emissive Atlas",
    };
    constexpr std::array<const wchar_t *, 5> upload_names = {
        L"AB3D2 DXR Base Color Atlas Upload",
        L"AB3D2 DXR Normal Atlas Upload",
        L"AB3D2 DXR Metalness Atlas Upload",
        L"AB3D2 DXR Roughness Atlas Upload",
        L"AB3D2 DXR Emissive Atlas Upload",
    };
    for (size_t channel = 0; channel < atlas_textures_.size(); ++channel) {
        result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &texture_description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&atlas_textures_[channel]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(PBR atlas)", result);
            return false;
        }
        atlas_textures_[channel]->SetName(texture_names[channel]);
        if (!create_buffer(device, total_bytes, D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_FLAG_NONE, upload_names[channel],
                           atlas_uploads_[channel], error)) {
            return false;
        }
        result = atlas_uploads_[channel]->Map(0, &no_read, &mapped);
        if (FAILED(result)) {
            error = hresult_error("ID3D12Resource::Map(PBR atlas upload)", result);
            return false;
        }
        for (UINT row = 0; row < row_count; ++row) {
            std::memcpy(
                static_cast<uint8_t *>(mapped) + footprint.Offset +
                    static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                atlas_pixels_[channel].data() +
                    static_cast<size_t>(row) * atlas_width_ * 4u,
                static_cast<size_t>(atlas_width_) * 4u);
        }
        atlas_uploads_[channel]->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = atlas_textures_[channel].Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = atlas_uploads_[channel].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    std::array<D3D12_RESOURCE_BARRIER, 8> uploads = {
        transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(material_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(emitter_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[2].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[3].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[4].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    command_list->ResourceBarrier(static_cast<UINT>(uploads.size()), uploads.data());

    D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.Transform3x4 = 0;
    geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = static_cast<UINT>(vertices_.size());
    geometry.Triangles.VertexBuffer.StartAddress = vertex_address();
    geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrSceneVertex);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs = {};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blas_inputs.NumDescs = 1;
    blas_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0u ||
        blas_info.ScratchDataSizeInBytes == 0u ||
        !create_buffer(device, blas_info.ScratchDataSizeInBytes,
                       D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR BLAS Scratch", blas_scratch_, error) ||
        !create_buffer(device, blas_info.ResultDataMaxSizeInBytes,
                       D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR World BLAS", blas_, error)) {
        if (error.empty()) {
            error = "DXR BLAS prebuild returned zero-sized storage";
        }
        return false;
    }
    const D3D12_RESOURCE_BARRIER blas_scratch_state = transition(
        blas_scratch_.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &blas_scratch_state);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build = {};
    blas_build.Inputs = blas_inputs;
    blas_build.ScratchAccelerationStructureData =
        blas_scratch_->GetGPUVirtualAddress();
    blas_build.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER blas_barrier = uav_barrier(blas_.Get());
    command_list->ResourceBarrier(1, &blas_barrier);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.NumDescs = 1;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    if (tlas_info.ResultDataMaxSizeInBytes == 0u ||
        tlas_info.ScratchDataSizeInBytes == 0u ||
        !create_buffer(device, tlas_info.ScratchDataSizeInBytes,
                       D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR TLAS Scratch", tlas_scratch_, error) ||
        !create_buffer(device, tlas_info.ResultDataMaxSizeInBytes,
                       D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR World TLAS", tlas_, error) ||
        !create_buffer(device, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                       D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, L"AB3D2 DXR TLAS Instance",
                       instance_upload_, error)) {
        if (error.empty()) {
            error = "DXR TLAS prebuild returned zero-sized storage";
        }
        return false;
    }
    const D3D12_RESOURCE_BARRIER tlas_scratch_state = transition(
        tlas_scratch_.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &tlas_scratch_state);
    result = instance_upload_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(TLAS instance)", result);
        return false;
    }
    auto *instance = static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(mapped);
    std::memset(instance, 0, sizeof(*instance));
    instance->Transform[0][0] = 1.0f;
    instance->Transform[1][1] = 1.0f;
    instance->Transform[2][2] = 1.0f;
    instance->InstanceMask = 0xffu;
    instance->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    instance->AccelerationStructure = blas_->GetGPUVirtualAddress();
    instance_upload_->Unmap(0, nullptr);
    tlas_inputs.InstanceDescs = instance_upload_->GetGPUVirtualAddress();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build = {};
    tlas_build.Inputs = tlas_inputs;
    tlas_build.ScratchAccelerationStructureData =
        tlas_scratch_->GetGPUVirtualAddress();
    tlas_build.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER tlas_barrier = uav_barrier(tlas_.Get());
    command_list->ResourceBarrier(1, &tlas_barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC tlas_view = {};
    tlas_view.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    tlas_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    tlas_view.RaytracingAccelerationStructure.Location =
        tlas_->GetGPUVirtualAddress();
    device->CreateShaderResourceView(nullptr, &tlas_view, tlas_descriptor);
    D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {};
    atlas_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    atlas_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    atlas_view.Texture2D.MipLevels = 1;
    for (size_t channel = 0; channel < atlas_textures_.size(); ++channel) {
        atlas_view.Format =
            (channel == static_cast<size_t>(DxrMaterialChannel::base_color) ||
             channel == static_cast<size_t>(DxrMaterialChannel::emissive)) ?
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        device->CreateShaderResourceView(atlas_textures_[channel].Get(), &atlas_view,
                                         atlas_descriptors[channel]);
    }
    debug_output("DXR SceneFrame build: " + std::to_string(triangle_count()) +
                 " triangles, " + std::to_string(materials_.size()) +
                 " PBR-capable materials, " +
                 std::to_string(emitter_count()) + " emissive triangles");
    return true;
}

}  // namespace ab3d2::dxr
