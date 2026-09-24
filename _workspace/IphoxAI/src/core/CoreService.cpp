#include "iphox/core/CoreService.hpp"

#include "iphox/chat/ChatCodec.hpp"
#include "iphox/cognitive/DecisionCodec.hpp"
#include "iphox/foundation/Sha256.hpp"
#include "iphox/ipc/Payload.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace iphox::core {
namespace {

template <typename T>
void AppendLe(
    std::vector<std::byte>& bytes,
    T value) {

    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U raw = static_cast<U>(value);

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes.push_back(
            static_cast<std::byte>(
                (raw >> (i * 8)) & U{0xFF}));
    }
}

} // namespace

CoreService::CoreService(
    generation::IGenerativeEngine& engine,
    cognitive::Supervisor& supervisor,
    std::size_t requestCapacity,
    std::size_t maxConversationTurns,
    std::size_t maxConversationBytes,
    std::string systemPrompt)
    : engine_(engine),
      supervisor_(supervisor),
      conversation_(
          maxConversationTurns,
          maxConversationBytes,
          std::move(systemPrompt)),
      requests_(requestCapacity) {}

HandleResult CoreService::Handle(
    const ipc::Frame& request,
    std::stop_token stopToken) {

    if ((request.header.flags &
            ipc::kFlagResponse) != 0) {
        return {
            .response = MakeError(
                request,
                "REQUEST_MARKED_AS_RESPONSE"),
            .requestShutdown = false
        };
    }

    const auto fingerprint =
        Fingerprint(request);

    if (!fingerprint.has_value()) {
        return {
            .response = MakeError(
                request,
                "REQUEST_FINGERPRINT_FAILED"),
            .requestShutdown = false
        };
    }

    const auto registration =
        requests_.Register(
            request.header.requestId,
            *fingerprint);

    if (registration ==
        foundation::RegisterResult::Conflict) {

        return {
            .response = MakeError(
                request,
                "REQUEST_ID_CONFLICT"),
            .requestShutdown = false
        };
    }

    if (registration ==
        foundation::RegisterResult::CapacityExhausted) {

        return {
            .response = MakeError(
                request,
                "REQUEST_REGISTRY_FULL"),
            .requestShutdown = false
        };
    }

    if (registration ==
        foundation::RegisterResult::Duplicate) {

        const auto cached =
            requests_.Find(
                request.header.requestId);

        if (cached.has_value() &&
            cached->completed &&
            !cached->encodedResponse.empty()) {

            const auto decoded =
                ipc::Protocol::Decode(
                    cached->encodedResponse);

            if (decoded.ok()) {
                return {
                    .response = decoded.frame,
                    .requestShutdown = false
                };
            }
        }

        return {
            .response = MakeError(
                request,
                "REQUEST_PENDING"),
            .requestShutdown = false
        };
    }

    HandleResult result;

    switch (request.header.type) {
    case ipc::MessageType::Hello:
        result.response =
            MakeResponse(
                request,
                ipc::MessageType::Hello);
        result.response.payload =
            ipc::ToPayload(
                "IphoxCore Native R0");
        break;

    case ipc::MessageType::Ping:
        result.response =
            MakeResponse(
                request,
                ipc::MessageType::Pong);
        break;

    case ipc::MessageType::RuntimeStatus: {
        const auto probe =
            engine_.Probe(stopToken);

        result.response =
            MakeResponse(
                request,
                ipc::MessageType::RuntimeStatus);

        std::string status;

        switch (probe.status) {
        case generation::EngineStatus::Ready:
            status = "READY";
            break;
        case generation::EngineStatus::Loading:
            status = "LOADING";
            break;
        case generation::EngineStatus::Unavailable:
            status = "UNAVAILABLE";
            break;
        case generation::EngineStatus::Failed:
            status = "FAILED";
            break;
        }

        if (!probe.detail.empty()) {
            status += ":";
            status += probe.detail;
        }

        result.response.payload =
            ipc::ToPayload(status);
        break;
    }


    case ipc::MessageType::ChatClear:
        conversation_.Clear();

        result.response =
            MakeResponse(
                request,
                ipc::MessageType::ChatClear);
        break;

    case ipc::MessageType::ChatSubmit: {
        const auto chat =
            chat::ChatCodec::Decode(
                request.payload);

        if (!chat.ok()) {
            result.response =
                MakeError(
                    request,
                    "INVALID_CHAT_PAYLOAD");
            break;
        }

        if (chat.value.text.empty()) {
            result.response =
                MakeError(
                    request,
                    "EMPTY_CHAT");
            break;
        }

        const auto generationRequest =
            conversation_.BuildRequest(
                chat.value.text);

        const auto generationResult =
            engine_.Generate(
                generationRequest,
                stopToken);

        switch (generationResult.status) {
        case generation::GenerationStatus::Completed:
            conversation_.AddTurn(
                chat.value.text,
                generationResult.text);

            result.response =
                MakeResponse(
                    request,
                    ipc::MessageType::ChatStatus);
            result.response.payload =
                ipc::ToPayload(
                    generationResult.text);
            break;

        case generation::GenerationStatus::Cancelled:
            result.response =
                MakeError(
                    request,
                    generationResult.errorCode.empty()
                        ? "CANCELLED"
                        : generationResult.errorCode);
            break;

        case generation::GenerationStatus::Unavailable:
            result.response =
                MakeError(
                    request,
                    generationResult.errorCode.empty()
                        ? "ENGINE_UNAVAILABLE"
                        : generationResult.errorCode);
            break;

        case generation::GenerationStatus::Failed:
            result.response =
                MakeError(
                    request,
                    generationResult.errorCode.empty()
                        ? "GENERATION_FAILED"
                        : generationResult.errorCode);
            break;
        }
        break;
    }

    case ipc::MessageType::DecisionEvaluate: {
        const auto decoded =
            cognitive::DecisionCodec::DecodeRequest(
                request.payload);

        if (!decoded.ok()) {
            result.response =
                MakeError(
                    request,
                    "INVALID_DECISION_PAYLOAD");
            break;
        }

        try {
            const auto supervisorResult =
                supervisor_.Evaluate(
                    request.header.requestId,
                    decoded.value,
                    *fingerprint,
                    "body-policy-unavailable",
                    stopToken);

            result.response =
                MakeResponse(
                    request,
                    ipc::MessageType::DecisionTrace);

            result.response.payload =
                cognitive::DecisionCodec::EncodeResponse(
                    supervisorResult.receipt.response);

        } catch (...) {
            result.response =
                MakeError(
                    request,
                    "INVALID_DECISION_REQUEST");
        }
        break;
    }

    case ipc::MessageType::CoreShutdown:
        result.response =
            MakeResponse(
                request,
                ipc::MessageType::CoreShutdown);
        result.requestShutdown = true;
        break;

    default:
        result.response =
            MakeError(
                request,
                "MESSAGE_UNIMPLEMENTED");
        break;
    }

    try {
        (void)requests_.MarkCompleted(
            request.header.requestId,
            ipc::Protocol::Encode(
                result.response));
    } catch (...) {
        result.response =
            MakeError(
                request,
                "RESPONSE_CACHE_FAILED");
        result.requestShutdown = false;
    }

    return result;
}

std::optional<std::string> CoreService::Fingerprint(
    const ipc::Frame& request) {

    std::vector<std::byte> material;
    material.reserve(
        sizeof(request.header.version) +
        sizeof(std::uint16_t) +
        sizeof(request.header.flags) +
        request.payload.size());

    AppendLe(
        material,
        request.header.version);

    AppendLe(
        material,
        static_cast<std::uint16_t>(
            request.header.type));

    AppendLe(
        material,
        request.header.flags);

    material.insert(
        material.end(),
        request.payload.begin(),
        request.payload.end());

    const auto digest =
        foundation::Sha256(material);

    if (!digest.has_value()) {
        return std::nullopt;
    }

    return foundation::Hex(*digest);
}

ipc::Frame CoreService::MakeResponse(
    const ipc::Frame& request,
    ipc::MessageType type) {

    ipc::Frame response;
    response.header.version =
        ipc::kProtocolVersion;
    response.header.type = type;
    response.header.requestId =
        request.header.requestId;
    response.header.flags =
        ipc::kFlagResponse;

    return response;
}

ipc::Frame CoreService::MakeError(
    const ipc::Frame& request,
    std::string code) {

    auto response =
        MakeResponse(
            request,
            ipc::MessageType::Error);

    response.header.flags |=
        ipc::kFlagError;

    response.payload =
        ipc::ToPayload(code);

    return response;
}

} // namespace iphox::core
