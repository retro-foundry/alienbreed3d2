#include "dxr_performance.h"

#include "dxr_debug.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ab3d2::dxr {

namespace {

constexpr std::array<const char *, static_cast<size_t>(DxrGpuStage::count)>
    gpu_stage_names = {
        "frame",
        "scene_build",
        "light_grid",
        "primary_visibility",
        "primary_shading",
        "indirect_resampling",
        "indirect_gradient",
        "indirect_temporal",
        "indirect_spatial",
        "indirect_reconstruct",
        "ray_reconstruction",
        "bloom",
        "tone_mapping",
        "diagnostics",
        "scene_history",
        "presentation",
        "validation_readback",
    };

static_assert(gpu_stage_names.size() ==
              static_cast<size_t>(DxrGpuStage::count));

bool environment_text(const char *name, std::string &value,
                      std::string &error)
{
    char buffer[64] = {};
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableA(
        name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0u) {
        const DWORD environment_error = GetLastError();
        if (environment_error == ERROR_ENVVAR_NOT_FOUND ||
            environment_error == ERROR_SUCCESS) {
            value.clear();
            return true;
        }
        error = hresult_error(
            (std::string("GetEnvironmentVariableA(") + name + ")").c_str(),
            HRESULT_FROM_WIN32(environment_error));
        return false;
    }
    if (length >= std::size(buffer)) {
        error = std::string(name) + " exceeds 63 bytes";
        return false;
    }
    value.assign(buffer, length);
    return true;
}

bool environment_uint(const char *name, uint32_t minimum, uint32_t maximum,
                      uint32_t &value, std::string &error)
{
    std::string text;
    if (!environment_text(name, text, error) || text.empty()) {
        return error.empty();
    }
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        error = std::string(name) + " must be " +
            std::to_string(minimum) + "-" + std::to_string(maximum);
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::string json_string(const std::string &value)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20u) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0')
                       << static_cast<unsigned>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

double percentile(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = quantile * static_cast<double>(values.size() - 1u);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

void write_distribution(std::ostringstream &output,
                        const std::vector<double> &values)
{
    const double maximum = values.empty() ? 0.0 :
        *std::max_element(values.begin(), values.end());
    output << "{\"median\":" << percentile(values, 0.5)
           << ",\"p95\":" << percentile(values, 0.95)
           << ",\"p99\":" << percentile(values, 0.99)
           << ",\"max\":" << maximum << '}';
}

bool same_configuration(const DxrPerformanceMetadata &left,
                        const DxrPerformanceMetadata &right)
{
    return left.presentation_width == right.presentation_width &&
        left.presentation_height == right.presentation_height &&
        left.tracing_width == right.tracing_width &&
        left.tracing_height == right.tracing_height &&
        left.reconstruction_width == right.reconstruction_width &&
        left.reconstruction_height == right.reconstruction_height &&
        left.samples_per_pixel == right.samples_per_pixel &&
        left.indirect_samples_per_pixel == right.indirect_samples_per_pixel &&
        left.maximum_depth == right.maximum_depth &&
        left.light_candidates == right.light_candidates &&
        left.reservoir_sample_limit == right.reservoir_sample_limit &&
        left.reconstruction_mode == right.reconstruction_mode &&
        left.validation_enabled == right.validation_enabled &&
        left.split_primary == right.split_primary &&
        left.single_primary_direct_survivor ==
            right.single_primary_direct_survivor &&
        left.single_continuation_lobe == right.single_continuation_lobe;
}

const char *reconstruction_name(RendererRayReconstructionMode mode)
{
    switch (mode) {
    case RENDERER_RAY_RECONSTRUCTION_DEFAULT: return "default";
    case RENDERER_RAY_RECONSTRUCTION_QUALITY: return "quality";
    case RENDERER_RAY_RECONSTRUCTION_BALANCED: return "balanced";
    case RENDERER_RAY_RECONSTRUCTION_PERFORMANCE: return "performance";
    case RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE:
        return "ultra-performance";
    case RENDERER_RAY_RECONSTRUCTION_OFF: return "off";
    }
    return "invalid";
}

D3D12_RESOURCE_DESC readback_description(UINT64 bytes)
{
    D3D12_RESOURCE_DESC description = {};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1u;
    description.DepthOrArraySize = 1u;
    description.MipLevels = 1u;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1u;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

}  // namespace

bool DxrGpuProfiler::configure(std::string &error)
{
    std::string enabled_text;
    if (!environment_text("AB3D2_DXR_PROFILE", enabled_text, error)) {
        return false;
    }
    if (enabled_text.empty() || enabled_text == "0") {
        enabled_ = false;
        return true;
    }
    if (enabled_text != "1") {
        error = "AB3D2_DXR_PROFILE must be 0 or 1";
        return false;
    }
    enabled_ = true;
    return environment_uint("AB3D2_DXR_PROFILE_WARMUP", 0u, 1000000u,
                            warmup_frames_, error) &&
        environment_uint("AB3D2_DXR_PROFILE_SAMPLES", 1u, 100000u,
                         sample_limit_, error);
}

bool DxrGpuProfiler::initialize(ID3D12Device5 *device,
                                ID3D12CommandQueue *command_queue,
                                IDXGIAdapter1 *adapter, std::string &error)
{
    shutdown();
    if (!configure(error) || !enabled_) {
        return error.empty();
    }
    if (!device || !command_queue || !adapter) {
        error = "DXR performance profiler received incomplete D3D12 state";
        return false;
    }
    HRESULT result = command_queue->GetTimestampFrequency(&timestamp_frequency_);
    if (FAILED(result) || timestamp_frequency_ == 0u) {
        error = FAILED(result) ?
            hresult_error("ID3D12CommandQueue::GetTimestampFrequency", result) :
            "D3D12 timestamp frequency is zero";
        return false;
    }

    D3D12_QUERY_HEAP_DESC query_description = {};
    query_description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query_description.Count = queries_per_frame * frame_count;
    result = device->CreateQueryHeap(&query_description,
                                     IID_PPV_ARGS(&query_heap_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateQueryHeap(DXR performance)", result);
        return false;
    }
    query_heap_->SetName(L"AB3D2 DXR Performance Timestamp Heap");

    D3D12_HEAP_PROPERTIES readback_heap = {};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    const D3D12_RESOURCE_DESC description = readback_description(
        static_cast<UINT64>(query_description.Count) * sizeof(UINT64));
    result = device->CreateCommittedResource(
        &readback_heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&readback_));
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Device::CreateCommittedResource(DXR performance readback)",
            result);
        return false;
    }
    readback_->SetName(L"AB3D2 DXR Performance Timestamp Readback");

    DXGI_ADAPTER_DESC1 adapter_description = {};
    result = adapter->GetDesc1(&adapter_description);
    if (FAILED(result)) {
        error = hresult_error("IDXGIAdapter1::GetDesc1(performance)", result);
        return false;
    }
    adapter_name_ = wide_to_utf8(adapter_description.Description);
    std::ostringstream luid;
    luid << std::hex << std::setw(8) << std::setfill('0')
         << static_cast<uint32_t>(adapter_description.AdapterLuid.HighPart)
         << ':' << std::setw(8)
         << adapter_description.AdapterLuid.LowPart;
    adapter_luid_ = luid.str();

    LARGE_INTEGER driver = {};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),
                                                 &driver))) {
        std::ostringstream version;
        version << HIWORD(driver.HighPart) << '.' << LOWORD(driver.HighPart)
                << '.' << HIWORD(driver.LowPart) << '.'
                << LOWORD(driver.LowPart);
        driver_version_ = version.str();
    } else {
        driver_version_ = "unavailable";
    }
    samples_.reserve(sample_limit_);
    std::fprintf(stdout,
                 "[DXR-PERF] enabled warmup=%u samples=%u adapter=%s\n",
                 warmup_frames_, sample_limit_, adapter_name_.c_str());
    std::fflush(stdout);
    return true;
}

UINT DxrGpuProfiler::query_index(UINT frame_slot, DxrGpuStage stage,
                                 bool end) const
{
    return frame_slot * queries_per_frame +
        static_cast<UINT>(stage) * 2u + (end ? 1u : 0u);
}

void DxrGpuProfiler::begin_frame(ID3D12GraphicsCommandList4 *command_list,
                                 UINT frame_slot, uint32_t frame_number)
{
    recording_frame_ = false;
    if (!enabled_ || !command_list || frame_slot >= frame_count) {
        return;
    }
    const bool profile_this_frame = submitted_frames_ >= warmup_frames_ &&
        submitted_profile_frames_ < sample_limit_;
    ++submitted_frames_;
    if (!profile_this_frame) {
        return;
    }
    FrameQueries &frame = frames_[frame_slot];
    if (frame.pending) {
        debug_output(
            "DXR performance query slice was reused before collection");
        return;
    }
    frame = {};
    frame.recording = true;
    frame.frame_number = frame_number;
    current_frame_slot_ = frame_slot;
    recording_frame_ = true;
    ++submitted_profile_frames_;
    frame.active[static_cast<size_t>(DxrGpuStage::frame)] = true;
    frame.began[static_cast<size_t>(DxrGpuStage::frame)] = true;
    command_list->EndQuery(
        query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        query_index(frame_slot, DxrGpuStage::frame, false));
}

void DxrGpuProfiler::begin_stage(
    ID3D12GraphicsCommandList4 *command_list, DxrGpuStage stage)
{
    const size_t index = static_cast<size_t>(stage);
    if (!recording_frame_ || !command_list ||
        stage == DxrGpuStage::frame || index >= stage_count) {
        return;
    }
    FrameQueries &frame = frames_[current_frame_slot_];
    if (frame.began[index]) {
        return;
    }
    frame.active[index] = true;
    frame.began[index] = true;
    command_list->EndQuery(
        query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        query_index(current_frame_slot_, stage, false));
}

void DxrGpuProfiler::end_stage(
    ID3D12GraphicsCommandList4 *command_list, DxrGpuStage stage)
{
    const size_t index = static_cast<size_t>(stage);
    if (!recording_frame_ || !command_list ||
        stage == DxrGpuStage::frame || index >= stage_count) {
        return;
    }
    FrameQueries &frame = frames_[current_frame_slot_];
    if (!frame.began[index] || frame.ended[index]) {
        return;
    }
    frame.ended[index] = true;
    command_list->EndQuery(
        query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        query_index(current_frame_slot_, stage, true));
}

void DxrGpuProfiler::set_metadata(const DxrPerformanceMetadata &metadata)
{
    if (recording_frame_) {
        frames_[current_frame_slot_].metadata = metadata;
    }
}

void DxrGpuProfiler::end_frame(ID3D12GraphicsCommandList4 *command_list)
{
    if (!recording_frame_ || !command_list) {
        return;
    }
    FrameQueries &frame = frames_[current_frame_slot_];
    for (size_t index = 1u; index < stage_count; ++index) {
        const auto stage = static_cast<DxrGpuStage>(index);
        if (!frame.began[index]) {
            command_list->EndQuery(
                query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                query_index(current_frame_slot_, stage, false));
            frame.began[index] = true;
        }
        if (!frame.ended[index]) {
            command_list->EndQuery(
                query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                query_index(current_frame_slot_, stage, true));
            frame.ended[index] = true;
        }
    }
    frame.ended[static_cast<size_t>(DxrGpuStage::frame)] = true;
    command_list->EndQuery(
        query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        query_index(current_frame_slot_, DxrGpuStage::frame, true));
    const UINT first_query = current_frame_slot_ * queries_per_frame;
    command_list->ResolveQueryData(
        query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        first_query, queries_per_frame, readback_.Get(),
        static_cast<UINT64>(first_query) * sizeof(UINT64));
    frame.pending = true;
    recording_frame_ = false;
}

void DxrGpuProfiler::set_cpu_timing(UINT frame_slot,
                                    const DxrCpuFrameTiming &timing)
{
    if (enabled_ && frame_slot < frame_count && frames_[frame_slot].pending) {
        frames_[frame_slot].cpu = timing;
    }
}

bool DxrGpuProfiler::collect(UINT frame_slot, std::string &error)
{
    if (!enabled_ || frame_slot >= frame_count ||
        !frames_[frame_slot].pending) {
        return true;
    }
    const UINT first_query = frame_slot * queries_per_frame;
    const SIZE_T byte_begin =
        static_cast<SIZE_T>(first_query) * sizeof(UINT64);
    const SIZE_T byte_end = byte_begin +
        static_cast<SIZE_T>(queries_per_frame) * sizeof(UINT64);
    const D3D12_RANGE read = {byte_begin, byte_end};
    void *mapped = nullptr;
    const HRESULT result = readback_->Map(0u, &read, &mapped);
    if (FAILED(result)) {
        error = hresult_error(
            "ID3D12Resource::Map(DXR performance readback)", result);
        return false;
    }
    const auto *timestamps = reinterpret_cast<const UINT64 *>(
        static_cast<const unsigned char *>(mapped) + byte_begin);
    Sample sample = {};
    FrameQueries &frame = frames_[frame_slot];
    sample.frame_number = frame.frame_number;
    sample.metadata = frame.metadata;
    sample.cpu = frame.cpu;
    for (size_t index = 0u; index < stage_count; ++index) {
        const UINT64 begin = timestamps[index * 2u];
        const UINT64 end = timestamps[index * 2u + 1u];
        if (end < begin) {
            const D3D12_RANGE no_write = {0u, 0u};
            readback_->Unmap(0u, &no_write);
            error = std::string("DXR performance timestamp order is invalid for ") +
                gpu_stage_names[index];
            return false;
        }
        sample.gpu_ms[index] = frame.active[index] ?
            1000.0 * static_cast<double>(end - begin) /
                static_cast<double>(timestamp_frequency_) : 0.0;
    }
    const D3D12_RANGE no_write = {0u, 0u};
    readback_->Unmap(0u, &no_write);
    samples_.push_back(sample);
    frame = {};
    if (samples_.size() >= sample_limit_) {
        report();
    }
    return true;
}

bool DxrGpuProfiler::collect_all(std::string &error)
{
    for (UINT frame_slot = 0u; frame_slot < frame_count; ++frame_slot) {
        if (!collect(frame_slot, error)) {
            return false;
        }
    }
    return true;
}

void DxrGpuProfiler::report()
{
    if (!enabled_ || reported_ || samples_.empty()) {
        return;
    }
    reported_ = true;
    size_t configuration_changes = 0u;
    size_t history_valid_frames = 0u;
    for (size_t index = 0u; index < samples_.size(); ++index) {
        history_valid_frames += samples_[index].metadata.history_valid ? 1u : 0u;
        if (index > 0u &&
            !same_configuration(samples_[index - 1u].metadata,
                                samples_[index].metadata)) {
            ++configuration_changes;
        }
    }
    const DxrPerformanceMetadata &metadata = samples_.back().metadata;
    std::ostringstream output;
    output << std::fixed << std::setprecision(4)
           << "{\"type\":\"summary\",\"samples\":" << samples_.size()
           << ",\"warmup_frames\":" << warmup_frames_
           << ",\"target_samples\":" << sample_limit_
           << ",\"configuration_changes\":" << configuration_changes
           << ",\"adapter\":" << json_string(adapter_name_)
           << ",\"adapter_luid\":" << json_string(adapter_luid_)
           << ",\"driver\":" << json_string(driver_version_)
           << ",\"presentation\":\"" << metadata.presentation_width
           << 'x' << metadata.presentation_height
           << "\",\"tracing\":\"" << metadata.tracing_width << 'x'
           << metadata.tracing_height
           << "\",\"reconstruction\":\""
           << metadata.reconstruction_width << 'x'
           << metadata.reconstruction_height << "\",\"settings\":{"
           << "\"spp\":" << metadata.samples_per_pixel
           << ",\"indirect_spp\":"
           << metadata.indirect_samples_per_pixel
           << ",\"maximum_depth\":" << metadata.maximum_depth
           << ",\"light_candidates\":" << metadata.light_candidates
           << ",\"reservoir_limit\":"
           << metadata.reservoir_sample_limit
           << ",\"rr_mode\":"
           << json_string(reconstruction_name(metadata.reconstruction_mode))
           << ",\"validation_enabled\":"
           << (metadata.validation_enabled ? "true" : "false")
           << ",\"split_primary\":"
           << (metadata.split_primary ? "true" : "false")
           << ",\"single_primary_direct_survivor\":"
           << (metadata.single_primary_direct_survivor ? "true" : "false")
           << ",\"single_continuation_lobe\":"
           << (metadata.single_continuation_lobe ? "true" : "false")
           << ",\"history_valid_frames\":" << history_valid_frames
           << ",\"scene_rebuilds_start\":"
           << samples_.front().metadata.scene_rebuild_count
           << ",\"scene_rebuilds_end\":"
           << samples_.back().metadata.scene_rebuild_count << "},\"gpu_ms\":{";
    for (size_t stage = 0u; stage < stage_count; ++stage) {
        if (stage != 0u) {
            output << ',';
        }
        std::vector<double> values;
        values.reserve(samples_.size());
        for (const Sample &sample : samples_) {
            values.push_back(sample.gpu_ms[stage]);
        }
        output << json_string(gpu_stage_names[stage]) << ':';
        write_distribution(output, values);
    }
    output << "},\"cpu_ms\":{";
    struct CpuMetric {
        const char *name;
        double DxrCpuFrameTiming::*member;
    };
    static constexpr CpuMetric cpu_metrics[] = {
        {"frame", &DxrCpuFrameTiming::frame_ms},
        {"present_wait", &DxrCpuFrameTiming::present_wait_ms},
        {"scene_update", &DxrCpuFrameTiming::scene_update_ms},
        {"frame_reuse_wait", &DxrCpuFrameTiming::frame_reuse_wait_ms},
        {"command_record", &DxrCpuFrameTiming::command_record_ms},
        {"queue_submit", &DxrCpuFrameTiming::queue_submit_ms},
        {"present", &DxrCpuFrameTiming::present_ms},
        {"validation_readback", &DxrCpuFrameTiming::validation_readback_ms},
    };
    for (size_t metric = 0u; metric < std::size(cpu_metrics); ++metric) {
        if (metric != 0u) {
            output << ',';
        }
        std::vector<double> values;
        values.reserve(samples_.size());
        for (const Sample &sample : samples_) {
            values.push_back(sample.cpu.*(cpu_metrics[metric].member));
        }
        output << json_string(cpu_metrics[metric].name) << ':';
        write_distribution(output, values);
    }
    output << "}}";
    std::fprintf(stdout, "[DXR-PERF] %s\n", output.str().c_str());
    std::fflush(stdout);
}

void DxrGpuProfiler::shutdown()
{
    report();
    query_heap_.Reset();
    readback_.Reset();
    frames_ = {};
    samples_.clear();
    samples_.shrink_to_fit();
    enabled_ = false;
    recording_frame_ = false;
    reported_ = false;
    warmup_frames_ = 120u;
    sample_limit_ = 600u;
    submitted_frames_ = 0u;
    submitted_profile_frames_ = 0u;
    current_frame_slot_ = 0u;
    timestamp_frequency_ = 0u;
    adapter_name_.clear();
    adapter_luid_.clear();
    driver_version_.clear();
}

}  // namespace ab3d2::dxr
