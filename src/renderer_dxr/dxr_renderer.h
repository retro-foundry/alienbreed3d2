#ifndef AB3D2_DXR_RENDERER_H
#define AB3D2_DXR_RENDERER_H

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "render_view.h"
#include "renderer_ray_tracing_options.h"
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
                    uint8_t world_light_tessellation,
                    const RendererRayTracingOptions &options,
                    std::string &error);
    bool present(const SceneFrame &frame, const RenderView &view,
                 std::string &error);
    bool wait_for_present(std::string &error);
    bool presentation_size(int &width, int &height) const;
    uint64_t last_scene_rgb_checksum() const;
    double last_scene_frame_delta() const;
    double last_scene_reprojected_frame_delta() const;
    uint64_t last_scene_nonzero_pixels() const;
    double last_scene_mean_luminance() const;
    uint64_t last_scene_saturated_pixels() const;
    uint64_t last_scene_temporal_outlier_pixels() const;
    uint64_t last_scene_reprojected_temporal_outlier_pixels() const;
    uint64_t last_scene_reprojected_pixel_count() const;
    uint64_t scene_rebuild_count() const;
    size_t last_view_weapon_coverage() const;
    uint64_t last_view_weapon_rgb_checksum() const;
    size_t last_world_bitmap_coverage() const;
    size_t last_world_vector_coverage() const;
    size_t last_world_additive_coverage() const;
    size_t last_direct_diffuse_coverage() const;
    size_t last_direct_specular_coverage() const;
    size_t last_invalid_lighting_or_guide_pixels() const;
    size_t last_smooth_specular_coverage() const;
    bool enable_noisy_radiance_readback();
    bool select_radiance_channel(uint32_t channel);
    size_t last_noisy_radiance_value_count() const;
    bool copy_last_noisy_radiance(uint16_t *out_values,
                                  size_t value_count) const;
    bool active_ray_tracing_options(RendererRayTracingOptions &options) const;

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
