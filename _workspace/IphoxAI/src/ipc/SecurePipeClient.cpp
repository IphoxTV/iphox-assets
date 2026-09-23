#include "iphox/ipc/SecurePipeClient.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace iphox::ipc {

SecurePipeClient::~SecurePipeClient() {
    Close();
}

bool SecurePipeClient::Connect(
    const std::wstring& pipeName,
    DWORD timeoutMs) {

    Close();

    if (!WaitNamedPipeW(
            pipeName.c_str(),
            timeoutMs)) {
        return false;
    }

    pipe_ = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (!IsOpen()) {
        return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(
            pipe_,
            &mode,
            nullptr,
            nullptr)) {
        Close();
        return false;
    }

    return true;
}

bool SecurePipeClient::WriteFrame(
    const Frame& frame) {

    if (!IsOpen()) {
        return false;
    }

    std::vector<std::byte> encoded;

    try {
        encoded = Protocol::Encode(frame);
    } catch (...) {
        return false;
    }

    DWORD written{};

    const BOOL ok = WriteFile(
        pipe_,
        encoded.data(),
        static_cast<DWORD>(encoded.size()),
        &written,
        nullptr);

    return ok != FALSE &&
        written == encoded.size();
}

std::optional<Frame> SecurePipeClient::ReadFrame() {
    if (!IsOpen()) {
        return std::nullopt;
    }

    constexpr DWORD kChunkBytes = 64 * 1024;
    constexpr std::size_t kMaxFrameBytes =
        static_cast<std::size_t>(kMaxPayloadBytes) + 64;

    std::array<std::byte, kChunkBytes> chunk{};
    std::vector<std::byte> bytes;

    for (;;) {
        DWORD read{};

        const BOOL ok = ReadFile(
            pipe_,
            chunk.data(),
            static_cast<DWORD>(chunk.size()),
            &read,
            nullptr);

        if (read != 0) {
            if (bytes.size() + read > kMaxFrameBytes) {
                return std::nullopt;
            }

            bytes.insert(
                bytes.end(),
                chunk.begin(),
                chunk.begin() + static_cast<std::ptrdiff_t>(read));
        }

        if (ok) {
            break;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            return std::nullopt;
        }
    }

    const auto decoded = Protocol::Decode(bytes);
    if (!decoded.ok()) {
        return std::nullopt;
    }

    return decoded.frame;
}

void SecurePipeClient::Close() noexcept {
    if (!IsOpen()) {
        return;
    }

    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
}

} // namespace iphox::ipc
