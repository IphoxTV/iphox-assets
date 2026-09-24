#pragma once

#include "iphox/chat/ConversationHistory.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/generation/IGenerativeEngine.hpp"
#include "iphox/ipc/Protocol.hpp"

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

namespace iphox::core {

struct HandleResult {
    ipc::Frame response;
    bool requestShutdown{};
};

class CoreService final {
public:
    CoreService(
        generation::IGenerativeEngine& engine,
        cognitive::Supervisor& supervisor,
        std::size_t requestCapacity = 1024,
        std::size_t maxConversationTurns = 24,
        std::size_t maxConversationBytes = 64u * 1024u,
        std::string systemPrompt =
            "You are IphoxAI, a local native AI assistant. "
            "Answer the latest user message directly and keep continuity "
            "with the available conversation history.");

    [[nodiscard]] HandleResult Handle(
        const ipc::Frame& request,
        std::stop_token stopToken = {});

private:
    [[nodiscard]] static std::optional<std::string> Fingerprint(
        const ipc::Frame& request);

    [[nodiscard]] static ipc::Frame MakeResponse(
        const ipc::Frame& request,
        ipc::MessageType type);

    [[nodiscard]] static ipc::Frame MakeError(
        const ipc::Frame& request,
        std::string code);

    generation::IGenerativeEngine& engine_;
    cognitive::Supervisor& supervisor_;
    chat::ConversationHistory conversation_;
    foundation::RequestRegistry requests_;
};

} // namespace iphox::core
