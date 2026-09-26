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
/*
 * Mirror debug_output to the console as well as the debugger, for the rest of
 * the run. Any diagnostic that switches itself on through an environment
 * variable should call this: asking for a diagnostic is asking to see it, and
 * a second variable governing where it lands only makes it look absent.
 * AB3D2_DXR_DEBUG_LOG sets it for the unconditional messages.
 */
void debug_output_enable_console_mirror();

}  // namespace ab3d2::dxr

#endif
