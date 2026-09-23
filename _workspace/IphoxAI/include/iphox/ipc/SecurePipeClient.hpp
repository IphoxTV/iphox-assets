#pragma once

#include "iphox/ipc/Protocol.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>
#include <string>

namespace iphox::ipc {

class SecurePipeClient final {
public:
    SecurePipeClient() = default;
    SecurePipeClient(const SecurePipeClient&) = delete;
    SecurePipeClient& operator=(const SecurePipeClient&) = delete;

    ~SecurePipeClient();

    [[nodiscard]] bool Connect(
        const std::wstring& pipeName,
        DWORD timeoutMs = 5000);

    [[nodiscard]] bool WriteFrame(const Frame& frame);
    [[nodiscard]] std::optional<Frame> ReadFrame();

    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept {
        return pipe_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE pipe_{INVALID_HANDLE_VALUE};
};

} // namespace iphox::ipc
