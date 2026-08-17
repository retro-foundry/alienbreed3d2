#ifndef AB3D2_DXR_SCENE_H
#define AB3D2_DXR_SCENE_H

#include "scene_frame.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ab3d2::dxr {

struct DxrSceneVertex {
    float position[3];
    float texture_coordinate[2];
    uint32_t material_index;
};

struct DxrSceneMaterial {
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    float roughness;
    float metalness;
    float emissive[2];
};

class DxrScene final {
public:
    bool update(const SceneFrame &frame, bool &changed, std::string &error);
    bool record_build(ID3D12Device5 *device,
                      ID3D12GraphicsCommandList4 *command_list,
                      D3D12_CPU_DESCRIPTOR_HANDLE tlas_descriptor,
                      D3D12_CPU_DESCRIPTOR_HANDLE atlas_descriptor,
                      std::string &error);

    bool ready() const { return tlas_ && !vertices_.empty(); }
    D3D12_GPU_VIRTUAL_ADDRESS vertex_address() const;
    D3D12_GPU_VIRTUAL_ADDRESS material_address() const;
    uint32_t atlas_width() const { return atlas_width_; }
    uint32_t atlas_height() const { return atlas_height_; }
    uint32_t triangle_count() const {
        return static_cast<uint32_t>(vertices_.size() / 3u);
    }

private:
    bool compile(const SceneFrame &frame, uint64_t hash, std::string &error);
    void release_gpu();

    uint64_t scene_hash_ = 0;
    bool has_hash_ = false;
    bool gpu_build_pending_ = false;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::vector<DxrSceneVertex> vertices_;
    std::vector<DxrSceneMaterial> materials_;
    std::vector<uint8_t> atlas_pixels_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> atlas_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> atlas_upload_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_upload_;
};

}  // namespace ab3d2::dxr

#endif
