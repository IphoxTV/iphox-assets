#pragma once

#include <filesystem>

namespace iphox::runtime {

[[nodiscard]] std::filesystem::path ExecutablePath();
[[nodiscard]] std::filesystem::path ExecutableDirectory();

} // namespace iphox::runtime
