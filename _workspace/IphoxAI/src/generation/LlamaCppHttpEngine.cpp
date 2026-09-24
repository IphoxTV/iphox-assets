#include "iphox/generation/LlamaCppHttpEngine.hpp"

#include "iphox/foundation/JsonLite.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace iphox::generation {
namespace {

class InternetHandle final {
public:
    InternetHandle() = default;

    explicit InternetHandle(HINTERNET handle)
        : handle_(handle) {}

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    InternetHandle(InternetHandle&& other) noexcept
        : handle_(
              std::exchange(
                  other.handle_,
                  nullptr)) {}

    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ =
                std::exchange(
                    other.handle_,
                    nullptr);
        }
        return *this;
    }

    ~InternetHandle() {
        Reset();
    }

    [[nodiscard]] HINTERNET Get() const noexcept {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr;
    }

private:
    void Reset() noexcept {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    HINTERNET handle_{};
};

GenerationResult Cancelled() {
    return {
        .status = GenerationStatus::Cancelled,
        .text = {},
        .errorCode = "CANCELLED"
    };
}

GenerationResult Failed(
    std::string code) {

    return {
        .status = GenerationStatus::Failed,
        .text = {},
        .errorCode = std::move(code)
    };
}

GenerationResult Unavailable(
    std::string code) {

    return {
        .status = GenerationStatus::Unavailable,
        .text = {},
        .errorCode = std::move(code)
    };
}

std::string StatusCodeError(
    DWORD status) {

    return "LLAMA_HTTP_" +
        std::to_string(status);
}

bool ReadBody(
    HINTERNET request,
    std::string& body,
    std::stop_token stopToken) {

    constexpr std::size_t kMaxResponseBytes =
        8u * 1024u * 1024u;

    body.clear();

    for (;;) {
        if (stopToken.stop_requested()) {
            return false;
        }

        DWORD available{};

        if (!WinHttpQueryDataAvailable(
                request,
                &available)) {
            return false;
        }

        if (available == 0) {
            return true;
        }

        if (body.size() + available >
            kMaxResponseBytes) {
            return false;
        }

        const auto oldSize = body.size();
        body.resize(
            oldSize +
            available);

        DWORD read{};

        if (!WinHttpReadData(
                request,
                body.data() + oldSize,
                available,
                &read)) {
            return false;
        }

        body.resize(
            oldSize +
            read);
    }
}

} // namespace

LlamaCppHttpEngine::LlamaCppHttpEngine(
    LlamaCppHttpConfig config)
    : config_(std::move(config)) {}

EngineProbeResult LlamaCppHttpEngine::Probe(
    std::stop_token stopToken) {

    if (stopToken.stop_requested()) {
        return {
            .status = EngineStatus::Unavailable,
            .detail = "CANCELLED"
        };
    }

    if (!IsLoopbackHost(config_.host) ||
        config_.port == 0) {
        return {
            .status = EngineStatus::Failed,
            .detail = "INVALID_LLAMA_CONFIG"
        };
    }

    InternetHandle session{
        WinHttpOpen(
            L"IphoxAI-Native/0.1",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0)
    };

    if (!session) {
        return {
            .status = EngineStatus::Unavailable,
            .detail = "WINHTTP_SESSION_FAILED"
        };
    }

    if (!WinHttpSetTimeouts(
            session.Get(),
            config_.resolveTimeoutMs,
            config_.connectTimeoutMs,
            config_.sendTimeoutMs,
            (std::min)(config_.receiveTimeoutMs, 5000))) {
        return {
            .status = EngineStatus::Failed,
            .detail = "WINHTTP_TIMEOUT_CONFIG_FAILED"
        };
    }

    InternetHandle connection{
        WinHttpConnect(
            session.Get(),
            config_.host.c_str(),
            config_.port,
            0)
    };

    if (!connection) {
        return {
            .status = EngineStatus::Unavailable,
            .detail = "LLAMA_CONNECT_FAILED"
        };
    }

    static constexpr wchar_t kAcceptJson[] =
        L"application/json";

    const wchar_t* acceptTypes[] = {
        kAcceptJson,
        nullptr
    };

    InternetHandle request{
        WinHttpOpenRequest(
            connection.Get(),
            L"GET",
            L"/health",
            nullptr,
            WINHTTP_NO_REFERER,
            acceptTypes,
            0)
    };

    if (!request) {
        return {
            .status = EngineStatus::Unavailable,
            .detail = "LLAMA_HEALTH_OPEN_FAILED"
        };
    }

    if (!WinHttpSendRequest(
            request.Get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(
            request.Get(),
            nullptr)) {
        return {
            .status = EngineStatus::Unavailable,
            .detail = "LLAMA_HEALTH_REQUEST_FAILED"
        };
    }

    DWORD status{};
    DWORD statusBytes = sizeof(status);

    if (!WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return {
            .status = EngineStatus::Failed,
            .detail = "LLAMA_HEALTH_STATUS_FAILED"
        };
    }

    std::string body;

    if (!ReadBody(
            request.Get(),
            body,
            stopToken)) {
        return {
            .status = stopToken.stop_requested()
                ? EngineStatus::Unavailable
                : EngineStatus::Failed,
            .detail = stopToken.stop_requested()
                ? "CANCELLED"
                : "LLAMA_HEALTH_READ_FAILED"
        };
    }

    const auto detail =
        foundation::JsonStringField(
            body,
            "status");

    if (status == 200) {
        return {
            .status = EngineStatus::Ready,
            .detail = detail.value_or("ok")
        };
    }

    if (status == 503) {
        return {
            .status = EngineStatus::Loading,
            .detail = detail.value_or("loading")
        };
    }

    return {
        .status = EngineStatus::Unavailable,
        .detail = StatusCodeError(status)
    };
}

GenerationResult LlamaCppHttpEngine::Generate(
    const GenerationRequest& request,
    std::stop_token stopToken) {

    if (stopToken.stop_requested()) {
        return Cancelled();
    }

    if (!IsLoopbackHost(config_.host)) {
        return Failed(
            "NON_LOOPBACK_LLAMA_HOST_REJECTED");
    }

    if (config_.port == 0 ||
        config_.nPredict == 0 ||
        config_.nPredict > 32768) {
        return Failed(
            "INVALID_LLAMA_CONFIG");
    }

    InternetHandle session{
        WinHttpOpen(
            L"IphoxAI-Native/0.1",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0)
    };

    if (!session) {
        return Unavailable(
            "WINHTTP_SESSION_FAILED");
    }

    if (!WinHttpSetTimeouts(
            session.Get(),
            config_.resolveTimeoutMs,
            config_.connectTimeoutMs,
            config_.sendTimeoutMs,
            config_.receiveTimeoutMs)) {
        return Failed(
            "WINHTTP_TIMEOUT_CONFIG_FAILED");
    }

    InternetHandle connection{
        WinHttpConnect(
            session.Get(),
            config_.host.c_str(),
            config_.port,
            0)
    };

    if (!connection) {
        return Unavailable(
            "LLAMA_CONNECT_FAILED");
    }

    static constexpr wchar_t kAcceptJson[] =
        L"application/json";

    const wchar_t* acceptTypes[] = {
        kAcceptJson,
        nullptr
    };

    InternetHandle httpRequest{
        WinHttpOpenRequest(
            connection.Get(),
            L"POST",
            L"/completion",
            nullptr,
            WINHTTP_NO_REFERER,
            acceptTypes,
            0)
    };

    if (!httpRequest) {
        return Unavailable(
            "LLAMA_REQUEST_OPEN_FAILED");
    }

    const auto prompt =
        foundation::JsonEscape(
            request.prompt);

    const std::string body =
        "{\"prompt\":\"" +
        prompt +
        "\",\"n_predict\":" +
        std::to_string(config_.nPredict) +
        ",\"stream\":false}";

    if (body.size() >
        (std::numeric_limits<DWORD>::max)()) {
        return Failed(
            "LLAMA_REQUEST_TOO_LARGE");
    }

    static constexpr wchar_t kHeaders[] =
        L"Content-Type: application/json\r\n"
        L"Accept: application/json\r\n";

    if (stopToken.stop_requested()) {
        return Cancelled();
    }

    if (!WinHttpSendRequest(
            httpRequest.Get(),
            kHeaders,
            static_cast<DWORD>(-1L),
            const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0)) {
        return Unavailable(
            "LLAMA_SEND_FAILED");
    }

    if (stopToken.stop_requested()) {
        return Cancelled();
    }

    if (!WinHttpReceiveResponse(
            httpRequest.Get(),
            nullptr)) {
        return Unavailable(
            "LLAMA_RECEIVE_FAILED");
    }

    DWORD status{};
    DWORD statusBytes = sizeof(status);

    if (!WinHttpQueryHeaders(
            httpRequest.Get(),
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusBytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return Failed(
            "LLAMA_STATUS_FAILED");
    }

    std::string responseBody;

    if (!ReadBody(
            httpRequest.Get(),
            responseBody,
            stopToken)) {

        return stopToken.stop_requested()
            ? Cancelled()
            : Failed(
                  "LLAMA_RESPONSE_READ_FAILED");
    }

    if (status == 503) {
        return Unavailable(
            "LLAMA_MODEL_LOADING");
    }

    if (status != 200) {
        return Failed(
            StatusCodeError(status));
    }

    const auto content =
        foundation::JsonStringField(
            responseBody,
            "content");

    if (!content.has_value()) {
        return Failed(
            "INVALID_LLAMA_RESPONSE");
    }

    return {
        .status = GenerationStatus::Completed,
        .text = *content,
        .errorCode = {}
    };
}

bool LlamaCppHttpEngine::IsLoopbackHost(
    const std::wstring& host) noexcept {

    return host == L"127.0.0.1" ||
        host == L"localhost" ||
        host == L"::1";
}

} // namespace iphox::generation
