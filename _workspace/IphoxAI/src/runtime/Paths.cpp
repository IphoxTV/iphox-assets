#include "iphox/runtime/Paths.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

namespace iphox::runtime {

std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(
                buffer.size()));

    if (length == 0 ||
        length >= buffer.size()) {
        return {};
    }

    return std::filesystem::path{
        std::wstring{
            buffer.data(),
            length
        }
    };
}

std::filesystem::path ExecutableDirectory() {
    const auto executable =
        ExecutablePath();

    if (executable.empty()) {
        return {};
    }

    return executable.parent_path();
}

} // namespace iphox::runtime
