#pragma once

#include "LlamaCppHttpEngine.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace iphox::runtime {

struct RuntimeConfig {
    generation::LlamaCppHttpConfig llama;
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
