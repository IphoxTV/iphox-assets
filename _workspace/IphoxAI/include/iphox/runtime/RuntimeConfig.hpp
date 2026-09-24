#pragma once

#include "iphox/generation/LlamaCppHttpEngine.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace iphox::runtime {

struct ChatRuntimeConfig {
    std::size_t maxTurns{24};
    std::size_t maxBytes{64u * 1024u};

    std::string systemPrompt{
        "You are IphoxAI, a local native AI assistant. "
        "Answer the latest user message directly and keep continuity "
        "with the available conversation history."
    };
};

struct RuntimeConfig {
    generation::LlamaCppHttpConfig llama;
    ChatRuntimeConfig chat;
};

struct RuntimeConfigResult {
    RuntimeConfig config;
    bool fileFound{};
    bool valid{true};
    std::vector<std::string> issues;
};

class RuntimeConfigLoader final {
public:
    [[nodiscard]] static RuntimeConfigResult Parse(
        std::string_view text);

    [[nodiscard]] static RuntimeConfigResult Load(
        const std::filesystem::path& path);
};

} // namespace iphox::runtime
