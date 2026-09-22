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
#include "dxr_indirect_reconstruction.h"
#include "dxr_light_grid.h"
#include "dxr_output.h"
#include "dxr_scene.h"

namespace ab3d2::dxr {

class DxrStreamline;
class DxrGpuProfiler;

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
    diffuse_hit_distance_history,
    count,
};

class DxrPipeline final {
public:
    bool initialize(ID3D12Device5 *device,
                    const RendererRayTracingOptions &options,
                    const DxrOutputConfiguration &output,
                    std::string &error);
    bool configure_output(ID3D12Device5 *device,
                          const DxrOutputConfiguration &output,
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
                DxrStreamline *streamline, bool validation_enabled,
                DxrGpuProfiler *profiler,
                std::string &error);
    void commit_presented_frame();
    bool collect_diagnostics(std::string &error);
    bool select_radiance_channel(uint32_t channel);

    ID3D12RootSignature *root_signature() const { return root_signature_.Get(); }
    ID3D12PipelineState *pipeline_state() const { return pipeline_state_.Get(); }
    bool has_scene() const { return scene_.ready(); }
    uint64_t scene_rebuild_count() const { return scene_.rebuild_count(); }
    bool prepare_vector_materials(const uint32_t *asset_ids, size_t asset_count,
                                  size_t &prepared, std::string &error)
    {
        return scene_.prepare_vector_materials(asset_ids, asset_count, prepared,
                                               error);
    }
    bool prepare_bitmap_materials(size_t asset_count, size_t &prepared,
                                  std::string &error)
    {
        return scene_.prepare_bitmap_materials(asset_count, prepared, error);
    }
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
    size_t last_direct_diffuse_coverage() const {
        return last_direct_diffuse_coverage_;
    }
    size_t last_direct_specular_coverage() const {
        return last_direct_specular_coverage_;
    }
    size_t last_invalid_lighting_or_guide_pixels() const {
        return last_invalid_lighting_or_guide_pixels_;
    }
    size_t last_smooth_specular_coverage() const {
        return last_smooth_specular_coverage_;
    }
    /* The settings actually in force, after ab3d2.ini and any environment
     * override have been applied over the tuned defaults. */
    void active_ray_tracing_options(RendererRayTracingOptions &options) const {
        options.samples_per_pixel = static_cast<uint8_t>(spp_);
        options.indirect_samples_per_pixel =
            static_cast<uint8_t>(indirect_spp_);
        options.indirect_light_samples =
            static_cast<uint8_t>(indirect_light_samples_);
        options.diffuse_gi_scale = diffuse_gi_scale_;
        options.diffuse_gi_scale_set = UINT8_MAX;
        options.specular_roughness_limit = specular_roughness_limit_;
        options.specular_roughness_limit_set = UINT8_MAX;
        options.maximum_bounces = static_cast<uint8_t>(maximum_depth_);
        options.light_candidates = static_cast<uint16_t>(candidate_count_);
        options.radiance_clamp = radiance_clamp_;
        options.exposure_bias_stops = exposure_bias_stops_;
        options.exposure_bias_set = UINT8_MAX;
        options.ndf_trim = ndf_trim_;
        options.output = output_.hdr ? RENDERER_OUTPUT_HDR : RENDERER_OUTPUT_SDR;
        options.hdr_peak_nits = output_.hdr ? output_.peak_nits : 0.0f;
        options.hdr_saturation_percent =
            output_.hdr ? output_.saturation_scale * 100.0f : 0.0f;
        options.hdr_saturation_percent_set = output_.hdr ? UINT8_MAX : 0u;
    }
    ID3D12Resource *reconstruction_resource(
        DxrReconstructionBuffer buffer) const;
    ID3D12Resource *streamline_scene_motion_resource() const
    {
        return streamline_scene_motion_.Get();
    }

private:
    static bool load_shader(const wchar_t *filename, std::vector<unsigned char> &bytes,
                            std::string &error);
    bool create_diagnostic_pipeline(ID3D12Device5 *device,
                                    const std::vector<unsigned char> &vertex_shader,
                                    const std::vector<unsigned char> &pixel_shader,
                                    DXGI_FORMAT render_target_format,
                                    std::string &error);
    bool create_present_pipeline(ID3D12Device5 *device,
                                 const std::vector<unsigned char> &vertex_shader,
                                 DXGI_FORMAT render_target_format,
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
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> burst_dispatch_signature_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
    Microsoft::WRL::ComPtr<ID3D12Resource> burst_dispatch_template_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blue_noise_sampler_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constants_;
    Microsoft::WRL::ComPtr<ID3D12Resource> light_grid_;
    Microsoft::WRL::ComPtr<ID3D12Resource> diagnostics_;
    Microsoft::WRL::ComPtr<ID3D12Resource> diagnostics_readback_;
    /* Current raw directional GI is written into one slot and replaced in-place
     * by the short accumulated result. The other slot retains the prior frame. */
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>
        indirect_radiance_histories_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirect_filtered_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>
        indirect_chroma_histories_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>
        indirect_history_metadata_;
    /* Streamline cannot use the renderer-private FP16 invalid-motion sentinel
     * when camera motion is already included. Keep a sanitized copy for RR. */
    Microsoft::WRL::ComPtr<ID3D12Resource> streamline_scene_motion_;
    /* Per-pixel weapon coverage and short RR rejection lifetime, ping-ponged
     * so a pose change can scrub the exact prior silhouette. */
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>
        view_weapon_histories_;
    /* Explicit Streamline/NGX temporal rejection for the current and prior
     * view-weapon silhouettes during an authored pose transition. */
    Microsoft::WRL::ComPtr<ID3D12Resource> rr_disocclusion_mask_;
    /* One explicitly means current color only for the visible weapon, avoiding
     * temporal retention inside a moving silhouette. */
    Microsoft::WRL::ComPtr<ID3D12Resource> rr_bias_current_color_mask_;
    /* Packed primary material F0 for rough-specular reconstruction. */
    Microsoft::WRL::ComPtr<ID3D12Resource> surface_parameters_;
    /* Exact primary-hit handoff used by the split-scheduling control. */
    Microsoft::WRL::ComPtr<ID3D12Resource> primary_visibility_;
    /* One compact pixel/count pair and one GPU dispatch argument per in-flight
     * frame. Their capacity is the exact internal pixel count. */
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               DxrScene::upload_frame_count> burst_work_items_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               DxrScene::upload_frame_count> burst_dispatch_arguments_;
    Microsoft::WRL::ComPtr<ID3D12Resource> automatic_exposure_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tone_map_histogram_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tone_map_state_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 6> bloom_targets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> post_hdr_output_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> diagnostic_cpu_heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               static_cast<size_t>(DxrReconstructionBuffer::count)>
        reconstruction_targets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> streamline_output_;
    UINT descriptor_size_ = 0;
    UINT render_width_ = 0;
    UINT render_height_ = 0;
    UINT present_width_ = 0;
    UINT present_height_ = 0;
    uint32_t candidate_count_ =
        RENDERER_RAY_TRACING_DEFAULT_LIGHT_CANDIDATES;
    float radiance_clamp_ = 0.0f;
    float exposure_bias_stops_ = -1.0f;
    float ndf_trim_ = 0.9f;
    uint32_t spp_ = 1u;
    uint32_t indirect_spp_ =
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_SAMPLES_PER_PIXEL;
    uint32_t indirect_light_samples_ =
        RENDERER_RAY_TRACING_DEFAULT_INDIRECT_LIGHT_SAMPLES;
    uint32_t indirect_temporal_window_ =
        indirect_reconstruction::temporal_window_default;
    float diffuse_gi_scale_ =
        RENDERER_RAY_TRACING_DEFAULT_DIFFUSE_GI_SCALE;
    /* Roughness at which specular stops being traced; see
     * renderer_ray_tracing_options.h. The long-standing behaviour is 0.3. */
    float specular_roughness_limit_ = 0.3f;
    /* Path length counting the primary hit; ab3d2.ini may change it. */
    uint32_t maximum_depth_ = 3u;
    uint32_t debug_view_ = 0;
    bool debug_view_requested_ = false;
    float debug_scalar_range_ = 8192.0f;
    uint32_t radiance_channel_ = 0u;
    bool split_primary_ = false;
    bool single_primary_direct_survivor_ = false;
    bool single_continuation_lobe_ = false;
    bool dense_mature_continuations_ = false;
    bool bounded_burst_continuations_ = false;
    bool interleaved_deep_diffuse_ = false;
    bool compact_local_primary_ = false;
    bool proxy_primary_candidates_ = false;
    bool force_specular_guide_ = false;
    size_t last_view_weapon_coverage_ = 0u;
    uint64_t last_view_weapon_rgb_checksum_ = 0u;
    size_t last_world_bitmap_coverage_ = 0u;
    size_t last_world_vector_coverage_ = 0u;
    size_t last_world_additive_coverage_ = 0u;
    size_t last_direct_diffuse_coverage_ = 0u;
    size_t last_direct_specular_coverage_ = 0u;
    size_t last_invalid_lighting_or_guide_pixels_ = 0u;
    size_t last_smooth_specular_coverage_ = 0u;
    size_t last_burst_work_overflow_ = 0u;
    size_t last_indirect_history_accepts_ = 0u;
    size_t last_indirect_history_rejects_ = 0u;
    std::array<size_t, 4> last_indirect_history_counts_ = {};
    float last_target_exposure_ = 1.0f;
    float last_automatic_exposure_ = 1.0f;
    float last_metered_average_luminance_ = 0.0f;
    float last_metered_low_luminance_ = 0.0f;
    float last_metered_high_luminance_ = 0.0f;
    uint32_t last_metered_weight_ = 0u;
    bool diagnostics_have_output_ = false;
    bool light_grid_needs_initial_transition_ = false;
    light_grid::Position light_grid_center_ = {};
    uint64_t light_grid_layout_hash_ = 0u;
    bool light_grid_cache_valid_ = false;
    struct DxrFrameHistory {
        reconstruction::CameraProjection previous_camera = {};
        reconstruction::PixelJitter previous_jitter = {};
        reconstruction::CameraProjection pending_camera = {};
        reconstruction::PixelJitter pending_jitter = {};
        uint64_t history_epoch = 0;
        uint64_t pending_history_epoch = 0;
        uint64_t emitter_state_hash = 0;
        uint64_t pending_emitter_state_hash = 0;
        uint32_t sample_index = 0;
        uint32_t pending_sample_index = 0;
        UINT input_width = 0;
        UINT input_height = 0;
        UINT pending_input_width = 0;
        UINT pending_input_height = 0;
        uint64_t weapon_pose_hash = 0u;
        uint64_t pending_weapon_pose_hash = 0u;
        bool weapon_pose_hash_valid = false;
        bool pending_weapon_pose_hash_valid = false;
        bool valid = false;
        bool pending = false;
    } history_;
    DxrScene scene_;
    DxrOutputConfiguration output_ = {};
};

}  // namespace ab3d2::dxr

#endif
