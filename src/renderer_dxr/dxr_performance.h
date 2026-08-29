#ifndef AB3D2_DXR_PERFORMANCE_H
#define AB3D2_DXR_PERFORMANCE_H

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "renderer_ray_tracing_options.h"

namespace ab3d2::dxr {

enum class DxrGpuStage : size_t {
    frame,
    scene_build,
    light_grid,
    primary_visibility,
    primary_shading,
    indirect_resampling,
    indirect_gradient,
    indirect_temporal,
    indirect_spatial,
    indirect_reconstruct,
    ray_reconstruction,
    bloom,
    tone_mapping,
    diagnostics,
    scene_history,
    presentation,
    validation_readback,
    count,
};

struct DxrCpuFrameTiming {
    double frame_ms = 0.0;
    double present_wait_ms = 0.0;
    double scene_update_ms = 0.0;
    double frame_reuse_wait_ms = 0.0;
    double command_record_ms = 0.0;
    double queue_submit_ms = 0.0;
    double present_ms = 0.0;
    double validation_readback_ms = 0.0;
};

struct DxrPerformanceMetadata {
    UINT presentation_width = 0u;
    UINT presentation_height = 0u;
    UINT tracing_width = 0u;
    UINT tracing_height = 0u;
    UINT reconstruction_width = 0u;
    UINT reconstruction_height = 0u;
    uint32_t samples_per_pixel = 0u;
    uint32_t indirect_samples_per_pixel = 0u;
    uint32_t maximum_depth = 0u;
    uint32_t light_candidates = 0u;
    uint32_t reservoir_sample_limit = 0u;
    RendererRayReconstructionMode reconstruction_mode =
        RENDERER_RAY_RECONSTRUCTION_OFF;
    uint64_t scene_rebuild_count = 0u;
    bool history_valid = false;
    bool validation_enabled = false;
    bool split_primary = false;
    bool single_primary_direct_survivor = false;
};

class DxrGpuProfiler final {
public:
    static constexpr UINT frame_count = 2u;

    bool initialize(ID3D12Device5 *device, ID3D12CommandQueue *command_queue,
                    IDXGIAdapter1 *adapter, std::string &error);
    void shutdown();

    bool enabled() const { return enabled_; }
    bool recording_frame() const { return recording_frame_; }

    bool collect(UINT frame_slot, std::string &error);
    bool collect_all(std::string &error);
    void begin_frame(ID3D12GraphicsCommandList4 *command_list,
                     UINT frame_slot, uint32_t frame_number);
    void begin_stage(ID3D12GraphicsCommandList4 *command_list,
                     DxrGpuStage stage);
    void end_stage(ID3D12GraphicsCommandList4 *command_list,
                   DxrGpuStage stage);
    void set_metadata(const DxrPerformanceMetadata &metadata);
    void end_frame(ID3D12GraphicsCommandList4 *command_list);
    void set_cpu_timing(UINT frame_slot, const DxrCpuFrameTiming &timing);

private:
    static constexpr size_t stage_count =
        static_cast<size_t>(DxrGpuStage::count);
    static constexpr UINT queries_per_frame =
        static_cast<UINT>(stage_count * 2u);

    struct FrameQueries {
        std::array<bool, stage_count> began = {};
        std::array<bool, stage_count> ended = {};
        std::array<bool, stage_count> active = {};
        DxrPerformanceMetadata metadata = {};
        DxrCpuFrameTiming cpu = {};
        uint32_t frame_number = 0u;
        bool recording = false;
        bool pending = false;
    };

    struct Sample {
        std::array<double, stage_count> gpu_ms = {};
        DxrPerformanceMetadata metadata = {};
        DxrCpuFrameTiming cpu = {};
        uint32_t frame_number = 0u;
    };

    bool configure(std::string &error);
    void report();
    UINT query_index(UINT frame_slot, DxrGpuStage stage, bool end) const;

    bool enabled_ = false;
    bool recording_frame_ = false;
    bool reported_ = false;
    uint32_t warmup_frames_ = 120u;
    uint32_t sample_limit_ = 600u;
    uint64_t submitted_frames_ = 0u;
    uint32_t submitted_profile_frames_ = 0u;
    UINT current_frame_slot_ = 0u;
    UINT64 timestamp_frequency_ = 0u;
    std::string adapter_name_;
    std::string adapter_luid_;
    std::string driver_version_;
    std::array<FrameQueries, frame_count> frames_ = {};
    std::vector<Sample> samples_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
};

class DxrGpuProfileScope final {
public:
    DxrGpuProfileScope(DxrGpuProfiler *profiler,
                       ID3D12GraphicsCommandList4 *command_list,
                       DxrGpuStage stage)
        : profiler_(profiler), command_list_(command_list), stage_(stage)
    {
        if (profiler_) {
            profiler_->begin_stage(command_list_, stage_);
        }
    }

    ~DxrGpuProfileScope()
    {
        if (profiler_) {
            profiler_->end_stage(command_list_, stage_);
        }
    }

    DxrGpuProfileScope(const DxrGpuProfileScope &) = delete;
    DxrGpuProfileScope &operator=(const DxrGpuProfileScope &) = delete;

private:
    DxrGpuProfiler *profiler_ = nullptr;
    ID3D12GraphicsCommandList4 *command_list_ = nullptr;
    DxrGpuStage stage_ = DxrGpuStage::frame;
};

}  // namespace ab3d2::dxr

#endif
