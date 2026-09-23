#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace iphox::foundation {

[[nodiscard]] std::string JsonEscape(
    std::string_view utf8);

[[nodiscard]] std::optional<std::string> JsonStringField(
    std::string_view json,
    std::string_view field);

} // namespace iphox::foundation
