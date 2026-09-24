#pragma once

#include <filesystem>

namespace iphox::runtime {

[[nodiscard]] std::filesystem::path ExecutablePath();
[[nodiscard]] std::filesystem::path ExecutableDirectory();

[[nodiscard]] std::filesystem::path ResolvePortablePath(
    const std::filesystem::path& root,
    const std::filesystem::path& configured);

} // namespace iphox::runtime
