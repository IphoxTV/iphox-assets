#pragma once

#include "iphox/generation/IGenerativeEngine.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace iphox::chat {

struct ConversationTurn {
    std::string user;
    std::string assistant;
};

class ConversationHistory final {
public:
    explicit ConversationHistory(
        std::size_t maxTurns = 24,
        std::size_t maxBytes = 64u * 1024u,
        std::string systemPrompt =
            "You are IphoxAI, a local native AI assistant. "
            "Answer the latest user message directly and keep continuity "
            "with the available conversation history.");

    void AddTurn(
        std::string user,
        std::string assistant);

    [[nodiscard]] generation::GenerationRequest BuildRequest(
        std::string currentUser) const;

    void Clear() noexcept;

    [[nodiscard]] std::size_t Size() const noexcept {
        return turns_.size();
    }

    [[nodiscard]] std::size_t Bytes() const noexcept {
        return bytes_;
    }

private:
    void Trim();

    std::size_t maxTurns_;
    std::size_t maxBytes_;
    std::size_t bytes_{};

    std::string systemPrompt_;
    std::deque<ConversationTurn> turns_;
};

} // namespace iphox::chat
