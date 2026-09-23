#pragma once

#include "iphox/ipc/Protocol.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>
#include <string>

namespace iphox::ipc {

inline constexpr wchar_t kCorePipeName[] =
    L"\\\\.\\pipe\\IphoxAI.Core.v1";

class SecurePipeServer final {
public:
    SecurePipeServer() = default;
    SecurePipeServer(const SecurePipeServer&) = delete;
    SecurePipeServer& operator=(const SecurePipeServer&) = delete;

    ~SecurePipeServer();

    [[nodiscard]] bool Open(
        const std::wstring& pipeName = kCorePipeName);

    [[nodiscard]] bool WaitForClient();

    [[nodiscard]] std::optional<Frame> ReadFrame();

    [[nodiscard]] bool WriteFrame(const Frame& frame);

    void Disconnect() noexcept;
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept {
        return pipe_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE pipe_{INVALID_HANDLE_VALUE};
};

} // namespace iphox::ipc
