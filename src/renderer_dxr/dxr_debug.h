#ifndef AB3D2_DXR_DEBUG_H
#define AB3D2_DXR_DEBUG_H

#include <windows.h>

#include <string>

namespace ab3d2::dxr {

std::string hresult_error(const char *operation, HRESULT result);
std::string wide_to_utf8(const wchar_t *text);
void debug_output(const std::string &message);

}  // namespace ab3d2::dxr

#endif
