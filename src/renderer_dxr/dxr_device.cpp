#include "dxr_device.h"

#include "dxr_debug.h"
#include "dxr_pipeline.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <sstream>
#include <vector>

namespace ab3d2::dxr {

namespace {

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
    const HRESULT result = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory_));
    if (FAILED(result)) {
        error = hresult_error("CreateDXGIFactory2(IDXGIFactory6)", result);
        return false;
    }
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
        Microsoft::WRL::ComPtr<ID3D12Device5> candidate_device;
        result = D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                                   IID_PPV_ARGS(&candidate_device));
        if (FAILED(result)) {
            rejected << adapter_name(description)
                     << " (D3D feature level 12_0 / ID3D12Device5 unavailable); ";
            continue;
        }
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
        info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
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
    HRESULT result = device_->CreateCommandQueue(
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
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = frame_count;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain;
    HRESULT result = factory_->CreateSwapChainForHwnd(
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
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return true;
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

bool DxrDevice::initialize(HWND window, bool hidden_window, std::string &error)
{
    RECT client = {};

    if (!window || !IsWindow(window)) {
        error = "DXR device initialization received no valid HWND";
        return false;
    }
    window_ = window;
    hidden_window_ = hidden_window;
    if (!GetClientRect(window_, &client)) {
        error = hresult_error("GetClientRect(DXR window)",
                              HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    width_ = static_cast<UINT>(std::max<LONG>(client.right - client.left, 1));
    height_ = static_cast<UINT>(std::max<LONG>(client.bottom - client.top, 1));

    if (!enable_diagnostics(error) || !create_factory(error) ||
        !select_adapter_and_device(error) || !create_command_objects(error) ||
        !create_swap_chain(error) || !create_frame_contexts(error) ||
        !create_render_targets(error)) {
        return false;
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
        frame_count, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(result)) {
        return fail_device_operation("IDXGISwapChain::ResizeBuffers", result, error);
    }
    width_ = width;
    height_ = height;
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    return create_render_targets(error) && check_debug_messages(error);
}

bool DxrDevice::render(const DxrPipeline &pipeline, std::string &error)
{
    RECT client = {};

    if (!device_ || !swap_chain_ || !pipeline.pipeline_state() ||
        !pipeline.root_signature()) {
        error = "DXR diagnostic render received incomplete device or pipeline state";
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
    if (!resize(static_cast<UINT>(client_width), static_cast<UINT>(client_height), error)) {
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
    result = command_list_->Reset(frame.command_allocator.Get(), pipeline.pipeline_state());
    if (FAILED(result)) {
        return fail_device_operation("ID3D12GraphicsCommandList::Reset", result, error);
    }

    const D3D12_RESOURCE_BARRIER to_render_target = transition_barrier(
        frame.render_target.Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list_->ResourceBarrier(1, &to_render_target);
    command_list_->SetGraphicsRootSignature(pipeline.root_signature());
    command_list_->SetPipelineState(pipeline.pipeline_state());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    command_list_->DrawInstanced(3, 1, 0, 0);
    const D3D12_RESOURCE_BARRIER to_present = transition_barrier(
        frame.render_target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    command_list_->ResourceBarrier(1, &to_present);
    result = command_list_->Close();
    if (FAILED(result)) {
        return fail_device_operation("ID3D12GraphicsCommandList::Close", result, error);
    }
    ID3D12CommandList *command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);

    result = swap_chain_->Present(hidden_window_ ? 0 : 1, 0);
    if (FAILED(result)) {
        return fail_device_operation("IDXGISwapChain::Present", result, error);
    }
    const UINT64 fence_value = next_fence_value_++;
    result = command_queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(result)) {
        return fail_device_operation("ID3D12CommandQueue::Signal(frame)", result, error);
    }
    frame.fence_value = fence_value;
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

void DxrDevice::shutdown()
{
    if (command_queue_ && fence_ && fence_event_) {
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
    render_target_view_heap_.Reset();
    swap_chain_.Reset();
    command_queue_.Reset();
    fence_.Reset();
    if (fence_event_) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    info_queue_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
}

}  // namespace ab3d2::dxr
