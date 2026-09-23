#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace iphox::foundation {

using Sha256Digest = std::array<std::byte, 32>;

[[nodiscard]] std::optional<Sha256Digest> Sha256(
    std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::string Hex(
    const Sha256Digest& digest);

} // namespace iphox::foundation
