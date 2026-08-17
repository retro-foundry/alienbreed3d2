#include "dxr_pipeline.h"

#include "dxr_debug.h"
#include "scene_geometry_compile.h"

#include <dxgi1_6.h>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace ab3d2::dxr {

namespace {

enum DescriptorIndex : UINT {
    output_uav = 0,
    scene_tlas = 1,
    base_color_atlas = 2,
    normal_atlas = 3,
    metalness_atlas = 4,
    roughness_atlas = 5,
    output_srv = 6,
    descriptor_count = 7,
};

constexpr UINT shader_record_size = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
constexpr UINT shader_table_size = shader_record_size * 4u;
constexpr float pi = 3.14159265358979323846f;
constexpr float source_fullscreen_depth_scale =
    4.0f * (32767.0f / 65536.0f) * (85.0f / 256.0f) * (927.0f / 1024.0f);

struct FrameConstants {
    float camera_position[3];
    float tan_half_fov_y;
    float camera_forward[3];
    float aspect;
    float camera_right[3];
    uint32_t frame_index;
    float camera_up[3];
    uint32_t maximum_depth;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t triangle_count;
    uint32_t emitter_count;
};

static_assert(sizeof(FrameConstants) == 20u * sizeof(uint32_t));

std::string path_text(const std::filesystem::path &path)
{
    const std::string converted = wide_to_utf8(path.c_str());
    return converted.empty() ? "<unprintable shader path>" : converted;
}

bool executable_directory(std::filesystem::path &directory, std::string &error)
{
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));

    if (length == 0 || length >= executable_path.size()) {
        error = hresult_error("GetModuleFileNameW", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    executable_path.resize(length);
    directory = std::filesystem::path(executable_path).parent_path();
    return true;
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

D3D12_RESOURCE_DESC buffer_description(UINT64 size)
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
    return description;
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

bool serialize_root_signature(const D3D12_ROOT_SIGNATURE_DESC &description,
                              ID3D12Device5 *device,
                              Microsoft::WRL::ComPtr<ID3D12RootSignature> &root,
                              const wchar_t *name, std::string &error)
{
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    HRESULT result = D3D12SerializeRootSignature(
        &description, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &messages);
    if (FAILED(result)) {
        error = hresult_error("D3D12SerializeRootSignature", result);
        if (messages && messages->GetBufferPointer()) {
            error += ": ";
            error.append(static_cast<const char *>(messages->GetBufferPointer()),
                         messages->GetBufferSize());
        }
        return false;
    }
    result = device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateRootSignature", result);
        return false;
    }
    root->SetName(name);
    return true;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_description(
    ID3D12RootSignature *root, const std::vector<unsigned char> &vertex_shader,
    const std::vector<unsigned char> &pixel_shader)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
    description.pRootSignature = root;
    description.VS = {vertex_shader.data(), vertex_shader.size()};
    description.PS = {pixel_shader.data(), pixel_shader.size()};
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    description.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.StencilEnable = FALSE;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    return description;
}

const SceneCamera *find_camera(const SceneFrame &frame)
{
    for (size_t index = 0; index < frame.count; ++index) {
        if (frame.commands[index].type == SCENE_COMMAND_CAMERA) {
            return &frame.commands[index].data.camera;
        }
    }
    return nullptr;
}

}  // namespace

bool DxrPipeline::load_shader(const wchar_t *filename,
                              std::vector<unsigned char> &bytes,
                              std::string &error)
{
    std::filesystem::path directory;

    if (!filename || !executable_directory(directory, error)) {
        return false;
    }
    const std::filesystem::path path = directory / L"renderer_dxr" / filename;
    std::error_code file_error;
    const uintmax_t file_size = std::filesystem::file_size(path, file_error);

    if (file_error) {
        error = "DXR shader is unavailable: " + path_text(path) +
                " (" + file_error.message() + ")";
        return false;
    }
    if (file_size == 0 || file_size > std::numeric_limits<size_t>::max()) {
        error = "DXR shader has an invalid size: " + path_text(path);
        return false;
    }
    bytes.resize(static_cast<size_t>(file_size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))) {
        error = "DXR shader could not be read completely: " + path_text(path);
        return false;
    }
    return true;
}

bool DxrPipeline::create_diagnostic_pipeline(
    ID3D12Device5 *device, const std::vector<unsigned char> &vertex_shader,
    const std::vector<unsigned char> &pixel_shader, std::string &error)
{
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (!serialize_root_signature(root_description, device, root_signature_,
                                  L"AB3D2 DXR Diagnostic Root Signature", error)) {
        return false;
    }
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC description = graphics_description(
        root_signature_.Get(), vertex_shader, pixel_shader);
    const HRESULT result = device->CreateGraphicsPipelineState(
        &description, IID_PPV_ARGS(&pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateGraphicsPipelineState(diagnostic)",
                              result);
        return false;
    }
    pipeline_state_->SetName(L"AB3D2 DXR Diagnostic Triangle Pipeline");
    return true;
}

bool DxrPipeline::create_present_pipeline(
    ID3D12Device5 *device, const std::vector<unsigned char> &vertex_shader,
    std::string &error)
{
    std::vector<unsigned char> pixel_shader;
    if (!load_shader(L"present_ps.dxil", pixel_shader, error)) {
        return false;
    }
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.NumParameters = 1;
    root_description.pParameters = &parameter;
    root_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (!serialize_root_signature(root_description, device, present_root_signature_,
                                  L"AB3D2 DXR Noisy Present Root Signature", error)) {
        return false;
    }
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC description = graphics_description(
        present_root_signature_.Get(), vertex_shader, pixel_shader);
    const HRESULT result = device->CreateGraphicsPipelineState(
        &description, IID_PPV_ARGS(&present_pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateGraphicsPipelineState(noisy present)",
                              result);
        return false;
    }
    present_pipeline_state_->SetName(L"AB3D2 DXR Noisy HDR Present Pipeline");
    return true;
}

bool DxrPipeline::create_raytracing_pipeline(ID3D12Device5 *device,
                                             std::string &error)
{
    std::vector<unsigned char> library;
    if (!load_shader(L"path_trace.dxil", library, error)) {
        return false;
    }
    std::array<D3D12_DESCRIPTOR_RANGE, 3> ranges = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = 4;
    ranges[2].BaseShaderRegister = 3;
    std::array<D3D12_ROOT_PARAMETER, 7> parameters = {};
    for (UINT index : {0u, 1u, 4u}) {
        const UINT range_index = index == 4u ? 2u : index;
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[index].DescriptorTable.pDescriptorRanges = &ranges[range_index];
    }
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[2].Descriptor.ShaderRegister = 1;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[3].Descriptor.ShaderRegister = 2;
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[5].Descriptor.ShaderRegister = 7;
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[6].Constants.Num32BitValues = sizeof(FrameConstants) / sizeof(uint32_t);
    parameters[6].Constants.ShaderRegister = 0;
    for (D3D12_ROOT_PARAMETER &parameter : parameters) {
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.NumParameters = static_cast<UINT>(parameters.size());
    root_description.pParameters = parameters.data();
    if (!serialize_root_signature(root_description, device, ray_root_signature_,
                                  L"AB3D2 DXR Path Trace Global Root Signature",
                                  error)) {
        return false;
    }

    static constexpr wchar_t ray_generation[] = L"RayGeneration";
    static constexpr wchar_t surface_miss[] = L"SurfaceMiss";
    static constexpr wchar_t shadow_miss[] = L"ShadowMiss";
    static constexpr wchar_t closest_hit[] = L"ClosestHit";
    static constexpr wchar_t hit_group_name[] = L"HitGroup";
    std::array<D3D12_EXPORT_DESC, 4> exports = {};
    exports[0].Name = ray_generation;
    exports[1].Name = surface_miss;
    exports[2].Name = shadow_miss;
    exports[3].Name = closest_hit;
    D3D12_DXIL_LIBRARY_DESC library_description = {};
    library_description.DXILLibrary = {library.data(), library.size()};
    library_description.NumExports = static_cast<UINT>(exports.size());
    library_description.pExports = exports.data();
    D3D12_HIT_GROUP_DESC hit_group = {};
    hit_group.HitGroupExport = hit_group_name;
    hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group.ClosestHitShaderImport = closest_hit;
    D3D12_RAYTRACING_SHADER_CONFIG shader_configuration = {};
    shader_configuration.MaxPayloadSizeInBytes = 20u;
    shader_configuration.MaxAttributeSizeInBytes = 8u;
    std::array<const wchar_t *, 4> configured_exports = {
        ray_generation, surface_miss, shadow_miss, hit_group_name};
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shader_association = {};
    shader_association.NumExports = static_cast<UINT>(configured_exports.size());
    shader_association.pExports = configured_exports.data();
    D3D12_GLOBAL_ROOT_SIGNATURE global_root = {ray_root_signature_.Get()};
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_configuration = {4u};
    std::array<D3D12_STATE_SUBOBJECT, 6> subobjects = {};
    subobjects[0] = {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library_description};
    subobjects[1] = {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group};
    subobjects[2] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
                     &shader_configuration};
    shader_association.pSubobjectToAssociate = &subobjects[2];
    subobjects[3] = {D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
                     &shader_association};
    subobjects[4] = {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root};
    subobjects[5] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
                     &pipeline_configuration};
    D3D12_STATE_OBJECT_DESC state_description = {};
    state_description.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_description.NumSubobjects = static_cast<UINT>(subobjects.size());
    state_description.pSubobjects = subobjects.data();
    HRESULT result = device->CreateStateObject(&state_description,
                                                IID_PPV_ARGS(&ray_state_object_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device5::CreateStateObject(path tracer)", result);
        return false;
    }
    ray_state_object_->SetName(L"AB3D2 DXR Fresh-Sample Path Tracer");

    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
    result = ray_state_object_.As(&properties);
    if (FAILED(result)) {
        error = hresult_error("Query ID3D12StateObjectProperties", result);
        return false;
    }
    const D3D12_HEAP_PROPERTIES upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC table_description = buffer_description(shader_table_size);
    result = device->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &table_description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shader_table_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateCommittedResource(shader table)",
                              result);
        return false;
    }
    shader_table_->SetName(L"AB3D2 DXR Shader Table");
    void *mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    result = shader_table_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(shader table)", result);
        return false;
    }
    std::memset(mapped, 0, shader_table_size);
    const void *identifiers[] = {
        properties->GetShaderIdentifier(ray_generation),
        properties->GetShaderIdentifier(surface_miss),
        properties->GetShaderIdentifier(shadow_miss),
        properties->GetShaderIdentifier(hit_group_name),
    };
    for (UINT index = 0; index < 4u; ++index) {
        if (!identifiers[index]) {
            shader_table_->Unmap(0, nullptr);
            error = "DXR state object did not expose every shader identifier";
            return false;
        }
        std::memcpy(static_cast<uint8_t *>(mapped) + index * shader_record_size,
                    identifiers[index], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    }
    shader_table_->Unmap(0, nullptr);
    return true;
}

bool DxrPipeline::create_descriptor_heap(ID3D12Device5 *device,
                                         std::string &error)
{
    D3D12_DESCRIPTOR_HEAP_DESC description = {};
    description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    description.NumDescriptors = descriptor_count;
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    const HRESULT result = device->CreateDescriptorHeap(
        &description, IID_PPV_ARGS(&descriptor_heap_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateDescriptorHeap(DXR resources)",
                              result);
        return false;
    }
    descriptor_heap_->SetName(L"AB3D2 DXR Resource Descriptor Heap");
    descriptor_size_ = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (descriptor_size_ == 0u) {
        error = "D3D12 returned a zero DXR resource descriptor increment";
        return false;
    }
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE DxrPipeline::cpu_descriptor(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptor_size_;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DxrPipeline::gpu_descriptor(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle =
        descriptor_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * descriptor_size_;
    return handle;
}

bool DxrPipeline::ensure_output(ID3D12Device5 *device, UINT width, UINT height,
                                std::string &error)
{
    if (noisy_radiance_ && output_width_ == width && output_height_ == height) {
        return true;
    }
    noisy_radiance_.Reset();
    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_HEAP_PROPERTIES default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT result = device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&noisy_radiance_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateCommittedResource(noisy HDR)",
                              result);
        return false;
    }
    noisy_radiance_->SetName(L"AB3D2 Fresh Noisy HDR Radiance");
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = description.Format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(noisy_radiance_.Get(), nullptr, &uav,
                                      cpu_descriptor(output_uav));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = description.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(noisy_radiance_.Get(), &srv,
                                     cpu_descriptor(output_srv));
    output_width_ = width;
    output_height_ = height;
    return true;
}

bool DxrPipeline::initialize(ID3D12Device5 *device, std::string &error)
{
    std::vector<unsigned char> vertex_shader;
    std::vector<unsigned char> pixel_shader;
    std::vector<unsigned char> present_vertex_shader;

    if (!device) {
        error = "DXR pipeline received no D3D12 device";
        return false;
    }
    return load_shader(L"diagnostic_vs.dxil", vertex_shader, error) &&
        load_shader(L"diagnostic_ps.dxil", pixel_shader, error) &&
        load_shader(L"present_vs.dxil", present_vertex_shader, error) &&
        create_diagnostic_pipeline(device, vertex_shader, pixel_shader, error) &&
        create_present_pipeline(device, present_vertex_shader, error) &&
        create_raytracing_pipeline(device, error) &&
        create_descriptor_heap(device, error);
}

bool DxrPipeline::update_scene(const SceneFrame &frame, bool &changed,
                               std::string &error)
{
    return scene_.update(frame, changed, error);
}

bool DxrPipeline::record(ID3D12Device5 *device,
                         ID3D12GraphicsCommandList4 *command_list,
                         UINT width, UINT height, const SceneFrame &frame,
                         const RenderView &view, uint32_t frame_number,
                         std::string &error)
{
    if (!device || !command_list) {
        error = "DXR frame recording received incomplete D3D12 state";
        return false;
    }
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> atlas_descriptors = {
        cpu_descriptor(base_color_atlas),
        cpu_descriptor(normal_atlas),
        cpu_descriptor(metalness_atlas),
        cpu_descriptor(roughness_atlas),
    };
    if (!scene_.record_build(device, command_list, cpu_descriptor(scene_tlas),
                             atlas_descriptors, error)) {
        return false;
    }
    if (!scene_.ready()) {
        command_list->SetGraphicsRootSignature(root_signature_.Get());
        command_list->SetPipelineState(pipeline_state_.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->DrawInstanced(3, 1, 0, 0);
        return true;
    }
    const SceneCamera *camera = find_camera(frame);
    if (!camera) {
        error = "DXR SceneFrame has geometry but no camera command";
        return false;
    }
    if (!ensure_output(device, width, height, error)) {
        return false;
    }
    const float yaw = static_cast<float>(camera->yaw) * (2.0f * pi / 8192.0f);
    const float pitch = view.pitch_degrees * (pi / 180.0f);
    const SceneRenderPoint eye = scene_render_camera_point(camera);
    FrameConstants constants = {};
    constants.camera_position[0] = eye.x;
    constants.camera_position[1] = eye.y;
    constants.camera_position[2] = eye.z;
    constants.tan_half_fov_y = 1.0f /
        (16.0f / (15.0f * source_fullscreen_depth_scale));
    constants.camera_forward[0] = std::sin(yaw) * std::cos(pitch);
    constants.camera_forward[1] = std::sin(pitch);
    constants.camera_forward[2] = std::cos(yaw) * std::cos(pitch);
    constants.aspect = static_cast<float>(width) / static_cast<float>(height);
    constants.camera_right[0] = std::cos(yaw);
    constants.camera_right[2] = -std::sin(yaw);
    constants.frame_index = frame_number;
    constants.camera_up[0] = constants.camera_right[2] * constants.camera_forward[1];
    constants.camera_up[1] = constants.camera_forward[2] * constants.camera_right[0] -
        constants.camera_forward[0] * constants.camera_right[2];
    constants.camera_up[2] = -constants.camera_right[0] * constants.camera_forward[1];
    constants.maximum_depth = 3u;
    constants.atlas_width = scene_.atlas_width();
    constants.atlas_height = scene_.atlas_height();
    constants.triangle_count = scene_.triangle_count();
    constants.emitter_count = scene_.emitter_count();

    ID3D12DescriptorHeap *heaps[] = {descriptor_heap_.Get()};
    command_list->SetDescriptorHeaps(1, heaps);
    command_list->SetComputeRootSignature(ray_root_signature_.Get());
    command_list->SetComputeRootDescriptorTable(0, gpu_descriptor(output_uav));
    command_list->SetComputeRootDescriptorTable(1, gpu_descriptor(scene_tlas));
    command_list->SetComputeRootShaderResourceView(2, scene_.vertex_address());
    command_list->SetComputeRootShaderResourceView(3, scene_.material_address());
    command_list->SetComputeRootDescriptorTable(4,
                                                gpu_descriptor(base_color_atlas));
    command_list->SetComputeRootShaderResourceView(5, scene_.emitter_address());
    command_list->SetComputeRoot32BitConstants(
        6, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    command_list->SetPipelineState1(ray_state_object_.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS table = shader_table_->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch = {};
    dispatch.RayGenerationShaderRecord = {table, shader_record_size};
    dispatch.MissShaderTable = {table + shader_record_size,
                                shader_record_size * 2u, shader_record_size};
    dispatch.HitGroupTable = {table + shader_record_size * 3u, shader_record_size,
                              shader_record_size};
    dispatch.Width = width;
    dispatch.Height = height;
    dispatch.Depth = 1;
    command_list->DispatchRays(&dispatch);

    const D3D12_RESOURCE_BARRIER to_present_shader = transition(
        noisy_radiance_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &to_present_shader);
    command_list->SetGraphicsRootSignature(present_root_signature_.Get());
    command_list->SetPipelineState(present_pipeline_state_.Get());
    command_list->SetGraphicsRootDescriptorTable(0, gpu_descriptor(output_srv));
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);
    const D3D12_RESOURCE_BARRIER to_next_sample = transition(
        noisy_radiance_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &to_next_sample);
    return true;
}

}  // namespace ab3d2::dxr
