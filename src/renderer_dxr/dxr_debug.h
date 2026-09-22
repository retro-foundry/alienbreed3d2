#ifndef AB3D2_DXR_DEBUG_H
#define AB3D2_DXR_DEBUG_H

#include <windows.h>

#include <string>

namespace ab3d2::dxr {

std::string hresult_error(const char *operation, HRESULT result);
std::string wide_to_utf8(const wchar_t *text);
void debug_output(const std::string &message);
/*
 * AB3D2_DXR_HITCH_MS: log any frame at or above this many milliseconds, and
 * the cause of each scene rebuild. One reader, so the scene's rebuild log and
 * the device's frame log can never disagree about what the variable means.
 * Returns false and leaves `threshold` untouched when logging is off.
 */
bool hitch_log_threshold_ms(double &threshold);

}  // namespace ab3d2::dxr

#endif
