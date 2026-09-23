#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace iphox::chat {

inline constexpr std::uint16_t kChatPayloadVersion = 1;
inline constexpr std::uint32_t kMaxChatTextBytes = 256u * 1024u;

enum class ChatCodecStatus : std::uint8_t {
    Ok,
    TooShort,
    BadMagic,
    UnsupportedVersion,
    InvalidFlags,
    InvalidAttachmentCount,
    TextTooLarge,
    LengthMismatch,
    InvalidUtf8
};

struct ChatSubmit {
    std::string text;
};

struct ChatDecodeResult {
    ChatCodecStatus status{ChatCodecStatus::TooShort};
    ChatSubmit value;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return status == ChatCodecStatus::Ok;
    }
};

class ChatCodec final {
public:
    [[nodiscard]] static std::vector<std::byte> Encode(
        const ChatSubmit& submit);

    [[nodiscard]] static ChatDecodeResult Decode(
        std::span<const std::byte> bytes);

    [[nodiscard]] static bool IsValidUtf8(
        std::string_view text) noexcept;
};

} // namespace iphox::chat
