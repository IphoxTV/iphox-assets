#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace iphox::runtime {

struct LlamaServerLaunchConfig {
    std::filesystem::path serverPath;
    std::filesystem::path modelPath;

    std::uint16_t port{8080};
    std::uint32_t contextSize{8192};
    std::wstring gpuLayers{L"auto"};
};

class LlamaServerProcessHost final {
public:
    LlamaServerProcessHost() = default;
    LlamaServerProcessHost(
        const LlamaServerProcessHost&) = delete;
    LlamaServerProcessHost& operator=(
        const LlamaServerProcessHost&) = delete;

    ~LlamaServerProcessHost();

    [[nodiscard]] bool Start(
        const LlamaServerLaunchConfig& config);

    [[nodiscard]] bool IsRunning() const noexcept;

    [[nodiscard]] bool WaitForExit(
        DWORD timeoutMs) const noexcept;

    void Stop() noexcept;

private:
    HANDLE job_{};
    HANDLE process_{};
};

} // namespace iphox::runtime
