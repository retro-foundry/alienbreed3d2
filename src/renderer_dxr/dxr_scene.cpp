#include "dxr_scene.h"
#include "dxr_emitter_history.h"
#include "dxr_material_mip.h"

#include "dxr_alias_table.h"
#include "dxr_debug.h"
#include "object_runtime.h"
#include "scene_geometry_compile.h"
#include "source_bitmap_sprite.h"
#include "source_vector_model_scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>

namespace ab3d2::dxr {

struct DxrViewWeaponCompilation {
    SourceVectorSceneMesh source = {};
    std::vector<DxrSceneVertex> vertices;
    const SceneSprite *sprite = nullptr;
    uint64_t layout_hash = UINT64_C(1469598103934665603);
    uint64_t vertex_hash = UINT64_C(1469598103934665603);

    ~DxrViewWeaponCompilation()
    {
        source_vector_scene_mesh_destroy(&source);
    }

    DxrViewWeaponCompilation() = default;
    DxrViewWeaponCompilation(const DxrViewWeaponCompilation &) = delete;
    DxrViewWeaponCompilation &operator=(const DxrViewWeaponCompilation &) = delete;
};

struct DxrWorldBitmapInstance {
    SourceBitmapSceneMesh source = {};
    std::vector<DxrSceneVertex> vertices;
    const SceneSpriteInstance *instance = nullptr;
    uint64_t vertex_hash = UINT64_C(1469598103934665603);
    /*
     * False for a reserved projectile slot that no live sprite occupies. The
     * slot still carries its six vertices, collapsed onto a point, so the
     * scene's instance list keeps its shape while projectiles come and go.
     */
    bool occupied = true;
};

struct DxrWorldBitmapCompilation {
    /*
     * The lasting billboards first, in frame order, then a fixed run of
     * projectile slots. A lasting billboard is matched by identity across
     * frames; a projectile slot is matched by position, because the ObjT slot
     * behind it is a pool the source reuses and its occupant is expected to
     * change.
     */
    std::vector<DxrWorldBitmapInstance> instances;
    size_t pool_first = 0u;
    size_t pool_capacity = 0u;
    /* Slots the frame needed beyond `pool_capacity`. Non-zero asks the caller
     * to grow the pool, which is a layout change and so a rebuild. */
    size_t pool_overflow = 0u;
    uint64_t layout_hash = UINT64_C(1469598103934665603);
    uint64_t vertex_hash = UINT64_C(1469598103934665603);
};

struct DxrWorldVectorInstance {
    SourceVectorSceneMesh source = {};
    std::vector<DxrSceneVertex> vertices;
    const SceneSpriteInstance *instance = nullptr;
    uint64_t vertex_hash = UINT64_C(1469598103934665603);
};

struct DxrWorldVectorCompilation {
    std::vector<DxrWorldVectorInstance> instances;
    uint64_t layout_hash = UINT64_C(1469598103934665603);
    uint64_t vertex_hash = UINT64_C(1469598103934665603);

    ~DxrWorldVectorCompilation()
    {
        for (DxrWorldVectorInstance &instance : instances) {
            source_vector_scene_mesh_destroy(&instance.source);
        }
    }

    DxrWorldVectorCompilation() = default;
    DxrWorldVectorCompilation(const DxrWorldVectorCompilation &) = delete;
    DxrWorldVectorCompilation &operator=(
        const DxrWorldVectorCompilation &) = delete;
};

namespace {

constexpr uint32_t atlas_maximum_extent = 8192u;
/* tools/world_material_images.py expands each authoritative world texel to a
 * 4x4 PBR block. This is part of the packaged world-material contract. */
constexpr uint32_t world_texture_scale = 4u;
/* hires.s:Draw_Flats wraps both floor texture coordinates at 64 texels. */
constexpr uint32_t floor_texture_extent = 64u;
constexpr uint8_t world_instance_mask = 0x01u;
constexpr uint64_t fnv_prime = UINT64_C(1099511628211);
/*
 * `objdrawhires.s:draw_bitmap_glare` adds a blend-table result rather than the
 * texel, so a glare reads dimmer than draw_bitmap_additive's full-strength add.
 * The packaged emissive channel carries the decoded texel for both, and this is
 * the factor renderer_opengl.c holds a glare's contribution at, so both
 * backends show the effect at the same strength.
 */
constexpr float source_glare_additive_strength = 0.8f;
/*
 * Every world billboard lives in a fixed run of reserved instances rather than
 * appearing in and disappearing from the scene's instance list.
 *
 * A sprite entering or leaving that list is a layout change, and a layout
 * change is a full rebuild: every BLAS, the TLAS, the emitter table and the
 * whole PBR atlas, behind a GPU flush, with the reconstruction history reset on
 * top. On Level A at 2560x1440 that measures around 95 ms, and it used to be
 * spent whenever a bullet appeared or retired, an alien died, or an item was
 * collected: `object_scene_submit_active` publishes the live prefix of the ObjT
 * array and skips vacated records, so any of those shifts the whole list.
 *
 * The slots are a pool, matched by position and not by identity, because the
 * ObjT records behind them are a pool the source reuses. A vacated record
 * shifting its successors up therefore costs each one a vertex write and a
 * two-triangle BLAS refit - what an animating billboard already costs - instead
 * of a rebuild. The baseline covers a level's ordinary live-object count and
 * the pool doubles to a high-water mark past it; growing is itself a layout
 * change, so it costs one rebuild and then stops happening.
 */
constexpr size_t world_bitmap_pool_baseline =
    4u * OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
constexpr size_t world_bitmap_pool_limit = 1024u;
/*
 * The vertex hash a reserved slot reports while nothing occupies it. It must not
 * depend on the camera or the frame: an empty slot's vertices stay exactly where
 * its last occupant left them, collapsed onto a point, so a constant here is
 * what keeps the geometry update from refitting every empty slot every frame.
 */
constexpr uint64_t empty_world_bitmap_slot_hash = UINT64_C(0x9e3779b97f4a7c15);

uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= fnv_prime;
    }
    return hash;
}

uint64_t compute_light_grid_layout_hash(
    const std::vector<DxrEmissiveTriangle> &emitters)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    const size_t emitter_count = emitters.size();
    hash = hash_bytes(hash, &emitter_count, sizeof(emitter_count));
    for (const DxrEmissiveTriangle &emitter : emitters) {
        /* A cached entry already stores the proposal probability that created
         * it. Power and position changes therefore remain valid; only a slot
         * naming a different triangle/sample-space area invalidates identity. */
        hash = hash_bytes(hash, &emitter.first_vertex,
                          sizeof(emitter.first_vertex));
        hash = hash_bytes(hash, &emitter.inverse_area,
                          sizeof(emitter.inverse_area));
    }
    return hash;
}

uint64_t compute_emitter_state_hash(
    const std::vector<DxrEmissiveTriangle> &emitters,
    const std::vector<DxrSceneVertex> &vertices)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    const size_t emitter_count = emitters.size();
    hash = hash_bytes(hash, &emitter_count, sizeof(emitter_count));
    for (const DxrEmissiveTriangle &emitter : emitters) {
        hash = hash_bytes(hash, &emitter, sizeof(emitter));
        if (emitter.first_vertex > vertices.size() ||
            3u > vertices.size() - emitter.first_vertex) {
            continue;
        }
        for (size_t vertex_index = 0u; vertex_index < 3u; ++vertex_index) {
            const DxrSceneVertex &vertex =
                vertices[emitter.first_vertex + vertex_index];
            hash = hash_bytes(hash, vertex.position, sizeof(vertex.position));
            hash = hash_bytes(
                hash, &vertex.emissive_scale, sizeof(vertex.emissive_scale));
        }
    }
    return hash;
}

struct MaterialKey {
    SceneMaterialSource source;
    uint32_t source_asset_id;
    uint32_t texture_v_period;
    uint32_t texture_u_offset;
    uint32_t texture_u_period;

    auto tie() const {
        return std::tie(source, source_asset_id, texture_v_period,
                        texture_u_offset, texture_u_period);
    }
    bool operator<(const MaterialKey &other) const { return tie() < other.tie(); }
};

struct MaterialImage {
    MaterialKey key = {};
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t mip_count = 1u;
    bool material_mips = false;
    std::array<material_mip::Levels,
               static_cast<size_t>(DxrMaterialChannel::count)> mip_pixels;
    float normal_strength = 1.0f;
    float specular_factor = 1.0f;
    float emissive_factor[3] = {};
    float maximum_emissive_luminance = 0.0f;
};

/* Every software mip is surrounded by one wrapped texel. The ray shader can
 * then use one hardware-bilinear sample without filtering into another atlas
 * tile, while the existing explicit Load path still addresses the content
 * origin recorded in DxrSceneMaterial. */
uint32_t material_atlas_packed_height(const MaterialImage &image)
{
    return image.material_mips ?
        material_mip::wrapped_packed_height(image.width, image.height) :
        image.height + 2u;
}

void copy_wrapped_material_level(std::vector<uint8_t> &atlas,
                                 uint32_t atlas_width,
                                 uint32_t content_x, uint32_t content_y,
                                 const std::vector<uint8_t> &pixels,
                                 uint32_t width, uint32_t height)
{
    for (uint32_t padded_y = 0u; padded_y < height + 2u; ++padded_y) {
        const uint32_t source_y = (padded_y + height - 1u) % height;
        for (uint32_t padded_x = 0u; padded_x < width + 2u; ++padded_x) {
            const uint32_t source_x = (padded_x + width - 1u) % width;
            const size_t source_offset =
                (static_cast<size_t>(source_y) * width + source_x) * 4u;
            const size_t destination_offset =
                (static_cast<size_t>(content_y - 1u + padded_y) *
                     atlas_width +
                 content_x - 1u + padded_x) * 4u;
            std::memcpy(atlas.data() + destination_offset,
                        pixels.data() + source_offset, 4u);
        }
    }
}

bool is_floor_material_source(SceneMaterialSource source)
{
    return source == SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE ||
        source == SCENE_MATERIAL_SOURCE_LEVEL_FLOOR_TEXTURE_OVERRIDE;
}

bool compile_view_weapon(
    const SceneFrame &frame,
    const reconstruction::CameraProjection *camera,
    DxrViewWeaponCompilation &result, std::string &error)
{
    for (size_t index = 0; index < frame.count; ++index) {
        const SceneCommand &command = frame.commands[index];
        if (command.type != SCENE_COMMAND_SPRITE_INSTANCE ||
            command.data.sprite_instance.sprite.presentation !=
                SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON) {
            continue;
        }
        if (result.sprite) {
            error = "DXR SceneFrame contains multiple player view weapons";
            return false;
        }
        result.sprite = &command.data.sprite_instance.sprite;
    }

    const uint8_t present = result.sprite ? 1u : 0u;
    result.layout_hash = hash_bytes(result.layout_hash, &present,
                                    sizeof(present));
    if (!result.sprite) {
        return true;
    }
    if (!camera) {
        error = "DXR view weapon requires a valid camera transform";
        return false;
    }

    char compile_error[512] = {};
    if (!source_vector_scene_compile_view_weapon_camera(
            result.sprite, &result.source, compile_error,
            sizeof(compile_error))) {
        error = "DXR source view weapon compilation failed: ";
        error += compile_error;
        return false;
    }
    if (!result.source.triangles || result.source.triangle_count == 0u ||
        !result.source.materials || result.source.material_count == 0u ||
        result.source.triangle_count > UINT32_MAX / 3u ||
        result.source.material_count > UINT32_MAX) {
        error = "DXR source view weapon compiled no drawable vector faces";
        return false;
    }

    result.layout_hash = hash_bytes(
        result.layout_hash, &result.sprite->source_asset_id,
        sizeof(result.sprite->source_asset_id));
    result.layout_hash = hash_bytes(
        result.layout_hash, &result.source.triangle_count,
        sizeof(result.source.triangle_count));
    result.layout_hash = hash_bytes(
        result.layout_hash, &result.source.material_count,
        sizeof(result.source.material_count));
    for (size_t material_index = 0;
         material_index < result.source.material_count; ++material_index) {
        const SourceVectorSceneMaterial &material =
            result.source.materials[material_index];
        if (!material.rgba || material.width == 0u || material.height == 0u ||
            static_cast<size_t>(material.width) >
                std::numeric_limits<size_t>::max() / material.height / 4u) {
            error = "DXR source view weapon contains an invalid material";
            return false;
        }
        result.layout_hash = hash_bytes(result.layout_hash, &material.width,
                                        sizeof(material.width));
        result.layout_hash = hash_bytes(result.layout_hash, &material.height,
                                        sizeof(material.height));
        result.layout_hash = hash_bytes(
            result.layout_hash, &material.source_map_offset,
            sizeof(material.source_map_offset));
        result.layout_hash = hash_bytes(result.layout_hash, &material.minimum_u,
                                        sizeof(material.minimum_u));
        result.layout_hash = hash_bytes(result.layout_hash, &material.maximum_u,
                                        sizeof(material.maximum_u));
        result.layout_hash = hash_bytes(result.layout_hash, &material.minimum_v,
                                        sizeof(material.minimum_v));
        result.layout_hash = hash_bytes(result.layout_hash, &material.maximum_v,
                                        sizeof(material.maximum_v));
        result.layout_hash = hash_bytes(result.layout_hash, &material.glare,
                                        sizeof(material.glare));
    }

    result.vertices.reserve(result.source.triangle_count * 3u);
    for (size_t triangle_index = 0;
         triangle_index < result.source.triangle_count; ++triangle_index) {
        const SourceVectorSceneTriangle &triangle =
            result.source.triangles[triangle_index];
        if (triangle.material_index >= result.source.material_count) {
            error = "DXR source view weapon references an invalid material";
            return false;
        }
        result.layout_hash = hash_bytes(
            result.layout_hash, &triangle.material_index,
            sizeof(triangle.material_index));
        result.layout_hash = hash_bytes(result.layout_hash, &triangle.additive,
                                        sizeof(triangle.additive));
        for (const SourceVectorSceneVertex &source : triangle.vertices) {
            if (!(source.z > 0.0f) || !std::isfinite(source.x) ||
                !std::isfinite(source.y) || !std::isfinite(source.z) ||
                !std::isfinite(source.u) || !std::isfinite(source.v)) {
                error = "DXR source view weapon contains an invalid camera-local vertex";
                return false;
            }

            /*
             * The source compiler returns the authored model at its documented
             * quarter-level-unit scale in camera-local axes.  Attach that real
             * geometry to the current camera without reverse-projecting a
             * screen-space silhouette.
             */
            DxrSceneVertex vertex = {};
            vertex.position[0] = camera->position.x +
                camera->forward.x * source.z +
                camera->right.x * source.x + camera->up.x * source.y;
            vertex.position[1] = camera->position.y +
                camera->forward.y * source.z +
                camera->right.y * source.x + camera->up.y * source.y;
            vertex.position[2] = camera->position.z +
                camera->forward.z * source.z +
                camera->right.z * source.x + camera->up.z * source.y;
            vertex.texture_coordinate[0] = source.u;
            vertex.texture_coordinate[1] = source.v;
            vertex.material_index = triangle.material_index;
            vertex.emitter_index = UINT32_MAX;
            vertex.primitive = static_cast<uint32_t>(
                DxrScenePrimitive::view_weapon);
            /* Camera-local weapon vertices deliberately carry no
             * objdrawhires.s:doapoly flat/Gouraud modulation. Authored PBR
             * emission remains unscaled and all incident light is traced. */
            vertex.emissive_scale = 1.0f;
            vertex.view_weapon_position[0] = source.x;
            vertex.view_weapon_position[1] = source.y;
            vertex.view_weapon_position[2] = source.z;
            result.layout_hash = hash_bytes(
                result.layout_hash, vertex.texture_coordinate,
                sizeof(vertex.texture_coordinate));
            result.vertex_hash = hash_bytes(result.vertex_hash, vertex.position,
                                            sizeof(vertex.position));
            result.vertices.push_back(vertex);
        }
    }
    return true;
}

/*
 * Whether the frame draws anything that compiles a material. A frame that draws
 * nothing has no material for a reserved projectile slot to name, and reserving
 * slots in a scene with no geometry would describe a pool of nothing.
 */
bool frame_compiles_a_material(const SceneFrame &frame)
{
    for (size_t index = 0; index < frame.count; ++index) {
        const SceneCommand &command = frame.commands[index];
        if (command.type == SCENE_COMMAND_GEOMETRY_INSTANCE &&
            command.data.geometry_instance.mesh.surface_count != 0u) {
            return true;
        }
        if (command.type != SCENE_COMMAND_SPRITE_INSTANCE) {
            continue;
        }
        const SceneSprite &sprite = command.data.sprite_instance.sprite;
        if (sprite.presentation == SCENE_SPRITE_PRESENTATION_WORLD_OBJECT &&
            (sprite.source == SCENE_SPRITE_SOURCE_OBJECT_BITMAP ||
             sprite.source == SCENE_SPRITE_SOURCE_GLARE_BITMAP ||
             sprite.source == SCENE_SPRITE_SOURCE_VECTOR_MODEL)) {
            return true;
        }
    }
    return false;
}

bool compile_world_bitmaps(const SceneFrame &frame, size_t pool_capacity,
                           DxrWorldBitmapCompilation &result,
                           std::string &error)
{
    if (!frame_compiles_a_material(frame)) {
        pool_capacity = 0u;
    }
    const SceneCamera *camera = nullptr;
    for (size_t index = 0; index < frame.count; ++index) {
        if (frame.commands[index].type == SCENE_COMMAND_CAMERA) {
            if (camera) {
                error = "DXR SceneFrame contains multiple cameras";
                return false;
            }
            camera = &frame.commands[index].data.camera;
        }
    }
    std::vector<DxrWorldBitmapInstance> occupants;
    for (size_t index = 0; index < frame.count; ++index) {
        const SceneCommand &command = frame.commands[index];
        if (command.type != SCENE_COMMAND_SPRITE_INSTANCE) {
            continue;
        }
        const SceneSpriteInstance &scene_instance =
            command.data.sprite_instance;
        const SceneSprite &sprite = scene_instance.sprite;
        /*
         * Projectiles are traced with every other world bitmap. The source
         * draws them from the same objdrawhires.s:draw_Bitmap paths, and
         * source_bitmap_scene_compile_world already applies the contact bias a
         * depth-ordered scene needs, so the only thing excluding them achieved
         * was that a bullet, its impact pop and every additive particle were
         * missing from the ray-traced image.
         */
        if (sprite.presentation != SCENE_SPRITE_PRESENTATION_WORLD_OBJECT ||
            (sprite.source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP &&
             sprite.source != SCENE_SPRITE_SOURCE_GLARE_BITMAP)) {
            continue;
        }
        if (!camera) {
            error = "DXR world billboards require a SceneFrame camera";
            return false;
        }
        DxrWorldBitmapInstance compiled;
        char compile_error[512] = {};
        if (!source_bitmap_scene_compile_world(
                &sprite, camera, &compiled.source, compile_error,
                sizeof(compile_error))) {
            error = "DXR source world-bitmap compilation failed: ";
            error += compile_error;
            return false;
        }
        compiled.instance = &scene_instance;
        compiled.vertices.reserve(6u);
        for (const SourceBitmapSceneVertex &source : compiled.source.vertices) {
            DxrSceneVertex vertex = {};
            vertex.position[0] = source.x;
            vertex.position[1] = source.y;
            vertex.position[2] = source.z;
            vertex.texture_coordinate[0] = source.u;
            vertex.texture_coordinate[1] = source.v;
            vertex.emitter_index = UINT32_MAX;
            vertex.primitive = static_cast<uint32_t>(
                compiled.source.additive ? DxrScenePrimitive::world_effect :
                                           DxrScenePrimitive::world_billboard);
            /*
             * The packaged PBR maps are unlit assets. Ignore draw_Bitmap's
             * palette brightness and let traced incident radiance light them.
             *
             * An additive billboard is the exception: it is never lit, and its
             * emissive channel is the decoded source blend result, so this is
             * the additive strength instead. The renderer-neutral compiler
             * reports the glare/additive split from objdrawhires.s, and the
             * OpenGL path holds a glare at 0.8 where an additive bitmap draws
             * at full strength; keeping the same pair keeps the two backends
             * showing the same effect.
             */
            vertex.emissive_scale =
                compiled.source.material_mode ==
                        SOURCE_BITMAP_MATERIAL_MODE_GLARE
                    ? source_glare_additive_strength : 1.0f;
            compiled.vertex_hash = hash_bytes(
                compiled.vertex_hash, vertex.position,
                sizeof(vertex.position));
            compiled.vertex_hash = hash_bytes(
                compiled.vertex_hash, vertex.texture_coordinate,
                sizeof(vertex.texture_coordinate));
            compiled.vertices.push_back(vertex);
        }
        compiled.vertex_hash = hash_bytes(
            compiled.vertex_hash, &sprite.frame_index,
            sizeof(sprite.frame_index));
        compiled.vertex_hash = hash_bytes(
            compiled.vertex_hash, &compiled.source.material_mode,
            sizeof(compiled.source.material_mode));
        /*
         * Nothing about an occupant reaches the layout. Its identity, mesh and
         * asset belong to whatever the source put in that ObjT record, not to
         * the scene's shape, and hashing any of it here is what made an alien
         * dying or an item being collected rebuild the whole scene.
         */
        result.vertex_hash = hash_bytes(
            result.vertex_hash, &compiled.vertex_hash,
            sizeof(compiled.vertex_hash));
        occupants.push_back(std::move(compiled));
    }
    result.pool_first = 0u;
    result.pool_capacity = pool_capacity;
    result.pool_overflow = occupants.size() > pool_capacity ?
        occupants.size() - pool_capacity : 0u;
    result.layout_hash =
        hash_bytes(result.layout_hash, &pool_capacity, sizeof(pool_capacity));
    if (result.pool_overflow != 0u) {
        /* The caller grows the pool and rebuilds; there is no point compiling
         * against a capacity it is about to discard. */
        return true;
    }
    for (DxrWorldBitmapInstance &occupant : occupants) {
        result.instances.push_back(std::move(occupant));
    }
    /*
     * Reserved-but-empty slots. Six vertices collapsed onto the camera so the
     * run has its full shape on a rebuild; a geometry update leaves an empty
     * slot's vertices wherever its last occupant died, which is why the hash is
     * a constant rather than a fold of these positions.
     */
    while (result.instances.size() < result.pool_first + pool_capacity) {
        DxrWorldBitmapInstance empty;
        empty.occupied = false;
        empty.vertex_hash = empty_world_bitmap_slot_hash;
        empty.vertices.assign(6u, DxrSceneVertex{});
        for (DxrSceneVertex &vertex : empty.vertices) {
            const SceneRenderPoint origin =
                scene_render_world_point(camera ? camera->position :
                                                  SceneWorldPoint{});
            vertex.position[0] = origin.x;
            vertex.position[1] = origin.y;
            vertex.position[2] = origin.z;
            vertex.emitter_index = UINT32_MAX;
            vertex.primitive =
                static_cast<uint32_t>(DxrScenePrimitive::world_billboard);
            vertex.emissive_scale = 1.0f;
        }
        result.vertex_hash = hash_bytes(
            result.vertex_hash, &empty.vertex_hash,
            sizeof(empty.vertex_hash));
        result.instances.push_back(std::move(empty));
    }
    return true;
}

bool compile_world_vectors(const SceneFrame &frame,
                           DxrWorldVectorCompilation &result,
                           std::string &error)
{
    for (size_t index = 0; index < frame.count; ++index) {
        const SceneCommand &command = frame.commands[index];
        if (command.type != SCENE_COMMAND_SPRITE_INSTANCE) {
            continue;
        }
        const SceneSpriteInstance &scene_instance =
            command.data.sprite_instance;
        const SceneSprite &sprite = scene_instance.sprite;
        /* Projectile vector models are traced with every other world vector,
         * for the reason given in compile_world_bitmaps. */
        if (sprite.presentation != SCENE_SPRITE_PRESENTATION_WORLD_OBJECT ||
            sprite.source != SCENE_SPRITE_SOURCE_VECTOR_MODEL) {
            continue;
        }
        result.instances.emplace_back();
        DxrWorldVectorInstance &compiled = result.instances.back();
        compiled.instance = &scene_instance;
        char compile_error[512] = {};
        if (!source_vector_scene_compile_world_ray_traced(
                &sprite, &compiled.source, compile_error,
                sizeof(compile_error))) {
            error = "DXR source world-vector compilation failed: ";
            error += compile_error;
            return false;
        }
        if (!compiled.source.triangles ||
            compiled.source.triangle_count == 0u ||
            !compiled.source.materials ||
            compiled.source.material_count == 0u ||
            compiled.source.triangle_count > UINT32_MAX / 3u ||
            compiled.source.material_count > UINT32_MAX) {
            error = "DXR source world vector compiled no stable faces";
            return false;
        }
        result.layout_hash = hash_bytes(
            result.layout_hash, &sprite.source_record_id,
            sizeof(sprite.source_record_id));
        result.layout_hash = hash_bytes(
            result.layout_hash, &scene_instance.source_mesh_id,
            sizeof(scene_instance.source_mesh_id));
        result.layout_hash = hash_bytes(
            result.layout_hash, &sprite.source_asset_id,
            sizeof(sprite.source_asset_id));
        result.layout_hash = hash_bytes(
            result.layout_hash, &compiled.source.triangle_count,
            sizeof(compiled.source.triangle_count));
        result.layout_hash = hash_bytes(
            result.layout_hash, &compiled.source.material_count,
            sizeof(compiled.source.material_count));
        for (size_t material_index = 0;
             material_index < compiled.source.material_count;
             ++material_index) {
            const SourceVectorSceneMaterial &material =
                compiled.source.materials[material_index];
            if (!material.rgba || material.width == 0u ||
                material.height == 0u) {
                error = "DXR source world vector contains an invalid material";
                return false;
            }
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.source_map_offset,
                sizeof(material.source_map_offset));
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.minimum_u,
                sizeof(material.minimum_u));
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.maximum_u,
                sizeof(material.maximum_u));
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.minimum_v,
                sizeof(material.minimum_v));
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.maximum_v,
                sizeof(material.maximum_v));
            result.layout_hash = hash_bytes(
                result.layout_hash, &material.glare,
                sizeof(material.glare));
        }
        compiled.vertices.reserve(compiled.source.triangle_count * 3u);
        for (size_t triangle_index = 0;
             triangle_index < compiled.source.triangle_count;
             ++triangle_index) {
            const SourceVectorSceneTriangle &triangle =
                compiled.source.triangles[triangle_index];
            if (triangle.material_index >= compiled.source.material_count) {
                error = "DXR source world vector references an invalid material";
                return false;
            }
            result.layout_hash = hash_bytes(
                result.layout_hash, &triangle.material_index,
                sizeof(triangle.material_index));
            result.layout_hash = hash_bytes(
                result.layout_hash, &triangle.additive,
                sizeof(triangle.additive));
            for (const SourceVectorSceneVertex &source : triangle.vertices) {
                if (!std::isfinite(source.x) || !std::isfinite(source.y) ||
                    !std::isfinite(source.z) || !std::isfinite(source.u) ||
                    !std::isfinite(source.v)) {
                    error = "DXR source world vector contains a non-finite vertex";
                    return false;
                }
                DxrSceneVertex vertex = {};
                vertex.position[0] = source.x;
                vertex.position[1] = source.y;
                vertex.position[2] = source.z;
                vertex.texture_coordinate[0] = source.u;
                vertex.texture_coordinate[1] = source.v;
                vertex.material_index = triangle.material_index;
                vertex.emitter_index = UINT32_MAX;
                vertex.primitive = static_cast<uint32_t>(
                    triangle.additive ? DxrScenePrimitive::world_effect :
                                        DxrScenePrimitive::world_vector);
                /*
                 * Do not carry doapoly flat/Gouraud light into PBR entities.
                 * A `predoglare` face keeps full strength as well: unlike a
                 * glare bitmap, renderer_opengl.c draws the additive vector
                 * pass at an opacity of one.
                 */
                vertex.emissive_scale = 1.0f;
                compiled.vertex_hash = hash_bytes(
                    compiled.vertex_hash, vertex.position,
                    sizeof(vertex.position));
                compiled.vertex_hash = hash_bytes(
                    compiled.vertex_hash, vertex.texture_coordinate,
                    sizeof(vertex.texture_coordinate));
                compiled.vertices.push_back(vertex);
            }
        }
        result.vertex_hash = hash_bytes(
            result.vertex_hash, &compiled.vertex_hash,
            sizeof(compiled.vertex_hash));
    }
    const size_t count = result.instances.size();
    result.layout_hash = hash_bytes(result.layout_hash, &count, sizeof(count));
    return true;
}

float srgb_to_linear(uint8_t encoded)
{
    const float value = static_cast<float>(encoded) / 255.0f;
    return value <= 0.04045f ? value / 12.92f :
        std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float maximum_emissive_luminance(const MaterialImage &image)
{
    if (image.emissive_factor[0] == 0.0f &&
        image.emissive_factor[1] == 0.0f &&
        image.emissive_factor[2] == 0.0f) {
        return 0.0f;
    }
    const std::vector<uint8_t> &emissive = image.pixels[
        static_cast<size_t>(DxrMaterialChannel::emissive)];
    double maximum_luminance = 0.0;
    for (size_t offset = 0; offset < emissive.size(); offset += 4u) {
        const double red = srgb_to_linear(emissive[offset + 0u]) *
            image.emissive_factor[0];
        const double green = srgb_to_linear(emissive[offset + 1u]) *
            image.emissive_factor[1];
        const double blue = srgb_to_linear(emissive[offset + 2u]) *
            image.emissive_factor[2];
        maximum_luminance = std::max(
            maximum_luminance,
            red * 0.2126 + green * 0.7152 + blue * 0.0722);
    }
    return static_cast<float>(maximum_luminance);
}

bool build_material_mip_chain(MaterialImage &image, const char *kind,
                              std::string &error)
{
    image.maximum_emissive_luminance = maximum_emissive_luminance(image);
    constexpr std::array<material_mip::Semantic,
                         static_cast<size_t>(DxrMaterialChannel::count)>
        semantics = {
            material_mip::Semantic::srgb,
            material_mip::Semantic::normal,
            material_mip::Semantic::linear,
            material_mip::Semantic::linear,
            material_mip::Semantic::srgb,
        };
    image.mip_count = material_mip::level_count(image.width, image.height);
    image.material_mips = true;
    for (size_t channel = 0u; channel < semantics.size(); ++channel) {
        if (!material_mip::generate(
                semantics[channel], image.pixels[channel], image.width,
                image.height, image.mip_pixels[channel], error)) {
            error = "DXR " + std::string(kind) +
                " PBR mip generation failed: " + error;
            return false;
        }
        image.pixels[channel].clear();
        image.pixels[channel].shrink_to_fit();
    }
    return true;
}

bool build_wall_material_image(const SceneGeometry &geometry,
                               const DxrMaterialDefinition &pbr,
                               MaterialImage &image, std::string &error)
{
    const uint32_t origin_x =
        static_cast<uint32_t>(geometry.texture_window.u_offset) *
        world_texture_scale;
    const uint32_t extent_x =
        static_cast<uint32_t>(geometry.texture_window.u_period) *
        world_texture_scale;
    const uint32_t extent_y =
        static_cast<uint32_t>(geometry.texture_window.v_period) *
        world_texture_scale;
    if (extent_x == 0u || extent_y == 0u) {
        error = "DXR wall geometry has no source texture window";
        return false;
    }
    if (origin_x > pbr.width || extent_x > pbr.width - origin_x ||
        extent_y != pbr.height) {
        std::ostringstream report;
        report << "DXR wall PBR texture window does not match its material image"
               << " (record=" << geometry.source_record_id
               << " source=" << geometry.texture_window.u_offset << ","
               << geometry.texture_window.u_period << "x"
               << geometry.texture_window.v_period
               << " PBR=" << pbr.width << "x" << pbr.height << ")";
        error = report.str();
        return false;
    }
    if (extent_x > UINT16_MAX || extent_y > UINT16_MAX ||
        static_cast<size_t>(pbr.width) >
            std::numeric_limits<size_t>::max() / pbr.height / 4u) {
        error = "DXR wall PBR texture window exceeds its supported range";
        return false;
    }

    const size_t source_bytes =
        static_cast<size_t>(pbr.width) * pbr.height * 4u;
    const size_t destination_bytes =
        static_cast<size_t>(extent_x) * extent_y * 4u;
    image.width = extent_x;
    image.height = extent_y;
    for (size_t channel = 0u;
         channel < static_cast<size_t>(DxrMaterialChannel::count);
         ++channel) {
        const std::vector<uint8_t> &source = pbr.pixels[channel];
        if (source.size() != source_bytes) {
            error = "DXR wall PBR channel does not match its declared image";
            return false;
        }
        std::vector<uint8_t> &destination = image.pixels[channel];
        destination.resize(destination_bytes);
        for (uint32_t row = 0u; row < extent_y; ++row) {
            std::memcpy(
                destination.data() +
                    static_cast<size_t>(row) * extent_x * 4u,
                source.data() +
                    (static_cast<size_t>(row) * pbr.width + origin_x) * 4u,
                static_cast<size_t>(extent_x) * 4u);
        }
    }

    return build_material_mip_chain(image, "wall", error);
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

D3D12_RESOURCE_DESC buffer_description(UINT64 size,
                                       D3D12_RESOURCE_FLAGS flags =
                                           D3D12_RESOURCE_FLAG_NONE)
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
    description.Flags = flags;
    return description;
}

bool create_buffer(ID3D12Device5 *device, UINT64 size, D3D12_HEAP_TYPE heap_type,
                   D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags,
                   const wchar_t *name, Microsoft::WRL::ComPtr<ID3D12Resource> &resource,
                   std::string &error)
{
    const D3D12_HEAP_PROPERTIES properties = heap_properties(heap_type);
    const D3D12_RESOURCE_DESC description = buffer_description(size, flags);
    const HRESULT result = device->CreateCommittedResource(
        &properties, D3D12_HEAP_FLAG_NONE, &description, state, nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateCommittedResource(scene buffer)",
                              result);
        return false;
    }
    resource->SetName(name);
    return true;
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

bool append_geometry_vertices(const SceneGeometry &geometry,
                              uint32_t material_index,
                              uint32_t material_width,
                              uint32_t material_height,
                              std::vector<DxrSceneVertex> &vertices,
                              std::string &error)
{
    /* Mipmapped world surfaces carry a non-zero repeat extent. Wall materials
     * are first cropped to the exact hireswall.s:Draw_Wall window; floors use
     * the complete PBR image for Draw_Flats' fixed 64x64 logical tile. */
    uint32_t texture_window_origin = 0u;
    uint32_t texture_window_extent = 0u;
    if (geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
        const uint32_t extent_x =
            static_cast<uint32_t>(geometry.texture_window.u_period) *
            world_texture_scale;
        const uint32_t extent_y =
            static_cast<uint32_t>(geometry.texture_window.v_period) *
            world_texture_scale;
        if (extent_x == 0u || extent_y == 0u) {
            error = "DXR wall geometry has no source texture window";
            return false;
        }
        if (extent_x > UINT16_MAX || extent_y > UINT16_MAX) {
            error = "DXR wall PBR texture window exceeds its packed vertex range";
            return false;
        }
        if (extent_x != material_width || extent_y != material_height) {
            std::ostringstream report;
            report << "DXR cropped wall PBR material does not match its source window"
                   << " (record=" << geometry.source_record_id
                   << " source=" << geometry.texture_window.u_offset << ","
                   << geometry.texture_window.u_period << "x"
                   << geometry.texture_window.v_period
                   << " PBR=" << material_width << "x" << material_height;
            if (geometry.vertices && geometry.vertex_count != 0u) {
                report << " U=";
                for (uint32_t vertex_index = 0;
                     vertex_index < geometry.vertex_count; ++vertex_index) {
                    if (vertex_index != 0u) {
                        report << ",";
                    }
                    report << geometry.vertices[vertex_index].texture_u;
                }
            }
            report << ")";
            error = report.str();
            return false;
        }
        texture_window_extent = extent_x | (extent_y << 16u);
    } else if (geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_FLOOR ||
               geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_CEILING) {
        constexpr uint32_t expected_extent =
            floor_texture_extent * world_texture_scale;
        if (material_width != expected_extent ||
            material_height != expected_extent) {
            std::ostringstream report;
            report << "DXR floor/ceiling PBR material does not match "
                      "Draw_Flats' 64x64 source tile (record="
                   << geometry.source_record_id << " PBR="
                   << material_width << "x" << material_height << ")";
            error = report.str();
            return false;
        }
        texture_window_extent =
            material_width | (material_height << 16u);
    }
    uint32_t *indices = nullptr;
    uint32_t index_count = 0;
    char triangulation_error[512] = {};
    if (!scene_geometry_triangle_indices(
            &geometry, &indices, &index_count, triangulation_error,
            sizeof(triangulation_error))) {
        error = "DXR SceneFrame triangulation failed: ";
        error += triangulation_error;
        return false;
    }
    const float u_scale =
        geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
        1.0f / static_cast<float>(geometry.texture_window.u_period) :
        1.0f / static_cast<float>(floor_texture_extent);
    const float v_scale =
        geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
        1.0f / static_cast<float>(geometry.texture_window.v_period) :
        1.0f / static_cast<float>(floor_texture_extent);
    if (vertices.size() > std::numeric_limits<uint32_t>::max() - index_count) {
        scene_geometry_triangle_indices_release(indices);
        error = "DXR SceneFrame has too many triangle vertices";
        return false;
    }
    for (uint32_t index = 0; index < index_count; ++index) {
        const SceneVertex &source = geometry.vertices[indices[index]];
        const SceneRenderPoint position =
            scene_render_world_point(source.position);
        DxrSceneVertex vertex = {};
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        vertex.texture_coordinate[0] = source.texture_u * u_scale;
        vertex.texture_coordinate[1] = source.texture_v * v_scale;
        vertex.material_index = material_index;
        vertex.emitter_index = UINT32_MAX;
        vertex.primitive = static_cast<uint32_t>(DxrScenePrimitive::world);
        vertex.texture_window_origin = texture_window_origin;
        vertex.texture_window_extent = texture_window_extent;
        /* PBR emission is authored radiance. Source Gouraud/ZoneT values are
         * raster-lighting inputs retained for OpenGL, not an emitter control. */
        vertex.emissive_scale = 1.0f;
        vertices.push_back(vertex);
    }
    scene_geometry_triangle_indices_release(indices);
    return true;
}

bool compile_emissive_triangles(
    std::vector<DxrSceneVertex> &vertices,
    const std::vector<float> &material_emissive_bound,
    std::vector<DxrEmissiveTriangle> &emitters, std::string &error)
{
    emitters.clear();
    std::vector<float> emitter_weights;
    for (DxrSceneVertex &vertex : vertices) {
        vertex.emitter_index = UINT32_MAX;
    }
    for (size_t first_vertex = 0; first_vertex < vertices.size();
         first_vertex += 3u) {
        const uint32_t material_index = vertices[first_vertex].material_index;
        if (vertices[first_vertex].primitive == static_cast<uint32_t>(
                DxrScenePrimitive::view_weapon) ||
            vertices[first_vertex].primitive == static_cast<uint32_t>(
                DxrScenePrimitive::world_effect)) {
            continue;
        }
        if (material_index >= material_emissive_bound.size()) {
            error = "DXR geometry references an out-of-range material";
            return false;
        }
        const float luminance = material_emissive_bound[material_index];
        if (!(luminance > 0.0f)) {
            continue;
        }
        const float *first = vertices[first_vertex + 0u].position;
        const float *second = vertices[first_vertex + 1u].position;
        const float *third = vertices[first_vertex + 2u].position;
        const float edge_a[3] = {
            second[0] - first[0], second[1] - first[1],
            second[2] - first[2]};
        const float edge_b[3] = {
            third[0] - first[0], third[1] - first[1], third[2] - first[2]};
        const float cross[3] = {
            edge_a[1] * edge_b[2] - edge_a[2] * edge_b[1],
            edge_a[2] * edge_b[0] - edge_a[0] * edge_b[2],
            edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0],
        };
        const float area = 0.5f * std::sqrt(
            cross[0] * cross[0] + cross[1] * cross[1] +
            cross[2] * cross[2]);
        /* The emitter proposal uses the material's conservative radiance bound,
         * like a light tree/ReGIR cell, rather than average texture power. */
        const float weight = area * luminance;
        if (!(area > 1.0e-6f) || !std::isfinite(weight) ||
            !(weight > 0.0f)) {
            continue;
        }
        DxrEmissiveTriangle emitter = {};
        emitter.first_vertex = static_cast<uint32_t>(first_vertex);
        emitter.inverse_area = 1.0f / area;
        const uint32_t emitter_index = static_cast<uint32_t>(emitters.size());
        for (size_t vertex = 0; vertex < 3u; ++vertex) {
            vertices[first_vertex + vertex].emitter_index = emitter_index;
        }
        emitters.push_back(emitter);
        emitter_weights.push_back(weight);
    }
    if (emitters.empty()) {
        return true;
    }
    /*
     * Reservoir resampling draws tens of candidates per pixel per frame, so the
     * emitter distribution has to be sampled in constant time.
     */
    std::vector<alias_table::Entry> alias_entries;
    if (!alias_table::build(emitter_weights, alias_entries) ||
        alias_entries.size() != emitters.size()) {
        error = "DXR emissive triangles produced an unusable light distribution";
        return false;
    }
    for (size_t index = 0; index < emitters.size(); ++index) {
        emitters[index].selection_probability = alias_entries[index].probability;
        emitters[index].alias_threshold = alias_entries[index].threshold;
        emitters[index].alias_index = alias_entries[index].alias;
    }
    return true;
}

}  // namespace

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::vertex_address() const
{
    return vertex_buffer_ ? vertex_buffer_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::previous_vertex_address() const
{
    return previous_vertex_buffer_ ?
        previous_vertex_buffer_->GetGPUVirtualAddress() : 0;
}

bool DxrScene::view_weapon_pose_hash(uint64_t &pose_hash) const
{
    pose_hash = UINT64_C(1469598103934665603);
    for (const CompiledInstance &instance : instances_) {
        if (!instance.view_weapon ||
            instance.first_vertex > vertices_.size() ||
            instance.vertex_count > vertices_.size() - instance.first_vertex) {
            continue;
        }
        for (size_t vertex_index = 0u;
             vertex_index < instance.vertex_count; ++vertex_index) {
            const DxrSceneVertex &vertex =
                vertices_[instance.first_vertex + vertex_index];
            pose_hash = hash_bytes(
                pose_hash, vertex.view_weapon_position,
                sizeof(vertex.view_weapon_position));
        }
        return true;
    }
    return false;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::material_address() const
{
    return material_buffer_ ? material_buffer_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrScene::emitter_address() const
{
    return emitter_buffer_ ? emitter_buffer_->GetGPUVirtualAddress() : 0;
}

void DxrScene::release_gpu()
{
    vertex_buffer_.Reset();
    previous_vertex_buffer_.Reset();
    material_buffer_.Reset();
    emitter_buffer_.Reset();
    upload_buffer_.Reset();
    for (auto &upload : geometry_uploads_) {
        upload.Reset();
    }
    for (auto &texture : atlas_textures_) {
        texture.Reset();
    }
    for (auto &upload : atlas_uploads_) {
        upload.Reset();
    }
    blas_scratch_.Reset();
    blases_.clear();
    tlas_scratch_.Reset();
    tlas_.Reset();
    instance_upload_.Reset();
}

bool DxrScene::update(const SceneFrame &frame,
                      const reconstruction::CameraProjection *camera,
                      bool &requires_flush, std::string &error)
{
    DxrViewWeaponCompilation view_weapon;
    DxrWorldBitmapCompilation world_bitmaps;
    DxrWorldVectorCompilation world_vectors;
    if (!compile_view_weapon(frame, camera, view_weapon, error)) {
        return false;
    }
    /*
     * Size the projectile pool before compiling against it. It starts at the
     * source's own bound and only grows, doubling past a frame that needed more
     * so a busy firefight settles after one rebuild instead of one per shot.
     */
    if (world_bitmap_pool_capacity_ == 0u) {
        world_bitmap_pool_capacity_ = world_bitmap_pool_baseline;
    }
    if (!compile_world_bitmaps(frame, world_bitmap_pool_capacity_,
                               world_bitmaps, error)) {
        return false;
    }
    if (world_bitmaps.pool_overflow != 0u) {
        const size_t needed =
            world_bitmap_pool_capacity_ + world_bitmaps.pool_overflow;
        if (needed > world_bitmap_pool_limit) {
            error = "DXR world-bitmap projectile pool exceeded its limit";
            return false;
        }
        size_t grown = world_bitmap_pool_capacity_;
        while (grown < needed) {
            grown *= 2u;
        }
        world_bitmap_pool_capacity_ = std::min(grown, world_bitmap_pool_limit);
        debug_output("DXR projectile pool grown to " +
                     std::to_string(world_bitmap_pool_capacity_) + " slots");
        world_bitmaps = DxrWorldBitmapCompilation{};
        if (!compile_world_bitmaps(frame, world_bitmap_pool_capacity_,
                                   world_bitmaps, error)) {
            return false;
        }
        if (world_bitmaps.pool_overflow != 0u) {
            error = "DXR world-bitmap projectile pool could not be sized";
            return false;
        }
    }
    if (!compile_world_vectors(frame, world_vectors, error)) {
        return false;
    }
    DxrSceneGeometryHashes hashes = dxr_scene_geometry_hashes(frame);
    const uint64_t world_layout = hashes.layout;
    hashes.layout = hash_bytes(hashes.layout, &view_weapon.layout_hash,
                               sizeof(view_weapon.layout_hash));
    hashes.vertex_data = hash_bytes(hashes.vertex_data,
                                    &view_weapon.vertex_hash,
                                    sizeof(view_weapon.vertex_hash));
    hashes.layout = hash_bytes(hashes.layout, &world_bitmaps.layout_hash,
                               sizeof(world_bitmaps.layout_hash));
    hashes.vertex_data = hash_bytes(hashes.vertex_data,
                                    &world_bitmaps.vertex_hash,
                                    sizeof(world_bitmaps.vertex_hash));
    hashes.layout = hash_bytes(hashes.layout, &world_vectors.layout_hash,
                               sizeof(world_vectors.layout_hash));
    hashes.vertex_data = hash_bytes(hashes.vertex_data,
                                    &world_vectors.vertex_hash,
                                    sizeof(world_vectors.vertex_hash));
    const DxrSceneUpdateKind update_kind =
        dxr_scene_classify_update(has_hashes_, scene_hashes_, hashes);
    requires_flush = false;
    if (update_kind == DxrSceneUpdateKind::unchanged) {
        return true;
    }
    if (update_kind == DxrSceneUpdateKind::rebuild) {
        history_reset_pending_ = true;
        requires_flush = true;
        return compile(frame, view_weapon, world_bitmaps, world_vectors,
                       hashes, world_layout, error);
    }

    bool static_changed = false;
    if (!compile_geometry_update(frame, view_weapon, world_bitmaps,
                                 world_vectors,
                                 static_changed, error)) {
        return false;
    }
    if (static_changed) {
        debug_output(
            "DXR static SceneFrame geometry changed; rebuilding scene resources");
        requires_flush = true;
        history_reset_pending_ = true;
        return compile(frame, view_weapon, world_bitmaps, world_vectors,
                       hashes, world_layout, error);
    }
    scene_hashes_ = hashes;
    has_hashes_ = true;
    gpu_geometry_update_pending_ = true;
    return true;
}

bool DxrScene::compile(const SceneFrame &frame,
                       const DxrViewWeaponCompilation &view_weapon,
                       const DxrWorldBitmapCompilation &world_bitmaps,
                       const DxrWorldVectorCompilation &world_vectors,
                       const DxrSceneGeometryHashes &hashes,
                       uint64_t world_layout, std::string &error)
{
    std::vector<DxrSceneVertex> compiled_vertices;
    std::vector<MaterialImage> images;
    std::map<MaterialKey, uint32_t> material_indices;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t>
        compiled_bitmap_material_indices;
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, uint8_t,
                        uint8_t, uint8_t, uint8_t>, uint32_t>
        compiled_vector_material_indices;
    std::vector<uint32_t> compiled_surface_material_indices;
    std::vector<CompiledInstance> compiled_instances;

    if (!material_library_.loaded() &&
        !material_library_.load_from_executable(error)) {
        return false;
    }

    for (size_t command_index = 0; command_index < frame.count; ++command_index) {
        const SceneCommand &command = frame.commands[command_index];
        if (command.type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        const SceneGeometryInstance &instance = command.data.geometry_instance;
        const SceneMesh &mesh = instance.mesh;
        const size_t first_vertex = compiled_vertices.size();
        const size_t first_surface =
            compiled_surface_material_indices.size();
        if (!mesh.surfaces && mesh.surface_count != 0u) {
            error = "SceneFrame geometry instance has no surface array";
            return false;
        }
        for (uint32_t surface_index = 0; surface_index < mesh.surface_count;
             ++surface_index) {
            const SceneMeshSurface &surface = mesh.surfaces[surface_index];
            const SceneGeometry &geometry = surface.geometry;
            const DxrMaterialDefinition *surface_pbr = nullptr;
            const bool wall =
                geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL;
            const bool floor_material =
                is_floor_material_source(surface.material.source);
            const uint32_t texture_v_period =
                wall ? geometry.texture_window.v_period : 0u;
            if (!material_library_.resolve(
                    surface.material.source,
                    surface.material.source_asset_id,
                    texture_v_period,
                    surface_pbr, error)) {
                return false;
            }
            MaterialKey key = {
                surface.material.source,
                surface.material.source_asset_id,
                texture_v_period,
                wall ? geometry.texture_window.u_offset : 0u,
                wall ? geometry.texture_window.u_period : 0u,
            };
            uint32_t material_index;
            auto found = material_indices.find(key);
            if (found == material_indices.end()) {
                MaterialImage image;
                image.key = key;
                const DxrMaterialDefinition *pbr = surface_pbr;
                image.normal_strength = pbr->normal_strength;
                image.specular_factor = pbr->specular_factor;
                std::memcpy(image.emissive_factor, pbr->emissive_factor,
                            sizeof(image.emissive_factor));
                if (wall) {
                    if (!build_wall_material_image(geometry, *pbr, image,
                                                   error)) {
                        return false;
                    }
                } else {
                    image.width = pbr->width;
                    image.height = pbr->height;
                    image.pixels = pbr->pixels;
                    if (floor_material) {
                        if (!build_material_mip_chain(image, "floor", error)) {
                            return false;
                        }
                    } else {
                        image.maximum_emissive_luminance =
                            maximum_emissive_luminance(image);
                    }
                }
                material_index = static_cast<uint32_t>(images.size());
                material_indices.emplace(key, material_index);
                images.push_back(std::move(image));

                std::ostringstream report;
                report << "DXR PBR PNG material: " << pbr->name
                       << " source=" << static_cast<unsigned>(key.source)
                       << " asset=" << key.source_asset_id
                       << " window=" << key.texture_u_offset << ","
                       << key.texture_u_period << "x"
                       << key.texture_v_period
                       << " mips=" << images[material_index].mip_count;
                debug_output(report.str());
            } else {
                material_index = found->second;
            }
            compiled_surface_material_indices.push_back(material_index);
            if (!append_geometry_vertices(geometry, material_index,
                                          images[material_index].width,
                                          images[material_index].height,
                                          compiled_vertices, error)) {
                return false;
            }
        }
        if (compiled_vertices.size() != first_vertex) {
            const size_t vertex_count = compiled_vertices.size() - first_vertex;
            if (first_surface > UINT32_MAX || first_vertex > UINT32_MAX ||
                vertex_count > UINT32_MAX ||
                first_vertex / 3u > UINT32_C(0x00ffffff)) {
                error = "DXR SceneFrame instance exceeds DXR index limits";
                return false;
            }
            CompiledInstance compiled_instance;
            compiled_instance.source_instance_id = instance.source_instance_id;
            compiled_instance.source_mesh_id = mesh.source_mesh_id;
            compiled_instance.first_surface =
                static_cast<uint32_t>(first_surface);
            compiled_instance.surface_count = mesh.surface_count;
            compiled_instance.first_vertex = static_cast<uint32_t>(first_vertex);
            compiled_instance.vertex_count = static_cast<uint32_t>(vertex_count);
            compiled_instance.acceleration_class = mesh.acceleration_class;
            compiled_instance.vertex_hash =
                dxr_scene_instance_vertex_hash(instance);
            compiled_instances.push_back(compiled_instance);
        }
    }

    /*
     * A level load replaces the world, and with it every object that can appear
     * in it, so the remembered draw modes start again from nothing. A frame with
     * no world geometry at all is not a level: the directed probes the hidden
     * smoke presents look like that, and forgetting on them would throw the
     * memory away for the frame that follows.
     */
    bool frame_has_world = false;
    for (size_t index = 0; index < frame.count && !frame_has_world; ++index) {
        frame_has_world =
            frame.commands[index].type == SCENE_COMMAND_GEOMETRY_INSTANCE &&
            frame.commands[index].data.geometry_instance.mesh.surface_count != 0u;
    }
    if (frame_has_world && bitmap_modes_world_layout_ != world_layout) {
        bitmap_modes_seen_.clear();
        bitmap_modes_world_layout_ = world_layout;
    }

    const auto load_bitmap_mode = [&](uint32_t asset, uint32_t mode) -> bool {
        for (const auto &entry : compiled_bitmap_material_indices) {
            if (std::get<0>(entry.first) == asset &&
                std::get<2>(entry.first) == mode) {
                return true;
            }
        }
        std::vector<DxrBitmapMaterialBinding> bindings;
        if (!material_library_.resolve_bitmap_asset_mode(asset, mode, bindings,
                                                        error)) {
            return false;
        }
        for (const DxrBitmapMaterialBinding &binding : bindings) {
            const auto key = std::make_tuple(
                binding.source_asset_id, binding.frame_index,
                binding.source_mode);
            if (compiled_bitmap_material_indices.find(key) !=
                compiled_bitmap_material_indices.end()) {
                continue;
            }
            if (!binding.definition || images.size() >= UINT32_MAX) {
                error = "DXR bitmap PBR material enumeration is invalid";
                return false;
            }
            const DxrMaterialDefinition &pbr = *binding.definition;
            MaterialImage image;
            image.width = pbr.width;
            image.height = pbr.height;
            image.pixels = pbr.pixels;
            image.normal_strength = pbr.normal_strength;
            image.specular_factor = pbr.specular_factor;
            std::memcpy(image.emissive_factor, pbr.emissive_factor,
                        sizeof(image.emissive_factor));
            image.maximum_emissive_luminance = maximum_emissive_luminance(image);
            compiled_bitmap_material_indices.emplace(
                key, static_cast<uint32_t>(images.size()));
            images.push_back(std::move(image));
        }
        return true;
    };

    /* Everything the level has ever shown, before anything currently on
     * screen, so a repack keeps the whole set rather than this instant's. */
    for (const auto &seen : bitmap_modes_seen_) {
        if (!load_bitmap_mode(seen.first, seen.second)) {
            return false;
        }
    }

    for (size_t bitmap_index = 0; bitmap_index < world_bitmaps.instances.size();
         ++bitmap_index) {
        const DxrWorldBitmapInstance &bitmap =
            world_bitmaps.instances[bitmap_index];
        const bool pool_slot = bitmap_index >= world_bitmaps.pool_first;
        if (!bitmap.occupied) {
            /*
             * A reserved slot nothing occupies. It still needs its vertices and
             * its instance so the run keeps its shape, but it has no sprite to
             * resolve a material from, so it borrows index zero; its geometry is
             * a point and can never be hit.
             */
            const size_t first_vertex = compiled_vertices.size();
            for (DxrSceneVertex vertex : bitmap.vertices) {
                vertex.material_index = 0u;
                compiled_vertices.push_back(vertex);
            }
            CompiledInstance compiled_instance;
            compiled_instance.first_surface = static_cast<uint32_t>(
                compiled_surface_material_indices.size());
            compiled_instance.first_vertex =
                static_cast<uint32_t>(first_vertex);
            compiled_instance.vertex_count =
                static_cast<uint32_t>(bitmap.vertices.size());
            compiled_instance.acceleration_class =
                SCENE_ACCELERATION_CLASS_DYNAMIC;
            compiled_instance.vertex_hash = bitmap.vertex_hash;
            compiled_instance.world_bitmap = true;
            compiled_instance.bitmap_pool = true;
            compiled_instance.opaque = false;
            compiled_instances.push_back(compiled_instance);
            continue;
        }
        const SceneSprite &sprite = bitmap.instance->sprite;
        bitmap_modes_seen_.emplace(sprite.source_asset_id,
                                   bitmap.source.material_mode);
        if (!load_bitmap_mode(sprite.source_asset_id,
                              bitmap.source.material_mode)) {
            return false;
        }
        const auto current_key = std::make_tuple(
            sprite.source_asset_id, static_cast<uint32_t>(sprite.frame_index),
            bitmap.source.material_mode);
        const auto current = compiled_bitmap_material_indices.find(current_key);
        if (current == compiled_bitmap_material_indices.end() ||
            compiled_vertices.size() > UINT32_MAX - bitmap.vertices.size()) {
            std::ostringstream message;
            message << "DXR current bitmap PBR binding is unavailable: asset="
                    << sprite.source_asset_id << " frame="
                    << sprite.frame_index << " mode="
                    << bitmap.source.material_mode;
            error = message.str();
            return false;
        }
        const size_t first_vertex = compiled_vertices.size();
        for (DxrSceneVertex vertex : bitmap.vertices) {
            vertex.material_index = current->second;
            compiled_vertices.push_back(vertex);
        }
        CompiledInstance compiled_instance;
        compiled_instance.source_instance_id = sprite.source_record_id;
        compiled_instance.source_mesh_id = bitmap.instance->source_mesh_id;
        compiled_instance.first_surface = static_cast<uint32_t>(
            compiled_surface_material_indices.size());
        compiled_instance.first_vertex = static_cast<uint32_t>(first_vertex);
        compiled_instance.vertex_count = static_cast<uint32_t>(
            bitmap.vertices.size());
        compiled_instance.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
        compiled_instance.vertex_hash = bitmap.vertex_hash;
        compiled_instance.world_bitmap = true;
        compiled_instance.bitmap_pool = pool_slot;
        compiled_instance.opaque = false;
        compiled_instances.push_back(compiled_instance);
    }
    if (!world_bitmaps.instances.empty()) {
        debug_output(
            "DXR world billboards: dynamic alpha-tested PBR geometry shares "
            "the ray-traced scene and preloads active animation frames");
    }

    for (const DxrWorldVectorInstance &vector : world_vectors.instances) {
        const SceneSprite &sprite = vector.instance->sprite;
        for (size_t material_index = 0;
             material_index < vector.source.material_count;
             ++material_index) {
            const SourceVectorSceneMaterial &source =
                vector.source.materials[material_index];
            const auto key = std::make_tuple(
                sprite.source_asset_id, source.source_map_offset,
                source.minimum_u, source.maximum_u,
                source.minimum_v, source.maximum_v, source.glare);
            if (compiled_vector_material_indices.find(key) !=
                compiled_vector_material_indices.end()) {
                continue;
            }
            const DxrMaterialDefinition *pbr = nullptr;
            if (!material_library_.resolve_vector(
                    sprite.source_asset_id, source.source_map_offset,
                    source.minimum_u, source.maximum_u,
                    source.minimum_v, source.maximum_v, source.glare,
                    pbr, error)) {
                return false;
            }
            if (!pbr || pbr->width != source.width ||
                pbr->height != source.height || images.size() >= UINT32_MAX) {
                error = "DXR world-vector PBR PNG extent disagrees with its source face";
                return false;
            }
            MaterialImage image;
            image.width = pbr->width;
            image.height = pbr->height;
            image.pixels = pbr->pixels;
            image.normal_strength = pbr->normal_strength;
            image.specular_factor = pbr->specular_factor;
            std::memcpy(image.emissive_factor, pbr->emissive_factor,
                        sizeof(image.emissive_factor));
            image.maximum_emissive_luminance =
                maximum_emissive_luminance(image);
            compiled_vector_material_indices.emplace(
                key, static_cast<uint32_t>(images.size()));
            images.push_back(std::move(image));
        }
        if (compiled_vertices.size() >
            UINT32_MAX - vector.vertices.size()) {
            error = "DXR world vector exceeds scene index limits";
            return false;
        }
        const size_t first_vertex = compiled_vertices.size();
        for (DxrSceneVertex vertex : vector.vertices) {
            if (vertex.material_index >= vector.source.material_count) {
                error = "DXR world vector references an invalid source material";
                return false;
            }
            const SourceVectorSceneMaterial &source =
                vector.source.materials[vertex.material_index];
            const auto key = std::make_tuple(
                sprite.source_asset_id, source.source_map_offset,
                source.minimum_u, source.maximum_u,
                source.minimum_v, source.maximum_v, source.glare);
            const auto material = compiled_vector_material_indices.find(key);
            if (material == compiled_vector_material_indices.end()) {
                error = "DXR world-vector PBR material was not packed";
                return false;
            }
            vertex.material_index = material->second;
            compiled_vertices.push_back(vertex);
        }
        CompiledInstance compiled_instance;
        compiled_instance.source_instance_id = sprite.source_record_id;
        compiled_instance.source_mesh_id = vector.instance->source_mesh_id;
        compiled_instance.first_surface = static_cast<uint32_t>(
            compiled_surface_material_indices.size());
        compiled_instance.first_vertex = static_cast<uint32_t>(first_vertex);
        compiled_instance.vertex_count = static_cast<uint32_t>(
            vector.vertices.size());
        compiled_instance.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
        compiled_instance.vertex_hash = vector.vertex_hash;
        compiled_instance.world_vector = true;
        compiled_instance.opaque = false;
        compiled_instances.push_back(compiled_instance);
    }
    if (!world_vectors.instances.empty()) {
        debug_output(
            "DXR world vectors: stable animated PBR meshes share the ray-traced scene");
    }

    uint32_t compiled_view_weapon_first_material =
        static_cast<uint32_t>(images.size());
    uint32_t compiled_view_weapon_material_count = 0u;
    if (view_weapon.sprite) {
        if (images.size() > UINT32_MAX - view_weapon.source.material_count ||
            compiled_vertices.size() > UINT32_MAX - view_weapon.vertices.size()) {
            error = "DXR view weapon exceeds scene index limits";
            return false;
        }
        for (size_t material_index = 0;
             material_index < view_weapon.source.material_count;
             ++material_index) {
            const SourceVectorSceneMaterial &source =
                view_weapon.source.materials[material_index];
            const DxrMaterialDefinition *pbr = nullptr;
            if (!material_library_.resolve_vector(
                    view_weapon.sprite->source_asset_id,
                    source.source_map_offset,
                    source.minimum_u, source.maximum_u,
                    source.minimum_v, source.maximum_v, source.glare,
                    pbr, error)) {
                return false;
            }
            if (pbr->width != source.width || pbr->height != source.height) {
                error = "DXR view-weapon PBR PNG extent disagrees with the source face";
                return false;
            }
            MaterialImage image;
            image.width = pbr->width;
            image.height = pbr->height;
            image.pixels = pbr->pixels;
            image.normal_strength = pbr->normal_strength;
            image.specular_factor = pbr->specular_factor;
            std::memcpy(image.emissive_factor, pbr->emissive_factor,
                        sizeof(image.emissive_factor));
            image.maximum_emissive_luminance =
                maximum_emissive_luminance(image);
            images.push_back(std::move(image));
        }
        compiled_view_weapon_material_count =
            static_cast<uint32_t>(view_weapon.source.material_count);

        const size_t first_vertex = compiled_vertices.size();
        for (DxrSceneVertex vertex : view_weapon.vertices) {
            vertex.material_index += compiled_view_weapon_first_material;
            compiled_vertices.push_back(vertex);
        }
        CompiledInstance compiled_instance;
        compiled_instance.source_instance_id =
            view_weapon.sprite->source_record_id;
        compiled_instance.source_mesh_id =
            view_weapon.sprite->source_asset_id;
        compiled_instance.first_surface = static_cast<uint32_t>(
            compiled_surface_material_indices.size());
        compiled_instance.first_vertex = static_cast<uint32_t>(first_vertex);
        compiled_instance.vertex_count = static_cast<uint32_t>(
            view_weapon.vertices.size());
        compiled_instance.acceleration_class =
            SCENE_ACCELERATION_CLASS_DYNAMIC;
        compiled_instance.vertex_hash = view_weapon.vertex_hash;
        compiled_instance.view_weapon = true;
        compiled_instance.opaque = false;
        compiled_instances.push_back(compiled_instance);
        debug_output(
            "DXR view weapon: source-scale camera-relative geometry shares "
            "world depth and uses exact-face PBR PNG materials");
    }

    std::vector<DxrEmissiveTriangle> compiled_emitters;
    std::vector<float> compiled_material_emissive_bound;
    compiled_material_emissive_bound.reserve(images.size());
    for (const MaterialImage &image : images) {
        compiled_material_emissive_bound.push_back(
            image.maximum_emissive_luminance);
    }
    if (!compile_emissive_triangles(compiled_vertices,
                                    compiled_material_emissive_bound,
                                    compiled_emitters, error)) {
        return false;
    }

    std::vector<DxrSceneMaterial> compiled_materials(images.size());
    std::array<std::vector<uint8_t>,
               static_cast<size_t>(DxrMaterialChannel::count)> compiled_atlases;
    uint32_t atlas_width = 0;
    uint32_t atlas_height = 0;
    if (!images.empty()) {
        bool packed = false;
        for (uint32_t candidate_width = 512u;
             candidate_width <= atlas_maximum_extent; candidate_width *= 2u) {
            uint32_t x = 0u;
            uint32_t y = 0u;
            uint32_t row_height = 0u;
            bool fits = true;
            for (MaterialImage &image : images) {
                if (image.width > atlas_maximum_extent - 2u ||
                    image.height > atlas_maximum_extent - 2u) {
                    fits = false;
                    break;
                }
                const uint32_t packed_height =
                    material_atlas_packed_height(image);
                const uint32_t packed_width = image.width + 2u;
                if (packed_width > candidate_width) {
                    fits = false;
                    break;
                }
                if (x + packed_width > candidate_width) {
                    x = 0u;
                    y += row_height;
                    row_height = 0u;
                }
                if (y + packed_height > atlas_maximum_extent) {
                    fits = false;
                    break;
                }
                image.x = x + 1u;
                image.y = y + 1u;
                x += packed_width;
                row_height = std::max(row_height, packed_height);
            }
            if (fits) {
                atlas_width = candidate_width;
                atlas_height = std::max(1u, y + row_height);
                packed = true;
                break;
            }
        }
        if (!packed || static_cast<size_t>(atlas_width) >
                           std::numeric_limits<size_t>::max() / atlas_height / 4u) {
            error = "DXR PBR atlas exceeds the 8192-pixel limit";
            return false;
        }
        const size_t atlas_bytes =
            static_cast<size_t>(atlas_width) * atlas_height * 4u;
        for (auto &atlas : compiled_atlases) {
            atlas.assign(atlas_bytes, 0u);
        }
        for (size_t image_index = 0; image_index < images.size(); ++image_index) {
            const MaterialImage &image = images[image_index];
            for (size_t channel = 0;
                 channel < static_cast<size_t>(DxrMaterialChannel::count);
                 ++channel) {
                for (uint32_t level = 0u; level < image.mip_count; ++level) {
                    const uint32_t level_width =
                        material_mip::level_extent(image.width, level);
                    const uint32_t level_height =
                        material_mip::level_extent(image.height, level);
                    const uint32_t level_y = image.y +
                        (image.material_mips ?
                             material_mip::wrapped_level_y_offset(
                                 image.height, level) :
                             0u);
                    const std::vector<uint8_t> &level_pixels =
                        image.material_mips ?
                            image.mip_pixels[channel][level] :
                            image.pixels[channel];
                    copy_wrapped_material_level(
                        compiled_atlases[channel], atlas_width,
                        image.x, level_y, level_pixels,
                        level_width, level_height);
                }
            }
            DxrSceneMaterial &material = compiled_materials[image_index];
            material.atlas_x = image.x;
            material.atlas_y = image.y;
            material.width = image.width;
            material.height = image.height;
            material.mip_count = image.mip_count;
            material.normal_strength = image.normal_strength;
            material.specular_factor = image.specular_factor;
            std::memcpy(material.emissive, image.emissive_factor,
                        sizeof(material.emissive));
        }
    }

    vertices_ = std::move(compiled_vertices);
    materials_ = std::move(compiled_materials);
    emissive_triangles_ = std::move(compiled_emitters);
    light_grid_layout_hash_ =
        compute_light_grid_layout_hash(emissive_triangles_);
    emitter_state_hash_ =
        compute_emitter_state_hash(emissive_triangles_, vertices_);
    surface_material_indices_ =
        std::move(compiled_surface_material_indices);
    material_emissive_bound_ =
        std::move(compiled_material_emissive_bound);
    view_weapon_first_material_ = compiled_view_weapon_first_material;
    view_weapon_material_count_ = compiled_view_weapon_material_count;
    bitmap_material_indices_ =
        std::move(compiled_bitmap_material_indices);
    vector_material_indices_ =
        std::move(compiled_vector_material_indices);
    instances_ = std::move(compiled_instances);
    blas_update_pending_.assign(instances_.size(), false);
    atlas_pixels_ = std::move(compiled_atlases);
    atlas_width_ = atlas_width;
    atlas_height_ = atlas_height;
    scene_hashes_ = hashes;
    has_hashes_ = true;
    gpu_build_pending_ = true;
    gpu_geometry_update_pending_ = false;
    geometry_update_count_ = 0;
    ++rebuild_count_;
    return true;
}

bool DxrScene::compile_geometry_update(const SceneFrame &frame,
                                       const DxrViewWeaponCompilation &view_weapon,
                                       const DxrWorldBitmapCompilation &world_bitmaps,
                                       const DxrWorldVectorCompilation &world_vectors,
                                       bool &static_changed,
                                       std::string &error)
{
    std::vector<DxrSceneVertex> compiled_vertices = vertices_;
    std::vector<CompiledInstance> compiled_instances;
    std::vector<bool> compiled_updates;
    size_t surface_cursor = 0;
    size_t instance_cursor = 0;

    static_changed = false;
    compiled_instances.reserve(instances_.size());
    compiled_updates.reserve(instances_.size());
    for (size_t command_index = 0; command_index < frame.count;
         ++command_index) {
        const SceneCommand &command = frame.commands[command_index];
        if (command.type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        const SceneGeometryInstance &instance = command.data.geometry_instance;
        const SceneMesh &mesh = instance.mesh;
        if (!mesh.surfaces && mesh.surface_count != 0u) {
            error = "SceneFrame geometry instance has no surface array";
            return false;
        }
        if (mesh.surface_count == 0u) {
            continue;
        }
        if (instance_cursor >= instances_.size()) {
            error = "DXR geometry-only update added a BLAS instance";
            return false;
        }
        CompiledInstance compiled_instance = instances_[instance_cursor];
        compiled_instance.vertex_hash =
            dxr_scene_instance_vertex_hash(instance);
        const CompiledInstance &previous = instances_[instance_cursor];
        if (instance.source_instance_id != previous.source_instance_id ||
            mesh.source_mesh_id != previous.source_mesh_id ||
            mesh.acceleration_class != previous.acceleration_class ||
            surface_cursor != previous.first_surface ||
            mesh.surface_count != previous.surface_count ||
            surface_cursor + mesh.surface_count >
                surface_material_indices_.size()) {
            error = "DXR geometry-only update changed the BLAS layout";
            return false;
        }
        const bool instance_changed =
            compiled_instance.vertex_hash != previous.vertex_hash;
        if (instance_changed && compiled_instance.acceleration_class ==
                                    SCENE_ACCELERATION_CLASS_STATIC) {
            static_changed = true;
            return true;
        }
        if (instance_changed) {
            std::vector<DxrSceneVertex> updated_vertices;
            updated_vertices.reserve(previous.vertex_count);
            for (uint32_t surface_index = 0;
                 surface_index < mesh.surface_count; ++surface_index) {
                const uint32_t material_index = surface_material_indices_[
                    surface_cursor + surface_index];
                if (material_index >= materials_.size()) {
                    error = "DXR geometry-only update references an invalid material";
                    return false;
                }
                if (!append_geometry_vertices(
                        mesh.surfaces[surface_index].geometry,
                        material_index,
                        materials_[material_index].width,
                        materials_[material_index].height,
                        updated_vertices, error)) {
                    return false;
                }
            }
            if (updated_vertices.size() != previous.vertex_count) {
                error = "DXR dynamic BLAS update changed its vertex count";
                return false;
            }
            std::copy(updated_vertices.begin(), updated_vertices.end(),
                      compiled_vertices.begin() + previous.first_vertex);
        }
        compiled_instances.push_back(compiled_instance);
        compiled_updates.push_back(instance_changed);
        surface_cursor += mesh.surface_count;
        ++instance_cursor;
    }
    for (size_t bitmap_index = 0; bitmap_index < world_bitmaps.instances.size();
         ++bitmap_index) {
        const DxrWorldBitmapInstance &bitmap =
            world_bitmaps.instances[bitmap_index];
        const bool pool_slot = bitmap_index >= world_bitmaps.pool_first;
        if (instance_cursor >= instances_.size()) {
            error = "DXR geometry-only update added a world-bitmap BLAS";
            return false;
        }
        const CompiledInstance &previous = instances_[instance_cursor];
        if (!previous.world_bitmap || previous.view_weapon ||
            previous.bitmap_pool != pool_slot ||
            previous.acceleration_class != SCENE_ACCELERATION_CLASS_DYNAMIC ||
            previous.vertex_count != 6u) {
            error = "DXR geometry-only update changed the world-bitmap layout";
            return false;
        }
        CompiledInstance compiled_instance = previous;
        compiled_instance.vertex_hash = bitmap.vertex_hash;
        const bool instance_changed =
            compiled_instance.vertex_hash != previous.vertex_hash;
        if (!bitmap.occupied) {
            /*
             * A slot whose projectile has gone. Collapse its six vertices onto
             * the point its last occupant left, which costs one refit of a
             * two-triangle BLAS and leaves nothing hittable behind. The vertex
             * hash a vacant slot reports is a constant, so this happens once on
             * the frame it empties and never again while it stays empty.
             */
            if (instance_changed) {
                DxrSceneVertex collapsed =
                    compiled_vertices[previous.first_vertex];
                for (uint32_t vertex_index = 0;
                     vertex_index < previous.vertex_count; ++vertex_index) {
                    compiled_vertices[previous.first_vertex + vertex_index] =
                        collapsed;
                }
            }
            compiled_instances.push_back(compiled_instance);
            compiled_updates.push_back(instance_changed);
            ++instance_cursor;
            continue;
        }
        const SceneSprite &sprite = bitmap.instance->sprite;
        /*
         * A pool slot has no identity to check: its occupant is whichever
         * object the source left in that ObjT record, and checking identity
         * here is exactly what the pool exists to avoid.
         */
        if (previous.vertex_count != bitmap.vertices.size()) {
            error = "DXR geometry-only update changed the world-bitmap layout";
            return false;
        }
        const auto material_key = std::make_tuple(
            sprite.source_asset_id, static_cast<uint32_t>(sprite.frame_index),
            bitmap.source.material_mode);
        const auto material = bitmap_material_indices_.find(material_key);
        if (material == bitmap_material_indices_.end()) {
            if (!pool_slot) {
                error = "DXR bitmap animation selected a material outside its preloaded atlas";
                return false;
            }
            /*
             * The first time an object of this kind appears its PBR maps are
             * not in the atlas yet. Rebuilding is the only way to add them, and
             * it happens once per kind per level rather than once per shot or
             * once per death.
             */
            static_changed = true;
            return true;
        }
        /*
         * A pool slot that changed hands has to be rewritten even if the new
         * occupant happens to hash the same, because the material index is what
         * carries the occupant's identity into the vertex buffer.
         */
        const bool occupant_changed = pool_slot &&
            (previous.source_instance_id != sprite.source_record_id ||
             previous.source_mesh_id != bitmap.instance->source_mesh_id);
        if (pool_slot) {
            compiled_instance.source_instance_id = sprite.source_record_id;
            compiled_instance.source_mesh_id = bitmap.instance->source_mesh_id;
        }
        const bool slot_changed = instance_changed || occupant_changed;
        if (slot_changed) {
            for (size_t vertex_index = 0;
                 vertex_index < bitmap.vertices.size(); ++vertex_index) {
                DxrSceneVertex vertex = bitmap.vertices[vertex_index];
                vertex.material_index = material->second;
                compiled_vertices[previous.first_vertex + vertex_index] = vertex;
            }
        }
        compiled_instances.push_back(compiled_instance);
        compiled_updates.push_back(slot_changed);
        ++instance_cursor;
    }
    for (const DxrWorldVectorInstance &vector : world_vectors.instances) {
        if (instance_cursor >= instances_.size()) {
            error = "DXR geometry-only update added a world-vector BLAS";
            return false;
        }
        const CompiledInstance &previous = instances_[instance_cursor];
        const SceneSprite &sprite = vector.instance->sprite;
        if (!previous.world_vector || previous.world_bitmap ||
            previous.view_weapon ||
            previous.source_instance_id != sprite.source_record_id ||
            previous.source_mesh_id != vector.instance->source_mesh_id ||
            previous.acceleration_class != SCENE_ACCELERATION_CLASS_DYNAMIC ||
            previous.vertex_count != vector.vertices.size()) {
            error = "DXR geometry-only update changed the world-vector layout";
            return false;
        }
        CompiledInstance compiled_instance = previous;
        compiled_instance.vertex_hash = vector.vertex_hash;
        const bool instance_changed =
            compiled_instance.vertex_hash != previous.vertex_hash;
        if (instance_changed) {
            for (size_t vertex_index = 0;
                 vertex_index < vector.vertices.size(); ++vertex_index) {
                DxrSceneVertex vertex = vector.vertices[vertex_index];
                if (vertex.material_index >= vector.source.material_count) {
                    error = "DXR updated world vector references an invalid material";
                    return false;
                }
                const SourceVectorSceneMaterial &source =
                    vector.source.materials[vertex.material_index];
                const auto key = std::make_tuple(
                    sprite.source_asset_id, source.source_map_offset,
                    source.minimum_u, source.maximum_u,
                    source.minimum_v, source.maximum_v, source.glare);
                const auto material = vector_material_indices_.find(key);
                if (material == vector_material_indices_.end()) {
                    error = "DXR world-vector animation selected an unpacked PBR material";
                    return false;
                }
                vertex.material_index = material->second;
                compiled_vertices[previous.first_vertex + vertex_index] = vertex;
            }
        }
        compiled_instances.push_back(compiled_instance);
        compiled_updates.push_back(instance_changed);
        ++instance_cursor;
    }
    if (view_weapon.sprite) {
        if (instance_cursor >= instances_.size()) {
            error = "DXR geometry-only update added the view-weapon BLAS";
            return false;
        }
        const CompiledInstance &previous = instances_[instance_cursor];
        if (!previous.view_weapon || previous.acceleration_class !=
                SCENE_ACCELERATION_CLASS_DYNAMIC ||
            previous.vertex_count != view_weapon.vertices.size() ||
            view_weapon_material_count_ !=
                view_weapon.source.material_count ||
            view_weapon_first_material_ > materials_.size() ||
            view_weapon_material_count_ >
                materials_.size() - view_weapon_first_material_) {
            error = "DXR geometry-only update changed the view-weapon layout";
            return false;
        }
        CompiledInstance compiled_instance = previous;
        compiled_instance.vertex_hash = view_weapon.vertex_hash;
        const bool instance_changed =
            compiled_instance.vertex_hash != previous.vertex_hash;
        if (instance_changed) {
            for (size_t vertex_index = 0;
                 vertex_index < view_weapon.vertices.size(); ++vertex_index) {
                DxrSceneVertex vertex = view_weapon.vertices[vertex_index];
                vertex.material_index += view_weapon_first_material_;
                compiled_vertices[previous.first_vertex + vertex_index] = vertex;
            }
        }
        compiled_instances.push_back(compiled_instance);
        compiled_updates.push_back(instance_changed);
        ++instance_cursor;
    }
    if (surface_cursor != surface_material_indices_.size() ||
        instance_cursor != instances_.size()) {
        error = "DXR geometry-only update did not preserve the scene layout";
        return false;
    }

    std::vector<DxrEmissiveTriangle> compiled_emitters;
    if (!compile_emissive_triangles(compiled_vertices,
                                    material_emissive_bound_,
                                    compiled_emitters, error)) {
        return false;
    }
    if (!emitter_history_layout_compatible<DxrEmissiveTriangle>(
            emissive_triangles_, compiled_emitters)) {
        /*
         * Compaction can shift every later emitter when a dynamic triangle
         * becomes emissive, non-emissive, or degenerate. Do not let an old
         * reservoir index silently select a different triangle in that case.
         */
        history_reset_pending_ = true;
    }
    light_grid_layout_hash_ =
        compute_light_grid_layout_hash(compiled_emitters);
    vertices_ = std::move(compiled_vertices);
    instances_ = std::move(compiled_instances);
    emissive_triangles_ = std::move(compiled_emitters);
    emitter_state_hash_ =
        compute_emitter_state_hash(emissive_triangles_, vertices_);
    blas_update_pending_ = std::move(compiled_updates);
    return true;
}

bool DxrScene::record_build(ID3D12Device5 *device,
                            ID3D12GraphicsCommandList4 *command_list,
                            uint32_t frame_slot,
                            D3D12_CPU_DESCRIPTOR_HANDLE tlas_descriptor,
                            const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,
                                             static_cast<size_t>(
                                                 DxrMaterialChannel::count)>
                                &atlas_descriptors,
                            std::string &error)
{
    if (gpu_geometry_update_pending_) {
        if (!device || !command_list || frame_slot >= geometry_uploads_.size() ||
            !geometry_uploads_[frame_slot] || !vertex_buffer_ ||
            !previous_vertex_buffer_ ||
            !emitter_buffer_ || !blas_scratch_ || !tlas_scratch_ || !tlas_ ||
            !instance_upload_ ||
            blases_.size() != instances_.size() ||
            blas_update_pending_.size() != instances_.size()) {
            error = "DXR geometry update received incomplete reusable GPU state";
            return false;
        }
        const UINT64 vertex_bytes =
            static_cast<UINT64>(vertices_.size()) * sizeof(vertices_[0]);
        const UINT64 emitter_bytes = std::max<UINT64>(
            sizeof(DxrEmissiveTriangle),
            static_cast<UINT64>(triangle_count()) *
                sizeof(DxrEmissiveTriangle));
        const UINT64 emitter_offset =
            (vertex_bytes + 255u) & ~UINT64_C(255);
        void *mapped = nullptr;
        D3D12_RANGE no_read = {0, 0};
        HRESULT result =
            geometry_uploads_[frame_slot]->Map(0, &no_read, &mapped);
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Resource::Map(dynamic scene upload)", result);
            return false;
        }
        std::memcpy(mapped, vertices_.data(), static_cast<size_t>(vertex_bytes));
        std::memset(static_cast<uint8_t *>(mapped) + emitter_offset, 0,
                    static_cast<size_t>(emitter_bytes));
        if (!emissive_triangles_.empty()) {
            std::memcpy(static_cast<uint8_t *>(mapped) + emitter_offset,
                        emissive_triangles_.data(),
                        emissive_triangles_.size() *
                            sizeof(emissive_triangles_[0]));
        }
        geometry_uploads_[frame_slot]->Unmap(0, nullptr);

        const std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
            transition(vertex_buffer_.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST),
            transition(emitter_buffer_.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_COPY_DEST),
        };
        command_list->ResourceBarrier(static_cast<UINT>(to_copy.size()),
                                      to_copy.data());
        command_list->CopyBufferRegion(vertex_buffer_.Get(), 0,
                                       geometry_uploads_[frame_slot].Get(), 0,
                                       vertex_bytes);
        command_list->CopyBufferRegion(
            emitter_buffer_.Get(), 0, geometry_uploads_[frame_slot].Get(),
            emitter_offset, emitter_bytes);
        const std::array<D3D12_RESOURCE_BARRIER, 2> to_read = {
            transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            transition(emitter_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        };
        command_list->ResourceBarrier(static_cast<UINT>(to_read.size()),
                                      to_read.data());

        /*
         * A Gouraud-only update rewrites the vertex buffer's authored emission
         * scale without moving a triangle, so it legitimately refits nothing.
         * Every acceleration structure is then left alone: refitting against
         * identical positions costs time and degrades traversal quality.
         */
        uint32_t updated_blas_count = 0;
        for (size_t index = 0; index < instances_.size(); ++index) {
            if (!blas_update_pending_[index]) {
                continue;
            }
            const CompiledInstance &instance = instances_[index];
            if (instance.acceleration_class !=
                SCENE_ACCELERATION_CLASS_DYNAMIC) {
                error = "DXR attempted to refit a static BLAS";
                return false;
            }
            D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags = instance.opaque ?
                D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE :
                D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.VertexCount = instance.vertex_count;
            geometry.Triangles.VertexBuffer.StartAddress = vertex_address() +
                static_cast<UINT64>(instance.first_vertex) *
                    sizeof(DxrSceneVertex);
            geometry.Triangles.VertexBuffer.StrideInBytes =
                sizeof(DxrSceneVertex);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
            inputs.Type =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.Flags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = &geometry;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
            build.Inputs = inputs;
            build.SourceAccelerationStructureData =
                blases_[index]->GetGPUVirtualAddress();
            build.ScratchAccelerationStructureData =
                blas_scratch_->GetGPUVirtualAddress();
            build.DestAccelerationStructureData =
                blases_[index]->GetGPUVirtualAddress();
            command_list->BuildRaytracingAccelerationStructure(
                &build, 0, nullptr);
            const std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {
                uav_barrier(blas_scratch_.Get()),
                uav_barrier(blases_[index].Get()),
            };
            command_list->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                          barriers.data());
            ++updated_blas_count;
        }
        if (updated_blas_count != 0u) {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
            tlas_inputs.Type =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            tlas_inputs.Flags =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            tlas_inputs.NumDescs = static_cast<UINT>(blases_.size());
            tlas_inputs.InstanceDescs =
                instance_upload_->GetGPUVirtualAddress();
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build = {};
            tlas_build.Inputs = tlas_inputs;
            tlas_build.SourceAccelerationStructureData =
                tlas_->GetGPUVirtualAddress();
            tlas_build.ScratchAccelerationStructureData =
                tlas_scratch_->GetGPUVirtualAddress();
            tlas_build.DestAccelerationStructureData =
                tlas_->GetGPUVirtualAddress();
            command_list->BuildRaytracingAccelerationStructure(
                &tlas_build, 0, nullptr);
            const D3D12_RESOURCE_BARRIER tlas_barrier =
                uav_barrier(tlas_.Get());
            command_list->ResourceBarrier(1, &tlas_barrier);
        }

        gpu_geometry_update_pending_ = false;
        std::fill(blas_update_pending_.begin(), blas_update_pending_.end(),
                  false);
        if (geometry_update_count_++ == 0u) {
            debug_output(
                "DXR dynamic update: retained PBR atlases and refit " +
                std::to_string(updated_blas_count) + " BLAS instance(s)");
        }
        return true;
    }
    if (!gpu_build_pending_) {
        return true;
    }
    release_gpu();
    gpu_build_pending_ = false;
    if (vertices_.empty()) {
        return true;
    }
    const bool missing_atlas = std::any_of(
        atlas_pixels_.begin(), atlas_pixels_.end(),
        [](const std::vector<uint8_t> &pixels) { return pixels.empty(); });
    if (!device || !command_list || materials_.empty() || missing_atlas ||
        atlas_width_ == 0u || atlas_height_ == 0u) {
        error = "DXR scene build received incomplete CPU or D3D12 state";
        return false;
    }

    const UINT64 vertex_bytes =
        static_cast<UINT64>(vertices_.size()) * sizeof(vertices_[0]);
    const UINT64 material_bytes =
        static_cast<UINT64>(materials_.size()) * sizeof(materials_[0]);
    const UINT64 emitter_bytes = std::max<UINT64>(
        sizeof(DxrEmissiveTriangle),
        static_cast<UINT64>(triangle_count()) *
            sizeof(DxrEmissiveTriangle));
    const UINT64 material_offset = (vertex_bytes + 255u) & ~UINT64_C(255);
    const UINT64 emitter_offset =
        (material_offset + material_bytes + 255u) & ~UINT64_C(255);
    const UINT64 upload_bytes = emitter_offset + emitter_bytes;
    const UINT64 geometry_emitter_offset =
        (vertex_bytes + 255u) & ~UINT64_C(255);
    const UINT64 geometry_upload_bytes = geometry_emitter_offset + emitter_bytes;
    if (!create_buffer(device, vertex_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Vertices", vertex_buffer_, error) ||
        !create_buffer(device, vertex_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Previous Scene Vertices",
                       previous_vertex_buffer_, error) ||
        !create_buffer(device, material_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Materials", material_buffer_, error) ||
        !create_buffer(device, emitter_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Emissive Triangles", emitter_buffer_, error) ||
        !create_buffer(device, upload_bytes, D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                       L"AB3D2 DXR Scene Upload", upload_buffer_, error)) {
        return false;
    }
    for (size_t slot = 0; slot < geometry_uploads_.size(); ++slot) {
        wchar_t name[96] = {};
        (void)swprintf_s(name, L"AB3D2 DXR Dynamic Scene Upload %zu", slot);
        if (!create_buffer(device, geometry_upload_bytes,
                           D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_FLAG_NONE, name,
                           geometry_uploads_[slot], error)) {
            return false;
        }
    }
    void *mapped = nullptr;
    D3D12_RANGE no_read = {0, 0};
    HRESULT result = upload_buffer_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(scene upload)", result);
        return false;
    }
    std::memcpy(mapped, vertices_.data(), static_cast<size_t>(vertex_bytes));
    std::memcpy(static_cast<uint8_t *>(mapped) + material_offset,
                materials_.data(), static_cast<size_t>(material_bytes));
    std::memset(static_cast<uint8_t *>(mapped) + emitter_offset, 0,
                static_cast<size_t>(emitter_bytes));
    if (!emissive_triangles_.empty()) {
        std::memcpy(static_cast<uint8_t *>(mapped) + emitter_offset,
                    emissive_triangles_.data(),
                    emissive_triangles_.size() * sizeof(emissive_triangles_[0]));
    }
    upload_buffer_->Unmap(0, nullptr);
    command_list->CopyBufferRegion(vertex_buffer_.Get(), 0, upload_buffer_.Get(), 0,
                                   vertex_bytes);
    command_list->CopyBufferRegion(previous_vertex_buffer_.Get(), 0,
                                   upload_buffer_.Get(), 0, vertex_bytes);
    command_list->CopyBufferRegion(material_buffer_.Get(), 0, upload_buffer_.Get(),
                                   material_offset, material_bytes);
    command_list->CopyBufferRegion(emitter_buffer_.Get(), 0, upload_buffer_.Get(),
                                   emitter_offset, emitter_bytes);

    D3D12_RESOURCE_DESC texture_description = {};
    texture_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_description.Width = atlas_width_;
    texture_description.Height = atlas_height_;
    texture_description.DepthOrArraySize = 1;
    texture_description.MipLevels = 1;
    texture_description.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    texture_description.SampleDesc.Count = 1;
    texture_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const D3D12_HEAP_PROPERTIES default_heap =
        heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT row_count = 0;
    UINT64 row_bytes = 0;
    UINT64 total_bytes = 0;
    device->GetCopyableFootprints(&texture_description, 0, 1, 0, &footprint,
                                  &row_count, &row_bytes, &total_bytes);
    constexpr std::array<const wchar_t *, 5> texture_names = {
        L"AB3D2 DXR Base Color Atlas",
        L"AB3D2 DXR Normal Atlas",
        L"AB3D2 DXR Metalness Atlas",
        L"AB3D2 DXR Roughness Atlas",
        L"AB3D2 DXR Emissive Atlas",
    };
    constexpr std::array<const wchar_t *, 5> upload_names = {
        L"AB3D2 DXR Base Color Atlas Upload",
        L"AB3D2 DXR Normal Atlas Upload",
        L"AB3D2 DXR Metalness Atlas Upload",
        L"AB3D2 DXR Roughness Atlas Upload",
        L"AB3D2 DXR Emissive Atlas Upload",
    };
    for (size_t channel = 0; channel < atlas_textures_.size(); ++channel) {
        result = device->CreateCommittedResource(
            &default_heap, D3D12_HEAP_FLAG_NONE, &texture_description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&atlas_textures_[channel]));
        if (FAILED(result)) {
            error = hresult_error(
                "ID3D12Device::CreateCommittedResource(PBR atlas)", result);
            return false;
        }
        atlas_textures_[channel]->SetName(texture_names[channel]);
        if (!create_buffer(device, total_bytes, D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ,
                           D3D12_RESOURCE_FLAG_NONE, upload_names[channel],
                           atlas_uploads_[channel], error)) {
            return false;
        }
        result = atlas_uploads_[channel]->Map(0, &no_read, &mapped);
        if (FAILED(result)) {
            error = hresult_error("ID3D12Resource::Map(PBR atlas upload)", result);
            return false;
        }
        for (UINT row = 0; row < row_count; ++row) {
            std::memcpy(
                static_cast<uint8_t *>(mapped) + footprint.Offset +
                    static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                atlas_pixels_[channel].data() +
                    static_cast<size_t>(row) * atlas_width_ * 4u,
                static_cast<size_t>(atlas_width_) * 4u);
        }
        atlas_uploads_[channel]->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = atlas_textures_[channel].Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = atlas_uploads_[channel].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    std::array<D3D12_RESOURCE_BARRIER, 9> uploads = {
        transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(previous_vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(material_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(emitter_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[2].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[3].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(atlas_textures_[4].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    command_list->ResourceBarrier(static_cast<UINT>(uploads.size()), uploads.data());

    if (instances_.empty() || instances_.size() > UINT32_MAX) {
        error = "DXR scene build has no valid BLAS instances";
        return false;
    }
    std::vector<UINT64> blas_result_sizes(instances_.size());
    UINT64 blas_scratch_bytes = 0;
    for (size_t index = 0; index < instances_.size(); ++index) {
        const CompiledInstance &instance = instances_[index];
        D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = instance.opaque ?
            D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE :
            D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount = instance.vertex_count;
        geometry.Triangles.VertexBuffer.StartAddress = vertex_address() +
            static_cast<UINT64>(instance.first_vertex) * sizeof(DxrSceneVertex);
        geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrSceneVertex);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (instance.acceleration_class == SCENE_ACCELERATION_CLASS_DYNAMIC) {
            inputs.Flags |=
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }
        inputs.NumDescs = 1;
        inputs.pGeometryDescs = &geometry;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
        if (info.ResultDataMaxSizeInBytes == 0u ||
            info.ScratchDataSizeInBytes == 0u) {
            error = "DXR BLAS prebuild returned zero-sized storage";
            return false;
        }
        blas_result_sizes[index] = info.ResultDataMaxSizeInBytes;
        blas_scratch_bytes = std::max(
            blas_scratch_bytes,
            std::max(info.ScratchDataSizeInBytes,
                     info.UpdateScratchDataSizeInBytes));
    }
    if (!create_buffer(device, blas_scratch_bytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR BLAS Scratch", blas_scratch_, error)) {
        return false;
    }
    blases_.resize(instances_.size());
    for (size_t index = 0; index < blases_.size(); ++index) {
        wchar_t name[96] = {};
        (void)swprintf_s(name, L"AB3D2 DXR BLAS %zu", index);
        if (!create_buffer(
                device, blas_result_sizes[index], D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, name,
                blases_[index], error)) {
            return false;
        }
    }
    const D3D12_RESOURCE_BARRIER blas_scratch_state = transition(
        blas_scratch_.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &blas_scratch_state);
    for (size_t index = 0; index < instances_.size(); ++index) {
        const CompiledInstance &instance = instances_[index];
        D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = instance.opaque ?
            D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE :
            D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometry.Triangles.VertexCount = instance.vertex_count;
        geometry.Triangles.VertexBuffer.StartAddress = vertex_address() +
            static_cast<UINT64>(instance.first_vertex) * sizeof(DxrSceneVertex);
        geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrSceneVertex);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.Flags =
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (instance.acceleration_class == SCENE_ACCELERATION_CLASS_DYNAMIC) {
            inputs.Flags |=
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }
        inputs.NumDescs = 1;
        inputs.pGeometryDescs = &geometry;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
        build.Inputs = inputs;
        build.ScratchAccelerationStructureData =
            blas_scratch_->GetGPUVirtualAddress();
        build.DestAccelerationStructureData =
            blases_[index]->GetGPUVirtualAddress();
        command_list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        const std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {
            uav_barrier(blas_scratch_.Get()),
            uav_barrier(blases_[index].Get()),
        };
        command_list->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                      barriers.data());
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    tlas_inputs.NumDescs = static_cast<UINT>(blases_.size());
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    const UINT64 tlas_scratch_bytes = std::max(
        tlas_info.ScratchDataSizeInBytes,
        tlas_info.UpdateScratchDataSizeInBytes);
    const UINT64 instance_bytes = static_cast<UINT64>(blases_.size()) *
        sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    if (tlas_info.ResultDataMaxSizeInBytes == 0u ||
        tlas_scratch_bytes == 0u ||
        !create_buffer(device, tlas_scratch_bytes,
                       D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR TLAS Scratch", tlas_scratch_, error) ||
        !create_buffer(device, tlas_info.ResultDataMaxSizeInBytes,
                       D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       L"AB3D2 DXR World TLAS", tlas_, error) ||
        !create_buffer(device, instance_bytes,
                       D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, L"AB3D2 DXR TLAS Instances",
                       instance_upload_, error)) {
        if (error.empty()) {
            error = "DXR TLAS prebuild returned zero-sized storage";
        }
        return false;
    }
    const D3D12_RESOURCE_BARRIER tlas_scratch_state = transition(
        tlas_scratch_.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command_list->ResourceBarrier(1, &tlas_scratch_state);
    result = instance_upload_->Map(0, &no_read, &mapped);
    if (FAILED(result)) {
        error = hresult_error("ID3D12Resource::Map(TLAS instance)", result);
        return false;
    }
    auto *instance_descriptions =
        static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(mapped);
    std::memset(instance_descriptions, 0, static_cast<size_t>(instance_bytes));
    for (size_t index = 0; index < instances_.size(); ++index) {
        D3D12_RAYTRACING_INSTANCE_DESC &description =
            instance_descriptions[index];
        description.Transform[0][0] = 1.0f;
        description.Transform[1][1] = 1.0f;
        description.Transform[2][2] = 1.0f;
        description.InstanceID = instances_[index].first_vertex / 3u;
        /* World and camera-attached weapon geometry share one depth-ordered
         * scene.  Primary, secondary, and visibility rays all see both. */
        description.InstanceMask = world_instance_mask;
        description.Flags =
            D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        description.AccelerationStructure =
            blases_[index]->GetGPUVirtualAddress();
    }
    instance_upload_->Unmap(0, nullptr);
    tlas_inputs.InstanceDescs = instance_upload_->GetGPUVirtualAddress();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build = {};
    tlas_build.Inputs = tlas_inputs;
    tlas_build.ScratchAccelerationStructureData =
        tlas_scratch_->GetGPUVirtualAddress();
    tlas_build.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    command_list->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER tlas_barrier = uav_barrier(tlas_.Get());
    command_list->ResourceBarrier(1, &tlas_barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC tlas_view = {};
    tlas_view.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    tlas_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    tlas_view.RaytracingAccelerationStructure.Location =
        tlas_->GetGPUVirtualAddress();
    device->CreateShaderResourceView(nullptr, &tlas_view, tlas_descriptor);
    D3D12_SHADER_RESOURCE_VIEW_DESC atlas_view = {};
    atlas_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    atlas_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    atlas_view.Texture2D.MipLevels = 1;
    for (size_t channel = 0; channel < atlas_textures_.size(); ++channel) {
        atlas_view.Format =
            (channel == static_cast<size_t>(DxrMaterialChannel::base_color) ||
             channel == static_cast<size_t>(DxrMaterialChannel::emissive)) ?
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        device->CreateShaderResourceView(atlas_textures_[channel].Get(), &atlas_view,
                                         atlas_descriptors[channel]);
    }
    debug_output("DXR SceneFrame build: " + std::to_string(triangle_count()) +
                 " triangles, " + std::to_string(materials_.size()) +
                 " PBR-capable materials, " +
                 std::to_string(emitter_count()) + " emissive triangles, " +
                 std::to_string(blases_.size()) + " BLAS instances");
    return true;
}

bool DxrScene::record_promote_vertex_history(
    ID3D12GraphicsCommandList4 *command_list, std::string &error)
{
    if (!command_list || !vertex_buffer_ || !previous_vertex_buffer_ ||
        vertices_.empty()) {
        error = "DXR vertex-history promotion received incomplete scene state";
        return false;
    }
    const std::array<D3D12_RESOURCE_BARRIER, 2> to_copy = {
        transition(vertex_buffer_.Get(),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(previous_vertex_buffer_.Get(),
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COPY_DEST),
    };
    command_list->ResourceBarrier(static_cast<UINT>(to_copy.size()),
                                  to_copy.data());
    command_list->CopyResource(previous_vertex_buffer_.Get(),
                               vertex_buffer_.Get());
    const std::array<D3D12_RESOURCE_BARRIER, 2> to_read = {
        transition(vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(previous_vertex_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
    };
    command_list->ResourceBarrier(static_cast<UINT>(to_read.size()),
                                  to_read.data());
    return true;
}

}  // namespace ab3d2::dxr
