#include "iphox/ipc/SecurePipeServer.hpp"

#include <sddl.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace iphox::ipc {
namespace {

class PipeSecurity final {
public:
    PipeSecurity() = default;
    PipeSecurity(const PipeSecurity&) = delete;
    PipeSecurity& operator=(const PipeSecurity&) = delete;

    ~PipeSecurity() {
        if (descriptor_ != nullptr) {
            LocalFree(descriptor_);
        }
    }

    [[nodiscard]] bool Build() {
        HANDLE token = nullptr;

        if (!OpenProcessToken(
                GetCurrentProcess(),
                TOKEN_QUERY,
                &token)) {
            return false;
        }

        DWORD bytes{};
        GetTokenInformation(
            token,
            TokenUser,
            nullptr,
            0,
            &bytes);

        if (bytes == 0) {
            CloseHandle(token);
            return false;
        }

        std::vector<std::byte> buffer(bytes);

        if (!GetTokenInformation(
                token,
                TokenUser,
                buffer.data(),
                bytes,
                &bytes)) {
            CloseHandle(token);
            return false;
        }

        CloseHandle(token);

        const auto* tokenUser =
            reinterpret_cast<const TOKEN_USER*>(
                buffer.data());

        LPWSTR sidString = nullptr;
        if (!ConvertSidToStringSidW(
                tokenUser->User.Sid,
                &sidString)) {
            return false;
        }

        const std::wstring sddl =
            L"D:P"
            L"(A;;GA;;;SY)"
            L"(A;;GA;;;" +
            std::wstring{sidString} +
            L")";

        LocalFree(sidString);

        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(),
                SDDL_REVISION_1,
                &descriptor_,
                nullptr)) {
            return false;
        }

        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = descriptor_;
        attributes_.bInheritHandle = FALSE;
        return true;
    }

    [[nodiscard]] SECURITY_ATTRIBUTES* Attributes() noexcept {
        return &attributes_;
    }

private:
    PSECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
};

} // namespace

SecurePipeServer::~SecurePipeServer() {
    Close();
}

bool SecurePipeServer::Open(
    const std::wstring& pipeName) {

    Close();

    PipeSecurity security;
    if (!security.Build()) {
        return false;
    }

    pipe_ = CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX |
            FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE |
            PIPE_READMODE_MESSAGE |
            PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1,
        64 * 1024,
        64 * 1024,
        0,
        security.Attributes());

    return pipe_ != INVALID_HANDLE_VALUE;
}

bool SecurePipeServer::WaitForClient() {
    if (!IsOpen()) {
        return false;
    }

    if (ConnectNamedPipe(pipe_, nullptr)) {
        return true;
    }

    return GetLastError() == ERROR_PIPE_CONNECTED;
}

std::optional<Frame> SecurePipeServer::ReadFrame() {
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

bool SecurePipeServer::WriteFrame(
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

    if (!ok || written != encoded.size()) {
        return false;
    }

    return FlushFileBuffers(pipe_) != FALSE;
}

void SecurePipeServer::Disconnect() noexcept {
    if (IsOpen()) {
        FlushFileBuffers(pipe_);
        DisconnectNamedPipe(pipe_);
    }
}

void SecurePipeServer::Close() noexcept {
    if (!IsOpen()) {
        return;
    }

    Disconnect();
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
}

} // namespace iphox::ipc
