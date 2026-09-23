#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace iphox::ipc {

inline std::vector<std::byte> ToPayload(
    std::string_view text) {

    std::vector<std::byte> bytes;
    bytes.reserve(text.size());

    for (const unsigned char ch : text) {
        bytes.push_back(
            static_cast<std::byte>(ch));
    }

    return bytes;
}

inline std::string FromPayload(
    std::span<const std::byte> payload) {

    std::string text;
    text.reserve(payload.size());

    for (const auto value : payload) {
        text.push_back(
            static_cast<char>(
                std::to_integer<unsigned char>(value)));
    }

    return text;
}

} // namespace iphox::ipc
