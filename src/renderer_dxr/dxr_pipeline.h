#ifndef AB3D2_DXR_PIPELINE_H
#define AB3D2_DXR_PIPELINE_H

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render_view.h"
#include "renderer_ray_tracing_options.h"
#include "scene_frame.h"
#include "dxr_reconstruction_math.h"
#include "dxr_light_grid.h"
#include "dxr_restir_gi.h"
#include "dxr_scene.h"

namespace ab3d2::dxr {

class DxrStreamline;

/*
 * Layout mirrored by `PackedLightReservoir` in shaders/path_trace.hlsl. One
 * direct-lighting reservoir per render-resolution pixel, double buffered so the
 * ray shader never reads and writes the same allocation.
 */
struct DxrLightReservoir {
    uint32_t emitter_index;
    uint32_t position_sample;
    float unbiased_weight;
    uint32_t sample_count;
    float surface_position[3];
    uint32_t surface_normal;
    float surface_texture_coordinate[2];
    uint32_t surface_geometric_normal;
    uint32_t surface_material_index;
    uint32_t surface_texture_window_origin;
    uint32_t surface_texture_window_extent;
};

static_assert(sizeof(DxrLightReservoir) == 56u);

enum class DxrReconstructionBuffer : size_t {
    noisy_radiance,
    diffuse_albedo,
    specular_albedo,
    shading_normal,
    linear_roughness,
    linear_depth,
    scene_motion,
    specular_hit_distance,
    diffuse_hit_distance,
    specular_hit_distance_history,
    count,
};

class DxrPipeline final {
public:
    bool initialize(ID3D12Device5 *device,
                    const RendererRayTracingOptions &options,
                    std::string &error);
    bool update_scene(const SceneFrame &frame, const RenderView &view,
                      UINT width, UINT height, bool &requires_flush,
                      std::string &error);
    bool record(ID3D12Device5 *device, ID3D12GraphicsCommandList4 *command_list,
                 UINT width, UINT height,
                 D3D12_CPU_DESCRIPTOR_HANDLE render_target_view,
                 const SceneFrame &frame,
                 const RenderView &view, uint32_t frame_number,
                 uint32_t frame_slot, float exposure_delta_seconds,
                 DxrStreamline *streamline,
                 std::string &error);
    void commit_presented_frame();
    bool collect_diagnostics(std::string &error);

    ID3D12RootSignature *root_signature() const { return root_signature_.Get(); }
    ID3D12PipelineState *pipeline_state() const { return pipeline_state_.Get(); }
    bool has_scene() const { return scene_.ready(); }
    uint64_t scene_rebuild_count() const { return scene_.rebuild_count(); }
    size_t last_view_weapon_coverage() const {
        return last_view_weapon_coverage_;
    }
    uint64_t last_view_weapon_rgb_checksum() const {
        return last_view_weapon_rgb_checksum_;
    }
    size_t last_world_bitmap_coverage() const {
        return last_world_bitmap_coverage_;
    }
    size_t last_world_vector_coverage() const {
        return last_world_vector_coverage_;
    }
    /* Primary-ray pixels that crossed at least one additive layer. Additive
     * effects are never the primary surface, so the two counters above cannot
     * report them. */
    size_t last_world_additive_coverage() const {
        return last_world_additive_coverage_;
    }
    /* The settings actually in force, after ab3d2.ini and any environment
     * override have been applied over the tuned defaults. */
    void active_ray_tracing_options(RendererRayTracingOptions &options) const {
        options.samples_per_pixel = static_cast<uint8_t>(spp_);
        options.maximum_bounces = static_cast<uint8_t>(maximum_depth_);
        options.light_candidates = static_cast<uint16_t>(candidate_count_);
        options.reservoir_sample_limit = reservoir_sample_limit_;
        options.reservoir_sample_limit_set = UINT8_MAX;
        options.radiance_clamp = radiance_clamp_;
        options.exposure = exposure_;
        options.ndf_trim = ndf_trim_;
    }
    ID3D12Resource *reconstruction_resource(
        DxrReconstructionBuffer buffer) const;

private:
    static bool load_shader(const wchar_t *filename, std::vector<unsigned char> &bytes,
                            std::string &error);
    bool create_diagnostic_pipeline(ID3D12Device5 *device,
                                    const std::vector<unsigned char> &vertex_shader,
                                    const std::vector<unsigned char> &pixel_shader,
                                    std::string &error);
    bool create_present_pipeline(ID3D12Device5 *device,
                                 const std::vector<unsigned char> &vertex_shader,
                                 std::string &error);
    bool create_post_pipeline(ID3D12Device5 *device, std::string &error);
    bool create_raytracing_pipeline(ID3D12Device5 *device, std::string &error);
    bool create_blue_noise_sampler(ID3D12Device5 *device, std::string &error);
    bool create_frame_constant_buffer(ID3D12Device5 *device,
                                      std::string &error);
    bool create_light_grid(ID3D12Device5 *device, std::string &error);
    bool create_diagnostics(ID3D12Device5 *device, std::string &error);
    bool create_descriptor_heap(ID3D12Device5 *device, std::string &error);
    bool configure_debug_view(std::string &error);
    bool configure_resampling(const RendererRayTracingOptions &options,
                              std::string &error);
    bool ensure_reconstruction_targets(ID3D12Device5 *device, UINT width,
                                       UINT height, UINT present_width,
                                       UINT present_height,
                                       bool create_streamline_output,
                                       bool &recreated,
                                       std::string &error);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE diagnostic_clear_descriptor() const;
    D3D12_CPU_DESCRIPTOR_HANDLE histogram_clear_descriptor() const;
    bool record_diagnostics_begin(ID3D12GraphicsCommandList4 *command_list,
                                  std::string &error);
    bool record_diagnostics_end(ID3D12GraphicsCommandList4 *command_list,
                                std::string &error);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> present_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> present_pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> post_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_histogram_pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_curve_pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> post_bloom_pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ray_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12StateObject> ray_state_object_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blue_noise_sampler_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constants_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_grid_;
    Microsoft::WRL::ComPtr<ID3D12Resource> diagnostics_;
    Microsoft::WRL::ComPtr<ID3D12Resource> diagnostics_readback_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirect_radiance_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirect_filtered_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirect_chroma_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirect_chroma_filtered_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>
        indirect_gradients_;
    Microsoft::WRL::ComPtr<ID3D12Resource> automatic_exposure_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tone_map_histogram_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tone_map_state_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 6> bloom_targets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> post_hdr_output_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> indirect_histories_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> diagnostic_cpu_heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               static_cast<size_t>(DxrReconstructionBuffer::count)>
        reconstruction_targets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> streamline_output_;
    Microsoft::WRL::ComPtr<ID3D12Resource> temporal_reservoirs_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> light_reservoirs_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> gi_reservoirs_;
    Microsoft::WRL::ComPtr<ID3D12Resource> gi_reservoir_scratch_;
    UINT descriptor_size_ = 0;
    UINT render_width_ = 0;
    UINT render_height_ = 0;
    UINT present_width_ = 0;
    UINT present_height_ = 0;
    uint32_t candidate_count_ =
        RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES;
    uint32_t reservoir_sample_limit_ =
        RENDERER_RAY_TRACING_DEFAULT_RESERVOIR_SAMPLE_LIMIT;
    float radiance_clamp_ = 200.0f;
    float exposure_ = 1.0f;
    float ndf_trim_ = 0.9f;
    uint32_t spp_ = 1u;
    /* Path length counting the primary hit; ab3d2.ini may change it. */
    uint32_t maximum_depth_ = 3u;
    uint32_t debug_view_ = 0;
    bool debug_view_requested_ = false;
    float debug_scalar_range_ = 8192.0f;
    uint32_t indirect_reconstruction_mode_ = 0u;
    uint32_t radiance_channel_ = 0u;
    size_t last_view_weapon_coverage_ = 0u;
    uint64_t last_view_weapon_rgb_checksum_ = 0u;
    size_t last_world_bitmap_coverage_ = 0u;
    size_t last_world_vector_coverage_ = 0u;
    size_t last_world_additive_coverage_ = 0u;
    float last_target_exposure_ = 1.0f;
    float last_automatic_exposure_ = 1.0f;
    float last_metered_average_luminance_ = 0.0f;
    float last_metered_low_luminance_ = 0.0f;
    float last_metered_high_luminance_ = 0.0f;
    uint32_t last_metered_weight_ = 0u;
    bool diagnostics_have_output_ = false;
    struct DxrFrameHistory {
        reconstruction::CameraProjection previous_camera = {};
        reconstruction::PixelJitter previous_jitter = {};
        reconstruction::CameraProjection pending_camera = {};
        reconstruction::PixelJitter pending_jitter = {};
        uint64_t history_epoch = 0;
        uint64_t pending_history_epoch = 0;
        uint32_t sample_index = 0;
        uint32_t pending_sample_index = 0;
        UINT input_width = 0;
        UINT input_height = 0;
        UINT pending_input_width = 0;
        UINT pending_input_height = 0;
        bool valid = false;
        bool pending = false;
    } history_;
    DxrScene scene_;
};

}  // namespace ab3d2::dxr

#endif
