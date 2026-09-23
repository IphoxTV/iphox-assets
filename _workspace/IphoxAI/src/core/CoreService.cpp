#include "iphox/core/CoreService.hpp"

#include "iphox/chat/ChatCodec.hpp"
#include "iphox/ipc/Payload.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>

namespace iphox::core {
namespace {

void MixByte(
    std::uint64_t& hash,
    std::uint8_t value) noexcept {

    constexpr std::uint64_t kPrime =
        1099511628211ull;

    hash ^= value;
    hash *= kPrime;
}

template <typename T>
void MixIntegral(
    std::uint64_t& hash,
    T value) noexcept {

    using U = std::make_unsigned_t<T>;
    const U raw = static_cast<U>(value);

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        MixByte(
            hash,
            static_cast<std::uint8_t>(
                (raw >> (i * 8)) & U{0xFF}));
    }
}

} // namespace

CoreService::CoreService(
    generation::IGenerativeEngine& engine,
    std::size_t requestCapacity)
    : engine_(engine),
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

    const auto registration =
        requests_.Register(
            request.header.requestId,
            fingerprint);

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

        const auto generation =
            engine_.Generate(
                {
                    .prompt = chat.value.text
                },
                stopToken);

        switch (generation.status) {
        case generation::GenerationStatus::Completed:
            result.response =
                MakeResponse(
                    request,
                    ipc::MessageType::ChatStatus);
            result.response.payload =
                ipc::ToPayload(
                    generation.text);
            break;

        case generation::GenerationStatus::Cancelled:
            result.response =
                MakeError(
                    request,
                    generation.errorCode.empty()
                        ? "CANCELLED"
                        : generation.errorCode);
            break;

        case generation::GenerationStatus::Unavailable:
            result.response =
                MakeError(
                    request,
                    generation.errorCode.empty()
                        ? "ENGINE_UNAVAILABLE"
                        : generation.errorCode);
            break;

        case generation::GenerationStatus::Failed:
            result.response =
                MakeError(
                    request,
                    generation.errorCode.empty()
                        ? "GENERATION_FAILED"
                        : generation.errorCode);
            break;
        }
        break;
    }

    case ipc::MessageType::DecisionEvaluate:
        result.response =
            MakeError(
                request,
                "DECISION_CODEC_UNAVAILABLE");
        break;

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

std::string CoreService::Fingerprint(
    const ipc::Frame& request) {

    std::uint64_t hash =
        14695981039346656037ull;

    MixIntegral(
        hash,
        request.header.version);

    MixIntegral(
        hash,
        static_cast<std::uint16_t>(
            request.header.type));

    MixIntegral(
        hash,
        request.header.flags);

    for (const auto byte : request.payload) {
        MixByte(
            hash,
            std::to_integer<std::uint8_t>(byte));
    }

    std::ostringstream out;
    out
        << std::hex
        << std::setw(16)
        << std::setfill('0')
        << hash;

    return out.str();
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
