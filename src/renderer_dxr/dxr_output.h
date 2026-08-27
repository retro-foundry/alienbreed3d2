#ifndef AB3D2_DXR_OUTPUT_H
#define AB3D2_DXR_OUTPUT_H

#include <dxgi1_6.h>

namespace ab3d2::dxr {

struct DxrOutputConfiguration {
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_COLOR_SPACE_TYPE color_space =
        DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    bool hdr = false;
    float peak_nits = 80.0f;
    float paper_white_nits = 80.0f;
};

inline bool output_configuration_equal(const DxrOutputConfiguration &left,
                                       const DxrOutputConfiguration &right)
{
    return left.format == right.format &&
        left.color_space == right.color_space && left.hdr == right.hdr &&
        left.peak_nits == right.peak_nits &&
        left.paper_white_nits == right.paper_white_nits;
}

inline DxrOutputConfiguration sdr_output_configuration()
{
    return {};
}

inline DxrOutputConfiguration hdr_output_configuration(float peak_nits,
                                                        float paper_white_nits)
{
    DxrOutputConfiguration output = {};
    output.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    output.color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    output.hdr = true;
    output.peak_nits = peak_nits;
    output.paper_white_nits = paper_white_nits;
    return output;
}

}  // namespace ab3d2::dxr

#endif
