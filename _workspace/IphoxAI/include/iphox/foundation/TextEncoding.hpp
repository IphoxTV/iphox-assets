#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace iphox::foundation {

[[nodiscard]] std::optional<std::wstring> Utf8ToWide(
    std::string_view text);

[[nodiscard]] std::optional<std::string> WideToUtf8(
    std::wstring_view text);

} // namespace iphox::foundation
