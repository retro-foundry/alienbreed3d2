#include "dxr_debug.h"

#include <cstdio>
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

void debug_output(const std::string &message)
{
    std::string line = "[AB3D2 DXR] " + message + "\n";
    OutputDebugStringA(line.c_str());
}

}  // namespace ab3d2::dxr
