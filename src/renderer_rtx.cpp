#include "renderer_rtx.h"

#include "renderer_dxr/dxr_renderer.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <string>

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
        if (!renderer->implementation->initialize(
                window_width, window_height, window_title,
                desktop_window != 0, hidden_window != 0,
                world_light_tessellation, implementation_error)) {
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
    return 1;
}

extern "C" int renderer_rtx_get_presentation_size(
    const RendererRtx *renderer, int *out_width, int *out_height)
{
    if (!renderer || !renderer->implementation || !out_width || !out_height) {
        return 0;
    }
    return renderer->implementation->presentation_size(*out_width, *out_height) ? 1 : 0;
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

extern "C" uint64_t renderer_rtx_last_frame_saturated_pixels(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_saturated_pixels() : UINT64_C(0);
}

extern "C" uint64_t renderer_rtx_last_scene_emissive_scale_fold(
    const RendererRtx *renderer)
{
    return renderer && renderer->implementation ?
        renderer->implementation->last_scene_emissive_scale_fold() :
        UINT64_C(0);
}
