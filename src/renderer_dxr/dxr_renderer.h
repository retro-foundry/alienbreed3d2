#ifndef AB3D2_DXR_RENDERER_H
#define AB3D2_DXR_RENDERER_H

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "render_view.h"
#include "scene_frame.h"

namespace ab3d2::dxr {

class DxrDevice;
class DxrPipeline;
class DxrStreamline;

class DxrRenderer final {
public:
    DxrRenderer();
    ~DxrRenderer();
    DxrRenderer(const DxrRenderer &) = delete;
    DxrRenderer &operator=(const DxrRenderer &) = delete;

    bool initialize(int window_width, int window_height, const char *window_title,
                    bool desktop_window, bool hidden_window,
                    uint8_t world_light_tessellation, std::string &error);
    bool present(const SceneFrame &frame, const RenderView &view,
                 std::string &error);
    bool presentation_size(int &width, int &height) const;
    uint64_t last_scene_rgb_checksum() const;
    double last_scene_frame_delta() const;
    uint64_t last_scene_saturated_pixels() const;
    uint64_t last_scene_emissive_scale_fold() const;
    size_t last_view_weapon_coverage() const;
    uint64_t last_view_weapon_rgb_checksum() const;

private:
    bool create_window(int window_width, int window_height, const char *window_title,
                       bool desktop_window, bool hidden_window, std::string &error);
    bool expand_desktop_client_window(int desktop_x, int desktop_y,
                                      std::string &error);

    SDL_Window *window_ = nullptr;
    std::unique_ptr<DxrDevice> device_;
    std::unique_ptr<DxrPipeline> pipeline_;
#if defined(AB3D2_ENABLE_STREAMLINE)
    std::unique_ptr<DxrStreamline> streamline_;
#endif
};

}  // namespace ab3d2::dxr

#endif
