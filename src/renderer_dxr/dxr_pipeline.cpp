#include "dxr_pipeline.h"

#include "dxr_blue_noise.h"
#include "dxr_debug.h"
#include "scene_geometry_compile.h"
#if defined(AB3D2_ENABLE_STREAMLINE)
#include "dxr_streamline.h"
#endif

#include <dxgi1_6.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace ab3d2::dxr {

namespace {

enum DescriptorIndex : UINT {
    noisy_radiance_uav = 0,
    diffuse_albedo_uav = 1,
    specular_albedo_uav = 2,
    shading_normal_uav = 3,
    linear_roughness_uav = 4,
    linear_depth_uav = 5,
    scene_motion_uav = 6,
    specular_hit_distance_uav = 7,
    specular_hit_distance_history_uav = 8,
    scene_tlas = 9,
    base_color_atlas = 10,
    normal_atlas = 11,
    metalness_atlas = 12,
    roughness_atlas = 13,
    emissive_atlas = 14,
    reconstruction_srv_start = 15,
    descriptor_count = 24,
};

constexpr std::array<DescriptorIndex,
                     static_cast<size_t>(DxrReconstructionBuffer::count)>
    reconstruction_uavs = {
        noisy_radiance_uav,
        diffuse_albedo_uav,
        specular_albedo_uav,
        shading_normal_uav,
        linear_roughness_uav,
        linear_depth_uav,
        scene_motion_uav,
        specular_hit_distance_uav,
        specular_hit_distance_history_uav,
    };

constexpr std::array<DXGI_FORMAT,
                     static_cast<size_t>(DxrReconstructionBuffer::count)>
    reconstruction_formats = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
    };

constexpr std::array<const wchar_t *,
                     static_cast<size_t>(DxrReconstructionBuffer::count)>
    reconstruction_names = {
        L"AB3D2 Fresh Noisy HDR Radiance",
        L"AB3D2 RR Diffuse Albedo",
        L"AB3D2 RR Specular Albedo",
        L"AB3D2 RR World Shading Normal",
        L"AB3D2 RR Linear Roughness",
        L"AB3D2 RR Linear View Depth",
        L"AB3D2 RR Scene Motion Pixels",
        L"AB3D2 RR Specular Hit Distance",
        L"AB3D2 RR Specular Hit Distance History",
    };

/*
 * Emitter candidates resampled per primary hit, and the number of candidates a
 * pixel's reservoir history is allowed to stand for.
 *
 * Reservoir resampling is enabled by default on explicit user direction. These
 * are the values measured best among the enabled configurations, not the values
 * measured best overall: Phase 11c in DXR_RAY_RECONSTRUCTION_PLAN.md records that
 * resampling lowers the path-traced input's variance while *raising* the
 * reconstructed image's residual frame-to-frame difference, because it trades
 * high-frequency screen-space blue noise for error correlated across neighbouring
 * pixels and across frames. Disabling it entirely still measures most stable by
 * that metric, which is a temporal metric and cannot see the spatial character of
 * the error it trades away.
 *
 * Within the enabled configurations the candidate count dominates and the history
 * cap is nearly irrelevant: four candidates measure 1.4046 against 1.4674 for
 * thirty-two, while sweeping the cap from 64 to 640 moves the result by under
 * half a percent. The single-sample blue-noise estimator (1 candidate, limit 0)
 * measures 1.1896 against 1.40+ for every resampling configuration, because its
 * error is high-frequency and spatially decorrelated — exactly what Ray
 * Reconstruction is designed to filter.
 *
 * `AB3D2_DXR_CANDIDATES` and `AB3D2_DXR_RESERVOIR_LIMIT` override both, and a
 * limit of zero disables temporal reuse to recover the single-sample estimator.
 */
constexpr uint32_t reservoir_candidate_count = 1u;
constexpr uint32_t reservoir_sample_limit = 0u;

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
    uint32_t sample_index;
    float camera_up[3];
    uint32_t maximum_depth;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t triangle_count;
    uint32_t emitter_count;
    float previous_camera_position[3];
    uint32_t history_valid;
    float previous_camera_forward[3];
    float previous_tan_half_fov_y;
    float previous_camera_right[3];
    float previous_aspect;
    float previous_camera_up[3];
    float jitter_x;
    float jitter_y;
    float previous_jitter_x;
    float previous_jitter_y;
    uint32_t candidate_count;
    uint32_t reservoir_sample_limit;
    float radiance_clamp;
};

/*
 * The ray root signature spends two DWORDs per root descriptor and one per
 * descriptor table, so this block plus the current bindings sits at roughly 58 of
 * the 64 available DWORDs. Move this to a constant buffer view rather than
 * trimming it if more constants are needed.
 */
static_assert(sizeof(FrameConstants) == 42u * sizeof(uint32_t));

struct PresentConstants {
    uint32_t debug_view;
    float scalar_range;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t target_width;
    uint32_t target_height;
};

static_assert(sizeof(PresentConstants) == 6u * sizeof(uint32_t));

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

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource *resource)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
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

reconstruction::CameraProjection camera_projection(
    const SceneCamera &camera, const RenderView &view, UINT width, UINT height)
{
    const float yaw = static_cast<float>(camera.yaw) * (2.0f * pi / 8192.0f);
    const float pitch = view.pitch_degrees * (pi / 180.0f);
    const SceneRenderPoint eye = scene_render_camera_point(&camera);
    reconstruction::CameraProjection result = {};
    result.position = {eye.x, eye.y, eye.z};
    result.tan_half_fov_y = 1.0f /
        (16.0f / (15.0f * source_fullscreen_depth_scale));
    result.forward = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                      std::cos(yaw) * std::cos(pitch)};
    result.aspect = static_cast<float>(width) / static_cast<float>(height);
    result.right = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    result.up = {
        result.right.z * result.forward.y,
        result.forward.z * result.right.x -
            result.forward.x * result.right.z,
        -result.right.x * result.forward.y,
    };
    result.width = width;
    result.height = height;
    return result;
}

void copy_vector(float destination[3], brdf::Vec3 source)
{
    destination[0] = source.x;
    destination[1] = source.y;
    destination[2] = source.z;
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

bool DxrPipeline::configure_debug_view(std::string &error)
{
    constexpr std::array<const char *,
                         static_cast<size_t>(DxrReconstructionBuffer::count)>
        names = {
            "noisy",
            "diffuse-albedo",
            "specular-albedo",
            "normal",
            "roughness",
            "depth",
            "motion",
            "specular-hit-distance",
            "specular-hit-distance-history",
        };
    char value[64] = {};
    const DWORD length = GetEnvironmentVariableA(
        "AB3D2_DXR_DEBUG_VIEW", value, static_cast<DWORD>(sizeof(value)));
    if (length >= sizeof(value)) {
        error = "AB3D2_DXR_DEBUG_VIEW exceeds 63 bytes";
        return false;
    }
    debug_view_ = 0u;
    debug_view_requested_ = length != 0u;
    if (length != 0u) {
        const auto found = std::find_if(
            names.begin(), names.end(),
            [&value](const char *name) { return std::strcmp(value, name) == 0; });
        if (found == names.end()) {
            error = "AB3D2_DXR_DEBUG_VIEW must be noisy, diffuse-albedo, "
                    "specular-albedo, normal, roughness, depth, motion, "
                    "specular-hit-distance, or "
                    "specular-hit-distance-history";
            return false;
        }
        debug_view_ = static_cast<uint32_t>(found - names.begin());
    }

    char range_text[64] = {};
    const DWORD range_length = GetEnvironmentVariableA(
        "AB3D2_DXR_DEBUG_RANGE", range_text,
        static_cast<DWORD>(sizeof(range_text)));
    if (range_length >= sizeof(range_text)) {
        error = "AB3D2_DXR_DEBUG_RANGE exceeds 63 bytes";
        return false;
    }
    debug_scalar_range_ = debug_view_ == static_cast<uint32_t>(
        DxrReconstructionBuffer::scene_motion) ? 32.0f :
        reconstruction::scene_far_plane;
    if (range_length != 0u) {
        char *end = nullptr;
        errno = 0;
        const float parsed = std::strtof(range_text, &end);
        if (errno != 0 || end == range_text || *end != '\0' ||
            !std::isfinite(parsed) || !(parsed > 0.0f)) {
            error = "AB3D2_DXR_DEBUG_RANGE must be a finite positive number";
            return false;
        }
        debug_scalar_range_ = parsed;
    }
    if (debug_view_ != 0u) {
        debug_output(std::string("DXR reconstruction debug view: ") +
                     names[debug_view_]);
    }
    return true;
}

/*
 * Reads the two resampling counts from the environment so the candidate count and
 * the history cap can be swept against the `--gpu-smoke` stability metric without
 * a rebuild. The trade-off they control is real: a larger cap removes more
 * variance from the path-traced input, but it also makes a pixel hold the same
 * light sample for longer, which turns the residual error low frequency and
 * temporally correlated.
 */
bool DxrPipeline::configure_resampling(std::string &error)
{
    candidate_count_ = reservoir_candidate_count;
    reservoir_sample_limit_ = reservoir_sample_limit;
    struct Override {
        const char *name;
        uint32_t minimum;
        uint32_t limit;
        uint32_t *target;
    };
    /* A reservoir limit of zero is a meaningful diagnostic setting rather than an
     * error: it gives every historical sample no weight, which isolates the
     * resampling from the temporal reuse. */
    const std::array<Override, 2> overrides = {
        Override{"AB3D2_DXR_CANDIDATES", 1u, 1024u, &candidate_count_},
        Override{"AB3D2_DXR_RESERVOIR_LIMIT", 0u, 65536u,
                 &reservoir_sample_limit_},
    };
    {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_RADIANCE_CLAMP", value,
            static_cast<DWORD>(sizeof(value)));
        if (length > 0u && length < sizeof(value)) {
            char *end = nullptr;
            errno = 0;
            const double parsed = std::strtod(value, &end);
            if (errno == 0 && end != value && *end == '\0' &&
                parsed >= 1.0 && parsed <= 100000.0) {
                radiance_clamp_ = static_cast<float>(parsed);
            }
        }
    }
    for (const Override &entry : overrides) {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            entry.name, value, static_cast<DWORD>(sizeof(value)));
        if (length >= sizeof(value)) {
            error = std::string(entry.name) + " exceeds 63 bytes";
            return false;
        }
        if (length == 0u) {
            continue;
        }
        char *end = nullptr;
        errno = 0;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (errno != 0 || end == value || *end != '\0' ||
            parsed < entry.minimum || parsed > entry.limit) {
            error = std::string(entry.name) + " must be " +
                std::to_string(entry.minimum) + "-" +
                std::to_string(entry.limit);
            return false;
        }
        *entry.target = static_cast<uint32_t>(parsed);
    }
    debug_output("DXR emitter resampling: candidates=" +
                 std::to_string(candidate_count_) + " reservoir limit=" +
                 std::to_string(reservoir_sample_limit_) + " radiance clamp=" +
                 std::to_string(radiance_clamp_));
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
    range.NumDescriptors =
        static_cast<UINT>(DxrReconstructionBuffer::count);
    range.BaseShaderRegister = 0;
    std::array<D3D12_ROOT_PARAMETER, 2> parameters = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.Num32BitValues =
        sizeof(PresentConstants) / sizeof(uint32_t);
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.NumParameters = static_cast<UINT>(parameters.size());
    root_description.pParameters = parameters.data();
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
    ranges[0].NumDescriptors =
        static_cast<UINT>(DxrReconstructionBuffer::count);
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = 5;
    ranges[2].BaseShaderRegister = 3;
    std::array<D3D12_ROOT_PARAMETER, 11> parameters = {};
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
    parameters[5].Descriptor.ShaderRegister = 8;
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[6].Descriptor.ShaderRegister = 9;
    parameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[7].Descriptor.ShaderRegister = 10;
    parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[8].Constants.Num32BitValues = sizeof(FrameConstants) / sizeof(uint32_t);
    parameters[8].Constants.ShaderRegister = 0;
    /* Both reservoir buffers bind as unordered-access root descriptors, which
     * keeps them in one resource state for the whole frame. */
    parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[9].Descriptor.ShaderRegister = 9;
    parameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[10].Descriptor.ShaderRegister = 10;
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

bool DxrPipeline::create_blue_noise_sampler(ID3D12Device5 *device,
                                            std::string &error)
{
    std::filesystem::path directory;
    if (!executable_directory(directory, error)) {
        return false;
    }
    const std::filesystem::path path =
        directory / L"renderer_dxr" / L"blue_noise_spp256.bin";
    std::error_code file_error;
    const uintmax_t file_size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        error = "DXR blue-noise sampler is unavailable: " + path_text(path) +
                " (" + file_error.message() + ")";
        return false;
    }
    if (file_size != blue_noise::package_size) {
        error = "DXR blue-noise sampler has an invalid size: " +
                path_text(path) + " (expected " +
                std::to_string(blue_noise::package_size) + " bytes, found " +
                std::to_string(file_size) + ")";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))) {
        error = "DXR blue-noise sampler could not be read completely: " +
                path_text(path);
        return false;
    }

    const D3D12_HEAP_PROPERTIES upload_heap =
        heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description =
        buffer_description(blue_noise::package_size);
    HRESULT result = device->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&blue_noise_sampler_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(blue-noise sampler)",
            result);
        return false;
    }
    blue_noise_sampler_->SetName(L"AB3D2 Blue-Noise Sobol Sampler");
    void *mapped = nullptr;
    const D3D12_RANGE no_read = {0, 0};
    result = blue_noise_sampler_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(blue-noise sampler)",
                              result);
        return false;
    }
    std::memcpy(mapped, bytes.data(), bytes.size());
    blue_noise_sampler_->Unmap(0, nullptr);
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

bool DxrPipeline::ensure_reconstruction_targets(ID3D12Device5 *device,
                                                 UINT width, UINT height,
                                                 UINT present_width,
                                                 UINT present_height,
                                                 bool create_streamline_output,
                                                 bool &recreated,
                                                 std::string &error)
{
    recreated = false;
    if (reconstruction_targets_[0] && render_width_ == width &&
        render_height_ == height && present_width_ == present_width &&
        present_height_ == present_height &&
        (streamline_output_.Get() != nullptr) == create_streamline_output) {
        return true;
    }
    for (auto &target : reconstruction_targets_) {
        target.Reset();
    }
    for (auto &reservoir : light_reservoirs_) {
        reservoir.Reset();
    }
    streamline_output_.Reset();
    render_width_ = 0;
    render_height_ = 0;
    present_width_ = 0;
    present_height_ = 0;
    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_HEAP_PROPERTIES default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    for (size_t index = 0; index < reconstruction_targets_.size(); ++index) {
        description.Format = reconstruction_formats[index];
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&reconstruction_targets_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(RR guide)", result);
            return false;
        }
        reconstruction_targets_[index]->SetName(reconstruction_names[index]);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            reconstruction_targets_[index].Get(), nullptr, &uav,
            cpu_descriptor(reconstruction_uavs[index]));
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = reconstruction_formats[
        static_cast<size_t>(DxrReconstructionBuffer::noisy_radiance)];
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    for (size_t index = 0; index < reconstruction_targets_.size(); ++index) {
        srv.Format = reconstruction_formats[index];
        device->CreateShaderResourceView(
            reconstruction_targets_[index].Get(), &srv,
            cpu_descriptor(reconstruction_srv_start + static_cast<UINT>(index)));
    }
    if (create_streamline_output) {
        description.Width = present_width;
        description.Height = present_height;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&streamline_output_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(DLSS-RR output)", result);
            return false;
        }
        streamline_output_->SetName(L"AB3D2 DLSS-RR Reconstructed HDR Output");
        if (!debug_view_requested_) {
            srv.Format = description.Format;
            device->CreateShaderResourceView(
                streamline_output_.Get(), &srv,
                cpu_descriptor(reconstruction_srv_start));
        }
    }
    /*
     * One reservoir per render-resolution pixel, double buffered. Their contents
     * are undefined until a frame writes them, which is safe because `recreated`
     * invalidates the renderer history and the ray shader only reads a previous
     * reservoir when that history is valid.
     */
    const D3D12_RESOURCE_DESC reservoir_description = [width, height] {
        D3D12_RESOURCE_DESC reservoir =
            buffer_description(static_cast<UINT64>(width) * height *
                               sizeof(DxrLightReservoir));
        reservoir.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return reservoir;
    }();
    for (size_t index = 0; index < light_reservoirs_.size(); ++index) {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &reservoir_description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&light_reservoirs_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(light reservoir)",
                result);
            return false;
        }
        light_reservoirs_[index]->SetName(index == 0u ?
            L"AB3D2 DXR Light Reservoirs A" :
            L"AB3D2 DXR Light Reservoirs B");
    }
    render_width_ = width;
    render_height_ = height;
    present_width_ = present_width;
    present_height_ = present_height;
    recreated = true;
    return true;
}

ID3D12Resource *DxrPipeline::reconstruction_resource(
    DxrReconstructionBuffer buffer) const
{
    const size_t index = static_cast<size_t>(buffer);
    return index < reconstruction_targets_.size() ?
        reconstruction_targets_[index].Get() : nullptr;
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
    return configure_debug_view(error) &&
        configure_resampling(error) &&
        load_shader(L"diagnostic_vs.dxil", vertex_shader, error) &&
        load_shader(L"diagnostic_ps.dxil", pixel_shader, error) &&
        load_shader(L"present_vs.dxil", present_vertex_shader, error) &&
        create_diagnostic_pipeline(device, vertex_shader, pixel_shader, error) &&
        create_present_pipeline(device, present_vertex_shader, error) &&
        create_blue_noise_sampler(device, error) &&
        create_raytracing_pipeline(device, error) &&
        create_descriptor_heap(device, error);
}

bool DxrPipeline::update_scene(const SceneFrame &frame, bool &requires_flush,
                               std::string &error)
{
    return scene_.update(frame, requires_flush, error);
}

bool DxrPipeline::record(ID3D12Device5 *device,
                         ID3D12GraphicsCommandList4 *command_list,
                         UINT width, UINT height,
                         D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
                         const SceneFrame &frame,
                         const RenderView &view, uint32_t frame_number,
                         uint32_t frame_slot, DxrStreamline *streamline,
                         std::string &error)
{
    if (!device || !command_list) {
        error = "DXR frame recording received incomplete D3D12 state";
        return false;
    }
    const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
                     static_cast<size_t>(DxrMaterialChannel::count)>
        atlas_descriptors = {
        cpu_descriptor(base_color_atlas),
        cpu_descriptor(normal_atlas),
        cpu_descriptor(metalness_atlas),
        cpu_descriptor(roughness_atlas),
        cpu_descriptor(emissive_atlas),
    };
    if (!scene_.record_build(device, command_list, frame_slot,
                             cpu_descriptor(scene_tlas),
                             atlas_descriptors, error)) {
        return false;
    }
    if (!scene_.ready()) {
        history_.valid = false;
        history_.pending = false;
        command_list->SetGraphicsRootSignature(root_signature_.Get());
        command_list->SetPipelineState(pipeline_state_.Get());
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->DrawInstanced(3, 1, 0, 0);
        return true;
    }
    UINT render_width = width;
    UINT render_height = height;
    bool streamline_active = false;
#if defined(AB3D2_ENABLE_STREAMLINE)
    if (!streamline ||
        !streamline->configure_output(width, height, render_width,
                                      render_height, error)) {
        return false;
    }
    streamline_active = streamline->active();
#else
    (void)streamline;
    (void)frame_number;
#endif
    const SceneCamera *camera = find_camera(frame);
    if (!camera) {
        error = "DXR SceneFrame has geometry but no camera command";
        return false;
    }
    bool targets_recreated = false;
    if (!ensure_reconstruction_targets(device, render_width, render_height,
                                       width, height, streamline_active,
                                       targets_recreated, error)) {
        return false;
    }
    const reconstruction::CameraProjection current_camera =
        camera_projection(*camera, view, render_width, render_height);
    const bool history_valid = history_.valid && !targets_recreated &&
        !scene_.history_reset_pending() &&
        history_.history_epoch == frame.history_epoch &&
        history_.input_width == render_width &&
        history_.input_height == render_height;
    const uint32_t sample_index =
        history_valid ? history_.sample_index + 1u : 0u;
    const reconstruction::PixelJitter current_jitter =
        reconstruction::frame_jitter(sample_index);
    const reconstruction::CameraProjection &previous_camera =
        history_valid ? history_.previous_camera : current_camera;
    const reconstruction::PixelJitter previous_jitter = history_valid ?
        history_.previous_jitter : current_jitter;
    FrameConstants constants = {};
    copy_vector(constants.camera_position, current_camera.position);
    constants.tan_half_fov_y = current_camera.tan_half_fov_y;
    copy_vector(constants.camera_forward, current_camera.forward);
    constants.aspect = current_camera.aspect;
    copy_vector(constants.camera_right, current_camera.right);
    constants.sample_index = sample_index;
    copy_vector(constants.camera_up, current_camera.up);
    constants.maximum_depth = 3u;
    constants.atlas_width = scene_.atlas_width();
    constants.atlas_height = scene_.atlas_height();
    constants.triangle_count = scene_.triangle_count();
    constants.emitter_count = scene_.emitter_count();
    copy_vector(constants.previous_camera_position, previous_camera.position);
    constants.history_valid = history_valid ? 1u : 0u;
    copy_vector(constants.previous_camera_forward, previous_camera.forward);
    constants.previous_tan_half_fov_y = previous_camera.tan_half_fov_y;
    copy_vector(constants.previous_camera_right, previous_camera.right);
    constants.previous_aspect = previous_camera.aspect;
    copy_vector(constants.previous_camera_up, previous_camera.up);
    constants.jitter_x = current_jitter.x;
    constants.jitter_y = current_jitter.y;
    constants.previous_jitter_x = previous_jitter.x;
    constants.previous_jitter_y = previous_jitter.y;
    constants.candidate_count = candidate_count_;
    constants.reservoir_sample_limit = reservoir_sample_limit_;
    constants.radiance_clamp = radiance_clamp_;

    ID3D12DescriptorHeap *heaps[] = {descriptor_heap_.Get()};
    command_list->SetDescriptorHeaps(1, heaps);
    command_list->SetComputeRootSignature(ray_root_signature_.Get());
    command_list->SetComputeRootDescriptorTable(
        0, gpu_descriptor(noisy_radiance_uav));
    command_list->SetComputeRootDescriptorTable(1, gpu_descriptor(scene_tlas));
    command_list->SetComputeRootShaderResourceView(2, scene_.vertex_address());
    command_list->SetComputeRootShaderResourceView(3, scene_.material_address());
    command_list->SetComputeRootDescriptorTable(4,
                                                gpu_descriptor(base_color_atlas));
    command_list->SetComputeRootShaderResourceView(5, scene_.emitter_address());
    command_list->SetComputeRootShaderResourceView(
        6, scene_.previous_vertex_address());
    command_list->SetComputeRootShaderResourceView(
        7, blue_noise_sampler_->GetGPUVirtualAddress());
    command_list->SetComputeRoot32BitConstants(
        8, sizeof(constants) / sizeof(uint32_t), &constants, 0);
    const size_t reservoir_slot = sample_index & 1u;
    command_list->SetComputeRootUnorderedAccessView(
        9, light_reservoirs_[reservoir_slot]->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        10, light_reservoirs_[1u - reservoir_slot]->GetGPUVirtualAddress());
    command_list->SetPipelineState1(ray_state_object_.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS table = shader_table_->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch = {};
    dispatch.RayGenerationShaderRecord = {table, shader_record_size};
    dispatch.MissShaderTable = {table + shader_record_size,
                                shader_record_size * 2u, shader_record_size};
    dispatch.HitGroupTable = {table + shader_record_size * 3u, shader_record_size,
                              shader_record_size};
    dispatch.Width = render_width;
    dispatch.Height = render_height;
    dispatch.Depth = 1;
    command_list->DispatchRays(&dispatch);

    std::array<D3D12_RESOURCE_BARRIER,
               static_cast<size_t>(DxrReconstructionBuffer::count)>
        guide_barriers = {};
    for (size_t index = 0; index < guide_barriers.size(); ++index) {
        guide_barriers[index] =
            uav_barrier(reconstruction_targets_[index].Get());
    }
    command_list->ResourceBarrier(static_cast<UINT>(guide_barriers.size()),
                                  guide_barriers.data());
    const D3D12_RESOURCE_BARRIER reservoir_barriers[] = {
        uav_barrier(light_reservoirs_[0].Get()),
        uav_barrier(light_reservoirs_[1].Get()),
    };
    command_list->ResourceBarrier(2, reservoir_barriers);

    /*
     * Publish this frame's specular hit-distance guide as next frame's history.
     * The guide is a reprojected running estimate, so the ray shader has to read
     * a different resource from the one it writes.
     */
    ID3D12Resource *hit_distance =
        reconstruction_resource(DxrReconstructionBuffer::specular_hit_distance);
    ID3D12Resource *hit_distance_history = reconstruction_resource(
        DxrReconstructionBuffer::specular_hit_distance_history);
    const D3D12_RESOURCE_BARRIER to_history_copy[] = {
        transition(hit_distance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(hit_distance_history, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST),
    };
    command_list->ResourceBarrier(2, to_history_copy);
    command_list->CopyResource(hit_distance_history, hit_distance);
    const D3D12_RESOURCE_BARRIER from_history_copy[] = {
        transition(hit_distance, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        transition(hit_distance_history, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    command_list->ResourceBarrier(2, from_history_copy);
#if defined(AB3D2_ENABLE_STREAMLINE)
    if (streamline_active) {
        const DxrStreamlineResources resources = {
            reconstruction_resource(DxrReconstructionBuffer::noisy_radiance),
            streamline_output_.Get(),
            reconstruction_resource(DxrReconstructionBuffer::diffuse_albedo),
            reconstruction_resource(DxrReconstructionBuffer::specular_albedo),
            reconstruction_resource(DxrReconstructionBuffer::shading_normal),
            reconstruction_resource(DxrReconstructionBuffer::linear_roughness),
            reconstruction_resource(DxrReconstructionBuffer::linear_depth),
            reconstruction_resource(DxrReconstructionBuffer::scene_motion),
            reconstruction_resource(
                DxrReconstructionBuffer::specular_hit_distance),
        };
        if (!streamline->evaluate(command_list, frame_number, current_camera,
                                  previous_camera, current_jitter,
                                  history_valid, resources, error)) {
            return false;
        }
        const D3D12_RESOURCE_BARRIER output_barrier =
            uav_barrier(streamline_output_.Get());
        command_list->ResourceBarrier(1, &output_barrier);
    }
#endif
    if (!scene_.record_promote_vertex_history(command_list, error)) {
        return false;
    }

    const bool present_streamline_output =
        streamline_active && !debug_view_requested_;
    ID3D12Resource *present_resource = present_streamline_output ?
        streamline_output_.Get() : reconstruction_targets_[debug_view_].Get();
    const D3D12_RESOURCE_BARRIER to_present_shader = transition(
        present_resource,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &to_present_shader);
    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {
        0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    command_list->OMSetRenderTargets(1, &render_target_view, FALSE, nullptr);
    ID3D12DescriptorHeap *present_heaps[] = {descriptor_heap_.Get()};
    command_list->SetDescriptorHeaps(1, present_heaps);
    command_list->SetGraphicsRootSignature(present_root_signature_.Get());
    command_list->SetPipelineState(present_pipeline_state_.Get());
    command_list->SetGraphicsRootDescriptorTable(
        0, gpu_descriptor(reconstruction_srv_start));
    const PresentConstants present_constants = {
        debug_view_, debug_scalar_range_,
        present_streamline_output ? width : render_width,
        present_streamline_output ? height : render_height,
        width, height};
    command_list->SetGraphicsRoot32BitConstants(
        1, sizeof(present_constants) / sizeof(uint32_t), &present_constants, 0);
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);
    const D3D12_RESOURCE_BARRIER to_next_sample = transition(
        present_resource,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &to_next_sample);
    history_.pending_camera = current_camera;
    history_.pending_jitter = current_jitter;
    history_.pending_history_epoch = frame.history_epoch;
    history_.pending_sample_index = sample_index;
    history_.pending_input_width = render_width;
    history_.pending_input_height = render_height;
    history_.pending = true;
    return true;
}

void DxrPipeline::commit_presented_frame()
{
    if (!history_.pending) {
        return;
    }
    history_.previous_camera = history_.pending_camera;
    history_.previous_jitter = history_.pending_jitter;
    history_.history_epoch = history_.pending_history_epoch;
    history_.sample_index = history_.pending_sample_index;
    history_.input_width = history_.pending_input_width;
    history_.input_height = history_.pending_input_height;
    history_.valid = true;
    history_.pending = false;
    scene_.mark_history_promoted();
}

}  // namespace ab3d2::dxr
