#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace iphox::runtime {

class CoreProcessHost final {
public:
    CoreProcessHost() = default;
    CoreProcessHost(const CoreProcessHost&) = delete;
    CoreProcessHost& operator=(const CoreProcessHost&) = delete;

    ~CoreProcessHost();

    [[nodiscard]] bool StartSiblingCore();
    [[nodiscard]] bool WaitForExit(DWORD timeoutMs) const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;

    void Close() noexcept;

private:
    HANDLE job_{};
    HANDLE process_{};
};

} // namespace iphox::runtime
