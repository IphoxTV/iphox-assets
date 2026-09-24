#include "iphox/generation/LlamaCppWire.hpp"

#include "iphox/foundation/JsonLite.hpp"

namespace iphox::generation {
namespace {

const char* RoleName(
    GenerationRole role) noexcept {

    switch (role) {
    case GenerationRole::System:
        return "system";
    case GenerationRole::User:
        return "user";
    case GenerationRole::Assistant:
        return "assistant";
    }

    return "user";
}

} // namespace

std::optional<std::string>
LlamaCppWire::BuildChatRequest(
    const GenerationRequest& request,
    std::uint32_t maxTokens) {

    if (request.messages.empty() ||
        maxTokens == 0 ||
        maxTokens > 32768) {
        return std::nullopt;
    }

    std::string body =
        "{\"messages\":[";

    bool first = true;

    for (const auto& message :
         request.messages) {

        if (!first) {
            body += ",";
        }

        first = false;

        body +=
            "{\"role\":\"" +
            std::string{
                RoleName(message.role)
            } +
            "\",\"content\":\"" +
            foundation::JsonEscape(
                message.text) +
            "\"}";
    }

    body +=
        "],\"max_tokens\":" +
        std::to_string(maxTokens) +
        ",\"stream\":false}";

    return body;
}

std::optional<std::string>
LlamaCppWire::ParseChatContent(
    std::string_view json) {

    return foundation::JsonStringField(
        json,
        "content");
}

} // namespace iphox::generation
