#ifndef AB3D2_DXR_PIPELINE_H
#define AB3D2_DXR_PIPELINE_H

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render_view.h"
#include "scene_frame.h"
#include "dxr_reconstruction_math.h"
#include "dxr_scene.h"

namespace ab3d2::dxr {

enum class DxrReconstructionBuffer : size_t {
    noisy_radiance,
    diffuse_albedo,
    specular_albedo,
    shading_normal,
    linear_roughness,
    linear_depth,
    scene_motion,
    specular_hit_distance,
    count,
};

class DxrPipeline final {
public:
    bool initialize(ID3D12Device5 *device, std::string &error);
    bool update_scene(const SceneFrame &frame, bool &requires_flush,
                      std::string &error);
    bool record(ID3D12Device5 *device, ID3D12GraphicsCommandList4 *command_list,
                UINT width, UINT height, const SceneFrame &frame,
                const RenderView &view, uint32_t frame_number,
                uint32_t frame_slot,
                std::string &error);
    void commit_presented_frame();

    ID3D12RootSignature *root_signature() const { return root_signature_.Get(); }
    ID3D12PipelineState *pipeline_state() const { return pipeline_state_.Get(); }
    bool has_scene() const { return scene_.ready(); }
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
    bool create_raytracing_pipeline(ID3D12Device5 *device, std::string &error);
    bool create_descriptor_heap(ID3D12Device5 *device, std::string &error);
    bool configure_debug_view(std::string &error);
    bool ensure_reconstruction_targets(ID3D12Device5 *device, UINT width,
                                       UINT height, bool &recreated,
                                       std::string &error);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_descriptor(UINT index) const;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> present_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> present_pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> ray_root_signature_;
    Microsoft::WRL::ComPtr<ID3D12StateObject> ray_state_object_;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               static_cast<size_t>(DxrReconstructionBuffer::count)>
        reconstruction_targets_;
    UINT descriptor_size_ = 0;
    UINT output_width_ = 0;
    UINT output_height_ = 0;
    uint32_t debug_view_ = 0;
    float debug_scalar_range_ = 8192.0f;
    struct DxrFrameHistory {
        reconstruction::CameraProjection previous_camera = {};
        reconstruction::PixelJitter previous_jitter = {};
        reconstruction::CameraProjection pending_camera = {};
        reconstruction::PixelJitter pending_jitter = {};
        uint64_t history_epoch = 0;
        uint64_t pending_history_epoch = 0;
        uint64_t presented_frame = 0;
        uint64_t pending_presented_frame = 0;
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
