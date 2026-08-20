#ifndef AB3D2_DXR_SCENE_H
#define AB3D2_DXR_SCENE_H

#include "dxr_materials.h"
#include "dxr_reconstruction_math.h"
#include "dxr_scene_update.h"
#include "scene_frame.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace ab3d2::dxr {

enum class DxrScenePrimitive : uint32_t {
    world = 0u,
    view_weapon = 1u,
    world_billboard = 2u,
    world_effect = 3u,
    world_vector = 4u,
};

struct DxrViewWeaponCompilation;
struct DxrWorldBitmapCompilation;
struct DxrWorldVectorCompilation;

/* Layout mirrored by `SceneVertex` in shaders/path_trace.hlsl. */
struct DxrSceneVertex {
    float position[3];
    float texture_coordinate[2];
    uint32_t material_index;
    uint32_t emitter_index;
    uint32_t primitive;
    /*
     * The source Gouraud shade response for this vertex, scaling the material's
     * authored emission. `hires.s:goursides` selects a flat's shade row from its
     * CurrentPointBrights word, so a zone whose points carry an
     * Anim_BrightTable index pulses its authored emissive panels through
     * newanims.s:brightanim. One is the brightest source row.
     *
     * On `DxrScenePrimitive::world` the shader also reads it as the level's
     * authored ambience, which secondary rays gather and primary rays ignore.
     * Every other primitive writes one so its own emissive materials survive,
     * and `authoredAmbientRadiance` in shaders/path_trace.hlsl skips them for
     * exactly that reason.
     */
    float emissive_scale;
};

static_assert(sizeof(DxrSceneVertex) == 36u);

struct DxrSceneMaterial {
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    float normal_strength;
    float specular_factor;
    float emissive[3];
};

static_assert(sizeof(DxrSceneMaterial) == 36u);

/*
 * Layout mirrored by `EmissiveTriangle` in shaders/path_trace.hlsl. The alias
 * pair replaces the former cumulative-distribution value so selection costs one
 * lookup instead of a linear walk; see dxr_alias_table.h.
 */
struct DxrEmissiveTriangle {
    uint32_t first_vertex;
    float selection_probability;
    float inverse_area;
    float alias_threshold;
    uint32_t alias_index;
};

class DxrScene final {
public:
    static constexpr uint32_t upload_frame_count = 3u;

    bool update(const SceneFrame &frame,
                const reconstruction::CameraProjection *camera,
                bool &requires_flush, std::string &error);
    bool record_build(ID3D12Device5 *device,
                      ID3D12GraphicsCommandList4 *command_list,
                      uint32_t frame_slot,
                      D3D12_CPU_DESCRIPTOR_HANDLE tlas_descriptor,
                      const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
                                       static_cast<size_t>(DxrMaterialChannel::count)>
                          &atlas_descriptors,
                      std::string &error);
    bool record_promote_vertex_history(
        ID3D12GraphicsCommandList4 *command_list, std::string &error);

    bool ready() const { return tlas_ && !vertices_.empty(); }
    D3D12_GPU_VIRTUAL_ADDRESS vertex_address() const;
    D3D12_GPU_VIRTUAL_ADDRESS previous_vertex_address() const;
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
    /*
     * Fold of every vertex's authored emission scale. The hidden GPU smoke uses
     * it to assert that newanims.s:brightanim reaches the vertex buffer, which
     * an image comparison cannot show while the sampler is still boiling.
     */
    uint64_t emissive_scale_fold() const;
    uint64_t rebuild_count() const { return rebuild_count_; }
    bool history_reset_pending() const { return history_reset_pending_; }
    void mark_history_promoted() { history_reset_pending_ = false; }

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
        bool view_weapon = false;
        bool world_bitmap = false;
        bool world_vector = false;
        bool opaque = true;
    };

    bool compile(const SceneFrame &frame,
                 const DxrViewWeaponCompilation &view_weapon,
                 const DxrWorldBitmapCompilation &world_bitmaps,
                 const DxrWorldVectorCompilation &world_vectors,
                 const DxrSceneGeometryHashes &hashes, std::string &error);
    bool compile_geometry_update(const SceneFrame &frame,
                                 const DxrViewWeaponCompilation &view_weapon,
                                 const DxrWorldBitmapCompilation &world_bitmaps,
                                 const DxrWorldVectorCompilation &world_vectors,
                                 bool light_changed,
                                 bool &static_changed, std::string &error);
    void release_gpu();

    DxrSceneGeometryHashes scene_hashes_ = {};
    bool has_hashes_ = false;
    bool gpu_build_pending_ = false;
    bool gpu_geometry_update_pending_ = false;
    bool history_reset_pending_ = true;
    uint64_t geometry_update_count_ = 0;
    uint64_t rebuild_count_ = 0;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::vector<DxrSceneVertex> vertices_;
    std::vector<DxrSceneMaterial> materials_;
    std::vector<DxrEmissiveTriangle> emissive_triangles_;
    std::vector<uint32_t> surface_material_indices_;
    std::vector<float> material_emissive_luminance_;
    uint32_t view_weapon_first_material_ = 0u;
    uint32_t view_weapon_material_count_ = 0u;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t>
        bitmap_material_indices_;
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, uint8_t,
                        uint8_t, uint8_t, uint8_t>, uint32_t>
        vector_material_indices_;
    std::vector<CompiledInstance> instances_;
    std::vector<bool> blas_update_pending_;
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> atlas_pixels_;
    DxrMaterialLibrary material_library_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> previous_vertex_buffer_;
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
