#include "iphox/chat/ConversationHistory.hpp"

#include <stdexcept>
#include <utility>

namespace iphox::chat {

ConversationHistory::ConversationHistory(
    std::size_t maxTurns,
    std::size_t maxBytes,
    std::string systemPrompt)
    : maxTurns_(maxTurns),
      maxBytes_(maxBytes),
      systemPrompt_(std::move(systemPrompt)) {

    if (maxTurns_ == 0 ||
        maxBytes_ == 0) {
        throw std::invalid_argument(
            "ConversationHistory limits must be non-zero");
    }
}

void ConversationHistory::AddTurn(
    std::string user,
    std::string assistant) {

    bytes_ +=
        user.size() +
        assistant.size();

    turns_.push_back({
        .user = std::move(user),
        .assistant = std::move(assistant)
    });

    Trim();
}

generation::GenerationRequest
ConversationHistory::BuildRequest(
    std::string currentUser) const {

    generation::GenerationRequest request;

    request.messages.push_back({
        .role = generation::GenerationRole::System,
        .text = systemPrompt_
    });

    for (const auto& turn : turns_) {
        request.messages.push_back({
            .role = generation::GenerationRole::User,
            .text = turn.user
        });

        request.messages.push_back({
            .role = generation::GenerationRole::Assistant,
            .text = turn.assistant
        });
    }

    request.messages.push_back({
        .role = generation::GenerationRole::User,
        .text = std::move(currentUser)
    });

    return request;
}

void ConversationHistory::Clear() noexcept {
    turns_.clear();
    bytes_ = 0;
}

void ConversationHistory::Trim() {
    while (!turns_.empty() &&
           (turns_.size() > maxTurns_ ||
            bytes_ > maxBytes_)) {

        bytes_ -=
            turns_.front().user.size() +
            turns_.front().assistant.size();

        turns_.pop_front();
    }
}

} // namespace iphox::chat
