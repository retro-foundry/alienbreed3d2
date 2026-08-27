#ifndef AB3D2_DXR_DEVICE_H
#define AB3D2_DXR_DEVICE_H

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "render_view.h"
#include "renderer_ray_tracing_options.h"
#include "scene_frame.h"
#include "dxr_output.h"

namespace ab3d2::dxr {

class DxrPipeline;
class DxrStreamline;

class DxrDevice final {
public:
    static constexpr UINT frame_count = 3;

    DxrDevice() = default;
    ~DxrDevice();
    DxrDevice(const DxrDevice &) = delete;
    DxrDevice &operator=(const DxrDevice &) = delete;

    bool initialize(HWND window, bool hidden_window,
                    const RendererRayTracingOptions &options,
                    DxrStreamline *streamline,
                    std::string &error);
    bool render(DxrPipeline &pipeline, const SceneFrame &frame,
                const RenderView &view, std::string &error);
    bool flush(std::string &error);
    /* flush_queue is false only when the owner has already made the queue idle
     * before shutting down Streamline's proxy layer. */
    void shutdown(bool flush_queue = true);
    bool presentation_size(int &width, int &height) const;
    uint64_t last_scene_rgb_checksum() const { return last_scene_rgb_checksum_; }
    /*
     * Mean absolute per-component difference between the last two presented
     * frames, on the 0-255 display scale, or a negative value when fewer than
     * two frames have been read back. This is the temporal-stability signal:
     * with a frozen camera and scene it must fall as the reconstruction settles.
     */
    double last_scene_frame_delta() const { return last_scene_frame_delta_; }
    /* Presented pixels with any component at or above 250, a proxy for radiance
     * outliers that survive tone mapping. */
    uint64_t last_scene_saturated_pixels() const
    {
        return last_scene_saturated_pixels_;
    }
    uint64_t last_scene_temporal_outlier_pixels() const
    {
        return last_scene_temporal_outlier_pixels_;
    }

    ID3D12Device5 *device() const { return device_.Get(); }
    const DxrOutputConfiguration &output_configuration() const {
        return output_;
    }

private:
    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> render_target;
        D3D12_CPU_DESCRIPTOR_HANDLE render_target_view = {};
        UINT64 fence_value = 0;
    };

    bool enable_diagnostics(std::string &error);
    bool create_factory(std::string &error);
    bool select_adapter_and_device(std::string &error);
    bool create_command_objects(std::string &error);
    bool create_swap_chain(std::string &error);
    bool create_frame_contexts(std::string &error);
    bool create_render_targets(std::string &error);
    bool configure_output_request(const RendererRayTracingOptions &options,
                                  std::string &error);
    bool choose_output_configuration(DxrOutputConfiguration &output,
                                     std::string &display_name,
                                     std::string &error) const;
    bool reconfigure_swap_chain(DxrOutputConfiguration &output,
                                UINT width, UINT height,
                                bool recreate_render_targets,
                                bool allow_hdr_fallback,
                                std::string &error);
    bool refresh_output_configuration(DxrPipeline &pipeline,
                                      UINT width, UINT height,
                                      std::string &error);
    bool ensure_scene_readback(std::string &error);
    bool collect_scene_readback(UINT64 fence_value, std::string &error);
    bool resize(UINT width, UINT height, std::string &error);
    bool wait_for_frame(FrameContext &frame, std::string &error);
    bool wait_for_fence(UINT64 fence_value, const char *operation,
                        std::string &error);
    bool check_debug_messages(std::string &error);
    bool fail_device_operation(const char *operation, HRESULT result,
                               std::string &error);
    std::string device_removed_report(const char *operation, HRESULT result) const;

    HWND window_ = nullptr;
    bool hidden_window_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT frame_index_ = 0;
    UINT render_target_descriptor_size_ = 0;
    UINT64 next_fence_value_ = 1;
    uint32_t rendered_frame_count_ = 0;
    uint64_t last_scene_rgb_checksum_ = 0;
    double last_scene_frame_delta_ = -1.0;
    uint64_t last_scene_saturated_pixels_ = 0;
    uint64_t last_scene_temporal_outlier_pixels_ = 0;
    std::chrono::steady_clock::time_point previous_render_time_ = {};
    bool previous_render_time_valid_ = false;
    std::vector<uint8_t> previous_readback_rgb_;
    UINT readback_width_ = 0;
    UINT readback_height_ = 0;
    UINT readback_row_count_ = 0;
    UINT64 readback_total_bytes_ = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readback_footprint_ = {};
    HANDLE fence_event_ = nullptr;
    DxrStreamline *streamline_ = nullptr;
    RendererOutputMode requested_output_ = RENDERER_OUTPUT_AUTO;
    float requested_hdr_peak_nits_ = 0.0f;
    float requested_hdr_paper_white_nits_ = 0.0f;
    DxrOutputConfiguration output_ = {};

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
#if defined(AB3D2_ENABLE_STREAMLINE)
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_proxy_;
#endif
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
#if defined(AB3D2_ENABLE_STREAMLINE)
    Microsoft::WRL::ComPtr<ID3D12Device5> device_proxy_;
#endif
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> render_target_view_heap_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12Resource> scene_readback_;
    std::array<FrameContext, frame_count> frames_ = {};
};

}  // namespace ab3d2::dxr

#endif
