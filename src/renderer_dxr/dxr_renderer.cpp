#include "dxr_renderer.h"

#include "dxr_debug.h"
#include "dxr_device.h"
#include "dxr_pipeline.h"
#if defined(AB3D2_ENABLE_STREAMLINE)
#include "dxr_streamline.h"
#endif

#include <SDL_syswm.h>


namespace ab3d2::dxr {

namespace {

bool tessellation_factor_valid(uint8_t factor)
{
    return factor == 1 || factor == 2 || factor == 4 || factor == 8;
}

std::string sdl_error(const char *operation)
{
    std::string result = operation ? operation : "SDL operation failed";
    const char *message = SDL_GetError();
    if (message && *message) {
        result += ": ";
        result += message;
    }
    return result;
}

}  // namespace

DxrRenderer::DxrRenderer() = default;

DxrRenderer::~DxrRenderer()
{
    /* Stop presenting an apparently hung window while the synchronous GPU and
     * Streamline teardown completes. The SDL window remains alive until the
     * device no longer owns its HWND. */
    if (window_) {
        SDL_HideWindow(window_);
    }
    if (device_) {
        std::string flush_error;
        if (!device_->flush(flush_error)) {
            debug_output("renderer shutdown flush failed: " + flush_error);
        }
    }
#if defined(AB3D2_ENABLE_STREAMLINE)
    if (streamline_) {
        std::string release_error;
        if (!streamline_->release_resources(release_error)) {
            debug_output("DLSS-RR resource release failed: " + release_error);
        }
    }
#endif
    pipeline_.reset();
#if defined(AB3D2_ENABLE_STREAMLINE)
    if (streamline_) {
        std::string shutdown_error;
        if (!streamline_->shutdown(shutdown_error)) {
            debug_output("Streamline shutdown failed: " + shutdown_error);
        }
    }
#endif
    if (device_) {
        /* The queue was made idle before Streamline resources and the proxy
         * runtime were released. Signaling the proxy queue after slShutdown is
         * redundant and can leave application teardown waiting indefinitely. */
        device_->shutdown(false);
        device_.reset();
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool DxrRenderer::create_window(int window_width, int window_height,
                                const char *window_title, bool desktop_window,
                                bool hidden_window, std::string &error)
{
    int window_x = SDL_WINDOWPOS_CENTERED;
    int window_y = SDL_WINDOWPOS_CENTERED;
    Uint32 window_flags = (hidden_window ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) |
                          SDL_WINDOW_RESIZABLE;
    /*
     * The desktop window is borderless at the monitor's bounds, so the swap
     * chain is the monitor. It used to be a framed window grown until its
     * client area covered the desktop with the frame pushed off screen, which
     * left 11 columns and 45 rows of every traced, reconstructed and
     * tone-mapped frame outside the monitor at 3840x2160 and the view centre
     * 22 pixels above the screen's.
     */
    if (desktop_window && !hidden_window) {
        window_flags |= SDL_WINDOW_BORDERLESS;
    }

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        error = "D3D12/DXR renderer requires SDL_INIT_VIDEO before creation";
        return false;
    }
    if (desktop_window && !hidden_window) {
        SDL_DisplayMode desktop_mode = {};
        SDL_Rect desktop_bounds = {};

        if (SDL_GetDesktopDisplayMode(0, &desktop_mode) == 0 &&
            desktop_mode.w >= 96 && desktop_mode.h >= 80) {
            window_width = desktop_mode.w;
            window_height = desktop_mode.h;
        }
        if (SDL_GetDisplayBounds(0, &desktop_bounds) == 0) {
            window_x = desktop_bounds.x;
            window_y = desktop_bounds.y;
            if (desktop_bounds.w >= 96 && desktop_bounds.h >= 80) {
                window_width = desktop_bounds.w;
                window_height = desktop_bounds.h;
            }
        }
    }
    window_ = SDL_CreateWindow(window_title, window_x, window_y,
                               window_width, window_height, window_flags);
    if (!window_) {
        error = sdl_error("SDL D3D12/DXR window creation failed");
        return false;
    }
    return true;
}

bool DxrRenderer::initialize(int window_width, int window_height,
                             const char *window_title, bool desktop_window,
                             bool hidden_window, uint8_t world_light_tessellation,
                             const RendererRayTracingOptions &options,
                             std::string &error)
{
    if (!window_title || window_width < 96 || window_height < 80 ||
        !tessellation_factor_valid(world_light_tessellation)) {
        error = "D3D12/DXR window configuration is invalid";
        return false;
    }
#if defined(AB3D2_ENABLE_STREAMLINE)
    streamline_ = std::make_unique<DxrStreamline>();
    if (!streamline_->initialize(options.reconstruction, error)) {
        return false;
    }
#endif
    if (!create_window(window_width, window_height, window_title,
                       desktop_window, hidden_window, error)) {
        return false;
    }

    SDL_SysWMinfo window_information = {};
    SDL_VERSION(&window_information.version);
    if (SDL_GetWindowWMInfo(window_, &window_information) != SDL_TRUE) {
        error = sdl_error("SDL_GetWindowWMInfo for D3D12/DXR failed");
        return false;
    }
    if (window_information.subsystem != SDL_SYSWM_WINDOWS ||
        !window_information.info.win.window) {
        error = "SDL D3D12/DXR window did not provide a Windows HWND";
        return false;
    }

    device_ = std::make_unique<DxrDevice>();
    pipeline_ = std::make_unique<DxrPipeline>();
    if (!device_->initialize(
            window_information.info.win.window, hidden_window, options,
#if defined(AB3D2_ENABLE_STREAMLINE)
            streamline_.get(),
#else
            nullptr,
#endif
            error) ||
        !pipeline_->initialize(device_->device(), options,
                               device_->output_configuration(), error)) {
        return false;
    }
    debug_output(
        "SceneFrame DXR renderer initialized; world, non-projectile bitmap "
        "and glare commands, animated world vectors, and the exact "
        "camera-relative companion weapon use shared-depth in-world PBR "
        "visibility before reconstruction; transient projectiles, HUD, and "
        "text remain outside this milestone"
#if defined(AB3D2_ENABLE_STREAMLINE)
        "; Streamline DLSS Ray Reconstruction 2.12 integration is enabled"
#endif
    );
    return true;
}

bool DxrRenderer::present(const SceneFrame &frame, const RenderView &view,
                          std::string &error)
{
    if (!device_ || !pipeline_) {
        error = "D3D12/DXR renderer is not initialized";
        return false;
    }
    return device_->render(*pipeline_, frame, view, error);
}

bool DxrRenderer::wait_for_present(std::string &error)
{
    if (!device_ || !pipeline_) {
        error = "D3D12/DXR renderer is not initialized";
        return false;
    }
    return device_->wait_for_present(error);
}

bool DxrRenderer::presentation_size(int &width, int &height) const
{
    return device_ && device_->presentation_size(width, height);
}

uint64_t DxrRenderer::last_scene_rgb_checksum() const
{
    return device_ ? device_->last_scene_rgb_checksum() : UINT64_C(0);
}

double DxrRenderer::last_scene_frame_delta() const
{
    return device_ ? device_->last_scene_frame_delta() : -1.0;
}

double DxrRenderer::last_scene_reprojected_frame_delta() const
{
    return device_ ? device_->last_scene_reprojected_frame_delta() : -1.0;
}

uint64_t DxrRenderer::last_scene_nonzero_pixels() const
{
    return device_ ? device_->last_scene_nonzero_pixels() : UINT64_C(0);
}

double DxrRenderer::last_scene_mean_luminance() const
{
    return device_ ? device_->last_scene_mean_luminance() : 0.0;
}

uint64_t DxrRenderer::last_scene_saturated_pixels() const
{
    return device_ ? device_->last_scene_saturated_pixels() : UINT64_C(0);
}

uint64_t DxrRenderer::last_scene_temporal_outlier_pixels() const
{
    return device_ ? device_->last_scene_temporal_outlier_pixels() : UINT64_C(0);
}

uint64_t DxrRenderer::last_scene_reprojected_temporal_outlier_pixels() const
{
    return device_ ?
        device_->last_scene_reprojected_temporal_outlier_pixels() :
        UINT64_C(0);
}

uint64_t DxrRenderer::last_scene_reprojected_pixel_count() const
{
    return device_ ? device_->last_scene_reprojected_pixel_count() :
        UINT64_C(0);
}

uint64_t DxrRenderer::scene_rebuild_count() const
{
    return pipeline_ ? pipeline_->scene_rebuild_count() : UINT64_C(0);
}

bool DxrRenderer::prepare_vector_materials(const uint32_t *asset_ids,
                                           size_t asset_count, size_t &prepared,
                                           std::string &error)
{
    prepared = 0u;
    if (!pipeline_) {
        error = "D3D12/DXR resource preparation ran before the pipeline existed";
        return false;
    }
    return pipeline_->prepare_vector_materials(asset_ids, asset_count, prepared,
                                               error);
}

bool DxrRenderer::prepare_bitmap_materials(size_t asset_count,
                                           size_t &prepared,
                                           std::string &error)
{
    prepared = 0u;
    if (!pipeline_) {
        error = "D3D12/DXR resource preparation ran before the pipeline existed";
        return false;
    }
    return pipeline_->prepare_bitmap_materials(asset_count, prepared, error);
}

size_t DxrRenderer::last_view_weapon_coverage() const
{
    return pipeline_ ? pipeline_->last_view_weapon_coverage() : 0u;
}

uint64_t DxrRenderer::last_view_weapon_rgb_checksum() const
{
    return pipeline_ ? pipeline_->last_view_weapon_rgb_checksum() :
        UINT64_C(0);
}

size_t DxrRenderer::last_world_bitmap_coverage() const
{
    return pipeline_ ? pipeline_->last_world_bitmap_coverage() : 0u;
}

size_t DxrRenderer::last_world_vector_coverage() const
{
    return pipeline_ ? pipeline_->last_world_vector_coverage() : 0u;
}

size_t DxrRenderer::last_world_additive_coverage() const
{
    return pipeline_ ? pipeline_->last_world_additive_coverage() : 0u;
}

size_t DxrRenderer::last_direct_diffuse_coverage() const
{
    return pipeline_ ? pipeline_->last_direct_diffuse_coverage() : 0u;
}

size_t DxrRenderer::last_direct_specular_coverage() const
{
    return pipeline_ ? pipeline_->last_direct_specular_coverage() : 0u;
}

size_t DxrRenderer::last_invalid_lighting_or_guide_pixels() const
{
    return pipeline_ ? pipeline_->last_invalid_lighting_or_guide_pixels() : 0u;
}

size_t DxrRenderer::last_smooth_specular_coverage() const
{
    return pipeline_ ? pipeline_->last_smooth_specular_coverage() : 0u;
}

bool DxrRenderer::enable_noisy_radiance_readback()
{
    return device_ && device_->enable_noisy_radiance_readback();
}

bool DxrRenderer::select_radiance_channel(uint32_t channel)
{
    return pipeline_ && pipeline_->select_radiance_channel(channel);
}

size_t DxrRenderer::last_noisy_radiance_value_count() const
{
    return device_ ? device_->last_noisy_radiance_value_count() : 0u;
}

bool DxrRenderer::copy_last_noisy_radiance(
    uint16_t *out_values, size_t value_count) const
{
    return device_ &&
        device_->copy_last_noisy_radiance(out_values, value_count);
}

bool DxrRenderer::active_ray_tracing_options(
    RendererRayTracingOptions &options) const
{
    if (!pipeline_) {
        return false;
    }
    options = RendererRayTracingOptions{};
    pipeline_->active_ray_tracing_options(options);
#if defined(AB3D2_ENABLE_STREAMLINE)
    options.reconstruction = streamline_ ? streamline_->active_mode() :
        RENDERER_RAY_RECONSTRUCTION_DEFAULT;
#endif
    return true;
}

}  // namespace ab3d2::dxr
