#include "dxr_pipeline.h"

#include "dxr_debug.h"

#include <dxgi1_6.h>

#include <filesystem>
#include <fstream>
#include <limits>

namespace ab3d2::dxr {

namespace {

std::string path_text(const std::filesystem::path &path)
{
    const std::string converted = wide_to_utf8(path.c_str());
    return converted.empty() ? "<unprintable shader path>" : converted;
}

bool executable_directory(std::filesystem::path &directory, std::string &error)
{
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));

    if (length == 0 || length >= executable_path.size()) {
        error = hresult_error("GetModuleFileNameW", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    executable_path.resize(length);
    directory = std::filesystem::path(executable_path).parent_path();
    return true;
}

}  // namespace

bool DxrPipeline::load_shader(const wchar_t *filename,
                              std::vector<unsigned char> &bytes,
                              std::string &error)
{
    std::filesystem::path directory;

    if (!filename || !executable_directory(directory, error)) {
        return false;
    }
    const std::filesystem::path path = directory / L"renderer_dxr" / filename;
    std::error_code file_error;
    const uintmax_t file_size = std::filesystem::file_size(path, file_error);

    if (file_error) {
        error = "DXR diagnostic shader is unavailable: " + path_text(path) +
                " (" + file_error.message() + ")";
        return false;
    }
    if (file_size == 0 || file_size > std::numeric_limits<size_t>::max()) {
        error = "DXR diagnostic shader has an invalid size: " + path_text(path);
        return false;
    }
    bytes.resize(static_cast<size_t>(file_size));
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.read(reinterpret_cast<char *>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()))) {
        error = "DXR diagnostic shader could not be read completely: " + path_text(path);
        return false;
    }
    return true;
}

bool DxrPipeline::initialize(ID3D12Device5 *device, std::string &error)
{
    std::vector<unsigned char> vertex_shader;
    std::vector<unsigned char> pixel_shader;
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error;
    D3D12_ROOT_SIGNATURE_DESC root_signature_description = {};
    HRESULT result;

    if (!device) {
        error = "DXR diagnostic pipeline received no D3D12 device";
        return false;
    }
    if (!load_shader(L"diagnostic_vs.dxil", vertex_shader, error) ||
        !load_shader(L"diagnostic_ps.dxil", pixel_shader, error)) {
        return false;
    }

    root_signature_description.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    result = D3D12SerializeRootSignature(
        &root_signature_description, D3D_ROOT_SIGNATURE_VERSION_1,
        &root_signature_blob, &root_signature_error);
    if (FAILED(result)) {
        error = hresult_error("D3D12SerializeRootSignature", result);
        if (root_signature_error && root_signature_error->GetBufferPointer()) {
            error += ": ";
            error.append(static_cast<const char *>(root_signature_error->GetBufferPointer()),
                         root_signature_error->GetBufferSize());
        }
        return false;
    }
    result = device->CreateRootSignature(
        0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(),
        IID_PPV_ARGS(&root_signature_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateRootSignature", result);
        return false;
    }
    root_signature_->SetName(L"AB3D2 DXR Diagnostic Root Signature");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
    description.pRootSignature = root_signature_.Get();
    description.VS = {vertex_shader.data(), vertex_shader.size()};
    description.PS = {pixel_shader.data(), pixel_shader.size()};
    description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    description.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    description.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    description.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    description.SampleMask = UINT_MAX;
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.StencilEnable = FALSE;
    description.InputLayout = {nullptr, 0};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;

    result = device->CreateGraphicsPipelineState(&description,
                                                 IID_PPV_ARGS(&pipeline_state_));
    if (FAILED(result)) {
        error = hresult_error("ID3D12Device::CreateGraphicsPipelineState", result);
        return false;
    }
    pipeline_state_->SetName(L"AB3D2 DXR Diagnostic Triangle Pipeline");
    return true;
}

}  // namespace ab3d2::dxr
