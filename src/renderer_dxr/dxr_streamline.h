#ifndef AB3D2_DXR_STREAMLINE_H
#define AB3D2_DXR_STREAMLINE_H

#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <filesystem>

#include "renderer_ray_tracing_options.h"
#include <string>

#include "dxr_reconstruction_math.h"

namespace ab3d2::dxr {

struct DxrStreamlineResources {
    ID3D12Resource *noisy_radiance = nullptr;
    ID3D12Resource *output = nullptr;
    ID3D12Resource *diffuse_albedo = nullptr;
    ID3D12Resource *specular_albedo = nullptr;
    ID3D12Resource *shading_normal = nullptr;
    ID3D12Resource *linear_roughness = nullptr;
    ID3D12Resource *linear_depth = nullptr;
    ID3D12Resource *scene_motion = nullptr;
    ID3D12Resource *specular_hit_distance = nullptr;
};

class DxrStreamline final {
public:
    enum class Mode {
        off,
        quality,
        balanced,
        performance,
        ultra_performance,
    };

    DxrStreamline() = default;
    ~DxrStreamline();
    DxrStreamline(const DxrStreamline &) = delete;
    DxrStreamline &operator=(const DxrStreamline &) = delete;

    bool initialize(RendererRayReconstructionMode mode, std::string &error);
    bool adapter_supported(const LUID &luid, std::string &reason) const;
    bool get_native_factory(IDXGIFactory6 *proxy, IDXGIFactory6 **native,
                            std::string &error) const;
    bool get_native_device(ID3D12Device5 *proxy, ID3D12Device5 **native,
                           std::string &error) const;
    bool set_device(ID3D12Device5 *device, const LUID &luid,
                    std::string &error);
    bool configure_output(UINT output_width, UINT output_height,
                          UINT &render_width, UINT &render_height,
                          std::string &error);
    bool evaluate(ID3D12GraphicsCommandList4 *command_list,
                  uint32_t frame_number,
                  const reconstruction::CameraProjection &current_camera,
                  const reconstruction::CameraProjection &previous_camera,
                  reconstruction::PixelJitter jitter, bool history_valid,
                  const DxrStreamlineResources &resources,
                  std::string &error);
    bool release_resources(std::string &error);
    bool shutdown(std::string &error);

    bool active() const;
    /* The mode in force, for reporting the applied settings back. */
    RendererRayReconstructionMode active_mode() const;
    UINT render_width() const { return render_width_; }
    UINT render_height() const { return render_height_; }

private:
    bool configure_mode(RendererRayReconstructionMode requested,
                        std::string &error);
    bool verify_runtime(std::string &error);

    std::filesystem::path runtime_directory_;
    Mode mode_ = Mode::quality;
    UINT output_width_ = 0;
    UINT output_height_ = 0;
    UINT render_width_ = 0;
    UINT render_height_ = 0;
    bool initialized_ = false;
    bool device_set_ = false;
    bool resources_allocated_ = false;
};

}  // namespace ab3d2::dxr

#endif
