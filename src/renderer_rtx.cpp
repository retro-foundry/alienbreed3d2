#include "renderer_rtx.h"

#include "renderer_dxr/dxr_renderer.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct RendererRtx {
    std::unique_ptr<ab3d2::dxr::DxrRenderer> implementation;
};

namespace {

void copy_error(char *destination, size_t destination_size, const std::string &message)
{
    if (destination && destination_size != 0) {
        (void)std::snprintf(destination, destination_size, "%s", message.c_str());
    }
}

void exception_error(char *destination, size_t destination_size,
                     const char *operation, const std::exception &exception)
{
    copy_error(destination, destination_size,
               std::string(operation) + " raised an exception: " + exception.what());
}

}  // namespace

extern "C" RendererRtx *renderer_rtx_create(
    int window_width, int window_height, const char *window_title,
    int desktop_window, int hidden_window, uint8_t world_light_tessellation,
    const RendererRayTracingOptions *options,
    char *error, size_t error_size)
{
    try {
        std::unique_ptr<RendererRtx> renderer(new (std::nothrow) RendererRtx());
        if (!renderer) {
            copy_error(error, error_size, "D3D12/DXR renderer allocation failed");
            return nullptr;
        }
        renderer->implementation = std::make_unique<ab3d2::dxr::DxrRenderer>();
        std::string implementation_error;
        RendererRayTracingOptions defaults = {};
        defaults.output = RENDERER_OUTPUT_SDR;
        if (!renderer->implementation->initialize(
                window_width, window_height, window_title,
                desktop_window != 0, hidden_window != 0,
                world_light_tessellation, options ? *options : defaults,
                implementation_error)) {
            copy_error(error, error_size, implementation_error);
            return nullptr;
        }
        return renderer.release();
    } catch (const std::exception &exception) {
        exception_error(error, error_size, "D3D12/DXR renderer creation", exception);
        return nullptr;
    }
}

extern "C" void renderer_rtx_destroy(RendererRtx *renderer)
{
    delete renderer;
}

extern "C" int renderer_rtx_prepare_resources(
    RendererRtx *renderer, const RendererResourceCatalog *catalog,
    size_t *out_prepared_vector_material_count, char *error, size_t error_size)
{
    if (!renderer || !renderer->implementation || !catalog ||
        !out_prepared_vector_material_count) {
        copy_error(error, error_size,
                   "D3D12/DXR resource preparation received invalid state");
        return 0;
    }
    *out_prepared_vector_material_count = 0;
    /*
     * Decode the level's vector-model PBR art now rather than on first use.
     * DxrMaterialLibrary resolves lazily, so without this the first frame that
     * shows a weapon-firing pose, a projectile or a hit reaction pays a package
     * seek and five PNG decodes mid-frame - the hitch on the first shot and the
     * first kill.
     */
    if (catalog->vector_resource_count != 0u && !catalog->vector_resources) {
        copy_error(error, error_size,
                   "D3D12/DXR resource preparation received an invalid vector catalog");
        return 0;
    }
    try {
        std::vector<uint32_t> asset_ids;
        asset_ids.reserve(catalog->vector_resource_count);
        for (size_t index = 0; index < catalog->vector_resource_count; ++index) {
            asset_ids.push_back(catalog->vector_resources[index].source_asset_id);
        }
        std::string implementation_error;
        size_t prepared = 0u;
        if (!renderer->implementation->prepare_vector_materials(
                asset_ids.empty() ? nullptr : asset_ids.data(),
                asset_ids.size(), prepared, implementation_error)) {
            copy_error(error, error_size, implementation_error);
            return 0;
        }
        size_t prepared_bitmaps = 0u;
        if (!renderer->implementation->prepare_bitmap_materials(
                catalog->bitmap_asset_count, prepared_bitmaps,
                implementation_error)) {
            copy_error(error, error_size, implementation_error);
            return 0;
        }
        *out_prepared_vector_material_count = prepared + prepared_bitmaps;
        return 1;
    } catch (const std::exception &exception) {
        exception_error(error, error_size, "D3D12/DXR resource preparation",
                        exception);
        return 0;
    }
}

extern "C" int renderer_rtx_get_presentation_size(
    const RendererRtx *renderer, int *out_width, int *out_height)
{
    if (!renderer || !renderer->implementation || !out_width || !out_height) {
        return 0;
    }
    return renderer->implementation->presentation_size(*out_width, *out_height) ? 1 : 0;
}

extern "C" int renderer_rtx_wait_for_present(
    RendererRtx *renderer, char *error, size_t error_size)
{
    if (!renderer || !renderer->implementation) {
        copy_error(error, error_size,
                   "D3D12/DXR frame-latency wait received invalid state");
        return 0;
    }
    try {
        std::string implementation_error;
        if (!renderer->implementation->wait_for_present(implementation_error)) {
            copy_error(error, error_size, implementation_error);
            return 0;
        }
        return 1;
    } catch (const std::exception &exception) {
        exception_error(error, error_size, "D3D12/DXR frame-latency wait",
                        exception);
        return 0;
    }
}

extern "C" int renderer_rtx_present(
    RendererRtx *renderer, const SceneFrame *frame, const RenderView *view,
    char *error, size_t error_size)
{
    if (!renderer || !renderer->implementation || !frame || !view) {
        copy_error(error, error_size,
                   "D3D12/DXR presentation received invalid state");
        return 0;
    }
    try {
        std::string implementation_error;
        if (!renderer->implementation->present(*frame, *view, implementation_error)) {
            copy_error(error, error_size, implementation_error);
            return 0;
        }
        return 1;
    } catch (const std::exception &exception) {
        exception_error(error, error_size, "D3D12/DXR presentation",
                        exception);
        return 0;
    }
}

extern "C" size_t renderer_rtx_last_ui_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0;
}

extern "C" size_t renderer_rtx_last_view_weapon_coverage(const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_view_weapon_coverage() : 0u;
}

extern "C" uint64_t renderer_rtx_last_view_weapon_rgb_checksum(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_view_weapon_rgb_checksum() :
        UINT64_C(0);
}

extern "C" size_t renderer_rtx_last_world_bitmap_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_world_bitmap_coverage() : 0u;
}

extern "C" size_t renderer_rtx_last_world_vector_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_world_vector_coverage() : 0u;
}

extern "C" size_t renderer_rtx_last_world_additive_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_world_additive_coverage() : 0u;
}

extern "C" size_t renderer_rtx_last_direct_diffuse_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_direct_diffuse_coverage() : 0u;
}

extern "C" size_t renderer_rtx_last_direct_specular_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_direct_specular_coverage() : 0u;
}

extern "C" size_t renderer_rtx_last_invalid_lighting_or_guide_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_invalid_lighting_or_guide_pixels() : 0u;
}

extern "C" size_t renderer_rtx_last_smooth_specular_coverage(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_smooth_specular_coverage() : 0u;
}

extern "C" int renderer_rtx_enable_noisy_radiance_readback(
    RendererRtx *renderer)
{
    return renderer && renderer->implementation &&
        renderer->implementation->enable_noisy_radiance_readback() ? 1 : 0;
}

extern "C" int renderer_rtx_select_radiance_channel(
    RendererRtx *renderer, RendererRtxRadianceChannel channel)
{
    return renderer && renderer->implementation &&
        renderer->implementation->select_radiance_channel(
            static_cast<uint32_t>(channel)) ? 1 : 0;
}

extern "C" size_t renderer_rtx_last_noisy_radiance_value_count(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_noisy_radiance_value_count() : 0u;
}

extern "C" int renderer_rtx_copy_last_noisy_radiance(
    const RendererRtx *renderer, uint16_t *out_values, size_t value_count)
{
    return renderer && renderer->implementation &&
        renderer->implementation->copy_last_noisy_radiance(
            out_values, value_count) ? 1 : 0;
}

extern "C" int renderer_rtx_active_ray_tracing_options(
    const RendererRtx *renderer, RendererRayTracingOptions *out_options)
{
    if (!renderer || !renderer->implementation || !out_options) {
        return 0;
    }
    return renderer->implementation->active_ray_tracing_options(*out_options) ?
        1 : 0;
}

extern "C" size_t renderer_rtx_last_projectile_coverage(const RendererRtx *renderer)
{
    (void)renderer;
    return 0;
}

extern "C" uint64_t renderer_rtx_last_frame_rgb_checksum(const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_rgb_checksum() : UINT64_C(0);
}

extern "C" double renderer_rtx_last_frame_delta(const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_frame_delta() : -1.0;
}

extern "C" double renderer_rtx_last_frame_reprojected_delta(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_reprojected_frame_delta() : -1.0;
}

extern "C" uint64_t renderer_rtx_last_frame_nonzero_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_nonzero_pixels() : UINT64_C(0);
}

extern "C" double renderer_rtx_last_frame_mean_luminance(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_mean_luminance() : 0.0;
}

extern "C" uint64_t renderer_rtx_last_frame_saturated_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_saturated_pixels() : UINT64_C(0);
}

extern "C" uint64_t renderer_rtx_last_frame_temporal_outlier_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_temporal_outlier_pixels() :
        UINT64_C(0);
}

extern "C" uint64_t
renderer_rtx_last_frame_reprojected_temporal_outlier_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation
            ->last_scene_reprojected_temporal_outlier_pixels() :
        UINT64_C(0);
}

extern "C" uint64_t renderer_rtx_last_frame_reprojected_pixel_count(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_reprojected_pixel_count() :
        UINT64_C(0);
}

extern "C" uint64_t renderer_rtx_scene_rebuild_count(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->scene_rebuild_count() : UINT64_C(0);
}
