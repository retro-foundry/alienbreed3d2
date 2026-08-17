#ifndef AB3D2_DXR_PIPELINE_H
#define AB3D2_DXR_PIPELINE_H

#include <d3d12.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace ab3d2::dxr {

class DxrPipeline final {
public:
    bool initialize(ID3D12Device5 *device, std::string &error);

    ID3D12RootSignature *root_signature() const { return root_signature_.Get(); }
    ID3D12PipelineState *pipeline_state() const { return pipeline_state_.Get(); }

private:
    static bool load_shader(const wchar_t *filename, std::vector<unsigned char> &bytes,
                            std::string &error);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
};

}  // namespace ab3d2::dxr

#endif
