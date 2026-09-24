#ifndef AB3D2_DXR_SCENE_H
#define AB3D2_DXR_SCENE_H

#include "dxr_materials.h"
#include "dxr_reconstruction_math.h"
#include "dxr_scene_update.h"
#include "renderer_ray_tracing_options.h"
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

/* One reserved run of world-vector slots, per asset. Sized to that asset's own
 * face count, because vector models differ in size where billboards do not. */
struct DxrVectorPool {
    size_t capacity = 0u;
    size_t vertex_count = 0u;
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
    /*
     * Emitted radiance, the same on all three vertices of a triangle: the mean
     * of the material's emissive texture over that triangle's UVs, factor
     * included, emissive_scale not. The renderer never samples an emissive
     * texture -- every use of emission, whether seen directly, reflected, or
     * sampled as a light, reads this colour instead. The texture is read only
     * here, on the CPU, to measure it; see compile_emissive_triangles.
     */
    float emission[3];
    /*
     * The level's own lighting at this vertex, as a fraction of a fully lit
     * surface, from the source brightness tables renderer_opengl.c shades
     * with. Read only from the second bounce onward; see
     * dxr_source_lighting.h.
     */
    float source_irradiance;
    /*
     * The draw zone holding this surface, as ZoneT numbers them. Light
     * selection drops an emitter whose zone is absent from this zone's PVS
     * row, which is what stops a corridor spending its candidates on the
     * lights in the room next door. SceneGeometry and SceneSprite both carry
     * the source index already; see DxrScene::zone_visibility_.
     */
    uint32_t source_zone_index;
};

static_assert(sizeof(DxrSceneVertex) == 76u);

/*
 * A zone's candidate list is plain emitter indices, in no particular order.
 *
 * Q2RTX's light_lists.h carries no per-light probability either: the shader
 * takes a strided partition of up to MAX_BRUTEFORCE_SAMPLING of them and
 * builds its CDF from each one's solid angle at the shading point, which is a
 * far better importance measure than anything that can be baked per zone.
 */

/*
 * One opening between zones in render-world space, mirrored by `ZonePortal`
 * in shaders/path_trace.hlsl. The rectangle is corner + s * edge + t * rise
 * for s and t in [0, 1]; edge runs along the source EdgeT and rise spans the
 * zone's floor to roof, so the two are perpendicular and area is the product
 * of their lengths.
 *
 * `lit` is one when the zone on the far side can see at least one emitter and
 * zero otherwise. An opening onto a room with nothing to see has nothing to
 * gather, and proposing it would only spend the samples.
 */
struct DxrZonePortal {
    float corner[3];
    float area;
    float edge[3];
    uint32_t join_zone;
    float rise[3];
    float lit;
};

static_assert(sizeof(DxrZonePortal) == 48u);

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
    /*
     * What light sampling emits from this triangle: the mean of its emissive
     * texture over the triangle's own UVs, factor included. The per-vertex
     * emissive scale is applied on top in the shader, and the surface itself
     * still draws its texture; only the light it casts is averaged.
     *
     * It also weights selection. Selection used to be weighted by the
     * material's brightest texel while sampling read the texel under the
     * sampled point, so a sparse map was over-selected by the ratio of its
     * peak to its mean. wall_06_technolights peaks at 150-160 against means
     * of 1.5-5, so it took about 83% of all light samples in a room lit
     * almost entirely by floor_0101; each rare floor sample then carried a
     * huge weight, and those were the fireflies on the floor. Reading the
     * texel is also a lottery for a sparse map -- mostly black, occasionally
     * a full-strength strip -- which the mean removes. Measured per triangle
     * rather than per material because triangles of one material show
     * different parts of it: technolights' triangles range from 0.3 to 7.7.
     */
    float radiance[3];
};

/* A triangle's measured emission, kept until its UVs or material change. */
struct DxrEmitterRadianceCache {
    uint64_t key = 0u;
    float radiance[3] = {};
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
    /*
     * One (first, count) pair per zone, naming that zone's slice of
     * zone_light_address below. Element zero is not a zone: it carries the
     * zone count, so the shaders can bounds-check without another constant,
     * and zone z is at element 1 + z. A count of zero, or a zone count of
     * zero, means that zone has no table and selection falls back to the
     * scene-wide one.
     */
    D3D12_GPU_VIRTUAL_ADDRESS zone_light_range_address() const;
    /* The zones' candidate tables, concatenated; see DxrZoneLight. */
    D3D12_GPU_VIRTUAL_ADDRESS zone_light_address() const;
    /* One (first, count) pair per zone over zone_portal_address, laid out
     * exactly as zone_light_range_address is. */
    D3D12_GPU_VIRTUAL_ADDRESS zone_portal_range_address() const;
    /* Every zone's openings, concatenated; see DxrZonePortal. */
    D3D12_GPU_VIRTUAL_ADDRESS zone_portal_address() const;
    bool view_weapon_pose_hash(uint64_t &pose_hash) const;
    uint32_t atlas_width() const { return atlas_width_; }
    uint32_t atlas_height() const { return atlas_height_; }
    uint32_t triangle_count() const {
        return static_cast<uint32_t>(vertices_.size() / 3u);
    }
    /*
     * Multiplies every authored emissive factor; see
     * RENDERER_RAY_TRACING_DEFAULT_LIGHT_SCALE. Applied where a material image
     * takes its factor, so both the radiance a light shows and the radiance
     * next-event estimation measures move together.
     */
    void set_light_scale(float scale);
    float light_scale() const { return light_scale_; }
    uint32_t emitter_count() const {
        return static_cast<uint32_t>(emissive_triangles_.size());
    }
    uint64_t light_grid_layout_hash() const {
        return light_grid_layout_hash_;
    }
    uint64_t emitter_state_hash() const { return emitter_state_hash_; }
    uint64_t rebuild_count() const { return rebuild_count_; }
    /*
     * Decode every packaged texture region for the level's vector assets
     * before gameplay. DxrMaterialLibrary::resolve_index is lazy: the first use
     * of a material seeks into the package and PNG-decodes five channels inside
     * whatever frame happened to need it, which is the hitch on the first shot
     * and the first kill. Doing it here moves that cost to the load screen.
     */
    bool prepare_vector_materials(const uint32_t *asset_ids, size_t asset_count,
                                  size_t &prepared, std::string &error);
    /* The same, for ObjT bitmap art: muzzle flashes, impacts and projectiles.
     * Asset ids run from 0 to asset_count - 1. */
    bool prepare_bitmap_materials(size_t asset_count, size_t &prepared,
                                  std::string &error);
    bool history_reset_pending() const { return history_reset_pending_; }
    void mark_history_promoted() { history_reset_pending_ = false; }

private:
    struct CompiledInstance {
        uint32_t source_instance_id = 0;
        uint32_t source_mesh_id = 0;
        /* The vector pool run this slot belongs to; slots are matched by
         * position within their asset's run rather than by occupant. */
        uint32_t pool_asset_id = 0;
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
                 const DxrSceneGeometryHashes &hashes,
                 uint64_t world_layout, std::string &error);
    bool compile_geometry_update(const SceneFrame &frame,
                                 const DxrViewWeaponCompilation &view_weapon,
                                 const DxrWorldBitmapCompilation &world_bitmaps,
                                 const DxrWorldVectorCompilation &world_vectors,
                                 bool &static_changed, std::string &error);
    void release_gpu();

    /* Per-component layout hashes from the previous frame, so a rebuild can
     * name what moved. Diagnostic only; see dxr_debug.h. */
    uint64_t previous_world_layout_ = 0u;
    uint64_t previous_view_weapon_layout_ = 0u;
    uint64_t previous_bitmap_layout_ = 0u;
    uint64_t previous_vector_layout_ = 0u;
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
    /*
     * The view weapon's reserved vertex run, high-water across every weapon
     * model the level has shown. graphics_type selects the model, so firing can
     * swap it outright; reserving the run keeps that a vertex rewrite instead
     * of a rebuild, which measured over 300 ms.
     */
    size_t view_weapon_vertex_capacity_ = 0u;
    uint32_t atlas_width_ = 0;
    uint32_t atlas_height_ = 0;
    std::vector<DxrSceneVertex> vertices_;
    std::vector<DxrSceneMaterial> materials_;
    std::vector<DxrEmissiveTriangle> emissive_triangles_;
    uint64_t light_grid_layout_hash_ = 0u;
    uint64_t emitter_state_hash_ = 0u;
    std::vector<uint32_t> surface_material_indices_;
    std::vector<float> material_emissive_bound_;

public:
    /* The brightest emissive material in the compiled scene. A diffuse surface
     * cannot leave more radiance than arrives at it, so this bounds any single
     * next-event estimate and is a physical limit rather than a tuned one. */
    float maximum_emitter_radiance() const
    {
        float maximum = 0.0f;
        for (const float bound : material_emissive_bound_) {
            maximum = maximum > bound ? maximum : bound;
        }
        return maximum;
    }

private:
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
    std::set<std::pair<uint32_t, uint32_t>> discovered_bitmap_modes_;
    /*
     * Every vector asset the level has animated - the view weapon and world
     * vector objects. A rebuild packs all of each asset's authored texture
     * regions, so stepping an animation frame renames a material instead of
     * costing a rebuild. Cleared with bitmap_modes_seen_ on a world change.
     */
    std::set<uint32_t> discovered_vector_assets_;
    /*
     * What prepare_resources decoded and packed before gameplay. These describe
     * the art the level owns, so they outlive the per-world reset; the
     * discovered_ sets above answer the different question of what has actually
     * appeared so far, and are cleared with the world. compile() packs both;
     * nothing copies between them.
     */
    std::set<std::pair<uint32_t, uint32_t>> catalog_bitmap_modes_;
    std::set<uint32_t> catalog_vector_assets_;
    /*
     * Reserved world-vector slots per asset, matched by position like the
     * bitmap projectile pool. Without it a projectile spawning or an alien
     * dying changed the instance list and rebuilt the whole scene. Each run
     * only grows, to a high-water mark.
     */
    std::map<uint32_t, DxrVectorPool> world_vector_pools_;
    /*
     * Pooled triangles that have been emissive at least once since the last
     * rebuild, against the sample-space area they last had. They keep an
     * emitter entry at zero power while idle, at that remembered area, so a
     * shot or an impact coming and going changes neither the shape of the
     * emitter table nor any slot's identity. Keys are vertex offsets, so this
     * belongs to one compiled layout.
     */
    std::vector<float> reserved_emitter_slots_;
    /* Indexed by triangle like reserved_emitter_slots_ and cleared with it:
     * the emitter compile walks every triangle every frame, and re-measuring
     * the static world's lights each time would be wasted. */
    std::vector<DxrEmitterRadianceCache> emitter_radiance_cache_;
    uint64_t bitmap_modes_world_layout_ = 0;
    std::map<DxrVectorMaterialKey, uint32_t> vector_material_indices_;
    std::vector<CompiledInstance> instances_;
    std::vector<bool> blas_update_pending_;
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> atlas_pixels_;
    DxrMaterialLibrary material_library_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> previous_vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> material_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> emitter_buffer_;
    void capture_zone_visibility(const SceneFrame &frame);
    void build_zone_light_lists();
    void build_zone_portals();
    bool ensure_zone_lights(ID3D12Device5 *device, std::string &error);
    /*
     * The level's zone visibility: word zero the zone count, word one the
     * 32-bit words per row, then one row per zone, a set bit naming a zone
     * that zone can see. This stays on the CPU -- the shaders read the
     * candidate tables built from it, not the rows themselves.
     */
    std::vector<uint32_t> zone_visibility_;
    float light_scale_ = RENDERER_RAY_TRACING_DEFAULT_LIGHT_SCALE;
    std::vector<uint32_t> zone_light_ranges_;
    std::vector<uint32_t> zone_lights_;
    /* The level's openings as the frame published them, and the GPU tables
     * built from them and from the zone light tables. */
    std::vector<ScenePortal> zone_portal_source_;
    std::vector<uint32_t> zone_portal_ranges_;
    std::vector<DxrZonePortal> zone_portals_;
    /* Emitter power moves as pooled slots go live and idle, and the tables are
     * weighted by it, so they are rebuilt when that state changes rather than
     * once at load. */
    uint64_t zone_light_emitter_hash_ = 0u;
    bool zone_lights_dirty_ = true;
    /*
     * Deliberately outside release_gpu. That runs partway through
     * record_build, after ensure_zone_lights has already written these and
     * the frame is about to bind them, so releasing them there hands the
     * dispatch a freed address and the device is removed.
     */
    Microsoft::WRL::ComPtr<ID3D12Resource> zone_light_range_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> zone_light_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> zone_portal_range_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> zone_portal_buffer_;
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
