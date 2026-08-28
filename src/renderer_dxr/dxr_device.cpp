#include "dxr_device.h"

#include "dxr_debug.h"
#include "dxr_pipeline.h"
#include "dxr_temporal_metrics.h"
#if defined(AB3D2_ENABLE_STREAMLINE)
#include "dxr_streamline.h"
#endif

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <vector>

namespace ab3d2::dxr {

namespace {

static_assert(DxrDevice::frame_count == DxrScene::upload_frame_count);

std::string adapter_name(const DXGI_ADAPTER_DESC1 &description)
{
    const std::string name = wide_to_utf8(description.Description);
    return name.empty() ? "unnamed adapter" : name;
}

D3D12_RESOURCE_BARRIER transition_barrier(ID3D12Resource *resource,
                                           D3D12_RESOURCE_STATES before,
                                           D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

}  // namespace

DxrDevice::~DxrDevice()
{
    shutdown();
}

bool DxrDevice::enable_diagnostics(std::string &error)
{
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
    HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings));

    if (FAILED(result)) {
        error = hresult_error(
            "D3D12GetDebugInterface(ID3D12DeviceRemovedExtendedDataSettings)", result);
        return false;
    }
    dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

#if defined(AB3D2_DXR_ENABLE_DEBUG_LAYER)
    Microsoft::WRL::ComPtr<ID3D12Debug1> debug;
    result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    if (FAILED(result)) {
        error = hresult_error("D3D12GetDebugInterface(ID3D12Debug1)", result) +
                "; install the Windows Graphics Tools optional feature or use a "
                "non-Debug build";
        return false;
    }
    debug->EnableDebugLayer();
#if defined(AB3D2_DXR_GPU_VALIDATION)
    debug->SetEnableGPUBasedValidation(TRUE);
    debug->SetEnableSynchronizedCommandQueueValidation(TRUE);
#endif
#else
    (void)error;
#endif
    return true;
}

bool DxrDevice::create_factory(std::string &error)
{
    UINT flags = 0;
#if defined(AB3D2_DXR_ENABLE_DEBUG_LAYER)
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT result;
#if defined(AB3D2_ENABLE_STREAMLINE)
    result = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_proxy_));
#else
    result = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_));
#endif
    if (FAILED(result)) {
        error = hresult_error("CreateDXGIFactory2(IDXGIFactory6)", result);
        return false;
    }
#if defined(AB3D2_ENABLE_STREAMLINE)
    IDXGIFactory6 *native_factory = nullptr;
    if (!streamline_ ||
        !streamline_->get_native_factory(factory_proxy_.Get(), &native_factory,
                                         error)) {
        return false;
    }
    factory_.Attach(native_factory);
#endif
    return true;
}

bool DxrDevice::select_adapter_and_device(std::string &error)
{
    std::ostringstream rejected;
    bool saw_hardware_adapter = false;

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumeration_result = factory_->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));

        if (enumeration_result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(enumeration_result)) {
            error = hresult_error("IDXGIFactory6::EnumAdapterByGpuPreference",
                                  enumeration_result);
            return false;
        }
        DXGI_ADAPTER_DESC1 description = {};
        HRESULT result = candidate->GetDesc1(&description);
        if (FAILED(result)) {
            error = hresult_error("IDXGIAdapter1::GetDesc1", result);
            return false;
        }
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            rejected << adapter_name(description) << " (software adapter rejected); ";
            continue;
        }
        saw_hardware_adapter = true;
#if defined(AB3D2_ENABLE_STREAMLINE)
        std::string support_reason;
        if (!streamline_ ||
            !streamline_->adapter_supported(description.AdapterLuid,
                                            support_reason)) {
            rejected << adapter_name(description) << " (" << support_reason
                     << "); ";
            continue;
        }
        Microsoft::WRL::ComPtr<ID3D12Device5> candidate_proxy_device;
        result = D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&candidate_proxy_device));
        if (FAILED(result)) {
            rejected << adapter_name(description)
                     << " (D3D feature level 12_0 / ID3D12Device5 unavailable); ";
            continue;
        }
        Microsoft::WRL::ComPtr<ID3D12Device5> candidate_device;
        ID3D12Device5 *native_device = nullptr;
        if (!streamline_->get_native_device(candidate_proxy_device.Get(),
                                            &native_device, support_reason)) {
            rejected << adapter_name(description) << " (" << support_reason
                     << "); ";
            continue;
        }
        candidate_device.Attach(native_device);
#else
        Microsoft::WRL::ComPtr<ID3D12Device5> candidate_device;
        result = D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&candidate_device));
        if (FAILED(result)) {
            rejected << adapter_name(description)
                     << " (D3D feature level 12_0 / ID3D12Device5 unavailable); ";
            continue;
        }
#endif
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
        result = candidate_device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
        if (FAILED(result)) {
            rejected << adapter_name(description)
                     << " (D3D12_OPTIONS5 query failed); ";
            continue;
        }
        if (options.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            rejected << adapter_name(description) << " (DXR tier unsupported); ";
            continue;
        }

        adapter_ = std::move(candidate);
        device_ = std::move(candidate_device);
#if defined(AB3D2_ENABLE_STREAMLINE)
        device_proxy_ = std::move(candidate_proxy_device);
        if (!streamline_->set_device(device_.Get(), description.AdapterLuid,
                                     error)) {
            return false;
        }
#endif
        device_->SetName(L"AB3D2 DXR Device");
        debug_output("selected high-performance adapter " + adapter_name(description) +
                     " with DXR tier " +
                     (options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 ?
                          std::string("1.1") : std::string("1.0")));
        break;
    }

    if (!device_) {
        error = saw_hardware_adapter ?
            "no high-performance hardware adapter supports ID3D12Device5, feature "
            "level 12_0, and a nonzero DXR tier" :
            "no high-performance hardware graphics adapter was found";
        if (!rejected.str().empty()) {
            error += "; rejected adapters: " + rejected.str();
        }
        return false;
    }

#if defined(AB3D2_DXR_ENABLE_DEBUG_LAYER)
    if (SUCCEEDED(device_.As(&info_queue_))) {
        D3D12_MESSAGE_SEVERITY denied_severities[] = {
            D3D12_MESSAGE_SEVERITY_INFO,
            D3D12_MESSAGE_SEVERITY_MESSAGE
        };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumSeverities = static_cast<UINT>(std::size(denied_severities));
        filter.DenyList.pSeverityList = denied_severities;
        const HRESULT filter_result = info_queue_->PushStorageFilter(&filter);
        if (FAILED(filter_result)) {
            error = hresult_error("ID3D12InfoQueue::PushStorageFilter", filter_result);
            return false;
        }
        info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        /* Errors are returned through check_debug_messages so hidden GPU
         * validation reports their full text instead of terminating with the
         * debug layer's 0x87a breakpoint exception. */
        info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        info_queue_->ClearStoredMessages();
    } else {
        error = "D3D12 debug layer was enabled but ID3D12InfoQueue is unavailable";
        return false;
    }
#endif
    return true;
}

bool DxrDevice::create_command_objects(std::string &error)
{
    D3D12_COMMAND_QUEUE_DESC queue_description = {};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HRESULT result =
#if defined(AB3D2_ENABLE_STREAMLINE)
        device_proxy_->CreateCommandQueue(
#else
        device_->CreateCommandQueue(
#endif
        &queue_description, IID_PPV_ARGS(&command_queue_));
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Device::CreateCommandQueue", result, error);
    }
    command_queue_->SetName(L"AB3D2 DXR Direct Queue");

    D3D12_DESCRIPTOR_HEAP_DESC heap_description = {};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = frame_count;
    result = device_->CreateDescriptorHeap(
        &heap_description, IID_PPV_ARGS(&render_target_view_heap_));
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Device::CreateDescriptorHeap(RTV)", result,
                                     error);
    }
    render_target_view_heap_->SetName(L"AB3D2 DXR Swap Chain RTV Heap");
    render_target_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (render_target_descriptor_size_ == 0) {
        error = "D3D12 returned a zero RTV descriptor increment size";
        return false;
    }
    return true;
}

bool DxrDevice::create_swap_chain(std::string &error)
{
    DXGI_SWAP_CHAIN_DESC1 description = {};
    description.Width = width_;
    description.Height = height_;
    description.Format = output_.format;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = frame_count;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    description.Flags = hidden_window_ ? 0u : swap_chain_flags;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain;
    HRESULT result =
#if defined(AB3D2_ENABLE_STREAMLINE)
        factory_proxy_->CreateSwapChainForHwnd(
#else
        factory_->CreateSwapChainForHwnd(
#endif
        command_queue_.Get(), window_, &description, nullptr, nullptr, &swap_chain);
    if (FAILED(result)) {
        error = hresult_error("IDXGIFactory::CreateSwapChainForHwnd", result);
        return false;
    }
    result = factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result)) {
        error = hresult_error("IDXGIFactory::MakeWindowAssociation", result);
        return false;
    }
    result = swap_chain.As(&swap_chain_);
    if (FAILED(result)) {
        error = hresult_error("Query IDXGISwapChain4", result);
        return false;
    }
    if (!hidden_window_) {
        result = swap_chain_->SetMaximumFrameLatency(1u);
        if (FAILED(result)) {
            error = hresult_error(
                "IDXGISwapChain2::SetMaximumFrameLatency", result);
            return false;
        }
        frame_latency_waitable_object_ =
            swap_chain_->GetFrameLatencyWaitableObject();
        if (!frame_latency_waitable_object_) {
            const DWORD last_error = GetLastError();
            error = hresult_error(
                "IDXGISwapChain2::GetFrameLatencyWaitableObject",
                HRESULT_FROM_WIN32(last_error != ERROR_SUCCESS ?
                    last_error : ERROR_GEN_FAILURE));
            return false;
        }
    }
    frame_latency_wait_satisfied_ = false;
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return true;
}

bool DxrDevice::configure_output_request(
    const RendererRayTracingOptions &options, std::string &error)
{
    if (options.output < RENDERER_OUTPUT_AUTO ||
        options.output > RENDERER_OUTPUT_HDR) {
        error = "DXR output mode is invalid";
        return false;
    }
    requested_output_ = options.output;
    requested_hdr_peak_nits_ = options.hdr_peak_nits;
    requested_hdr_saturation_percent_ =
        options.hdr_saturation_percent_set != 0u ?
            options.hdr_saturation_percent : 100.0f;

    char output_value[64] = {};
    DWORD length = GetEnvironmentVariableA(
        "AB3D2_DXR_OUTPUT", output_value,
        static_cast<DWORD>(sizeof(output_value)));
    if (length >= sizeof(output_value)) {
        error = "AB3D2_DXR_OUTPUT exceeds 63 bytes";
        return false;
    }
    if (length != 0u) {
        if (_stricmp(output_value, "auto") == 0) {
            requested_output_ = RENDERER_OUTPUT_AUTO;
        } else if (_stricmp(output_value, "sdr") == 0) {
            requested_output_ = RENDERER_OUTPUT_SDR;
        } else if (_stricmp(output_value, "hdr") == 0) {
            requested_output_ = RENDERER_OUTPUT_HDR;
        } else {
            error = "AB3D2_DXR_OUTPUT must be auto, sdr, or hdr";
            return false;
        }
    }
    char obsolete_paper_white[2] = {};
    if (GetEnvironmentVariableA(
            "AB3D2_DXR_HDR_PAPER_WHITE_NITS", obsolete_paper_white,
            static_cast<DWORD>(sizeof(obsolete_paper_white))) != 0u) {
        error = "AB3D2_DXR_HDR_PAPER_WHITE_NITS was removed; Q2RTX has no "
            "scene paper-white remap";
        return false;
    }
    struct FloatOverride {
        const char *name;
        double minimum;
        double maximum;
        float *target;
    };
    const FloatOverride overrides[] = {
        {"AB3D2_DXR_HDR_PEAK_NITS", 100.0, 2000.0,
         &requested_hdr_peak_nits_},
        {"AB3D2_DXR_HDR_SATURATION", 0.0, 200.0,
         &requested_hdr_saturation_percent_},
    };
    for (const FloatOverride &entry : overrides) {
        char value[64] = {};
        length = GetEnvironmentVariableA(
            entry.name, value, static_cast<DWORD>(sizeof(value)));
        if (length >= sizeof(value)) {
            error = std::string(entry.name) + " exceeds 63 bytes";
            return false;
        }
        if (length == 0u) {
            continue;
        }
        char *end = nullptr;
        errno = 0;
        const double parsed = std::strtod(value, &end);
        if (errno != 0 || end == value || *end != '\0' ||
            !std::isfinite(parsed) || parsed < entry.minimum ||
            parsed > entry.maximum) {
            error = std::string(entry.name) + " must be " +
                std::to_string(entry.minimum) + "-" +
                std::to_string(entry.maximum);
            return false;
        }
        *entry.target = static_cast<float>(parsed);
    }
    if ((requested_hdr_peak_nits_ != 0.0f &&
         (!std::isfinite(requested_hdr_peak_nits_) ||
          requested_hdr_peak_nits_ < 100.0f ||
          requested_hdr_peak_nits_ > 2000.0f)) ||
        !std::isfinite(requested_hdr_saturation_percent_) ||
        requested_hdr_saturation_percent_ < 0.0f ||
        requested_hdr_saturation_percent_ > 200.0f) {
        error = "DXR HDR peak must be 100-2000 nits and saturation must be "
            "0-200 percent";
        return false;
    }
    return true;
}

bool DxrDevice::choose_output_configuration(
    DxrOutputConfiguration &output, std::string &display_name,
    std::string &error) const
{
    output = sdr_output_configuration();
    display_name.clear();
    if (hidden_window_ || requested_output_ == RENDERER_OUTPUT_SDR) {
        return true;
    }
    Microsoft::WRL::ComPtr<IDXGIOutput> containing_output;
    HRESULT result = swap_chain_->GetContainingOutput(&containing_output);
    if (FAILED(result)) {
        if (requested_output_ == RENDERER_OUTPUT_HDR) {
            error = hresult_error(
                "IDXGISwapChain::GetContainingOutput for required HDR", result);
            return false;
        }
        return true;
    }
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    result = containing_output.As(&output6);
    if (FAILED(result)) {
        if (requested_output_ == RENDERER_OUTPUT_HDR) {
            error = "HDR output was explicitly requested, but the current "
                "monitor does not expose IDXGIOutput6 advanced-color state";
            return false;
        }
        return true;
    }
    DXGI_OUTPUT_DESC1 description = {};
    result = output6->GetDesc1(&description);
    if (FAILED(result)) {
        if (requested_output_ == RENDERER_OUTPUT_HDR) {
            error = hresult_error("IDXGIOutput6::GetDesc1 for required HDR",
                                  result);
            return false;
        }
        return true;
    }
    display_name = wide_to_utf8(description.DeviceName);
    const bool advanced_color_enabled =
        description.ColorSpace ==
            DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
        (description.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 &&
         description.BitsPerColor >= 10u);
    if (!advanced_color_enabled) {
        if (requested_output_ == RENDERER_OUTPUT_HDR) {
            error = "HDR output was explicitly requested, but Windows advanced "
                "color is not enabled on the window's current monitor";
            return false;
        }
        return true;
    }
    float peak_nits = requested_hdr_peak_nits_;
    if (peak_nits == 0.0f) {
        peak_nits = 800.0f;
    }
    if (!std::isfinite(peak_nits) || peak_nits < 100.0f ||
        peak_nits > 2000.0f) {
        error = "HDR peak luminance must be 100-2000 nits";
        return false;
    }
    output = hdr_output_configuration(
        peak_nits, requested_hdr_saturation_percent_ * 0.01f);
    return true;
}

bool DxrDevice::reconfigure_swap_chain(DxrOutputConfiguration &output,
                                       UINT width, UINT height,
                                       bool recreate_render_targets,
                                       bool allow_hdr_fallback,
                                       std::string &error)
{
    for (FrameContext &frame : frames_) {
        frame.render_target.Reset();
    }
    scene_readback_.Reset();
    scene_motion_readback_.Reset();
    previous_readback_rgb_.clear();
    const auto resize_to = [&](DXGI_FORMAT format) -> bool {
        const HRESULT result = swap_chain_->ResizeBuffers(
            frame_count, width, height, format,
            hidden_window_ ? 0u : swap_chain_flags);
        if (FAILED(result)) {
            error = hresult_error("IDXGISwapChain::ResizeBuffers(output format)",
                                  result);
            return false;
        }
        return true;
    };
    if (output.format != output_.format || width != width_ || height != height_) {
        if (!resize_to(output.format)) {
            if (!output.hdr || !allow_hdr_fallback) {
                return false;
            }
            debug_output(
                "DXR output: FP16 swap-chain resize failed; using SDR");
            output = sdr_output_configuration();
            error.clear();
            if (!resize_to(output.format)) {
                return false;
            }
        }
    }
    if (output.hdr) {
        UINT support = 0u;
        HRESULT result = swap_chain_->CheckColorSpaceSupport(
            output.color_space, &support);
        if (FAILED(result) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0u) {
            if (!allow_hdr_fallback) {
                error = FAILED(result) ?
                    hresult_error(
                        "IDXGISwapChain::CheckColorSpaceSupport(scRGB)", result) :
                    "HDR output was explicitly requested, but the swap chain "
                    "cannot present FP16 scRGB on the current monitor";
                return false;
            }
            debug_output(
                "DXR output: current monitor rejected FP16 scRGB; using SDR");
            output = sdr_output_configuration();
            if (!resize_to(output.format)) {
                return false;
            }
        }
    }
    HRESULT color_result = swap_chain_->SetColorSpace1(output.color_space);
    if (FAILED(color_result) && output.hdr && allow_hdr_fallback) {
        debug_output(
            "DXR output: scRGB color-space activation failed; using SDR");
        output = sdr_output_configuration();
        if (!resize_to(output.format)) {
            return false;
        }
        color_result = swap_chain_->SetColorSpace1(output.color_space);
    }
    if (FAILED(color_result)) {
        error = hresult_error("IDXGISwapChain3::SetColorSpace1", color_result);
        return false;
    }
    output_ = output;
    width_ = width;
    height_ = height;
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    if (recreate_render_targets && !create_render_targets(error)) {
        return false;
    }
    return true;
}

bool DxrDevice::refresh_output_configuration(DxrPipeline &pipeline,
                                             UINT width, UINT height,
                                             std::string &error)
{
    DxrOutputConfiguration desired;
    std::string display_name;
    if (!choose_output_configuration(desired, display_name, error)) {
        return false;
    }
    if (desired.format != output_.format ||
        desired.color_space != output_.color_space) {
        if (!flush(error) ||
            !reconfigure_swap_chain(
                desired, width, height, true,
                requested_output_ != RENDERER_OUTPUT_HDR, error) ||
            !pipeline.configure_output(device_.Get(), desired, error) ||
            !check_debug_messages(error)) {
            return false;
        }
        const std::string output_message = std::string("DXR output: ") +
            (desired.hdr ? "FP16 scRGB HDR" : "8-bit sRGB SDR") +
            (display_name.empty() ? std::string() :
                std::string(" on ") + display_name) +
            (desired.hdr ?
                std::string(" peak=") + std::to_string(desired.peak_nits) +
                    " nits saturation=" +
                    std::to_string(desired.saturation_scale * 100.0f) + "%" :
                std::string());
        debug_output(output_message);
        if (!hidden_window_) {
            std::fprintf(stdout, "[RENDER] %s\n", output_message.c_str());
        }
    } else if (!output_configuration_equal(desired, output_)) {
        output_ = desired;
        if (!pipeline.configure_output(device_.Get(), output_, error)) {
            return false;
        }
    }
    return resize(width, height, error);
}

bool DxrDevice::create_frame_contexts(std::string &error)
{
    HRESULT result;

    for (UINT index = 0; index < frame_count; ++index) {
        result = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frames_[index].command_allocator));
        if (FAILED(result)) {
            return fail_device_operation("ID3D12Device::CreateCommandAllocator", result,
                                         error);
        }
        wchar_t name[96] = {};
        (void)swprintf_s(name, L"AB3D2 DXR Frame %u Command Allocator", index);
        frames_[index].command_allocator->SetName(name);
    }
    result = device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        frames_[frame_index_].command_allocator.Get(), nullptr,
        IID_PPV_ARGS(&command_list_));
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Device::CreateCommandList", result, error);
    }
    command_list_->SetName(L"AB3D2 DXR Diagnostic Command List");
    result = command_list_->Close();
    if (FAILED(result)) {
        return fail_device_operation("ID3D12GraphicsCommandList::Close(initial)", result,
                                     error);
    }
    result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Device::CreateFence", result, error);
    }
    fence_->SetName(L"AB3D2 DXR Frame Fence");
    fence_event_ = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (!fence_event_) {
        error = hresult_error("CreateEventExW(D3D12 fence)",
                              HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    return true;
}

bool DxrDevice::create_render_targets(std::string &error)
{
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor =
        render_target_view_heap_->GetCPUDescriptorHandleForHeapStart();

    for (UINT index = 0; index < frame_count; ++index) {
        HRESULT result = swap_chain_->GetBuffer(
            index, IID_PPV_ARGS(&frames_[index].render_target));
        if (FAILED(result)) {
            error = hresult_error("IDXGISwapChain::GetBuffer", result);
            return false;
        }
        frames_[index].render_target_view = descriptor;
        device_->CreateRenderTargetView(frames_[index].render_target.Get(), nullptr,
                                        descriptor);
        wchar_t name[96] = {};
        (void)swprintf_s(name, L"AB3D2 DXR Swap Chain Buffer %u", index);
        frames_[index].render_target->SetName(name);
        descriptor.ptr += render_target_descriptor_size_;
    }
    return true;
}

bool DxrDevice::ensure_scene_readback(std::string &error)
{
    if (scene_readback_ && readback_width_ == width_ &&
        readback_height_ == height_) {
        return true;
    }
    scene_readback_.Reset();
    D3D12_RESOURCE_DESC source_description =
        frames_[frame_index_].render_target->GetDesc();
    UINT64 row_bytes = 0;
    device_->GetCopyableFootprints(
        &source_description, 0, 1, 0, &readback_footprint_,
        &readback_row_count_, &row_bytes, &readback_total_bytes_);
    if (readback_total_bytes_ == 0u || readback_row_count_ != height_) {
        error = "D3D12 returned invalid swap-chain readback footprints";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = readback_total_bytes_;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT result = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&scene_readback_));
    if (FAILED(result)) {
        return fail_device_operation(
            "ID3D12Device::CreateCommittedResource(scene readback)", result,
            error);
    }
    scene_readback_->SetName(L"AB3D2 DXR Hidden Smoke Readback");
    readback_width_ = width_;
    readback_height_ = height_;
    return true;
}

bool DxrDevice::ensure_scene_motion_readback(
    ID3D12Resource *source, std::string &error)
{
    if (!source) {
        error = "DXR temporal validation readback has no motion texture";
        return false;
    }
    const D3D12_RESOURCE_DESC source_description = source->GetDesc();
    if (source_description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source_description.Format != DXGI_FORMAT_R16G16_FLOAT ||
        source_description.Width == 0u ||
        source_description.Width > std::numeric_limits<UINT>::max() ||
        source_description.Height == 0u) {
        error = "DXR temporal validation motion texture has an invalid format";
        return false;
    }
    const UINT source_width = static_cast<UINT>(source_description.Width);
    if (scene_motion_readback_ && motion_readback_width_ == source_width &&
        motion_readback_height_ == source_description.Height) {
        return true;
    }
    scene_motion_readback_.Reset();
    UINT64 row_bytes = 0u;
    device_->GetCopyableFootprints(
        &source_description, 0u, 1u, 0u, &motion_readback_footprint_,
        &motion_readback_row_count_, &row_bytes,
        &motion_readback_total_bytes_);
    if (motion_readback_total_bytes_ == 0u ||
        motion_readback_row_count_ != source_description.Height ||
        row_bytes != static_cast<UINT64>(source_width) * 4u) {
        error = "D3D12 returned invalid temporal-motion readback footprints";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1u;
    heap.VisibleNodeMask = 1u;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = motion_readback_total_bytes_;
    buffer.Height = 1u;
    buffer.DepthOrArraySize = 1u;
    buffer.MipLevels = 1u;
    buffer.SampleDesc.Count = 1u;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT result = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&scene_motion_readback_));
    if (FAILED(result)) {
        return fail_device_operation(
            "ID3D12Device::CreateCommittedResource(scene motion readback)",
            result, error);
    }
    scene_motion_readback_->SetName(
        L"AB3D2 DXR Hidden Smoke Motion Readback");
    motion_readback_width_ = source_width;
    motion_readback_height_ = source_description.Height;
    return true;
}

bool DxrDevice::ensure_noisy_radiance_readback(
    ID3D12Resource *source, std::string &error)
{
    if (!source) {
        error = "DXR noisy-radiance readback has no source texture";
        return false;
    }
    const D3D12_RESOURCE_DESC source_description = source->GetDesc();
    if (source_description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source_description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        source_description.Width == 0u ||
        source_description.Width > std::numeric_limits<UINT>::max() ||
        source_description.Height == 0u) {
        error = "DXR noisy-radiance readback source has an invalid format";
        return false;
    }
    const UINT source_width = static_cast<UINT>(source_description.Width);
    if (noisy_radiance_readback_ &&
        noisy_readback_width_ == source_width &&
        noisy_readback_height_ == source_description.Height) {
        return true;
    }
    noisy_radiance_readback_.Reset();
    last_noisy_radiance_rgb_.clear();
    UINT64 row_bytes = 0u;
    device_->GetCopyableFootprints(
        &source_description, 0u, 1u, 0u, &noisy_readback_footprint_,
        &noisy_readback_row_count_, &row_bytes,
        &noisy_readback_total_bytes_);
    if (noisy_readback_total_bytes_ == 0u ||
        noisy_readback_row_count_ != source_description.Height ||
        row_bytes != static_cast<UINT64>(source_width) * 8u) {
        error = "D3D12 returned invalid noisy-radiance readback footprints";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    heap.CreationNodeMask = 1u;
    heap.VisibleNodeMask = 1u;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = noisy_readback_total_bytes_;
    buffer.Height = 1u;
    buffer.DepthOrArraySize = 1u;
    buffer.MipLevels = 1u;
    buffer.SampleDesc.Count = 1u;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT result = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&noisy_radiance_readback_));
    if (FAILED(result)) {
        return fail_device_operation(
            "ID3D12Device::CreateCommittedResource(noisy radiance readback)",
            result, error);
    }
    noisy_radiance_readback_->SetName(
        L"AB3D2 DXR Noisy Radiance Validation Readback");
    noisy_readback_width_ = source_width;
    noisy_readback_height_ = source_description.Height;
    return true;
}

bool DxrDevice::collect_noisy_radiance_readback(std::string &error)
{
    if (!noisy_radiance_readback_ || noisy_readback_width_ == 0u ||
        noisy_readback_height_ == 0u) {
        error = "DXR noisy-radiance validation readback is unavailable";
        return false;
    }
    const D3D12_RANGE read_range = {
        0u, static_cast<SIZE_T>(noisy_readback_total_bytes_)};
    void *mapped = nullptr;
    const HRESULT result = noisy_radiance_readback_->Map(
        0u, &read_range, &mapped);
    if (FAILED(result)) {
        return fail_device_operation(
            "ID3D12Resource::Map(noisy radiance readback)", result, error);
    }
    const size_t pixel_count =
        static_cast<size_t>(noisy_readback_width_) * noisy_readback_height_;
    last_noisy_radiance_rgb_.resize(pixel_count * 3u);
    const auto *pixels = static_cast<const uint8_t *>(mapped) +
        noisy_readback_footprint_.Offset;
    for (UINT y = 0u; y < noisy_readback_height_; ++y) {
        const uint8_t *row = pixels + static_cast<size_t>(y) *
            noisy_readback_footprint_.Footprint.RowPitch;
        for (UINT x = 0u; x < noisy_readback_width_; ++x) {
            const auto *pixel = reinterpret_cast<const uint16_t *>(
                row + static_cast<size_t>(x) * 8u);
            const size_t destination =
                (static_cast<size_t>(y) * noisy_readback_width_ + x) * 3u;
            std::copy_n(pixel, 3u,
                        last_noisy_radiance_rgb_.begin() + destination);
        }
    }
    const D3D12_RANGE no_write = {0u, 0u};
    noisy_radiance_readback_->Unmap(0u, &no_write);
    return true;
}

bool DxrDevice::enable_noisy_radiance_readback()
{
    if (!hidden_window_) {
        return false;
    }
    noisy_radiance_readback_enabled_ = true;
    return true;
}

bool DxrDevice::copy_last_noisy_radiance(
    uint16_t *out_values, size_t value_count) const
{
    if (!out_values || last_noisy_radiance_rgb_.empty() ||
        value_count != last_noisy_radiance_rgb_.size()) {
        return false;
    }
    std::copy(last_noisy_radiance_rgb_.begin(),
              last_noisy_radiance_rgb_.end(), out_values);
    return true;
}

bool DxrDevice::collect_scene_readback(UINT64 fence_value, std::string &error)
{
    if (!scene_readback_ || !scene_motion_readback_ || !wait_for_fence(
            fence_value, "wait for DXR hidden smoke readback", error)) {
        return false;
    }
    const D3D12_RANGE motion_read_range = {
        0u, static_cast<SIZE_T>(motion_readback_total_bytes_)};
    void *mapped_motion = nullptr;
    HRESULT result = scene_motion_readback_->Map(
        0u, &motion_read_range, &mapped_motion);
    if (FAILED(result)) {
        return fail_device_operation(
            "ID3D12Resource::Map(scene motion readback)", result, error);
    }
    const size_t motion_pixel_count =
        static_cast<size_t>(motion_readback_width_) * motion_readback_height_;
    std::vector<uint16_t> current_motion(motion_pixel_count * 2u);
    const auto *motion_pixels = static_cast<const uint8_t *>(mapped_motion) +
        motion_readback_footprint_.Offset;
    for (UINT y = 0u; y < motion_readback_height_; ++y) {
        const auto *row = reinterpret_cast<const uint16_t *>(
            motion_pixels + static_cast<size_t>(y) *
                motion_readback_footprint_.Footprint.RowPitch);
        std::copy_n(
            row, static_cast<size_t>(motion_readback_width_) * 2u,
            current_motion.begin() +
                static_cast<size_t>(y) * motion_readback_width_ * 2u);
    }
    const D3D12_RANGE no_motion_write = {0u, 0u};
    scene_motion_readback_->Unmap(0u, &no_motion_write);

    D3D12_RANGE read_range = {0, static_cast<SIZE_T>(readback_total_bytes_)};
    void *mapped = nullptr;
    result = scene_readback_->Map(0, &read_range, &mapped);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Resource::Map(scene readback)",
                                     result, error);
    }
    uint64_t checksum = UINT64_C(1469598103934665603);
    uint64_t nonzero_pixels = 0u;
    uint64_t saturated_pixels = 0u;
    double luminance_sum = 0.0;
    uint8_t maximum_component = 0u;
    const size_t pixel_count =
        static_cast<size_t>(readback_width_) * readback_height_;
    /* Retaining the previous frame's RGB lets the smoke gate measure temporal
     * stability directly instead of inferring it from a checksum. */
    const bool comparable = previous_readback_rgb_.size() == pixel_count * 3u;
    std::vector<uint8_t> current_rgb(pixel_count * 3u);
    uint64_t delta_sum = 0u;
    uint64_t temporal_outlier_pixels = 0u;
    uint8_t maximum_temporal_delta = 0u;
    const auto *pixels = static_cast<const uint8_t *>(mapped) +
        readback_footprint_.Offset;
    for (UINT y = 0; y < readback_height_; ++y) {
        const uint8_t *row = pixels +
            static_cast<size_t>(y) * readback_footprint_.Footprint.RowPitch;
        for (UINT x = 0; x < readback_width_; ++x) {
            const uint8_t *pixel = row + static_cast<size_t>(x) * 4u;
            const size_t rgb_index =
                (static_cast<size_t>(y) * readback_width_ + x) * 3u;
            nonzero_pixels += pixel[0] != 0u || pixel[1] != 0u || pixel[2] != 0u;
            saturated_pixels += pixel[0] >= 250u || pixel[1] >= 250u ||
                pixel[2] >= 250u;
            luminance_sum += pixel[0] * 0.2126 + pixel[1] * 0.7152 +
                pixel[2] * 0.0722;
            uint8_t pixel_temporal_delta = 0u;
            for (UINT component = 0; component < 3u; ++component) {
                maximum_component = std::max(maximum_component, pixel[component]);
                checksum ^= pixel[component];
                checksum *= UINT64_C(1099511628211);
                current_rgb[rgb_index + component] = pixel[component];
                if (comparable) {
                    const int difference =
                        static_cast<int>(pixel[component]) -
                        static_cast<int>(
                            previous_readback_rgb_[rgb_index + component]);
                    delta_sum += static_cast<uint64_t>(
                        difference < 0 ? -difference : difference);
                    pixel_temporal_delta = std::max(
                        pixel_temporal_delta,
                        static_cast<uint8_t>(
                            difference < 0 ? -difference : difference));
                }
            }
            if (comparable) {
                maximum_temporal_delta = std::max(
                    maximum_temporal_delta, pixel_temporal_delta);
                temporal_outlier_pixels += pixel_temporal_delta >= 16u;
            }
        }
    }
    wchar_t capture_path[32768] = {};
    const DWORD capture_length = GetEnvironmentVariableW(
        L"AB3D2_DXR_CAPTURE_PPM", capture_path,
        static_cast<DWORD>(std::size(capture_path)));
    if (capture_length > 0u && capture_length < std::size(capture_path)) {
        std::ofstream capture(std::filesystem::path(capture_path),
                              std::ios::binary | std::ios::trunc);
        if (capture) {
            capture << "P6\n" << readback_width_ << ' ' << readback_height_
                    << "\n255\n";
            for (UINT y = 0; y < readback_height_; ++y) {
                const uint8_t *row = pixels + static_cast<size_t>(y) *
                    readback_footprint_.Footprint.RowPitch;
                for (UINT x = 0; x < readback_width_; ++x) {
                    capture.write(reinterpret_cast<const char *>(
                                      row + static_cast<size_t>(x) * 4u), 3);
                }
            }
        }
    }
    D3D12_RANGE no_write = {0, 0};
    scene_readback_->Unmap(0, &no_write);
    last_scene_rgb_checksum_ = nonzero_pixels == 0u ? 0u : checksum;
    last_scene_saturated_pixels_ = saturated_pixels;
    last_scene_temporal_outlier_pixels_ = temporal_outlier_pixels;
    last_scene_frame_delta_ = comparable && pixel_count != 0u ?
        static_cast<double>(delta_sum) /
            static_cast<double>(pixel_count * 3u) : -1.0;
    const temporal_metrics::Difference reprojected = comparable ?
        temporal_metrics::measure_reprojected_rgb(
            current_rgb, previous_readback_rgb_, readback_width_,
            readback_height_, current_motion, motion_readback_width_,
            motion_readback_height_, 4u) : temporal_metrics::Difference{};
    last_scene_reprojected_frame_delta_ =
        reprojected.mean_absolute_component;
    last_scene_reprojected_temporal_outlier_pixels_ =
        reprojected.outlier_pixels;
    last_scene_reprojected_pixel_count_ = reprojected.compared_pixels;
    previous_readback_rgb_ = std::move(current_rgb);
    std::ostringstream statistics;
    statistics << "readback: nonzero=" << nonzero_pixels << '/'
               << static_cast<uint64_t>(readback_width_) * readback_height_
               << " mean="
               << luminance_sum /
                    (static_cast<double>(readback_width_) * readback_height_)
               << " max=" << static_cast<unsigned>(maximum_component)
               << " saturated=" << saturated_pixels
               << " delta=" << last_scene_frame_delta_
               << " delta16=" << temporal_outlier_pixels
               << " deltaMax=" << static_cast<unsigned>(maximum_temporal_delta)
               << " reprojected=" << last_scene_reprojected_frame_delta_
               << " reprojected16="
               << last_scene_reprojected_temporal_outlier_pixels_
               << " reprojectedSamples="
               << last_scene_reprojected_pixel_count_
               << " reprojectedMax=" << reprojected.maximum_component;
    debug_output(statistics.str());
    return true;
}

bool DxrDevice::initialize(HWND window, bool hidden_window,
                           const RendererRayTracingOptions &options,
                           DxrStreamline *streamline, std::string &error)
{
    RECT client = {};

    if (!window || !IsWindow(window)) {
        error = "DXR device initialization received no valid HWND";
        return false;
    }
    window_ = window;
    hidden_window_ = hidden_window;
    streamline_ = streamline;
    if (!configure_output_request(options, error)) {
        return false;
    }
    if (!GetClientRect(window_, &client)) {
        error = hresult_error("GetClientRect(DXR window)",
                              HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    width_ = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
    height_ = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));

    if (!enable_diagnostics(error) || !create_factory(error) ||
        !select_adapter_and_device(error) || !create_command_objects(error) ||
        !create_swap_chain(error)) {
        return false;
    }
    DxrOutputConfiguration selected_output;
    std::string display_name;
    if (!choose_output_configuration(selected_output, display_name, error) ||
        !reconfigure_swap_chain(
            selected_output, width_, height_, false,
            requested_output_ != RENDERER_OUTPUT_HDR, error) ||
        !create_frame_contexts(error) ||
        !create_render_targets(error)) {
        return false;
    }
    const std::string output_message = std::string("DXR output: ") +
        (output_.hdr ? "FP16 scRGB HDR" : "8-bit sRGB SDR") +
        (hidden_window_ ? " (hidden validation forced SDR)" :
            (display_name.empty() ? std::string() :
                std::string(" on ") + display_name)) +
        (output_.hdr ?
            std::string(" peak=") + std::to_string(output_.peak_nits) +
                " nits saturation=" +
                std::to_string(output_.saturation_scale * 100.0f) + "%" :
            std::string());
    debug_output(output_message);
    if (!hidden_window_) {
        std::fprintf(stdout, "[RENDER] %s\n", output_message.c_str());
    }
    return check_debug_messages(error);
}

bool DxrDevice::wait_for_fence(UINT64 fence_value, const char *operation,
                               std::string &error)
{
    if (!fence_ || fence_->GetCompletedValue() >= fence_value) {
        return true;
    }
    HRESULT result = fence_->SetEventOnCompletion(fence_value, fence_event_);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12Fence::SetEventOnCompletion", result, error);
    }
    const DWORD wait_result = WaitForSingleObject(fence_event_, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD last_error = wait_result == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
        error = hresult_error(operation ? operation : "WaitForSingleObject(D3D12 fence)",
                              HRESULT_FROM_WIN32(last_error));
        return false;
    }
    return true;
}

bool DxrDevice::wait_for_frame(FrameContext &frame, std::string &error)
{
    return frame.fence_value == 0 ||
           wait_for_fence(frame.fence_value, "wait for reusable D3D12 frame", error);
}

bool DxrDevice::wait_for_present(std::string &error)
{
    /* Hidden validation reads every frame back through a GPU fence and has no
     * input-to-photon path. Display pacing there would contaminate its timing
     * metric with the monitor refresh interval. */
    if (hidden_window_ || frame_latency_wait_satisfied_ || IsIconic(window_)) {
        return true;
    }
    if (!swap_chain_ || !frame_latency_waitable_object_) {
        error = "DXR frame-latency wait received incomplete swap-chain state";
        return false;
    }
    const DWORD wait_result = WaitForSingleObject(
        frame_latency_waitable_object_, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        const DWORD last_error = wait_result == WAIT_FAILED ?
            GetLastError() : ERROR_GEN_FAILURE;
        error = hresult_error("wait for DXGI presentation slot",
                              HRESULT_FROM_WIN32(last_error));
        return false;
    }
    frame_latency_wait_satisfied_ = true;
    return true;
}

bool DxrDevice::flush(std::string &error)
{
    if (!command_queue_ || !fence_) {
        return true;
    }
    const UINT64 fence_value = next_fence_value_++;
    const HRESULT result = command_queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12CommandQueue::Signal(flush)", result, error);
    }
    if (!wait_for_fence(fence_value, "wait for D3D12 queue flush", error)) {
        return false;
    }
    for (FrameContext &frame : frames_) {
        frame.fence_value = 0;
    }
    return check_debug_messages(error);
}

bool DxrDevice::resize(UINT width, UINT height, std::string &error)
{
    if (width == 0 || height == 0 || (width == width_ && height == height_)) {
        return true;
    }
    if (!flush(error)) {
        return false;
    }
    for (FrameContext &frame : frames_) {
        frame.render_target.Reset();
    }
    const HRESULT result = swap_chain_->ResizeBuffers(
        frame_count, width, height, output_.format,
        hidden_window_ ? 0u : swap_chain_flags);
    if (FAILED(result)) {
        return fail_device_operation("IDXGISwapChain::ResizeBuffers", result, error);
    }
    width_ = width;
    height_ = height;
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return create_render_targets(error) && check_debug_messages(error);
}

bool DxrDevice::render(DxrPipeline &pipeline, const SceneFrame &scene_frame,
                       const RenderView &view, std::string &error)
{
    RECT client = {};
    bool scene_requires_flush = false;

    if (!device_ || !swap_chain_ || !pipeline.pipeline_state() ||
        !pipeline.root_signature()) {
        error = "DXR render received incomplete device or pipeline state";
        return false;
    }
    if (IsIconic(window_)) {
        return true;
    }
    if (!GetClientRect(window_, &client)) {
        error = hresult_error("GetClientRect(DXR present)",
                              HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    const LONG client_width = client.right - client.left;
    const LONG client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0) {
        return true;
    }
    /* Interactive callers wait before sampling input. Direct presentation
     * clients (including hidden validation) are paced here as a safe fallback. */
    if (!wait_for_present(error)) {
        return false;
    }
    if (!refresh_output_configuration(
            pipeline, static_cast<UINT>(client_width),
            static_cast<UINT>(client_height), error)) {
        return false;
    }
    if (!pipeline.update_scene(scene_frame, view, width_, height_,
                               scene_requires_flush, error)) {
        return false;
    }
    if (scene_requires_flush && !flush(error)) {
        return false;
    }

    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    if (frame_index_ >= frame_count) {
        error = "DXGI returned an out-of-range swap-chain frame index";
        return false;
    }
    FrameContext &frame = frames_[frame_index_];
    if (!wait_for_frame(frame, error)) {
        return false;
    }
    HRESULT result = frame.command_allocator->Reset();
    if (FAILED(result)) {
        return fail_device_operation("ID3D12CommandAllocator::Reset", result, error);
    }
    result = command_list_->Reset(frame.command_allocator.Get(), nullptr);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12GraphicsCommandList::Reset", result, error);
    }

    const D3D12_RESOURCE_BARRIER to_render_target = transition_barrier(
        frame.render_target.Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list_->ResourceBarrier(1, &to_render_target);
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->OMSetRenderTargets(1, &frame.render_target_view, FALSE, nullptr);
    static constexpr FLOAT clear_color[4] = {0.018f, 0.028f, 0.052f, 1.0f};
    command_list_->ClearRenderTargetView(frame.render_target_view, clear_color, 0, nullptr);
    const auto render_time = std::chrono::steady_clock::now();
    const float exposure_delta_seconds = previous_render_time_valid_ ?
        std::chrono::duration<float>(render_time - previous_render_time_).count() :
        0.0f;
    previous_render_time_ = render_time;
    previous_render_time_valid_ = true;
    if (!pipeline.record(device_.Get(), command_list_.Get(), width_, height_,
                          frame.render_target_view, scene_frame, view,
                          rendered_frame_count_++,
                          frame_index_, exposure_delta_seconds,
                          streamline_, error)) {
        return false;
    }
    const bool capture_scene = hidden_window_ && pipeline.has_scene();
    ID3D12Resource *const scene_motion =
        pipeline.streamline_scene_motion_resource();
    if (capture_scene) {
        if (!ensure_scene_motion_readback(scene_motion, error)) {
            return false;
        }
        const D3D12_RESOURCE_BARRIER to_motion_copy = transition_barrier(
            scene_motion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list_->ResourceBarrier(1u, &to_motion_copy);
        D3D12_TEXTURE_COPY_LOCATION motion_destination = {};
        motion_destination.pResource = scene_motion_readback_.Get();
        motion_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        motion_destination.PlacedFootprint = motion_readback_footprint_;
        D3D12_TEXTURE_COPY_LOCATION motion_source = {};
        motion_source.pResource = scene_motion;
        motion_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list_->CopyTextureRegion(
            &motion_destination, 0u, 0u, 0u, &motion_source, nullptr);
        const D3D12_RESOURCE_BARRIER from_motion_copy = transition_barrier(
            scene_motion, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        command_list_->ResourceBarrier(1u, &from_motion_copy);
    }
    ID3D12Resource *const noisy_radiance = pipeline.reconstruction_resource(
        DxrReconstructionBuffer::noisy_radiance);
    const bool capture_noisy_radiance = capture_scene &&
        noisy_radiance_readback_enabled_;
    if (capture_noisy_radiance) {
        if (!ensure_noisy_radiance_readback(noisy_radiance, error)) {
            return false;
        }
        const D3D12_RESOURCE_BARRIER to_noisy_copy = transition_barrier(
            noisy_radiance, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list_->ResourceBarrier(1u, &to_noisy_copy);
        D3D12_TEXTURE_COPY_LOCATION noisy_destination = {};
        noisy_destination.pResource = noisy_radiance_readback_.Get();
        noisy_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        noisy_destination.PlacedFootprint = noisy_readback_footprint_;
        D3D12_TEXTURE_COPY_LOCATION noisy_source = {};
        noisy_source.pResource = noisy_radiance;
        noisy_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list_->CopyTextureRegion(
            &noisy_destination, 0u, 0u, 0u, &noisy_source, nullptr);
        const D3D12_RESOURCE_BARRIER from_noisy_copy = transition_barrier(
            noisy_radiance, D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        command_list_->ResourceBarrier(1u, &from_noisy_copy);
    } else if (noisy_radiance_readback_enabled_) {
        last_noisy_radiance_rgb_.clear();
    }
    if (capture_scene) {
        if (!ensure_scene_readback(error)) {
            return false;
        }
        const D3D12_RESOURCE_BARRIER to_copy = transition_barrier(
            frame.render_target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list_->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = scene_readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = readback_footprint_;
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = frame.render_target.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        const D3D12_RESOURCE_BARRIER to_present = transition_barrier(
            frame.render_target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PRESENT);
        command_list_->ResourceBarrier(1, &to_present);
    } else {
        last_scene_rgb_checksum_ = 0;
        last_scene_frame_delta_ = -1.0;
        last_scene_reprojected_frame_delta_ = -1.0;
        last_scene_saturated_pixels_ = 0;
        last_scene_temporal_outlier_pixels_ = 0;
        last_scene_reprojected_temporal_outlier_pixels_ = 0;
        last_scene_reprojected_pixel_count_ = 0;
        previous_readback_rgb_.clear();
        const D3D12_RESOURCE_BARRIER to_present = transition_barrier(
            frame.render_target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        command_list_->ResourceBarrier(1, &to_present);
    }
    result = command_list_->Close();
    if (FAILED(result)) {
        std::string debug_error;
        if (!check_debug_messages(debug_error)) {
            error = hresult_error("ID3D12GraphicsCommandList::Close", result) +
                "; " + debug_error;
            return false;
        }
        return fail_device_operation("ID3D12GraphicsCommandList::Close", result, error);
    }
    ID3D12CommandList *command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);

    result = swap_chain_->Present(hidden_window_ ? 0 : 1, 0);
    if (FAILED(result)) {
        return fail_device_operation("IDXGISwapChain::Present", result, error);
    }
    frame_latency_wait_satisfied_ = false;
    pipeline.commit_presented_frame();
    const UINT64 fence_value = next_fence_value_++;
    result = command_queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12CommandQueue::Signal(frame)", result, error);
    }
    frame.fence_value = fence_value;
    if (capture_scene) {
        if (!collect_scene_readback(fence_value, error) ||
            (capture_noisy_radiance &&
             !collect_noisy_radiance_readback(error)) ||
            !pipeline.collect_diagnostics(error)) {
            return false;
        }
    }
    return check_debug_messages(error);
}

bool DxrDevice::presentation_size(int &width, int &height) const
{
    RECT client = {};
    if (!window_ || IsIconic(window_) || !GetClientRect(window_, &client) ||
        client.right <= client.left || client.bottom <= client.top) {
        return false;
    }
    width = client.right - client.left;
    height = client.bottom - client.top;
    return true;
}

bool DxrDevice::check_debug_messages(std::string &error)
{
    if (!info_queue_) {
        return true;
    }
    const UINT64 message_count = info_queue_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < message_count; ++index) {
        SIZE_T message_size = 0;
        HRESULT result = info_queue_->GetMessage(index, nullptr, &message_size);
        if (FAILED(result) || message_size < sizeof(D3D12_MESSAGE)) {
            error = hresult_error("ID3D12InfoQueue::GetMessage(size)", result);
            info_queue_->ClearStoredMessages();
            return false;
        }
        std::vector<unsigned char> storage(message_size);
        D3D12_MESSAGE *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        result = info_queue_->GetMessage(index, message, &message_size);
        if (FAILED(result)) {
            error = hresult_error("ID3D12InfoQueue::GetMessage", result);
            info_queue_->ClearStoredMessages();
            return false;
        }
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            error = "D3D12 debug layer reported ";
            error += message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ?
                "corruption: " : message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ?
                "an error: " : "a warning: ";
            if (message->pDescription) {
                error += message->pDescription;
            }
            info_queue_->ClearStoredMessages();
            return false;
        }
    }
    info_queue_->ClearStoredMessages();
    return true;
}

bool DxrDevice::fail_device_operation(const char *operation, HRESULT result,
                                      std::string &error)
{
    const HRESULT removal_reason = device_ ? device_->GetDeviceRemovedReason() : S_OK;
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
        FAILED(removal_reason)) {
        error = device_removed_report(operation, result);
    } else {
        error = hresult_error(operation, result);
    }
    debug_output(error);
    return false;
}

std::string DxrDevice::device_removed_report(const char *operation, HRESULT result) const
{
    std::ostringstream report;
    report << hresult_error(operation, result);
    if (!device_) {
        return report.str();
    }
    const HRESULT reason = device_->GetDeviceRemovedReason();
    report << "; device removal reason: " << hresult_error("GetDeviceRemovedReason", reason);

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (FAILED(device_.As(&dred))) {
        report << "; DRED output interface unavailable";
        return report.str();
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)) &&
        breadcrumbs.pHeadAutoBreadcrumbNode) {
        report << "; DRED breadcrumbs";
        const D3D12_AUTO_BREADCRUMB_NODE *node = breadcrumbs.pHeadAutoBreadcrumbNode;
        for (unsigned count = 0; node && count < 8; ++count, node = node->pNext) {
            const UINT completed = node->pLastBreadcrumbValue ?
                *node->pLastBreadcrumbValue : 0;
            report << " [" << (node->pCommandListDebugNameA ?
                                  node->pCommandListDebugNameA : "unnamed command list")
                   << " " << completed << "/" << node->BreadcrumbCount << "]";
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT page_fault = {};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&page_fault)) &&
        page_fault.PageFaultVA != 0) {
        report << "; DRED page-fault VA=0x" << std::hex << page_fault.PageFaultVA;
        if (page_fault.pHeadExistingAllocationNode) {
            const D3D12_DRED_ALLOCATION_NODE *node = page_fault.pHeadExistingAllocationNode;
            report << " existing="
                   << (node->ObjectNameA ? node->ObjectNameA : "unnamed allocation");
        }
        if (page_fault.pHeadRecentFreedAllocationNode) {
            const D3D12_DRED_ALLOCATION_NODE *node =
                page_fault.pHeadRecentFreedAllocationNode;
            report << " recently-freed="
                   << (node->ObjectNameA ? node->ObjectNameA : "unnamed allocation");
        }
    }
    return report.str();
}

void DxrDevice::shutdown(bool flush_queue)
{
    if (flush_queue && command_queue_ && fence_ && fence_event_) {
        std::string flush_error;
        if (!flush(flush_error)) {
            debug_output("shutdown flush failed: " + flush_error);
        }
    }
    for (FrameContext &frame : frames_) {
        frame.render_target.Reset();
        frame.command_allocator.Reset();
        frame.fence_value = 0;
    }
    command_list_.Reset();
    scene_readback_.Reset();
    scene_motion_readback_.Reset();
    noisy_radiance_readback_.Reset();
    render_target_view_heap_.Reset();
    swap_chain_.Reset();
    if (frame_latency_waitable_object_) {
        CloseHandle(frame_latency_waitable_object_);
        frame_latency_waitable_object_ = nullptr;
    }
    frame_latency_wait_satisfied_ = false;
    command_queue_.Reset();
    fence_.Reset();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    info_queue_.Reset();
    device_.Reset();
#if defined(AB3D2_ENABLE_STREAMLINE)
    device_proxy_.Reset();
#endif
    adapter_.Reset();
    factory_.Reset();
#if defined(AB3D2_ENABLE_STREAMLINE)
    factory_proxy_.Reset();
#endif
    streamline_ = nullptr;
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
    rendered_frame_count_ = 0;
    previous_render_time_ = {};
    previous_render_time_valid_ = false;
    last_scene_rgb_checksum_ = 0;
    last_scene_frame_delta_ = -1.0;
    last_scene_reprojected_frame_delta_ = -1.0;
    last_scene_saturated_pixels_ = 0;
    last_scene_temporal_outlier_pixels_ = 0;
    last_scene_reprojected_temporal_outlier_pixels_ = 0;
    last_scene_reprojected_pixel_count_ = 0;
    previous_readback_rgb_.clear();
    previous_readback_rgb_.shrink_to_fit();
    noisy_radiance_readback_enabled_ = false;
    last_noisy_radiance_rgb_.clear();
    last_noisy_radiance_rgb_.shrink_to_fit();
    readback_width_ = 0;
    readback_height_ = 0;
    readback_row_count_ = 0;
    readback_total_bytes_ = 0;
    readback_footprint_ = {};
    motion_readback_width_ = 0;
    motion_readback_height_ = 0;
    motion_readback_row_count_ = 0;
    motion_readback_total_bytes_ = 0;
    motion_readback_footprint_ = {};
    noisy_readback_width_ = 0;
    noisy_readback_height_ = 0;
    noisy_readback_row_count_ = 0;
    noisy_readback_total_bytes_ = 0;
    noisy_readback_footprint_ = {};
}

}  // namespace ab3d2::dxr
