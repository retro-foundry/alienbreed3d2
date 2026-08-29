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
#include <set>
#include <utility>
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
     * Packed 16-bit XY pairs in PBR-atlas image texels. Scene compilation
     * isolates the exact source subwindow selected by hireswall.s:Draw_Wall
     * (word +10 U origin and bytes +18/+16 U/V repeat masks), so wall vertices
     * carry origin zero and the isolated extent. Floors and ceilings carry
     * their complete PBR tile extent for mip LOD. A zero extent explicitly
     * selects the complete level-zero material image for water, sprites,
     * vectors, and the view weapon.
     */
    uint32_t texture_window_origin;
    uint32_t texture_window_extent;
    /* Explicit authored-emission strength. World PBR surfaces, ordinary
     * entities, vectors, and the weapon use neutral one. Glare bitmaps retain
     * their measured additive strength without turning source Gouraud/ZoneT
     * raster lighting into emitted radiance. */
    float emissive_scale;
    /* Camera-local source position for the view weapon. World geometry leaves
     * this zero. Keeping the small authored offset avoids losing its motion to
     * cancellation after attachment to a large world-space camera position. */
    float view_weapon_position[3];
};

static_assert(sizeof(DxrSceneVertex) == 56u);

struct DxrSceneMaterial {
    /* Content origin inside the material level's one-texel wrapped gutter. */
    uint32_t atlas_x;
    uint32_t atlas_y;
    uint32_t width;
    uint32_t height;
    /* One for level-zero-only materials. Wall, floor, and ceiling materials
     * carry a software mip pyramid below their level-zero texture window. */
    uint32_t mip_count;
    float normal_strength;
    float specular_factor;
    float emissive[3];
};

static_assert(sizeof(DxrSceneMaterial) == 40u);

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
    static constexpr uint32_t upload_frame_count = 2u;

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
    bool view_weapon_pose_hash(uint64_t &pose_hash) const;
    uint32_t atlas_width() const { return atlas_width_; }
    uint32_t atlas_height() const { return atlas_height_; }
    uint32_t triangle_count() const {
        return static_cast<uint32_t>(vertices_.size() / 3u);
    }
    uint32_t emitter_count() const {
        return static_cast<uint32_t>(emissive_triangles_.size());
    }
    uint64_t light_grid_layout_hash() const {
        return light_grid_layout_hash_;
    }
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
        /* A reserved projectile slot: matched by position rather than by
         * identity, because the ObjT record behind it is a pool the source
         * reuses and its occupant is expected to change. */
        bool bitmap_pool = false;
        bool world_vector = false;
        bool opaque = true;
    };

    bool compile(const SceneFrame &frame,
                 const DxrViewWeaponCompilation &view_weapon,
                 const DxrWorldBitmapCompilation &world_bitmaps,
                 const DxrWorldVectorCompilation &world_vectors,
                 const DxrSceneGeometryHashes &hashes,
                 uint64_t world_layout, std::string &error);
    bool compile_geometry_update(const SceneFrame &frame,
                                 const DxrViewWeaponCompilation &view_weapon,
                                 const DxrWorldBitmapCompilation &world_bitmaps,
                                 const DxrWorldVectorCompilation &world_vectors,
                                 bool &static_changed, std::string &error);
    void release_gpu();

    DxrSceneGeometryHashes scene_hashes_ = {};
    bool has_hashes_ = false;
    bool gpu_build_pending_ = false;
    bool gpu_geometry_update_pending_ = false;
    bool history_reset_pending_ = true;
    uint64_t geometry_update_count_ = 0;
    uint64_t rebuild_count_ = 0;
    /* Reserved projectile slots the compiled scene currently holds. It only
     * ever grows, to a high-water mark, because shrinking it is a layout
     * change and therefore a rebuild. */
    size_t world_bitmap_pool_capacity_ = 0u;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::vector<DxrSceneVertex> vertices_;
    std::vector<DxrSceneMaterial> materials_;
    std::vector<DxrEmissiveTriangle> emissive_triangles_;
    uint64_t light_grid_layout_hash_ = 0u;
    std::vector<uint32_t> surface_material_indices_;
    std::vector<float> material_emissive_bound_;
    uint32_t view_weapon_first_material_ = 0u;
    uint32_t view_weapon_material_count_ = 0u;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t>
        bitmap_material_indices_;
    /*
     * Every source-asset and draw-mode pair the level has shown, so a rebuild
     * repacks the atlas with all of them and not only the ones on screen at
     * that instant. Without it a recurring effect - a muzzle flash, an impact
     * pop - dropped out of the atlas whenever it was not visible and cost a
     * rebuild on its next appearance, over and over. Cleared when the world
     * geometry changes, which is what a level load looks like from here.
     */
    std::set<std::pair<uint32_t, uint32_t>> bitmap_modes_seen_;
    uint64_t bitmap_modes_world_layout_ = 0;
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
