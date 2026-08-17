#ifndef AB3D2_DXR_PIPELINE_H
#define AB3D2_DXR_PIPELINE_H

#include <d3d12.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "render_view.h"
#include "scene_frame.h"
#include "dxr_scene.h"

namespace ab3d2::dxr {

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

    ID3D12RootSignature *root_signature() const { return root_signature_.Get(); }
    ID3D12PipelineState *pipeline_state() const { return pipeline_state_.Get(); }
    bool has_scene() const { return scene_.ready(); }

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
    bool ensure_output(ID3D12Device5 *device, UINT width, UINT height,
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
    Microsoft::WRL::ComPtr<ID3D12Resource> noisy_radiance_;
    UINT descriptor_size_ = 0;
    UINT output_width_ = 0;
    UINT output_height_ = 0;
    DxrScene scene_;
};

}  // namespace ab3d2::dxr

#endif
