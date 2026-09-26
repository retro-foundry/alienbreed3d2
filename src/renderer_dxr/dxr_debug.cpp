#include "dxr_debug.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ab3d2::dxr {

std::string hresult_error(const char *operation, HRESULT result)
{
    char system_message[512] = {};
    char result_text[768] = {};
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, system_message,
        static_cast<DWORD>(sizeof(system_message)), nullptr);

    if (length != 0) {
        size_t trimmed_length = length;

        while (trimmed_length != 0 &&
               (system_message[trimmed_length - 1] == '\r' ||
                system_message[trimmed_length - 1] == '\n' ||
                system_message[trimmed_length - 1] == ' ')) {
            --trimmed_length;
        }
        system_message[trimmed_length] = '\0';
    }
    (void)std::snprintf(
        result_text, sizeof(result_text), "%s failed (HRESULT 0x%08lX%s%s)",
        operation ? operation : "DirectX operation",
        static_cast<unsigned long>(result),
        length != 0 ? ": " : "", length != 0 ? system_message : "");
    return result_text;
}

std::string wide_to_utf8(const wchar_t *text)
{
    if (!text || *text == L'\0') {
        return {};
    }
    const int byte_count = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (byte_count <= 1) {
        return {};
    }
    std::vector<char> bytes(static_cast<size_t>(byte_count));
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes.data(), byte_count,
                            nullptr, nullptr) == 0) {
        return {};
    }
    return std::string(bytes.data());
}

bool hitch_log_threshold_ms(double &threshold)
{
    static const double configured = [] {
        const char *value = std::getenv("AB3D2_DXR_HITCH_MS");
        const double parsed = value ? std::atof(value) : 0.0;
        return parsed > 0.0 ? parsed : 0.0;
    }();
    if (configured <= 0.0) {
        return false;
    }
    threshold = configured;
    return true;
}

/*
 * Whether debug output is mirrored to the console as well as to the debugger.
 *
 * Enumerating the diagnostics that should imply it does not work: this was
 * written knowing that AB3D2_DXR_SL_VERBOSE had to imply the mirror, and the
 * very next diagnostic added still logged into a debugger nobody was reading.
 * A diagnostic that asks to be produced is asking to be seen, so each one
 * turns the mirror on for itself through debug_output_enable_console_mirror
 * and there is no central list to forget.
 *
 * AB3D2_DXR_DEBUG_LOG turns it on with no diagnostic attached, for the
 * messages the renderer emits unconditionally.
 */
bool &console_mirror_flag()
{
    /* Read once: mirrored logging is per-message and an environment lookup
     * per line is not free. */
    static bool enabled = []() {
        char value[2] = {};
        const DWORD size = static_cast<DWORD>(sizeof(value));
        return GetEnvironmentVariableA("AB3D2_DXR_DEBUG_LOG", value, size) != 0u;
    }();
    return enabled;
}

void debug_output_enable_console_mirror()
{
    console_mirror_flag() = true;
}

void debug_output(const std::string &message)
{
    std::string line = "[AB3D2 DXR] " + message + "\n";
    OutputDebugStringA(line.c_str());
    if (console_mirror_flag()) {
        std::fputs(line.c_str(), stderr);
        std::fflush(stderr);
    }
}

}  // namespace ab3d2::dxr
