#ifndef AB3D2_DXR_SCENE_H
#define AB3D2_DXR_SCENE_H

#include "dxr_materials.h"
#include "dxr_scene_update.h"
#include "scene_frame.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ab3d2::dxr {

struct DxrSceneVertex {
    float position[3];
    float texture_coordinate[2];
    uint32_t material_index;
    uint32_t emitter_index;
};

struct DxrSceneMaterial {
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    float normal_strength;
    float emissive[3];
};

struct DxrEmissiveTriangle {
    uint32_t first_vertex;
    float selection_cdf;
    float selection_probability;
    float inverse_area;
};

class DxrScene final {
public:
    static constexpr uint32_t upload_frame_count = 3u;

    bool update(const SceneFrame &frame, bool &requires_flush,
                std::string &error);
    bool record_build(ID3D12Device5 *device,
                      ID3D12GraphicsCommandList4 *command_list,
                      uint32_t frame_slot,
                      D3D12_CPU_DESCRIPTOR_HANDLE tlas_descriptor,
                      const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
                                       static_cast<size_t>(DxrMaterialChannel::count)>
                          &atlas_descriptors,
                      std::string &error);

    bool ready() const { return tlas_ && !vertices_.empty(); }
    D3D12_GPU_VIRTUAL_ADDRESS vertex_address() const;
    D3D12_GPU_VIRTUAL_ADDRESS material_address() const;
    D3D12_GPU_VIRTUAL_ADDRESS emitter_address() const;
    uint32_t atlas_width() const { return atlas_width_; }
    uint32_t atlas_height() const { return atlas_height_; }
    uint32_t triangle_count() const {
        return static_cast<uint32_t>(vertices_.size() / 3u);
    }
    uint32_t emitter_count() const {
        return static_cast<uint32_t>(emissive_triangles_.size());
    }

private:
    struct CompiledInstance {
        uint32_t source_instance_id = 0;
        uint32_t source_mesh_id = 0;
        uint32_t first_surface = 0;
        uint32_t surface_count = 0;
        uint32_t first_vertex = 0;
        uint32_t vertex_count = 0;
        SceneAccelerationClass acceleration_class =
            SCENE_ACCELERATION_CLASS_STATIC;
        uint64_t vertex_hash = 0;
    };

    bool compile(const SceneFrame &frame,
                 const DxrSceneGeometryHashes &hashes, std::string &error);
    bool compile_geometry_update(const SceneFrame &frame, bool &static_changed,
                                 std::string &error);
    void release_gpu();

    DxrSceneGeometryHashes scene_hashes_ = {};
    bool has_hashes_ = false;
    bool gpu_build_pending_ = false;
    bool gpu_geometry_update_pending_ = false;
    uint64_t geometry_update_count_ = 0;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::vector<DxrSceneVertex> vertices_;
    std::vector<DxrSceneMaterial> materials_;
    std::vector<DxrEmissiveTriangle> emissive_triangles_;
    std::vector<uint32_t> surface_material_indices_;
    std::vector<float> material_emissive_luminance_;
    std::vector<CompiledInstance> instances_;
    std::vector<bool> blas_update_pending_;
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> atlas_pixels_;
    DxrMaterialLibrary material_library_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emitter_buffer_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               static_cast<size_t>(DxrMaterialChannel::count)> atlas_textures_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, upload_frame_count>
        geometry_uploads_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,
               static_cast<size_t>(DxrMaterialChannel::count)> atlas_uploads_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas_scratch_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> blases_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_scratch_;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_upload_;
};

}  // namespace ab3d2::dxr

#endif
