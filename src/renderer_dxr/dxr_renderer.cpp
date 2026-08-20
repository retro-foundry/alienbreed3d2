#include "dxr_renderer.h"

#include "dxr_debug.h"
#include "dxr_device.h"
#include "dxr_pipeline.h"
#if defined(AB3D2_ENABLE_STREAMLINE)
#include "dxr_streamline.h"
#endif

#include <SDL_syswm.h>

#include <climits>

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
        device_->shutdown();
        device_.reset();
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool DxrRenderer::expand_desktop_client_window(int desktop_x, int desktop_y,
                                               std::string &error)
{
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    int client_width = 0;
    int client_height = 0;

    if (SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) != 0) {
        error = sdl_error("SDL desktop window border measurement failed");
        return false;
    }
    if (top < 0 || left < 0 || bottom < 0 || right < 0) {
        error = "SDL desktop window reported invalid border dimensions";
        return false;
    }
    SDL_GetWindowSize(window_, &client_width, &client_height);
    if (client_width < 1 || client_height < 1 || client_width > INT_MAX - left ||
        client_height > INT_MAX - top) {
        error = "SDL desktop window client dimensions are invalid";
        return false;
    }
    SDL_SetWindowSize(window_, client_width + left, client_height + top);
    SDL_SetWindowPosition(window_, desktop_x - left, desktop_y - top);
    return true;
}

bool DxrRenderer::create_window(int window_width, int window_height,
                                const char *window_title, bool desktop_window,
                                bool hidden_window, std::string &error)
{
    int window_x = SDL_WINDOWPOS_CENTERED;
    int window_y = SDL_WINDOWPOS_CENTERED;
    Uint32 window_flags = (hidden_window ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) |
                          SDL_WINDOW_RESIZABLE;

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
    if (desktop_window && !hidden_window &&
        !expand_desktop_client_window(window_x, window_y, error)) {
        return false;
    }
    return true;
}

bool DxrRenderer::initialize(int window_width, int window_height,
                             const char *window_title, bool desktop_window,
                             bool hidden_window, uint8_t world_light_tessellation,
                             std::string &error)
{
    if (!window_title || window_width < 96 || window_height < 80 ||
        !tessellation_factor_valid(world_light_tessellation)) {
        error = "D3D12/DXR window configuration is invalid";
        return false;
    }
#if defined(AB3D2_ENABLE_STREAMLINE)
    streamline_ = std::make_unique<DxrStreamline>();
    if (!streamline_->initialize(error)) {
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
            window_information.info.win.window, hidden_window,
#if defined(AB3D2_ENABLE_STREAMLINE)
            streamline_.get(),
#else
            nullptr,
#endif
            error) ||
        !pipeline_->initialize(device_->device(), error)) {
        return false;
    }
    debug_output(
        "SceneFrame DXR renderer initialized; opaque world and the exact "
        "camera-relative companion weapon uses shared-depth in-world PBR "
        "visibility before reconstruction; sprites, "
        "other vector objects, HUD, and text remain outside this milestone"
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

uint64_t DxrRenderer::last_scene_saturated_pixels() const
{
    return device_ ? device_->last_scene_saturated_pixels() : UINT64_C(0);
}

uint64_t DxrRenderer::last_scene_emissive_scale_fold() const
{
    return pipeline_ ? pipeline_->scene_emissive_scale_fold() : UINT64_C(0);
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

}  // namespace ab3d2::dxr
