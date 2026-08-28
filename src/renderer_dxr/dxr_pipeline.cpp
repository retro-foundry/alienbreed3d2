#include "dxr_pipeline.h"

#include "dxr_blue_noise.h"
#include "dxr_debug.h"
#include "dxr_indirect_reconstruction.h"
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
    diffuse_hit_distance_uav = 8,
    diffuse_hit_distance_history_uav = 9,
    scene_tlas = 10,
    base_color_atlas = 11,
    normal_atlas = 12,
    metalness_atlas = 13,
    roughness_atlas = 14,
    emissive_atlas = 15,
    reconstruction_srv_start = 16,
    indirect_radiance_srv = 26,
    tone_map_state_srv = 27,
    diagnostics_uav = 28,
    light_grid_uav = 29,
    indirect_radiance_uav = 30,
    indirect_history_uav_start = 31,
    indirect_filtered_uav = 33,
    automatic_exposure_uav = 34,
    indirect_chroma_uav = 35,
    indirect_chroma_filtered_uav = 36,
    indirect_gradient_uav_start = 37,
    gi_reservoir_uav_start = 39,
    gi_reservoir_scratch_uav = 41,
    streamline_scene_motion_uav = 42,
    view_weapon_history_uav_start = 43,
    rr_disocclusion_mask_uav = 45,
    rr_bias_current_color_mask_uav = 46,
    surface_parameters_uav = 47,
    post_input_srv = 48,
    post_histogram_uav = 49,
    post_tone_map_state_uav = 50,
    bloom_srv_start = 51,
    post_hdr_srv = 57,
    bloom_uav_start = 58,
    post_hdr_uav = 64,
    descriptor_count = 65,
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
        diffuse_hit_distance_uav,
        diffuse_hit_distance_history_uav,
    };

constexpr std::array<DXGI_FORMAT,
                     static_cast<size_t>(DxrReconstructionBuffer::count)>
    reconstruction_formats = {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16_FLOAT,
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
        L"AB3D2 RR Diffuse Hit Distance",
        L"AB3D2 RR Diffuse Hit Distance History",
    };

/*
 * Fresh polygon-light candidates per surface vertex and the maximum number of
 * frames accumulated by low-frequency indirect reconstruction. Direct-light
 * reservoirs remain current-frame only; the history cap belongs to the
 * demodulated indirect channel.
 *
 * `AB3D2_DXR_CANDIDATES` and `AB3D2_DXR_RESERVOIR_LIMIT` override both, and a
 * limit of zero disables only indirect temporal accumulation.
 */
constexpr UINT shader_record_size = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
enum ShaderRecordIndex : UINT {
    shader_record_build_light_grid = 0u,
    shader_record_ray_generation,
    shader_record_temporal_gi,
    shader_record_spatial_gi,
    shader_record_spatial_shade,
    shader_record_build_indirect_gradient,
    shader_record_filter_indirect_gradient_0,
    shader_record_filter_indirect_gradient_1,
    shader_record_filter_indirect_gradient_2,
    shader_record_filter_indirect_gradient_3,
    shader_record_filter_indirect_gradient_4,
    shader_record_filter_indirect_gradient_5,
    shader_record_filter_indirect_gradient_6,
    shader_record_temporal_indirect,
    shader_record_filter_indirect_0,
    shader_record_deflicker_indirect,
    shader_record_filter_indirect_1,
    shader_record_filter_indirect_2,
    shader_record_filter_indirect_3,
    shader_record_resolve_indirect_filtered,
    shader_record_reconstruct_indirect,
    shader_record_calculate_automatic_exposure,
    shader_record_surface_miss,
    shader_record_shadow_miss,
    shader_record_hit_group,
    shader_record_count,
};
constexpr UINT shader_table_size = shader_record_size * shader_record_count;
constexpr UINT diagnostic_value_count = 15u;
constexpr UINT tone_map_histogram_bin_count = 128u;
constexpr UINT tone_map_state_value_count =
    tone_map_histogram_bin_count + 1u + 5u;
constexpr UINT bloom_target_count = 6u;
enum BloomOperation : uint32_t {
    bloom_extract = 0u,
    bloom_downsample,
    bloom_blur_horizontal,
    bloom_blur_vertical,
    bloom_upsample,
    bloom_composite,
};
enum BloomTargetIndex : UINT {
    bloom_half_a = 0u,
    bloom_half_b,
    bloom_quarter_a,
    bloom_quarter_b,
    bloom_eighth_a,
    bloom_eighth_b,
};
constexpr UINT64 frame_constant_stride =
    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
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
    uint32_t output_width;
    uint32_t output_height;
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
    float ndf_trim;
    uint32_t samples_per_pixel;
    float exposure_delta_seconds;
    uint32_t indirect_reconstruction_mode;
    uint32_t radiance_channel;
    uint32_t rr_weapon_pose_transition;
    uint32_t indirect_samples_per_pixel;
    float light_grid_center[3];
    uint32_t light_grid_rebuild;
    uint32_t ray_reconstruction_active;
    uint32_t diagnostic_guide_mask;
    float diffuse_gi_scale;
};

/*
 * Frame constants live in one 256-byte upload slice per in-flight frame. A
 * root CBV costs two root-signature DWORDs regardless of this structure's
 * size, leaving room for future bindings without trimming camera or exposure
 * state.
 */
static_assert(sizeof(FrameConstants) == 56u * sizeof(uint32_t));
static_assert(sizeof(FrameConstants) <= frame_constant_stride);

struct PresentConstants {
    uint32_t debug_view;
    float scalar_range;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t target_width;
    uint32_t target_height;
    float exposure_bias_stops;
    uint32_t frame_index;
    uint32_t hdr_output;
    float hdr_peak_nits;
    float hdr_saturation_scale;
};

static_assert(sizeof(PresentConstants) == 11u * sizeof(uint32_t));

struct PostConstants {
    uint32_t source_width;
    uint32_t source_height;
    uint32_t target_width;
    uint32_t target_height;
    float delta_seconds;
    uint32_t reset_history;
    uint32_t bloom_operation;
    uint32_t reserved;
};

static_assert(sizeof(PostConstants) == 8u * sizeof(uint32_t));

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
    const std::vector<unsigned char> &pixel_shader,
    DXGI_FORMAT render_target_format)
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
    description.RTVFormats[0] = render_target_format;
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
                         static_cast<size_t>(DxrReconstructionBuffer::count) + 1u>
        names = {
            "noisy",
            "diffuse-albedo",
            "specular-albedo",
            "normal",
            "roughness",
            "depth",
            "motion",
            "specular-hit-distance",
            "diffuse-hit-distance",
            "diffuse-hit-distance-history",
            "indirect",
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
                    "specular-hit-distance, diffuse-hit-distance, "
                    "diffuse-hit-distance-history, or indirect";
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

    constexpr std::array<const char *, 8> indirect_mode_names = {
        "full", "temporal", "raw", "regional", "deflicker", "wavelet1",
        "wavelet2", "restir"};
    char reconstruction_value[64] = {};
    const DWORD reconstruction_length = GetEnvironmentVariableA(
        "AB3D2_DXR_INDIRECT_RECONSTRUCTION", reconstruction_value,
        static_cast<DWORD>(sizeof(reconstruction_value)));
    if (reconstruction_length >= sizeof(reconstruction_value)) {
        error = "AB3D2_DXR_INDIRECT_RECONSTRUCTION exceeds 63 bytes";
        return false;
    }
    indirect_reconstruction_mode_ = static_cast<uint32_t>(
        indirect_reconstruction::Mode::full);
    if (reconstruction_length != 0u) {
        const auto found = std::find_if(
            indirect_mode_names.begin(), indirect_mode_names.end(),
            [&reconstruction_value](const char *name) {
                return std::strcmp(reconstruction_value, name) == 0;
            });
        if (found == indirect_mode_names.end()) {
            error = "AB3D2_DXR_INDIRECT_RECONSTRUCTION must be full, "
                    "temporal, raw, regional, deflicker, wavelet1, or "
                    "wavelet2, or restir";
            return false;
        }
        indirect_reconstruction_mode_ = static_cast<uint32_t>(
            found - indirect_mode_names.begin());
        debug_output(std::string("DXR indirect reconstruction mode: ") +
                     indirect_mode_names[indirect_reconstruction_mode_]);
    }

    constexpr std::array<const char *, 7> radiance_channel_names = {
        "combined", "emission", "direct-diffuse", "direct-specular",
        "indirect", "smooth-specular", "rough-specular"};
    char radiance_channel_value[64] = {};
    const DWORD radiance_channel_length = GetEnvironmentVariableA(
        "AB3D2_DXR_RADIANCE_CHANNEL", radiance_channel_value,
        static_cast<DWORD>(sizeof(radiance_channel_value)));
    if (radiance_channel_length >= sizeof(radiance_channel_value)) {
        error = "AB3D2_DXR_RADIANCE_CHANNEL exceeds 63 bytes";
        return false;
    }
    radiance_channel_ = static_cast<uint32_t>(
        indirect_reconstruction::RadianceChannel::combined);
    if (radiance_channel_length != 0u) {
        const auto found = std::find_if(
            radiance_channel_names.begin(), radiance_channel_names.end(),
            [&radiance_channel_value](const char *name) {
                return std::strcmp(radiance_channel_value, name) == 0;
            });
        if (found == radiance_channel_names.end()) {
            error = "AB3D2_DXR_RADIANCE_CHANNEL must be combined, emission, "
                    "direct-diffuse, direct-specular, indirect, "
                    "smooth-specular, or rough-specular";
            return false;
        }
        radiance_channel_ = static_cast<uint32_t>(
            found - radiance_channel_names.begin());
        debug_output(std::string("DXR radiance channel: ") +
                     radiance_channel_names[radiance_channel_]);
    }
    return true;
}

/*
 * Applies ab3d2.ini's ray-tracing settings over the tuned defaults, then lets
 * the environment override either, so bounce depth, candidate count, and
 * indirect history can be swept against `--gpu-smoke` without editing a file.
 * A larger history cap lengthens the running average of demodulated diffuse
 * incident radiance. The subsequent depth/normal-guided spatial reconstruction
 * is independent of the cap and still runs when it is zero.
 *
 * Zero keeps the renderer default for ordinary quality fields. It explicitly
 * disables the radiance clamp or diffuse-GI transfer when their setting is
 * present. Flags distinguish explicit zero from absence for GI transfer, the
 * history limit, and post-curve exposure bias.
 */
bool DxrPipeline::configure_resampling(const RendererRayTracingOptions &options,
                                       std::string &error)
{
    if (options.samples_per_pixel > 8u) {
        error = "DXR direct samples per pixel must be 1-8 when specified";
        return false;
    }
    if (options.indirect_samples_per_pixel > 32u) {
        error = "DXR indirect samples per pixel must be 1-32 when specified";
        return false;
    }
    candidate_count_ = options.light_candidates != 0u ?
        options.light_candidates :
            RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES;
    reservoir_sample_limit_ = (options.reservoir_sample_limit_set != 0u ||
                               options.reservoir_sample_limit != 0u) ?
        options.reservoir_sample_limit :
            RENDERER_RAY_TRACING_DEFAULT_RESERVOIR_SAMPLE_LIMIT;
    if (options.samples_per_pixel != 0u) {
        spp_ = options.samples_per_pixel;
    }
    indirect_spp_ = options.indirect_samples_per_pixel != 0u ?
        options.indirect_samples_per_pixel :
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_SAMPLES_PER_PIXEL;
    if (options.diffuse_gi_scale_set != 0u) {
        if (!std::isfinite(options.diffuse_gi_scale) ||
            options.diffuse_gi_scale < 0.0f ||
            options.diffuse_gi_scale > 1.0f) {
            error = "DXR diffuse GI transfer must be 0-1";
            return false;
        }
        diffuse_gi_scale_ = options.diffuse_gi_scale;
    }
    if (options.maximum_bounces != 0u) {
        maximum_depth_ = options.maximum_bounces;
    }
    if (!std::isfinite(options.radiance_clamp) ||
        options.radiance_clamp < 0.0f || options.radiance_clamp > 100000.0f) {
        error = "DXR radiance clamp must be 0-100000; zero disables it";
        return false;
    }
    radiance_clamp_ = options.radiance_clamp;
    if (options.exposure_bias_set != 0u) {
        if (!std::isfinite(options.exposure_bias_stops) ||
            options.exposure_bias_stops < -5.0f ||
            options.exposure_bias_stops > 0.0f) {
            error = "DXR exposure bias must be -5 through 0 EV";
            return false;
        }
        exposure_bias_stops_ = options.exposure_bias_stops;
    }
    if (options.ndf_trim > 0.0f) {
        ndf_trim_ = options.ndf_trim;
    }
    struct Override {
        const char *name;
        uint32_t minimum;
        uint32_t limit;
        uint32_t *target;
    };
    /* A history limit of zero is a meaningful diagnostic setting rather than an
     * error: it disables only indirect temporal accumulation. */
    const std::array<Override, 4> overrides = {
        Override{"AB3D2_DXR_MAX_BOUNCES", 1u,
                 indirect_reconstruction::maximum_path_depth,
                 &maximum_depth_},
        Override{"AB3D2_DXR_INDIRECT_SPP", 1u, 32u, &indirect_spp_},
        Override{"AB3D2_DXR_CANDIDATES", 1u, 1024u, &candidate_count_},
        Override{"AB3D2_DXR_RESERVOIR_LIMIT", 0u, 65536u,
                 &reservoir_sample_limit_},
    };
    {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_DIFFUSE_GI", value,
            static_cast<DWORD>(sizeof(value)));
        if (length >= sizeof(value)) {
            error = "AB3D2_DXR_DIFFUSE_GI exceeds 63 bytes";
            return false;
        }
        if (length > 0u) {
            char *end = nullptr;
            errno = 0;
            const double parsed = std::strtod(value, &end);
            if (errno != 0 || end == value || *end != '\0' ||
                !std::isfinite(parsed) || parsed < 0.0 || parsed > 1.0) {
                error = "AB3D2_DXR_DIFFUSE_GI must be 0-1";
                return false;
            }
            diffuse_gi_scale_ = static_cast<float>(parsed);
        }
    }
    {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_RADIANCE_CLAMP", value,
            static_cast<DWORD>(sizeof(value)));
        if (length > 0u && length < sizeof(value)) {
            char *end = nullptr;
            errno = 0;
            const double parsed = std::strtod(value, &end);
            if (errno != 0 || end == value || *end != '\0' ||
                !std::isfinite(parsed) || parsed < 0.0 || parsed > 100000.0) {
                error = "AB3D2_DXR_RADIANCE_CLAMP must be 0-100000; zero disables it";
                return false;
            }
            radiance_clamp_ = static_cast<float>(parsed);
        }
    }
    {
        char obsolete[2] = {};
        const DWORD obsolete_length = GetEnvironmentVariableA(
            "AB3D2_DXR_EXPOSURE", obsolete,
            static_cast<DWORD>(sizeof(obsolete)));
        if (obsolete_length != 0u) {
            error = "AB3D2_DXR_EXPOSURE was replaced by the Q2RTX-compatible "
                "AB3D2_DXR_EXPOSURE_BIAS";
            return false;
        }
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_EXPOSURE_BIAS", value,
            static_cast<DWORD>(sizeof(value)));
        if (length >= sizeof(value)) {
            error = "AB3D2_DXR_EXPOSURE_BIAS exceeds 63 bytes";
            return false;
        }
        if (length > 0u && length < sizeof(value)) {
            char *end = nullptr;
            errno = 0;
            const double parsed = std::strtod(value, &end);
            if (errno != 0 || end == value || *end != '\0' ||
                !std::isfinite(parsed) || parsed < -5.0 || parsed > 0.0) {
                error = "AB3D2_DXR_EXPOSURE_BIAS must be -5 through 0 EV";
                return false;
            }
            exposure_bias_stops_ = static_cast<float>(parsed);
        }
    }
    {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_NDF_TRIM", value,
            static_cast<DWORD>(sizeof(value)));
        if (length > 0u && length < sizeof(value)) {
            char *end = nullptr;
            errno = 0;
            const double parsed = std::strtod(value, &end);
            if (errno == 0 && end != value && *end == '\0' &&
                parsed >= 0.1 && parsed <= 1.0) {
                ndf_trim_ = static_cast<float>(parsed);
            }
        }
    }
    {
        char value[64] = {};
        const DWORD length = GetEnvironmentVariableA(
            "AB3D2_DXR_SPP", value,
            static_cast<DWORD>(sizeof(value)));
        if (length > 0u && length < sizeof(value)) {
            char *end = nullptr;
            errno = 0;
            const unsigned long parsed = std::strtoul(value, &end, 10);
            if (errno == 0 && end != value && *end == '\0' &&
                parsed >= 1u && parsed <= 8u) {
                spp_ = static_cast<uint32_t>(parsed);
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
    debug_output("DXR ray tracing: direct samples per pixel=" +
                 std::to_string(spp_) + " indirect sample ceiling=" +
                 std::to_string(indirect_spp_) + " diffuse GI=" +
                 std::to_string(diffuse_gi_scale_) + " bounces=" +
                 std::to_string(maximum_depth_) + " candidates=" +
                 std::to_string(candidate_count_) + " reservoir limit=" +
                 std::to_string(reservoir_sample_limit_) + " radiance clamp=" +
                 std::to_string(radiance_clamp_) + " exposure bias=" +
                 std::to_string(exposure_bias_stops_) + " EV NDF trim=" +
                 std::to_string(ndf_trim_));
    return true;
}

bool DxrPipeline::create_diagnostic_pipeline(
    ID3D12Device5 *device, const std::vector<unsigned char> &vertex_shader,
    const std::vector<unsigned char> &pixel_shader,
    DXGI_FORMAT render_target_format, std::string &error)
{
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (!serialize_root_signature(root_description, device, root_signature_,
                                  L"AB3D2 DXR Diagnostic Root Signature", error)) {
        return false;
    }
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC description = graphics_description(
        root_signature_.Get(), vertex_shader, pixel_shader,
        render_target_format);
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
    DXGI_FORMAT render_target_format, std::string &error)
{
    std::vector<unsigned char> pixel_shader;
    if (!load_shader(L"present_ps.dxil", pixel_shader, error)) {
        return false;
    }
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors =
        static_cast<UINT>(DxrReconstructionBuffer::count) + 2u;
    range.BaseShaderRegister = 0;
    std::array<D3D12_ROOT_PARAMETER, 3> parameters = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.Num32BitValues =
        sizeof(PresentConstants) / sizeof(uint32_t);
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[2].Descriptor.ShaderRegister = 12u;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC linear_sampler = {};
    linear_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linear_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.MaxAnisotropy = 1u;
    linear_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linear_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    linear_sampler.MinLOD = 0.0f;
    linear_sampler.MaxLOD = D3D12_FLOAT32_MAX;
    linear_sampler.ShaderRegister = 0u;
    linear_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.NumParameters = static_cast<UINT>(parameters.size());
    root_description.pParameters = parameters.data();
    root_description.NumStaticSamplers = 1u;
    root_description.pStaticSamplers = &linear_sampler;
    root_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (!serialize_root_signature(root_description, device, present_root_signature_,
                                  L"AB3D2 DXR Noisy Present Root Signature", error)) {
        return false;
    }
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC description = graphics_description(
        present_root_signature_.Get(), vertex_shader, pixel_shader,
        render_target_format);
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

bool DxrPipeline::create_post_pipeline(ID3D12Device5 *device,
                                       std::string &error)
{
    std::vector<unsigned char> histogram_shader;
    std::vector<unsigned char> curve_shader;
    std::vector<unsigned char> bloom_shader;
    if (!load_shader(L"post_histogram_cs.dxil", histogram_shader, error) ||
        !load_shader(L"post_curve_cs.dxil", curve_shader, error) ||
        !load_shader(L"post_bloom_cs.dxil", bloom_shader, error)) {
        return false;
    }

    std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1u;
    ranges[0].BaseShaderRegister = 0u;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 2u;
    ranges[1].BaseShaderRegister = 0u;

    D3D12_DESCRIPTOR_RANGE bloom_input_range = {};
    bloom_input_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bloom_input_range.NumDescriptors = 1u;
    bloom_input_range.BaseShaderRegister = 1u;
    D3D12_DESCRIPTOR_RANGE bloom_low_range = {};
    bloom_low_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bloom_low_range.NumDescriptors = 1u;
    bloom_low_range.BaseShaderRegister = 2u;
    D3D12_DESCRIPTOR_RANGE bloom_output_range = {};
    bloom_output_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    bloom_output_range.NumDescriptors = 1u;
    bloom_output_range.BaseShaderRegister = 3u;

    std::array<D3D12_ROOT_PARAMETER, 7> parameters = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 2u;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.Num32BitValues =
        sizeof(PostConstants) / sizeof(uint32_t);
    parameters[3].Constants.ShaderRegister = 0u;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[4].DescriptorTable.pDescriptorRanges = &bloom_input_range;
    parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[5].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[5].DescriptorTable.pDescriptorRanges = &bloom_low_range;
    parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[6].DescriptorTable.NumDescriptorRanges = 1u;
    parameters[6].DescriptorTable.pDescriptorRanges = &bloom_output_range;
    parameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC linear_sampler = {};
    linear_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linear_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear_sampler.MipLODBias = 0.0f;
    linear_sampler.MaxAnisotropy = 1u;
    linear_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    linear_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    linear_sampler.MinLOD = 0.0f;
    linear_sampler.MaxLOD = D3D12_FLOAT32_MAX;
    linear_sampler.ShaderRegister = 0u;
    linear_sampler.RegisterSpace = 0u;
    linear_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC root_description = {};
    root_description.NumParameters = static_cast<UINT>(parameters.size());
    root_description.pParameters = parameters.data();
    root_description.NumStaticSamplers = 1u;
    root_description.pStaticSamplers = &linear_sampler;
    if (!serialize_root_signature(
            root_description, device, post_root_signature_,
            L"AB3D2 Post-RR Tone Mapping Root Signature", error)) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_description = {};
    pipeline_description.pRootSignature = post_root_signature_.Get();
    pipeline_description.CS = {
        histogram_shader.data(), histogram_shader.size()};
    HRESULT result = device->CreateComputePipelineState(
        &pipeline_description, IID_PPV_ARGS(&post_histogram_pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateComputePipelineState(post-RR histogram)",
            result);
        return false;
    }
    post_histogram_pipeline_state_->SetName(
        L"AB3D2 Post-RR Luminance Histogram Pipeline");

    pipeline_description.CS = {curve_shader.data(), curve_shader.size()};
    result = device->CreateComputePipelineState(
        &pipeline_description, IID_PPV_ARGS(&post_curve_pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateComputePipelineState(post-RR tone curve)",
            result);
        return false;
    }
    post_curve_pipeline_state_->SetName(
        L"AB3D2 Post-RR Adaptive Tone Curve Pipeline");

    pipeline_description.CS = {bloom_shader.data(), bloom_shader.size()};
    result = device->CreateComputePipelineState(
        &pipeline_description, IID_PPV_ARGS(&post_bloom_pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateComputePipelineState(post-RR bloom)",
            result);
        return false;
    }
    post_bloom_pipeline_state_->SetName(
        L"AB3D2 Post-RR Linear-HDR Bloom Pipeline");
    return true;
}

bool DxrPipeline::create_raytracing_pipeline(ID3D12Device5 *device,
                                             std::string &error)
{
    std::vector<unsigned char> library;
    if (!load_shader(L"path_trace.dxil", library, error)) {
        return false;
    }
    std::array<D3D12_DESCRIPTOR_RANGE, 4> ranges = {};
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
    ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[3].NumDescriptors = 19;
    ranges[3].BaseShaderRegister = 13;
    std::array<D3D12_ROOT_PARAMETER, 13> parameters = {};
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
    parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[8].Descriptor.ShaderRegister = 0;
    /* Both reservoir buffers bind as unordered-access root descriptors, which
     * keeps them in one resource state for the whole frame. */
    parameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[9].Descriptor.ShaderRegister = 10;
    parameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[10].Descriptor.ShaderRegister = 11;
    parameters[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[11].Descriptor.ShaderRegister = 12;
    parameters[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[12].DescriptorTable.NumDescriptorRanges = 1;
    parameters[12].DescriptorTable.pDescriptorRanges = &ranges[3];
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

    static constexpr wchar_t build_light_grid[] = L"BuildLightGrid";
    static constexpr wchar_t ray_generation[] = L"RayGeneration";
    static constexpr wchar_t temporal_gi[] = L"TemporalGI";
    static constexpr wchar_t spatial_gi[] = L"SpatialGI";
    static constexpr wchar_t spatial_shade[] = L"SpatialShade";
    static constexpr wchar_t build_indirect_gradient[] =
        L"BuildIndirectGradient";
    static constexpr wchar_t filter_indirect_gradient_0[] =
        L"FilterIndirectGradient0";
    static constexpr wchar_t filter_indirect_gradient_1[] =
        L"FilterIndirectGradient1";
    static constexpr wchar_t filter_indirect_gradient_2[] =
        L"FilterIndirectGradient2";
    static constexpr wchar_t filter_indirect_gradient_3[] =
        L"FilterIndirectGradient3";
    static constexpr wchar_t filter_indirect_gradient_4[] =
        L"FilterIndirectGradient4";
    static constexpr wchar_t filter_indirect_gradient_5[] =
        L"FilterIndirectGradient5";
    static constexpr wchar_t filter_indirect_gradient_6[] =
        L"FilterIndirectGradient6";
    static constexpr wchar_t temporal_indirect[] = L"TemporalIndirect";
    static constexpr wchar_t filter_indirect_0[] = L"FilterIndirect0";
    static constexpr wchar_t deflicker_indirect[] = L"DeflickerIndirect";
    static constexpr wchar_t filter_indirect_1[] = L"FilterIndirect1";
    static constexpr wchar_t filter_indirect_2[] = L"FilterIndirect2";
    static constexpr wchar_t filter_indirect_3[] = L"FilterIndirect3";
    static constexpr wchar_t resolve_indirect_filtered[] =
        L"ResolveIndirectFiltered";
    static constexpr wchar_t reconstruct_indirect[] = L"ReconstructIndirect";
    static constexpr wchar_t calculate_automatic_exposure[] =
        L"CalculateAutomaticExposure";
    static constexpr wchar_t surface_miss[] = L"SurfaceMiss";
    static constexpr wchar_t shadow_miss[] = L"ShadowMiss";
    static constexpr wchar_t closest_hit[] = L"ClosestHit";
    static constexpr wchar_t any_hit[] = L"AnyHit";
    static constexpr wchar_t hit_group_name[] = L"HitGroup";
    std::array<D3D12_EXPORT_DESC, 26> exports = {};
    exports[0].Name = build_light_grid;
    exports[1].Name = ray_generation;
    exports[2].Name = temporal_gi;
    exports[3].Name = spatial_gi;
    exports[4].Name = spatial_shade;
    exports[5].Name = build_indirect_gradient;
    exports[6].Name = filter_indirect_gradient_0;
    exports[7].Name = filter_indirect_gradient_1;
    exports[8].Name = filter_indirect_gradient_2;
    exports[9].Name = filter_indirect_gradient_3;
    exports[10].Name = filter_indirect_gradient_4;
    exports[11].Name = filter_indirect_gradient_5;
    exports[12].Name = filter_indirect_gradient_6;
    exports[13].Name = temporal_indirect;
    exports[14].Name = filter_indirect_0;
    exports[15].Name = deflicker_indirect;
    exports[16].Name = filter_indirect_1;
    exports[17].Name = filter_indirect_2;
    exports[18].Name = filter_indirect_3;
    exports[19].Name = resolve_indirect_filtered;
    exports[20].Name = reconstruct_indirect;
    exports[21].Name = calculate_automatic_exposure;
    exports[22].Name = surface_miss;
    exports[23].Name = shadow_miss;
    exports[24].Name = closest_hit;
    exports[25].Name = any_hit;
    D3D12_DXIL_LIBRARY_DESC library_description = {};
    library_description.DXILLibrary = {library.data(), library.size()};
    library_description.NumExports = static_cast<UINT>(exports.size());
    library_description.pExports = exports.data();
    D3D12_HIT_GROUP_DESC hit_group = {};
    hit_group.HitGroupExport = hit_group_name;
    hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group.ClosestHitShaderImport = closest_hit;
    hit_group.AnyHitShaderImport = any_hit;
    D3D12_RAYTRACING_SHADER_CONFIG shader_configuration = {};
    shader_configuration.MaxPayloadSizeInBytes = 20u;
    shader_configuration.MaxAttributeSizeInBytes = 8u;
    std::array<const wchar_t *, 25> configured_exports = {
        build_light_grid, ray_generation, temporal_gi, spatial_gi,
        spatial_shade,
        build_indirect_gradient,
        filter_indirect_gradient_0, filter_indirect_gradient_1,
        filter_indirect_gradient_2, filter_indirect_gradient_3,
        filter_indirect_gradient_4, filter_indirect_gradient_5,
        filter_indirect_gradient_6, temporal_indirect,
        filter_indirect_0, deflicker_indirect,
        filter_indirect_1, filter_indirect_2,
        filter_indirect_3, resolve_indirect_filtered, reconstruct_indirect,
        calculate_automatic_exposure, surface_miss, shadow_miss,
        hit_group_name};
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
        properties->GetShaderIdentifier(build_light_grid),
        properties->GetShaderIdentifier(ray_generation),
        properties->GetShaderIdentifier(temporal_gi),
        properties->GetShaderIdentifier(spatial_gi),
        properties->GetShaderIdentifier(spatial_shade),
        properties->GetShaderIdentifier(build_indirect_gradient),
        properties->GetShaderIdentifier(filter_indirect_gradient_0),
        properties->GetShaderIdentifier(filter_indirect_gradient_1),
        properties->GetShaderIdentifier(filter_indirect_gradient_2),
        properties->GetShaderIdentifier(filter_indirect_gradient_3),
        properties->GetShaderIdentifier(filter_indirect_gradient_4),
        properties->GetShaderIdentifier(filter_indirect_gradient_5),
        properties->GetShaderIdentifier(filter_indirect_gradient_6),
        properties->GetShaderIdentifier(temporal_indirect),
        properties->GetShaderIdentifier(filter_indirect_0),
        properties->GetShaderIdentifier(deflicker_indirect),
        properties->GetShaderIdentifier(filter_indirect_1),
        properties->GetShaderIdentifier(filter_indirect_2),
        properties->GetShaderIdentifier(filter_indirect_3),
        properties->GetShaderIdentifier(resolve_indirect_filtered),
        properties->GetShaderIdentifier(reconstruct_indirect),
        properties->GetShaderIdentifier(calculate_automatic_exposure),
        properties->GetShaderIdentifier(surface_miss),
        properties->GetShaderIdentifier(shadow_miss),
        properties->GetShaderIdentifier(hit_group_name),
    };
    static_assert(std::size(identifiers) == shader_record_count);
    for (UINT index = 0; index < shader_record_count; ++index) {
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
    description.NumDescriptors = 2u;
    description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    const HRESULT cpu_result = device->CreateDescriptorHeap(
        &description, IID_PPV_ARGS(&diagnostic_cpu_heap_));
    if (FAILED(cpu_result)) {
        error = hresult_error(
            "ID3D12Device::CreateDescriptorHeap(DXR diagnostic CPU view)",
            cpu_result);
        return false;
    }
    diagnostic_cpu_heap_->SetName(L"AB3D2 DXR Diagnostic CPU Descriptor Heap");
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

bool DxrPipeline::create_frame_constant_buffer(ID3D12Device5 *device,
                                               std::string &error)
{
    const D3D12_HEAP_PROPERTIES upload_heap =
        heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = buffer_description(
        frame_constant_stride * DxrScene::upload_frame_count);
    const HRESULT result = device->CreateCommittedResource(
        &upload_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&frame_constants_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(frame constants)",
            result);
        return false;
    }
    frame_constants_->SetName(L"AB3D2 DXR Frame Constants");
    return true;
}

bool DxrPipeline::create_light_grid(ID3D12Device5 *device,
                                    std::string &error)
{
    D3D12_RESOURCE_DESC description = buffer_description(
        static_cast<UINT64>(light_grid::entry_count) *
        sizeof(light_grid::Entry));
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_HEAP_PROPERTIES default_heap =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const HRESULT result = device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&light_grid_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(ReGIR light grid)",
            result);
        return false;
    }
    light_grid_->SetName(L"AB3D2 DXR ReGIR Light Grid");
    light_grid_needs_initial_transition_ = true;
    light_grid_cache_valid_ = false;
    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.Buffer.NumElements = light_grid::entry_count;
    view.Buffer.StructureByteStride = sizeof(light_grid::Entry);
    device->CreateUnorderedAccessView(
        light_grid_.Get(), nullptr, &view, cpu_descriptor(light_grid_uav));
    return true;
}

bool DxrPipeline::create_diagnostics(ID3D12Device5 *device,
                                     std::string &error)
{
    constexpr UINT64 diagnostic_bytes =
        diagnostic_value_count * sizeof(uint32_t);
    D3D12_RESOURCE_DESC description = buffer_description(diagnostic_bytes);
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_HEAP_PROPERTIES default_heap =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = device->CreateCommittedResource(
        &default_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&diagnostics_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(DXR diagnostics)", result);
        return false;
    }
    diagnostics_->SetName(L"AB3D2 DXR Renderer Diagnostics");
    diagnostics_have_output_ = false;
    D3D12_UNORDERED_ACCESS_VIEW_DESC diagnostic_view = {};
    diagnostic_view.Format = DXGI_FORMAT_R32_UINT;
    diagnostic_view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    diagnostic_view.Buffer.NumElements = diagnostic_value_count;
    device->CreateUnorderedAccessView(
        diagnostics_.Get(), nullptr, &diagnostic_view,
        cpu_descriptor(diagnostics_uav));
    device->CreateUnorderedAccessView(
        diagnostics_.Get(), nullptr, &diagnostic_view,
        diagnostic_clear_descriptor());

    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    const D3D12_HEAP_PROPERTIES readback_heap =
        heap_properties(D3D12_HEAP_TYPE_READBACK);
    result = device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&diagnostics_readback_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(DXR diagnostic readback)",
            result);
        return false;
    }
    diagnostics_readback_->SetName(
        L"AB3D2 DXR Renderer Diagnostic Readback");
    return true;
}

bool DxrPipeline::record_diagnostics_begin(
    ID3D12GraphicsCommandList4 *command_list, std::string &error)
{
    if (!command_list || !diagnostics_ || !diagnostics_readback_ ||
        !descriptor_heap_) {
        error = "DXR renderer diagnostics are incomplete";
        return false;
    }
    const D3D12_RESOURCE_BARRIER to_write = transition(
        diagnostics_.Get(), diagnostics_have_output_ ?
            D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &to_write);
    ID3D12DescriptorHeap *heaps[] = {descriptor_heap_.Get()};
    command_list->SetDescriptorHeaps(1, heaps);
    const UINT zeros[4] = {};
    command_list->ClearUnorderedAccessViewUint(
        gpu_descriptor(diagnostics_uav), diagnostic_clear_descriptor(),
        diagnostics_.Get(), zeros, 0u, nullptr);
    const D3D12_RESOURCE_BARRIER cleared = uav_barrier(diagnostics_.Get());
    command_list->ResourceBarrier(1, &cleared);
    return true;
}

bool DxrPipeline::record_diagnostics_end(
    ID3D12GraphicsCommandList4 *command_list, std::string &error)
{
    if (!command_list || !diagnostics_ || !diagnostics_readback_) {
        error = "DXR renderer diagnostic readback is incomplete";
        return false;
    }
    const D3D12_RESOURCE_BARRIER finished = uav_barrier(diagnostics_.Get());
    command_list->ResourceBarrier(1, &finished);
    const D3D12_RESOURCE_BARRIER to_copy = transition(
        diagnostics_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    command_list->ResourceBarrier(1, &to_copy);
    command_list->CopyBufferRegion(diagnostics_readback_.Get(), 0,
                                   diagnostics_.Get(), 0,
                                   diagnostic_value_count * sizeof(uint32_t));
    diagnostics_have_output_ = true;
    return true;
}

bool DxrPipeline::collect_diagnostics(std::string &error)
{
    if (!diagnostics_readback_) {
        error = "DXR renderer diagnostic readback is unavailable";
        return false;
    }
    constexpr SIZE_T diagnostic_bytes =
        diagnostic_value_count * sizeof(uint32_t);
    const D3D12_RANGE read = {0, diagnostic_bytes};
    void *mapped = nullptr;
    const HRESULT result = diagnostics_readback_->Map(0, &read, &mapped);
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Resource::Map(DXR renderer diagnostics)", result);
        return false;
    }
    const auto *values = static_cast<const uint32_t *>(mapped);
    last_view_weapon_coverage_ = values[0];
    last_view_weapon_rgb_checksum_ = values[1];
    last_world_bitmap_coverage_ = values[2];
    last_world_vector_coverage_ = values[3];
    last_world_additive_coverage_ = values[4];
    std::memcpy(&last_target_exposure_, &values[5], sizeof(float));
    std::memcpy(&last_automatic_exposure_, &values[6], sizeof(float));
    std::memcpy(&last_metered_average_luminance_, &values[7], sizeof(float));
    std::memcpy(&last_metered_low_luminance_, &values[8], sizeof(float));
    std::memcpy(&last_metered_high_luminance_, &values[9], sizeof(float));
    last_metered_weight_ = values[10];
    last_direct_diffuse_coverage_ = values[11];
    last_direct_specular_coverage_ = values[12];
    last_invalid_lighting_or_guide_pixels_ = values[13];
    last_smooth_specular_coverage_ = values[14];
    const D3D12_RANGE no_write = {0, 0};
    diagnostics_readback_->Unmap(0, &no_write);
    debug_output(
        "DXR renderer diagnostics: view_weapon_primary_pixels=" +
        std::to_string(last_view_weapon_coverage_) + " radiance=" +
        std::to_string(last_view_weapon_rgb_checksum_) + " world_bitmaps=" +
        std::to_string(last_world_bitmap_coverage_) + " world_vectors=" +
        std::to_string(last_world_vector_coverage_) + " additive_layers=" +
        std::to_string(last_world_additive_coverage_) + " exposure_target=" +
        std::to_string(last_target_exposure_) + " exposure=" +
        std::to_string(last_automatic_exposure_) + " meter_average=" +
        std::to_string(last_metered_average_luminance_) + " meter_low=" +
        std::to_string(last_metered_low_luminance_) + " meter_high=" +
        std::to_string(last_metered_high_luminance_) + " meter_weight=" +
        std::to_string(last_metered_weight_) + " direct_diffuse=" +
        std::to_string(last_direct_diffuse_coverage_) +
        " direct_specular=" +
        std::to_string(last_direct_specular_coverage_) +
        " invalid_lighting_or_guides=" +
        std::to_string(last_invalid_lighting_or_guide_pixels_) +
        " smooth_specular=" +
        std::to_string(last_smooth_specular_coverage_));
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

D3D12_CPU_DESCRIPTOR_HANDLE DxrPipeline::diagnostic_clear_descriptor() const
{
    return diagnostic_cpu_heap_ ?
        diagnostic_cpu_heap_->GetCPUDescriptorHandleForHeapStart() :
        D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE DxrPipeline::histogram_clear_descriptor() const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = diagnostic_clear_descriptor();
    if (handle.ptr != 0u) {
        handle.ptr += descriptor_size_;
    }
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
    if (reconstruction_targets_[0] && indirect_radiance_ &&
        indirect_filtered_ && indirect_chroma_ &&
        indirect_chroma_filtered_ && indirect_gradients_[0] &&
        indirect_gradients_[1] && automatic_exposure_ &&
        tone_map_histogram_ && tone_map_state_ &&
        bloom_targets_[0] && bloom_targets_[1] && bloom_targets_[2] &&
        bloom_targets_[3] && bloom_targets_[4] && bloom_targets_[5] &&
        post_hdr_output_ &&
        indirect_histories_[0] && indirect_histories_[1] &&
        direct_reservoir_binding_ &&
        gi_reservoirs_[0] && gi_reservoirs_[1] &&
        gi_reservoir_scratch_ &&
        streamline_scene_motion_ &&
        view_weapon_histories_[0] && view_weapon_histories_[1] &&
        rr_disocclusion_mask_ && rr_bias_current_color_mask_ &&
        surface_parameters_ &&
        render_width_ == width &&
        render_height_ == height && present_width_ == present_width &&
        present_height_ == present_height &&
        (streamline_output_.Get() != nullptr) == create_streamline_output) {
        return true;
    }
    for (auto &target : reconstruction_targets_) {
        target.Reset();
    }
    direct_reservoir_binding_.Reset();
    for (auto &reservoir : gi_reservoirs_) {
        reservoir.Reset();
    }
    gi_reservoir_scratch_.Reset();
    streamline_scene_motion_.Reset();
    for (auto &history : view_weapon_histories_) {
        history.Reset();
    }
    rr_disocclusion_mask_.Reset();
    rr_bias_current_color_mask_.Reset();
    surface_parameters_.Reset();
    indirect_radiance_.Reset();
    indirect_filtered_.Reset();
    indirect_chroma_.Reset();
    indirect_chroma_filtered_.Reset();
    for (auto &gradient : indirect_gradients_) {
        gradient.Reset();
    }
    automatic_exposure_.Reset();
    tone_map_histogram_.Reset();
    tone_map_state_.Reset();
    for (auto &target : bloom_targets_) {
        target.Reset();
    }
    post_hdr_output_.Reset();
    for (auto &history : indirect_histories_) {
        history.Reset();
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
        D3D12_RESOURCE_DESC target_description = description;
        target_description.Format = reconstruction_formats[index];
        const bool private_diagnostic_target =
            index == static_cast<size_t>(
                DxrReconstructionBuffer::linear_roughness) ||
            index == static_cast<size_t>(
                DxrReconstructionBuffer::diffuse_hit_distance) ||
            index == static_cast<size_t>(
                DxrReconstructionBuffer::diffuse_hit_distance_history);
        const bool private_diagnostic_active =
            debug_view_ == static_cast<uint32_t>(index) ||
            ((index == static_cast<size_t>(
                  DxrReconstructionBuffer::diffuse_hit_distance) ||
              index == static_cast<size_t>(
                  DxrReconstructionBuffer::diffuse_hit_distance_history)) &&
             (debug_view_ == static_cast<uint32_t>(
                  DxrReconstructionBuffer::diffuse_hit_distance) ||
              debug_view_ == static_cast<uint32_t>(
                  DxrReconstructionBuffer::diffuse_hit_distance_history)));
        if (private_diagnostic_target && !private_diagnostic_active) {
            target_description.Width = 1u;
            target_description.Height = 1u;
        }
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &target_description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&reconstruction_targets_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(RR guide)", result);
            return false;
        }
        reconstruction_targets_[index]->SetName(reconstruction_names[index]);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = target_description.Format;
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
    description.Format = DXGI_FORMAT_R16G16_FLOAT;
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&streamline_scene_motion_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(Streamline motion)",
                result);
            return false;
        }
        streamline_scene_motion_->SetName(
            L"AB3D2 RR Streamline-Safe Scene Motion");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            streamline_scene_motion_.Get(), nullptr, &uav,
            cpu_descriptor(streamline_scene_motion_uav));
    }
    /* Coverage uses bit 8 and the rejection lifetime uses bits 0--7, so the
     * exact per-pixel state fits in sixteen bits. */
    description.Format = DXGI_FORMAT_R16_UINT;
    for (UINT index = 0u; index < view_weapon_histories_.size(); ++index) {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&view_weapon_histories_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(weapon RR history)",
                result);
            return false;
        }
        view_weapon_histories_[index]->SetName(index == 0u ?
            L"AB3D2 RR View-Weapon History A" :
            L"AB3D2 RR View-Weapon History B");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            view_weapon_histories_[index].Get(), nullptr, &uav,
            cpu_descriptor(view_weapon_history_uav_start + index));
    }
    /* Both RR masks are binary. R8 UNORM preserves their exact 0/1 values
     * while halving the two full-rate guide surfaces and their write traffic. */
    description.Format = DXGI_FORMAT_R8_UNORM;
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&rr_disocclusion_mask_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(RR disocclusion mask)",
                result);
            return false;
        }
        rr_disocclusion_mask_->SetName(
            L"AB3D2 RR View-Weapon Disocclusion Mask");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            rr_disocclusion_mask_.Get(), nullptr, &uav,
            cpu_descriptor(rr_disocclusion_mask_uav));
    }
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&rr_bias_current_color_mask_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(RR current-color mask)",
                result);
            return false;
        }
        rr_bias_current_color_mask_->SetName(
            L"AB3D2 RR View-Weapon Current-Color Mask");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            rr_bias_current_color_mask_.Get(), nullptr, &uav,
            cpu_descriptor(rr_bias_current_color_mask_uav));
    }
    description.Format = DXGI_FORMAT_R32_UINT;
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&surface_parameters_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(surface parameters)",
                result);
            return false;
        }
        surface_parameters_->SetName(
            L"AB3D2 Packed Primary Surface F0");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            surface_parameters_.Get(), nullptr, &uav,
            cpu_descriptor(surface_parameters_uav));
    }
    description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&indirect_radiance_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(indirect radiance)",
                result);
            return false;
        }
        indirect_radiance_->SetName(
            L"AB3D2 Low-Frequency Directional Luminance A");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            indirect_radiance_.Get(), nullptr, &uav,
            cpu_descriptor(indirect_radiance_uav));
    }
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&indirect_filtered_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(filtered indirect)",
                result);
            return false;
        }
        indirect_filtered_->SetName(
            L"AB3D2 Low-Frequency Directional Luminance B / RGB Output");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            indirect_filtered_.Get(), nullptr, &uav,
            cpu_descriptor(indirect_filtered_uav));
        srv.Format = description.Format;
        device->CreateShaderResourceView(
            indirect_filtered_.Get(), &srv,
            cpu_descriptor(indirect_radiance_srv));
    }
    description.Format = DXGI_FORMAT_R16G16_FLOAT;
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&indirect_chroma_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(indirect chroma)",
                result);
            return false;
        }
        indirect_chroma_->SetName(
            L"AB3D2 Low-Frequency Indirect Chroma A");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            indirect_chroma_.Get(), nullptr, &uav,
            cpu_descriptor(indirect_chroma_uav));
    }
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&indirect_chroma_filtered_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(filtered indirect chroma)",
                result);
            return false;
        }
        indirect_chroma_filtered_->SetName(
            L"AB3D2 Low-Frequency Indirect Chroma B");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = description.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            indirect_chroma_filtered_.Get(), nullptr, &uav,
            cpu_descriptor(indirect_chroma_filtered_uav));
    }
    {
        D3D12_RESOURCE_DESC gradient_description = description;
        gradient_description.Width =
            (width + indirect_reconstruction::downsample_factor - 1u) /
            indirect_reconstruction::downsample_factor;
        gradient_description.Height =
            (height + indirect_reconstruction::downsample_factor - 1u) /
            indirect_reconstruction::downsample_factor;
        for (size_t index = 0; index < indirect_gradients_.size(); ++index) {
            const HRESULT result = device->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &gradient_description,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                IID_PPV_ARGS(&indirect_gradients_[index]));
            if (FAILED(result)) {
                error = hresult_error(
                    "ID3D12Device::CreateCommittedResource(indirect gradient)",
                    result);
                return false;
            }
            indirect_gradients_[index]->SetName(index == 0u ?
                L"AB3D2 Low-Frequency Lighting Gradient A" :
                L"AB3D2 Low-Frequency Lighting Gradient B");
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
            uav.Format = gradient_description.Format;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(
                indirect_gradients_[index].Get(), nullptr, &uav,
                cpu_descriptor(indirect_gradient_uav_start +
                               static_cast<UINT>(index)));
        }
    }
    {
        const UINT64 pixel_count = static_cast<UINT64>(width) * height;
        D3D12_RESOURCE_DESC history_description = buffer_description(
            pixel_count *
            sizeof(indirect_reconstruction::PackedHistoryPixel));
        history_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        for (size_t index = 0; index < indirect_histories_.size(); ++index) {
            const HRESULT result = device->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &history_description,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&indirect_histories_[index]));
            if (FAILED(result)) {
                error = hresult_error(
                    "ID3D12Device::CreateCommittedResource(indirect history)",
                    result);
                return false;
            }
            indirect_histories_[index]->SetName(index == 0u ?
                L"AB3D2 Low-Frequency Indirect History A" :
                L"AB3D2 Low-Frequency Indirect History B");
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
            uav.Format = DXGI_FORMAT_UNKNOWN;
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements = static_cast<UINT>(pixel_count);
            uav.Buffer.StructureByteStride =
                sizeof(indirect_reconstruction::PackedHistoryPixel);
            device->CreateUnorderedAccessView(
                indirect_histories_[index].Get(), nullptr, &uav,
                cpu_descriptor(indirect_history_uav_start +
                               static_cast<UINT>(index)));
        }
    }
    {
        D3D12_RESOURCE_DESC exposure_description = buffer_description(
            sizeof(float));
        exposure_description.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &exposure_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&automatic_exposure_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(automatic exposure)",
                result);
            return false;
        }
        automatic_exposure_->SetName(L"AB3D2 Adapted Automatic Exposure");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 1u;
        uav.Buffer.StructureByteStride = sizeof(float);
        device->CreateUnorderedAccessView(
            automatic_exposure_.Get(), nullptr, &uav,
            cpu_descriptor(automatic_exposure_uav));
    }
    {
        D3D12_RESOURCE_DESC histogram_description = buffer_description(
            tone_map_histogram_bin_count * sizeof(uint32_t));
        histogram_description.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &histogram_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&tone_map_histogram_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(post-RR histogram)",
                result);
            return false;
        }
        tone_map_histogram_->SetName(
            L"AB3D2 Post-RR Luminance Histogram");
        D3D12_UNORDERED_ACCESS_VIEW_DESC histogram_uav = {};
        histogram_uav.Format = DXGI_FORMAT_R32_UINT;
        histogram_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        histogram_uav.Buffer.NumElements = tone_map_histogram_bin_count;
        device->CreateUnorderedAccessView(
            tone_map_histogram_.Get(), nullptr, &histogram_uav,
            cpu_descriptor(post_histogram_uav));
        device->CreateUnorderedAccessView(
            tone_map_histogram_.Get(), nullptr, &histogram_uav,
            histogram_clear_descriptor());

        D3D12_RESOURCE_DESC state_description = buffer_description(
            tone_map_state_value_count * sizeof(float));
        state_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &state_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&tone_map_state_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(post-RR tone state)",
                result);
            return false;
        }
        tone_map_state_->SetName(L"AB3D2 Post-RR Adaptive Tone State");
        D3D12_UNORDERED_ACCESS_VIEW_DESC state_uav = {};
        state_uav.Format = DXGI_FORMAT_UNKNOWN;
        state_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        state_uav.Buffer.NumElements = tone_map_state_value_count;
        state_uav.Buffer.StructureByteStride = sizeof(float);
        device->CreateUnorderedAccessView(
            tone_map_state_.Get(), nullptr, &state_uav,
            cpu_descriptor(post_tone_map_state_uav));
        D3D12_SHADER_RESOURCE_VIEW_DESC state_srv = {};
        state_srv.Format = DXGI_FORMAT_UNKNOWN;
        state_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        state_srv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        state_srv.Buffer.NumElements = tone_map_state_value_count;
        state_srv.Buffer.StructureByteStride = sizeof(float);
        device->CreateShaderResourceView(
            tone_map_state_.Get(), &state_srv,
            cpu_descriptor(tone_map_state_srv));
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
    }
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1u;
    device->CreateShaderResourceView(
        create_streamline_output ? streamline_output_.Get() :
                                   reconstruction_targets_[0].Get(),
        &srv, cpu_descriptor(post_input_srv));

    const UINT post_width = create_streamline_output ? present_width : width;
    const UINT post_height = create_streamline_output ? present_height : height;
    const std::array<UINT, 3> bloom_widths = {
        std::max(1u, (post_width + 1u) / 2u),
        std::max(1u, (post_width + 3u) / 4u),
        std::max(1u, (post_width + 7u) / 8u),
    };
    const std::array<UINT, 3> bloom_heights = {
        std::max(1u, (post_height + 1u) / 2u),
        std::max(1u, (post_height + 3u) / 4u),
        std::max(1u, (post_height + 7u) / 8u),
    };
    constexpr std::array<const wchar_t *, bloom_target_count> bloom_names = {
        L"AB3D2 Linear-HDR Bloom Half A",
        L"AB3D2 Linear-HDR Bloom Half B",
        L"AB3D2 Linear-HDR Bloom Quarter A",
        L"AB3D2 Linear-HDR Bloom Quarter B",
        L"AB3D2 Linear-HDR Bloom Eighth A",
        L"AB3D2 Linear-HDR Bloom Eighth B",
    };
    for (UINT index = 0u; index < bloom_target_count; ++index) {
        D3D12_RESOURCE_DESC bloom_description = {};
        bloom_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        bloom_description.Width = bloom_widths[index / 2u];
        bloom_description.Height = bloom_heights[index / 2u];
        bloom_description.DepthOrArraySize = 1u;
        bloom_description.MipLevels = 1u;
        /* Bloom is positive RGB and never consumes alpha. R11G11B10 retains
         * its HDR exponent range while halving all six multi-pass
         * intermediate surfaces and their read/write traffic. */
        bloom_description.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        bloom_description.SampleDesc.Count = 1u;
        bloom_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &bloom_description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&bloom_targets_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(linear-HDR bloom)",
                result);
            return false;
        }
        bloom_targets_[index]->SetName(bloom_names[index]);
        D3D12_SHADER_RESOURCE_VIEW_DESC bloom_srv = {};
        bloom_srv.Format = bloom_description.Format;
        bloom_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        bloom_srv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        bloom_srv.Texture2D.MipLevels = 1u;
        device->CreateShaderResourceView(
            bloom_targets_[index].Get(), &bloom_srv,
            cpu_descriptor(bloom_srv_start + index));
        D3D12_UNORDERED_ACCESS_VIEW_DESC bloom_uav = {};
        bloom_uav.Format = bloom_description.Format;
        bloom_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            bloom_targets_[index].Get(), nullptr, &bloom_uav,
            cpu_descriptor(bloom_uav_start + index));
    }
    {
        D3D12_RESOURCE_DESC output_description = {};
        output_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        output_description.Width = post_width;
        output_description.Height = post_height;
        output_description.DepthOrArraySize = 1u;
        output_description.MipLevels = 1u;
        output_description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        output_description.SampleDesc.Count = 1u;
        output_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &output_description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&post_hdr_output_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(post-bloom HDR)",
                result);
            return false;
        }
        post_hdr_output_->SetName(
            L"AB3D2 Post-RR Bloom-Composited Linear HDR");
        D3D12_SHADER_RESOURCE_VIEW_DESC output_srv = {};
        output_srv.Format = output_description.Format;
        output_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        output_srv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        output_srv.Texture2D.MipLevels = 1u;
        device->CreateShaderResourceView(
            post_hdr_output_.Get(), &output_srv,
            cpu_descriptor(post_hdr_srv));
        if (!debug_view_requested_) {
            device->CreateShaderResourceView(
                post_hdr_output_.Get(), &output_srv,
                cpu_descriptor(reconstruction_srv_start));
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
        output_uav.Format = output_description.Format;
        output_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(
            post_hdr_output_.Get(), nullptr, &output_uav,
            cpu_descriptor(post_hdr_uav));
    }
    /* SpatialShade is retained as a diagnostic shader export but is never
     * dispatched. Give both of its root UAVs the same single-element binding
     * instead of carrying three full-resolution direct-reservoir allocations. */
    const D3D12_RESOURCE_DESC direct_reservoir_description = [] {
        D3D12_RESOURCE_DESC reservoir =
            buffer_description(sizeof(DxrLightReservoir));
        reservoir.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return reservoir;
    }();
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE,
            &direct_reservoir_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&direct_reservoir_binding_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(direct reservoir binding)",
                result);
            return false;
        }
        direct_reservoir_binding_->SetName(
            L"AB3D2 Inactive Direct Reservoir Binding");
    }
    const bool use_restir_gi =
        indirect_reconstruction_mode_ == static_cast<uint32_t>(
            indirect_reconstruction::Mode::restir);
    const UINT gi_reservoir_element_count =
        use_restir_gi ? width * height : 1u;
    const D3D12_RESOURCE_DESC gi_reservoir_description =
        [gi_reservoir_element_count] {
        D3D12_RESOURCE_DESC reservoir = buffer_description(
            static_cast<UINT64>(gi_reservoir_element_count) *
            sizeof(restir_gi::PackedReservoir));
        reservoir.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return reservoir;
    }();
    D3D12_UNORDERED_ACCESS_VIEW_DESC gi_uav = {};
    gi_uav.Format = DXGI_FORMAT_UNKNOWN;
    gi_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    gi_uav.Buffer.NumElements = gi_reservoir_element_count;
    gi_uav.Buffer.StructureByteStride = sizeof(restir_gi::PackedReservoir);
    for (size_t index = 0; index < gi_reservoirs_.size(); ++index) {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &gi_reservoir_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&gi_reservoirs_[index]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(GI reservoir)",
                result);
            return false;
        }
        gi_reservoirs_[index]->SetName(index == 0u ?
            L"AB3D2 ReSTIR GI Reservoirs A" :
            L"AB3D2 ReSTIR GI Reservoirs B");
        device->CreateUnorderedAccessView(
            gi_reservoirs_[index].Get(), nullptr, &gi_uav,
            cpu_descriptor(gi_reservoir_uav_start +
                           static_cast<UINT>(index)));
    }
    {
        const HRESULT result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &gi_reservoir_description,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&gi_reservoir_scratch_));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(GI temporal scratch)",
                result);
            return false;
        }
        gi_reservoir_scratch_->SetName(
            L"AB3D2 ReSTIR GI Temporal Scratch");
        device->CreateUnorderedAccessView(
            gi_reservoir_scratch_.Get(), nullptr, &gi_uav,
            cpu_descriptor(gi_reservoir_scratch_uav));
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

bool DxrPipeline::configure_output(ID3D12Device5 *device,
                                   const DxrOutputConfiguration &output,
                                   std::string &error)
{
    std::vector<unsigned char> vertex_shader;
    std::vector<unsigned char> pixel_shader;
    std::vector<unsigned char> present_vertex_shader;

    if (!device) {
        error = "DXR output pipeline received no D3D12 device";
        return false;
    }
    if (pipeline_state_ && present_pipeline_state_ &&
        output_.format == output.format) {
        output_ = output;
        return true;
    }
    if (!load_shader(L"diagnostic_vs.dxil", vertex_shader, error) ||
        !load_shader(L"diagnostic_ps.dxil", pixel_shader, error) ||
        !load_shader(L"present_vs.dxil", present_vertex_shader, error) ||
        !create_diagnostic_pipeline(device, vertex_shader, pixel_shader,
                                    output.format, error) ||
        !create_present_pipeline(device, present_vertex_shader, output.format,
                                 error)) {
        return false;
    }
    output_ = output;
    return true;
}

bool DxrPipeline::initialize(ID3D12Device5 *device,
                            const RendererRayTracingOptions &options,
                            const DxrOutputConfiguration &output,
                            std::string &error)
{
    if (!device) {
        error = "DXR pipeline received no D3D12 device";
        return false;
    }
    return configure_debug_view(error) &&
        configure_resampling(options, error) &&
        configure_output(device, output, error) &&
        create_post_pipeline(device, error) &&
        create_blue_noise_sampler(device, error) &&
        create_frame_constant_buffer(device, error) &&
        create_descriptor_heap(device, error) &&
        create_light_grid(device, error) &&
        create_diagnostics(device, error) &&
        create_raytracing_pipeline(device, error);
}

bool DxrPipeline::update_scene(const SceneFrame &frame, const RenderView &view,
                               UINT width, UINT height,
                               bool &requires_flush, std::string &error)
{
    const SceneCamera *camera = find_camera(frame);
    const reconstruction::CameraProjection projection = camera ?
        camera_projection(*camera, view, width, height) :
        reconstruction::CameraProjection{};
    return scene_.update(frame, camera ? &projection : nullptr,
                         requires_flush, error);
}

bool DxrPipeline::record(ID3D12Device5 *device,
                         ID3D12GraphicsCommandList4 *command_list,
                         UINT width, UINT height,
                         D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
                         const SceneFrame &frame,
                         const RenderView &view, uint32_t frame_number,
                         uint32_t frame_slot, float exposure_delta_seconds,
                         DxrStreamline *streamline,
                         std::string &error)
{
    if (!device || !command_list || !frame_constants_ ||
        frame_slot >= DxrScene::upload_frame_count) {
        error = "DXR frame recording received incomplete D3D12 state";
        return false;
    }
    if (!record_diagnostics_begin(command_list, error)) {
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
        if (!record_diagnostics_end(command_list, error)) {
            return false;
        }
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
    UINT reconstruction_output_width = width;
    UINT reconstruction_output_height = height;
    bool streamline_active = false;
#if defined(AB3D2_ENABLE_STREAMLINE)
    streamline_active = streamline && streamline->active();
    if (streamline_active &&
        streamline->active_mode() ==
            RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE) {
        /* Reconstruct at two thirds of the physical presentation extent,
         * then use the ordinary final presentation triangle for the remaining
         * linear upscale. Ultra Performance therefore traces at roughly two
         * ninths of the physical width/height while keeping the swap-chain
         * extent. */
        reconstruction_output_width = std::max(
            1u, static_cast<UINT>(
                (static_cast<UINT64>(width) * 2u + 2u) / 3u));
        reconstruction_output_height = std::max(
            1u, static_cast<UINT>(
                (static_cast<UINT64>(height) * 2u + 2u) / 3u));
    }
    if (!streamline ||
        !streamline->configure_output(
            reconstruction_output_width, reconstruction_output_height,
            render_width, render_height, error)) {
        return false;
    }
    streamline_active = streamline->active();
#else
    (void)streamline;
#endif
    bool speed_first_post = false;
#if defined(AB3D2_ENABLE_STREAMLINE)
    speed_first_post = streamline_active && streamline &&
        streamline->active_mode() ==
            RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE;
#endif
    const SceneCamera *camera = find_camera(frame);
    if (!camera) {
        error = "DXR SceneFrame has geometry but no camera command";
        return false;
    }
    bool targets_recreated = false;
    if (!ensure_reconstruction_targets(device, render_width, render_height,
                                       reconstruction_output_width,
                                       reconstruction_output_height,
                                       streamline_active,
                                       targets_recreated, error)) {
        return false;
    }
    const reconstruction::CameraProjection current_camera =
        camera_projection(*camera, view, render_width, render_height);
    const light_grid::Position current_light_grid_center =
        light_grid::quantized_center({
            current_camera.position.x,
            current_camera.position.y,
            current_camera.position.z});
    const uint64_t current_light_grid_layout_hash =
        scene_.light_grid_layout_hash();
    const uint32_t effective_indirect_spp = diffuse_gi_scale_ > 0.0f ?
        indirect_spp_ : 0u;
    const bool diffuse_gi_active = maximum_depth_ >= 2u &&
        effective_indirect_spp > 0u;
    const bool light_grid_active = maximum_depth_ >= 2u &&
        scene_.emitter_count() > 0u;
    const bool rebuild_light_grid = light_grid_active &&
        light_grid::cache_needs_rebuild(
            light_grid_center_, light_grid_layout_hash_,
            current_light_grid_center, current_light_grid_layout_hash,
            light_grid_cache_valid_);
    const bool history_valid = history_.valid && !targets_recreated &&
        !scene_.history_reset_pending() &&
        history_.history_epoch == frame.history_epoch &&
        history_.input_width == render_width &&
        history_.input_height == render_height;
    const uint32_t sample_index =
        history_valid ? history_.sample_index + 1u : 0u;
    /* The diffuse polygon-light pass varies NEE/continuation samples, not
     * primary visibility. Moving the camera ray within each pixel made
     * otherwise stable geometry edges visibly shake, so keep it pixel-centred
     * and report the same zero primary jitter to Streamline. */
    const reconstruction::PixelJitter current_jitter = {};
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
    constants.maximum_depth = maximum_depth_;
    constants.output_width = width;
    constants.output_height = height;
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
    constants.ndf_trim = ndf_trim_;
    constants.samples_per_pixel = spp_;
    constants.exposure_delta_seconds =
        std::isfinite(exposure_delta_seconds) && exposure_delta_seconds > 0.0f ?
        exposure_delta_seconds : 0.0f;
    constants.indirect_reconstruction_mode = indirect_reconstruction_mode_;
    constants.radiance_channel = radiance_channel_;
    uint64_t current_weapon_pose_hash = 0u;
    const bool current_weapon_pose_hash_valid =
        scene_.view_weapon_pose_hash(current_weapon_pose_hash);
    const bool weapon_transition = history_valid &&
        (current_weapon_pose_hash_valid != history_.weapon_pose_hash_valid ||
         (current_weapon_pose_hash_valid &&
          current_weapon_pose_hash != history_.weapon_pose_hash));
    constants.rr_weapon_pose_transition = weapon_transition ? 1u : 0u;
    constants.indirect_samples_per_pixel = effective_indirect_spp;
    constants.light_grid_center[0] = current_light_grid_center.x;
    constants.light_grid_center[1] = current_light_grid_center.y;
    constants.light_grid_center[2] = current_light_grid_center.z;
    constants.light_grid_rebuild = rebuild_light_grid ? 1u : 0u;
    constants.ray_reconstruction_active =
        streamline_active && !debug_view_requested_ ? 1u : 0u;
    constants.diagnostic_guide_mask =
        (debug_view_ == static_cast<uint32_t>(
             DxrReconstructionBuffer::linear_roughness) ? 1u : 0u) |
        ((debug_view_ == static_cast<uint32_t>(
              DxrReconstructionBuffer::diffuse_hit_distance) ||
          debug_view_ == static_cast<uint32_t>(
              DxrReconstructionBuffer::diffuse_hit_distance_history)) ?
             2u : 0u);
    constants.diffuse_gi_scale = diffuse_gi_scale_;
    const UINT64 frame_constant_offset = frame_constant_stride * frame_slot;
    void *mapped_frame_constants = nullptr;
    const D3D12_RANGE no_read = {0, 0};
    const HRESULT map_result = frame_constants_->Map(
        0, &no_read, &mapped_frame_constants);
    if (FAILED(map_result)) {
        error = hresult_error("ID3D12Resource::Map(frame constants)",
                              map_result);
        return false;
    }
    std::memcpy(static_cast<unsigned char *>(mapped_frame_constants) +
                    frame_constant_offset,
                &constants, sizeof(constants));
    const D3D12_RANGE written = {
        static_cast<SIZE_T>(frame_constant_offset),
        static_cast<SIZE_T>(frame_constant_offset + sizeof(constants))};
    frame_constants_->Unmap(0, &written);
    if (light_grid_needs_initial_transition_) {
        const D3D12_RESOURCE_BARRIER light_grid_state = transition(
            light_grid_.Get(), D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        command_list->ResourceBarrier(1, &light_grid_state);
    }
    if (targets_recreated) {
        const std::array<D3D12_RESOURCE_BARRIER, 9> history_states = {
            transition(direct_reservoir_binding_.Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(gi_reservoirs_[0].Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(gi_reservoirs_[1].Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(gi_reservoir_scratch_.Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(indirect_histories_[0].Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(indirect_histories_[1].Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(automatic_exposure_.Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(tone_map_histogram_.Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(tone_map_state_.Get(),
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        command_list->ResourceBarrier(
            static_cast<UINT>(history_states.size()),
            history_states.data());
    }

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
    command_list->SetComputeRootConstantBufferView(
        8, frame_constants_->GetGPUVirtualAddress() + frame_constant_offset);
    command_list->SetComputeRootUnorderedAccessView(
        9, direct_reservoir_binding_->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        10, direct_reservoir_binding_->GetGPUVirtualAddress());
    command_list->SetComputeRootUnorderedAccessView(
        11, diagnostics_->GetGPUVirtualAddress());
    command_list->SetComputeRootDescriptorTable(
        12, gpu_descriptor(light_grid_uav));
    command_list->SetPipelineState1(ray_state_object_.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS table = shader_table_->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch = {};
    dispatch.RayGenerationShaderRecord = {
        table + shader_record_size * shader_record_build_light_grid,
        shader_record_size};
    dispatch.MissShaderTable = {
        table + shader_record_size * shader_record_surface_miss,
                                shader_record_size * 2u, shader_record_size};
    dispatch.HitGroupTable = {
        table + shader_record_size * shader_record_hit_group,
        shader_record_size, shader_record_size};
    if (light_grid_active) {
        /* A new center/layout fills all 512 entries per cell. Ordinary frames
         * replace one interleaved sixteenth, keeping proposal coverage fresh
         * while halving the recurring cache bandwidth and candidate work. */
        dispatch.Width = light_grid::cell_count;
        dispatch.Height = rebuild_light_grid ? light_grid::lights_per_cell :
            light_grid::lights_per_cell / light_grid::refresh_phase_count;
        dispatch.Depth = 1u;
        command_list->DispatchRays(&dispatch);
        const D3D12_RESOURCE_BARRIER light_grid_ready =
            uav_barrier(light_grid_.Get());
        command_list->ResourceBarrier(1, &light_grid_ready);
    }
    /* Primary polygon NEE keeps the complete global proposal. Diffuse and
     * smooth-specular reached surfaces draw from the grid built above.
     * SpatialShade remains dormant: no temporal or neighboring screen-space
     * direct reservoir is shaded. */
    dispatch.RayGenerationShaderRecord = {
        table + shader_record_size * shader_record_ray_generation,
        shader_record_size};
    dispatch.Width = render_width;
    dispatch.Height = render_height;
    dispatch.Depth = 1;
    command_list->DispatchRays(&dispatch);
    const auto indirect_mode = static_cast<indirect_reconstruction::Mode>(
        indirect_reconstruction_mode_);
    const bool use_restir_gi = diffuse_gi_active &&
        indirect_mode == indirect_reconstruction::Mode::restir;
    const D3D12_RESOURCE_BARRIER indirect_input_ready[] = {
        uav_barrier(indirect_radiance_.Get()),
        uav_barrier(indirect_chroma_.Get()),
        uav_barrier(indirect_histories_[sample_index & 1u].Get()),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::diffuse_albedo)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::shading_normal)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::linear_depth)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::scene_motion)),
        uav_barrier(surface_parameters_.Get()),
    };
    command_list->ResourceBarrier(
        static_cast<UINT>(std::size(indirect_input_ready)),
        indirect_input_ready);
    const UINT indirect_low_width = (render_width +
        indirect_reconstruction::downsample_factor - 1u) /
        indirect_reconstruction::downsample_factor;
    const UINT indirect_low_height = (render_height +
        indirect_reconstruction::downsample_factor - 1u) /
        indirect_reconstruction::downsample_factor;
    const bool use_temporal_reconstruction = diffuse_gi_active &&
        indirect_mode != indirect_reconstruction::Mode::raw &&
        !use_restir_gi;
    const bool use_regional_reconstruction = diffuse_gi_active &&
        indirect_mode != indirect_reconstruction::Mode::temporal &&
        indirect_mode != indirect_reconstruction::Mode::raw &&
        !use_restir_gi;
    const bool use_deflicker =
        indirect_mode == indirect_reconstruction::Mode::full ||
        indirect_mode == indirect_reconstruction::Mode::deflicker ||
        indirect_mode == indirect_reconstruction::Mode::wavelet1 ||
        indirect_mode == indirect_reconstruction::Mode::wavelet2;
    const UINT wavelet_pass_count =
        indirect_mode == indirect_reconstruction::Mode::full ? 3u :
        indirect_mode == indirect_reconstruction::Mode::wavelet1 ? 1u :
        indirect_mode == indirect_reconstruction::Mode::wavelet2 ? 2u : 0u;
    if (use_restir_gi) {
        const D3D12_RESOURCE_BARRIER initial_gi_ready =
            uav_barrier(gi_reservoirs_[sample_index & 1u].Get());
        command_list->ResourceBarrier(1, &initial_gi_ready);
        dispatch.Width = render_width;
        dispatch.Height = render_height;
        dispatch.RayGenerationShaderRecord = {
            table + shader_record_size * shader_record_temporal_gi,
            shader_record_size};
        command_list->DispatchRays(&dispatch);
        const D3D12_RESOURCE_BARRIER temporal_gi_ready =
            uav_barrier(gi_reservoir_scratch_.Get());
        command_list->ResourceBarrier(1, &temporal_gi_ready);

        dispatch.RayGenerationShaderRecord = {
            table + shader_record_size * shader_record_spatial_gi,
            shader_record_size};
        command_list->DispatchRays(&dispatch);
        const D3D12_RESOURCE_BARRIER spatial_gi_ready =
            uav_barrier(gi_reservoirs_[sample_index & 1u].Get());
        command_list->ResourceBarrier(1, &spatial_gi_ready);
    }
    if (use_temporal_reconstruction) {
        dispatch.Width = indirect_low_width;
        dispatch.Height = indirect_low_height;
        dispatch.RayGenerationShaderRecord = {
            table + shader_record_size * shader_record_build_indirect_gradient,
            shader_record_size};
        command_list->DispatchRays(&dispatch);
        D3D12_RESOURCE_BARRIER gradient_ready =
            uav_barrier(indirect_gradients_[0].Get());
        command_list->ResourceBarrier(1, &gradient_ready);
        for (UINT gradient_pass = 0u;
             gradient_pass <
                 indirect_reconstruction::gradient_filter_steps.size();
             ++gradient_pass) {
            dispatch.RayGenerationShaderRecord = {
                table + shader_record_size *
                    (shader_record_filter_indirect_gradient_0 + gradient_pass),
                shader_record_size};
            command_list->DispatchRays(&dispatch);
            const size_t output_slot = 1u - (gradient_pass & 1u);
            gradient_ready = uav_barrier(
                indirect_gradients_[output_slot].Get());
            command_list->ResourceBarrier(1, &gradient_ready);
        }

        dispatch.Width = render_width;
        dispatch.Height = render_height;
        dispatch.RayGenerationShaderRecord = {
            table + shader_record_size * shader_record_temporal_indirect,
            shader_record_size};
        command_list->DispatchRays(&dispatch);
        const size_t indirect_history_slot = sample_index & 1u;
        const D3D12_RESOURCE_BARRIER temporal_indirect_ready =
            uav_barrier(indirect_histories_[indirect_history_slot].Get());
        command_list->ResourceBarrier(1, &temporal_indirect_ready);
    }

    if (use_regional_reconstruction) {
        /* Integrate gradient-responsive temporal incident radiance into
         * guide-compatible 3x3 regions, deflicker that one-third-resolution
         * image, then run its three wavelet stages before bilateral
         * reconstruction. */
        ID3D12Resource *const indirect_filter_outputs[] = {
            indirect_radiance_.Get(), indirect_radiance_.Get(),
            indirect_filtered_.Get(), indirect_radiance_.Get()};
        ID3D12Resource *const indirect_chroma_outputs[] = {
            indirect_chroma_.Get(), indirect_chroma_.Get(),
            indirect_chroma_filtered_.Get(), indirect_chroma_.Get()};
        dispatch.Width = indirect_low_width;
        dispatch.Height = indirect_low_height;
        dispatch.RayGenerationShaderRecord = {
            table + shader_record_size * shader_record_filter_indirect_0,
            shader_record_size};
        command_list->DispatchRays(&dispatch);
        D3D12_RESOURCE_BARRIER filter_ready[] = {
            uav_barrier(indirect_filter_outputs[0]),
            uav_barrier(indirect_chroma_outputs[0]),
        };
        command_list->ResourceBarrier(
            static_cast<UINT>(std::size(filter_ready)), filter_ready);

        if (use_deflicker) {
            dispatch.RayGenerationShaderRecord = {
                table + shader_record_size * shader_record_deflicker_indirect,
                shader_record_size};
            command_list->DispatchRays(&dispatch);
            const D3D12_RESOURCE_BARRIER deflicker_ready[] = {
                uav_barrier(indirect_filtered_.Get()),
                uav_barrier(indirect_chroma_filtered_.Get()),
            };
            command_list->ResourceBarrier(
                static_cast<UINT>(std::size(deflicker_ready)),
                deflicker_ready);

            for (UINT filter_pass = 1u;
                 filter_pass <= wavelet_pass_count; ++filter_pass) {
                dispatch.RayGenerationShaderRecord = {
                    table + shader_record_size *
                        (shader_record_filter_indirect_1 + filter_pass - 1u),
                    shader_record_size};
                command_list->DispatchRays(&dispatch);
                filter_ready[0] =
                    uav_barrier(indirect_filter_outputs[filter_pass]);
                filter_ready[1] =
                    uav_barrier(indirect_chroma_outputs[filter_pass]);
                command_list->ResourceBarrier(
                    static_cast<UINT>(std::size(filter_ready)), filter_ready);
            }
        }
        const bool final_signal_is_filtered =
            indirect_mode == indirect_reconstruction::Mode::deflicker ||
            indirect_mode == indirect_reconstruction::Mode::wavelet2;
        if (final_signal_is_filtered) {
            dispatch.RayGenerationShaderRecord = {
                table + shader_record_size *
                    shader_record_resolve_indirect_filtered,
                shader_record_size};
            command_list->DispatchRays(&dispatch);
            const D3D12_RESOURCE_BARRIER resolved_ready[] = {
                uav_barrier(indirect_radiance_.Get()),
                uav_barrier(indirect_chroma_.Get()),
            };
            command_list->ResourceBarrier(
                static_cast<UINT>(std::size(resolved_ready)), resolved_ready);
        }
    }
    dispatch.Width = render_width;
    dispatch.Height = render_height;
    dispatch.RayGenerationShaderRecord = {
        table + shader_record_size * shader_record_reconstruct_indirect,
        shader_record_size};
    command_list->DispatchRays(&dispatch);
    const D3D12_RESOURCE_BARRIER indirect_output_ready[] = {
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::noisy_radiance)),
        uav_barrier(indirect_filtered_.Get()),
    };
    command_list->ResourceBarrier(
        static_cast<UINT>(std::size(indirect_output_ready)),
        indirect_output_ready);
    const D3D12_RESOURCE_BARRIER guide_barriers[] = {
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::noisy_radiance)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::diffuse_albedo)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::specular_albedo)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::shading_normal)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::linear_depth)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::scene_motion)),
        uav_barrier(reconstruction_resource(
            DxrReconstructionBuffer::specular_hit_distance)),
    };
    command_list->ResourceBarrier(
        static_cast<UINT>(std::size(guide_barriers)), guide_barriers);
    const D3D12_RESOURCE_BARRIER rr_weapon_guides_ready[] = {
        uav_barrier(streamline_scene_motion_.Get()),
        uav_barrier(view_weapon_histories_[sample_index & 1u].Get()),
        uav_barrier(rr_disocclusion_mask_.Get()),
        uav_barrier(rr_bias_current_color_mask_.Get()),
    };
    command_list->ResourceBarrier(
        static_cast<UINT>(std::size(rr_weapon_guides_ready)),
        rr_weapon_guides_ready);
    if (use_restir_gi) {
        const D3D12_RESOURCE_BARRIER reservoir_barriers[] = {
            uav_barrier(gi_reservoirs_[0].Get()),
            uav_barrier(gi_reservoirs_[1].Get()),
            uav_barrier(gi_reservoir_scratch_.Get()),
        };
        command_list->ResourceBarrier(
            static_cast<UINT>(std::size(reservoir_barriers)),
            reservoir_barriers);
    }

    const bool diffuse_distance_debug =
        debug_view_ == static_cast<uint32_t>(
            DxrReconstructionBuffer::diffuse_hit_distance) ||
        debug_view_ == static_cast<uint32_t>(
            DxrReconstructionBuffer::diffuse_hit_distance_history);
    if (diffuse_distance_debug) {
        /* This private distance is not a Streamline input or a production
         * reconstruction dependency. Publish its full history only when one
         * of the two explicit diagnostic views requested it at startup. */
        ID3D12Resource *hit_distance = reconstruction_resource(
            DxrReconstructionBuffer::diffuse_hit_distance);
        ID3D12Resource *hit_distance_history = reconstruction_resource(
            DxrReconstructionBuffer::diffuse_hit_distance_history);
        const D3D12_RESOURCE_BARRIER to_history_copy[] = {
            transition(hit_distance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(hit_distance_history,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_DEST),
        };
        command_list->ResourceBarrier(2, to_history_copy);
        command_list->CopyResource(hit_distance_history, hit_distance);
        const D3D12_RESOURCE_BARRIER from_history_copy[] = {
            transition(hit_distance, D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            transition(hit_distance_history,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        command_list->ResourceBarrier(2, from_history_copy);
    }
#if defined(AB3D2_ENABLE_STREAMLINE)
    if (streamline_active) {
        const DxrStreamlineResources resources = {
            reconstruction_resource(DxrReconstructionBuffer::noisy_radiance),
            streamline_output_.Get(),
            reconstruction_resource(DxrReconstructionBuffer::diffuse_albedo),
            reconstruction_resource(DxrReconstructionBuffer::specular_albedo),
            reconstruction_resource(DxrReconstructionBuffer::shading_normal),
            reconstruction_resource(DxrReconstructionBuffer::linear_depth),
            streamline_scene_motion_.Get(),
            reconstruction_resource(
                DxrReconstructionBuffer::specular_hit_distance),
            rr_disocclusion_mask_.Get(),
            rr_bias_current_color_mask_.Get(),
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
    ID3D12Resource *const post_resource = streamline_active ?
        streamline_output_.Get() :
        reconstruction_resource(DxrReconstructionBuffer::noisy_radiance);
    const UINT post_width = streamline_active ?
        reconstruction_output_width : render_width;
    const UINT post_height = streamline_active ?
        reconstruction_output_height : render_height;
    constexpr D3D12_RESOURCE_STATES post_shader_resource_state =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_BARRIER post_source_ready = transition(
        post_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        post_shader_resource_state);
    command_list->ResourceBarrier(1, &post_source_ready);

    ID3D12DescriptorHeap *post_heaps[] = {descriptor_heap_.Get()};
    command_list->SetDescriptorHeaps(1, post_heaps);
    command_list->SetComputeRootSignature(post_root_signature_.Get());
    command_list->SetComputeRootDescriptorTable(
        0, gpu_descriptor(post_input_srv));
    command_list->SetComputeRootDescriptorTable(
        1, gpu_descriptor(post_histogram_uav));
    command_list->SetComputeRootUnorderedAccessView(
        2, diagnostics_->GetGPUVirtualAddress());

    const UINT histogram_clear[4] = {};
    command_list->ClearUnorderedAccessViewUint(
        gpu_descriptor(post_histogram_uav), histogram_clear_descriptor(),
        tone_map_histogram_.Get(), histogram_clear, 0u, nullptr);
    const D3D12_RESOURCE_BARRIER histogram_cleared =
        uav_barrier(tone_map_histogram_.Get());
    command_list->ResourceBarrier(1, &histogram_cleared);

    const std::array<UINT, 3> bloom_widths = {
        std::max(1u, (post_width + 1u) / 2u),
        std::max(1u, (post_width + 3u) / 4u),
        std::max(1u, (post_width + 7u) / 8u),
    };
    const std::array<UINT, 3> bloom_heights = {
        std::max(1u, (post_height + 1u) / 2u),
        std::max(1u, (post_height + 3u) / 4u),
        std::max(1u, (post_height + 7u) / 8u),
    };
    const auto run_bloom = [&](BloomOperation operation,
                               UINT source_width, UINT source_height,
                               UINT target_width, UINT target_height,
                               UINT input_descriptor, UINT low_descriptor,
                               UINT output_descriptor,
                               ID3D12Resource *output_resource) {
        const PostConstants pass_constants = {
            source_width, source_height, target_width, target_height,
            constants.exposure_delta_seconds, history_valid ? 0u : 1u,
            static_cast<uint32_t>(operation), 0u};
        command_list->SetComputeRoot32BitConstants(
            3, sizeof(pass_constants) / sizeof(uint32_t),
            &pass_constants, 0u);
        command_list->SetComputeRootDescriptorTable(
            4, gpu_descriptor(input_descriptor));
        command_list->SetComputeRootDescriptorTable(
            5, gpu_descriptor(low_descriptor));
        command_list->SetComputeRootDescriptorTable(
            6, gpu_descriptor(output_descriptor));
        command_list->SetPipelineState(post_bloom_pipeline_state_.Get());
        command_list->Dispatch((target_width + 7u) / 8u,
                               (target_height + 7u) / 8u, 1u);
        const D3D12_RESOURCE_BARRIER written = uav_barrier(output_resource);
        command_list->ResourceBarrier(1, &written);
        const D3D12_RESOURCE_BARRIER readable = transition(
            output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &readable);
    };
    const auto bloom_srv = [](UINT index) {
        return static_cast<UINT>(bloom_srv_start) + index;
    };
    const auto bloom_uav = [](UINT index) {
        return static_cast<UINT>(bloom_uav_start) + index;
    };
    const auto make_bloom_writable = [&](UINT index) {
        const D3D12_RESOURCE_BARRIER writable = transition(
            bloom_targets_[index].Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        command_list->ResourceBarrier(1, &writable);
    };

    run_bloom(bloom_extract, post_width, post_height,
              bloom_widths[0], bloom_heights[0], post_input_srv,
              post_input_srv, bloom_uav(bloom_half_a),
              bloom_targets_[bloom_half_a].Get());
    if (!speed_first_post) {
        run_bloom(bloom_blur_horizontal, bloom_widths[0], bloom_heights[0],
                  bloom_widths[0], bloom_heights[0], bloom_srv(bloom_half_a),
                  post_input_srv, bloom_uav(bloom_half_b),
                  bloom_targets_[bloom_half_b].Get());
        make_bloom_writable(bloom_half_a);
        run_bloom(bloom_blur_vertical, bloom_widths[0], bloom_heights[0],
                  bloom_widths[0], bloom_heights[0], bloom_srv(bloom_half_b),
                  post_input_srv, bloom_uav(bloom_half_a),
                  bloom_targets_[bloom_half_a].Get());
    }

    run_bloom(bloom_downsample, bloom_widths[0], bloom_heights[0],
              bloom_widths[1], bloom_heights[1], bloom_srv(bloom_half_a),
              post_input_srv, bloom_uav(bloom_quarter_a),
              bloom_targets_[bloom_quarter_a].Get());
    if (!speed_first_post) {
        run_bloom(bloom_blur_horizontal, bloom_widths[1], bloom_heights[1],
                  bloom_widths[1], bloom_heights[1], bloom_srv(bloom_quarter_a),
                  post_input_srv, bloom_uav(bloom_quarter_b),
                  bloom_targets_[bloom_quarter_b].Get());
        make_bloom_writable(bloom_quarter_a);
        run_bloom(bloom_blur_vertical, bloom_widths[1], bloom_heights[1],
                  bloom_widths[1], bloom_heights[1], bloom_srv(bloom_quarter_b),
                  post_input_srv, bloom_uav(bloom_quarter_a),
                  bloom_targets_[bloom_quarter_a].Get());
    }

    run_bloom(bloom_downsample, bloom_widths[1], bloom_heights[1],
              bloom_widths[2], bloom_heights[2], bloom_srv(bloom_quarter_a),
              post_input_srv, bloom_uav(bloom_eighth_a),
              bloom_targets_[bloom_eighth_a].Get());
    run_bloom(bloom_blur_horizontal, bloom_widths[2], bloom_heights[2],
              bloom_widths[2], bloom_heights[2], bloom_srv(bloom_eighth_a),
              post_input_srv, bloom_uav(bloom_eighth_b),
              bloom_targets_[bloom_eighth_b].Get());
    make_bloom_writable(bloom_eighth_a);
    run_bloom(bloom_blur_vertical, bloom_widths[2], bloom_heights[2],
              bloom_widths[2], bloom_heights[2], bloom_srv(bloom_eighth_b),
              post_input_srv, bloom_uav(bloom_eighth_a),
              bloom_targets_[bloom_eighth_a].Get());

    if (!speed_first_post) {
        make_bloom_writable(bloom_quarter_b);
    }
    run_bloom(bloom_upsample, bloom_widths[1], bloom_heights[1],
              bloom_widths[1], bloom_heights[1], bloom_srv(bloom_quarter_a),
              bloom_srv(bloom_eighth_a), bloom_uav(bloom_quarter_b),
              bloom_targets_[bloom_quarter_b].Get());
    if (!speed_first_post) {
        make_bloom_writable(bloom_half_b);
    }
    run_bloom(bloom_upsample, bloom_widths[0], bloom_heights[0],
              bloom_widths[0], bloom_heights[0], bloom_srv(bloom_half_a),
              bloom_srv(bloom_quarter_b), bloom_uav(bloom_half_b),
              bloom_targets_[bloom_half_b].Get());
    run_bloom(bloom_composite, post_width, post_height,
              post_width, post_height, bloom_srv(bloom_half_b),
              post_input_srv, post_hdr_uav, post_hdr_output_.Get());

    command_list->SetComputeRootDescriptorTable(
        0, gpu_descriptor(post_hdr_srv));
    const PostConstants post_constants = {
        post_width, post_height, post_width, post_height,
        constants.exposure_delta_seconds, history_valid ? 0u : 1u,
        0u, 0u};
    command_list->SetComputeRoot32BitConstants(
        3, sizeof(post_constants) / sizeof(uint32_t), &post_constants, 0u);
    command_list->SetPipelineState(post_histogram_pipeline_state_.Get());
    command_list->Dispatch((post_width + 15u) / 16u,
                           (post_height + 15u) / 16u, 1u);
    const D3D12_RESOURCE_BARRIER histogram_ready =
        uav_barrier(tone_map_histogram_.Get());
    command_list->ResourceBarrier(1, &histogram_ready);
    command_list->SetPipelineState(post_curve_pipeline_state_.Get());
    command_list->Dispatch(1u, 1u, 1u);
    const D3D12_RESOURCE_BARRIER curve_ready =
        uav_barrier(tone_map_state_.Get());
    command_list->ResourceBarrier(1, &curve_ready);
    const D3D12_RESOURCE_BARRIER tone_state_to_present = transition(
        tone_map_state_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &tone_state_to_present);
    const D3D12_RESOURCE_BARRIER post_hdr_to_present = transition(
        post_hdr_output_.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &post_hdr_to_present);
    if (!record_diagnostics_end(command_list, error)) {
        return false;
    }

    if (!scene_.record_promote_vertex_history(command_list, error)) {
        return false;
    }

    const bool present_post_hdr = !debug_view_requested_;
    ID3D12Resource *present_resource = present_post_hdr ?
        post_hdr_output_.Get() :
        (debug_view_ == static_cast<uint32_t>(DxrReconstructionBuffer::count) ?
            indirect_filtered_.Get() : reconstruction_targets_[debug_view_].Get());
    if (present_resource != post_resource &&
        present_resource != post_hdr_output_.Get()) {
        const D3D12_RESOURCE_BARRIER debug_to_present = transition(
            present_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &debug_to_present);
    }
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
        present_post_hdr ? post_width : render_width,
        present_post_hdr ? post_height : render_height,
        width, height, exposure_bias_stops_,
        static_cast<uint32_t>(frame_number),
        output_.hdr ? 1u : 0u, output_.peak_nits,
        output_.saturation_scale};
    command_list->SetGraphicsRoot32BitConstants(
        1, sizeof(present_constants) / sizeof(uint32_t), &present_constants, 0);
    command_list->SetGraphicsRootShaderResourceView(
        2, blue_noise_sampler_->GetGPUVirtualAddress());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list->DrawInstanced(3, 1, 0, 0);
    std::array<D3D12_RESOURCE_BARRIER, 10> to_next_sample = {};
    UINT next_sample_barrier_count = 0u;
    if (present_resource != post_resource &&
        present_resource != post_hdr_output_.Get()) {
        to_next_sample[next_sample_barrier_count++] = transition(
            present_resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    to_next_sample[next_sample_barrier_count++] = transition(
        post_resource, post_shader_resource_state,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_next_sample[next_sample_barrier_count++] = transition(
        post_hdr_output_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_next_sample[next_sample_barrier_count++] = transition(
        tone_map_state_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (const auto &target : bloom_targets_) {
        to_next_sample[next_sample_barrier_count++] = transition(
            target.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    command_list->ResourceBarrier(next_sample_barrier_count,
                                  to_next_sample.data());
    history_.pending_camera = current_camera;
    history_.pending_jitter = current_jitter;
    history_.pending_history_epoch = frame.history_epoch;
    history_.pending_sample_index = sample_index;
    history_.pending_input_width = render_width;
    history_.pending_input_height = render_height;
    history_.pending_weapon_pose_hash = current_weapon_pose_hash;
    history_.pending_weapon_pose_hash_valid =
        current_weapon_pose_hash_valid;
    history_.pending = true;
    if (!light_grid_active) {
        light_grid_cache_valid_ = false;
    } else if (rebuild_light_grid) {
        light_grid_center_ = current_light_grid_center;
        light_grid_layout_hash_ = current_light_grid_layout_hash;
        light_grid_cache_valid_ = true;
    }
    light_grid_needs_initial_transition_ = false;
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
    history_.weapon_pose_hash = history_.pending_weapon_pose_hash;
    history_.weapon_pose_hash_valid =
        history_.pending_weapon_pose_hash_valid;
    history_.valid = true;
    history_.pending = false;
    scene_.mark_history_promoted();
}

bool DxrPipeline::select_radiance_channel(uint32_t channel)
{
    if (channel > static_cast<uint32_t>(
            indirect_reconstruction::RadianceChannel::rough_specular)) {
        return false;
    }
    radiance_channel_ = channel;
    return true;
}

}  // namespace ab3d2::dxr
