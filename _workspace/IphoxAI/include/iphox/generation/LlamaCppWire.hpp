#pragma once

#include "IGenerativeEngine.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace iphox::generation {

class LlamaCppWire final {
public:
    [[nodiscard]] static std::optional<std::string> BuildChatRequest(
        const GenerationRequest& request,
        std::uint32_t maxTokens);

    [[nodiscard]] static std::optional<std::string> ParseChatContent(
        std::string_view json);
};

} // namespace iphox::generation
