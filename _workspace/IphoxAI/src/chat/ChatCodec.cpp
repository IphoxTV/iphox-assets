#include "iphox/chat/ChatCodec.hpp"

#include <array>
#include <stdexcept>
#include <type_traits>

namespace iphox::chat {
namespace {

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'C'},
    std::byte{'H'},
    std::byte{'T'},
    std::byte{'1'}
};

constexpr std::size_t kHeaderSize =
    kMagic.size() +
    sizeof(std::uint16_t) +
    sizeof(std::uint16_t) +
    sizeof(std::uint32_t) +
    sizeof(std::uint16_t) +
    sizeof(std::uint16_t);

template <typename T>
void AppendLe(
    std::vector<std::byte>& out,
    T value) {

    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;

    const U raw = static_cast<U>(value);

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(
            static_cast<std::byte>(
                (raw >> (i * 8)) & U{0xFF}));
    }
}

template <typename T>
bool ReadLe(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    T& value) {

    static_assert(std::is_integral_v<T>);

    if (offset + sizeof(T) > bytes.size()) {
        return false;
    }

    using U = std::make_unsigned_t<T>;
    U raw{};

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        raw |= static_cast<U>(
            std::to_integer<unsigned int>(
                bytes[offset + i]))
            << (i * 8);
    }

    value = static_cast<T>(raw);
    offset += sizeof(T);
    return true;
}

ChatDecodeResult Fail(
    ChatCodecStatus status,
    std::string error) {

    ChatDecodeResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

} // namespace

std::vector<std::byte> ChatCodec::Encode(
    const ChatSubmit& submit) {

    if (submit.text.size() > kMaxChatTextBytes) {
        throw std::invalid_argument(
            "chat text exceeds maximum size");
    }

    if (!IsValidUtf8(submit.text)) {
        throw std::invalid_argument(
            "chat text is not valid UTF-8");
    }

    std::vector<std::byte> out;
    out.reserve(
        kHeaderSize +
        submit.text.size());

    out.insert(
        out.end(),
        kMagic.begin(),
        kMagic.end());

    AppendLe(
        out,
        kChatPayloadVersion);

    AppendLe<std::uint16_t>(
        out,
        0);

    AppendLe(
        out,
        static_cast<std::uint32_t>(
            submit.text.size()));

    // Attachments are deliberately zero in V1.
    AppendLe<std::uint16_t>(
        out,
        0);

    AppendLe<std::uint16_t>(
        out,
        0);

    for (const unsigned char ch : submit.text) {
        out.push_back(
            static_cast<std::byte>(ch));
    }

    return out;
}

ChatDecodeResult ChatCodec::Decode(
    std::span<const std::byte> bytes) {

    if (bytes.size() < kHeaderSize) {
        return Fail(
            ChatCodecStatus::TooShort,
            "chat payload shorter than header");
    }

    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (bytes[i] != kMagic[i]) {
            return Fail(
                ChatCodecStatus::BadMagic,
                "invalid chat payload magic");
        }
    }

    std::size_t offset = kMagic.size();

    std::uint16_t version{};
    std::uint16_t flags{};
    std::uint32_t textBytes{};
    std::uint16_t attachmentCount{};
    std::uint16_t reserved{};

    if (!ReadLe(bytes, offset, version) ||
        !ReadLe(bytes, offset, flags) ||
        !ReadLe(bytes, offset, textBytes) ||
        !ReadLe(bytes, offset, attachmentCount) ||
        !ReadLe(bytes, offset, reserved)) {

        return Fail(
            ChatCodecStatus::TooShort,
            "truncated chat payload header");
    }

    if (version != kChatPayloadVersion) {
        return Fail(
            ChatCodecStatus::UnsupportedVersion,
            "unsupported chat payload version");
    }

    if (flags != 0 || reserved != 0) {
        return Fail(
            ChatCodecStatus::InvalidFlags,
            "unsupported chat payload flags");
    }

    if (attachmentCount != 0) {
        return Fail(
            ChatCodecStatus::InvalidAttachmentCount,
            "attachments are not supported in chat payload V1");
    }

    if (textBytes > kMaxChatTextBytes) {
        return Fail(
            ChatCodecStatus::TextTooLarge,
            "chat text exceeds maximum size");
    }

    if (bytes.size() !=
        kHeaderSize +
        static_cast<std::size_t>(textBytes)) {

        return Fail(
            ChatCodecStatus::LengthMismatch,
            "chat payload length mismatch");
    }

    std::string text;
    text.reserve(textBytes);

    for (std::size_t i = 0; i < textBytes; ++i) {
        text.push_back(
            static_cast<char>(
                std::to_integer<unsigned char>(
                    bytes[offset + i])));
    }

    if (!IsValidUtf8(text)) {
        return Fail(
            ChatCodecStatus::InvalidUtf8,
            "chat text is not valid UTF-8");
    }

    ChatDecodeResult result;
    result.status = ChatCodecStatus::Ok;
    result.value.text = std::move(text);
    return result;
}

bool ChatCodec::IsValidUtf8(
    std::string_view text) noexcept {

    std::size_t i = 0;

    while (i < text.size()) {
        const auto c =
            static_cast<unsigned char>(
                text[i]);

        if (c <= 0x7F) {
            ++i;
            continue;
        }

        std::size_t continuation{};
        std::uint32_t codepoint{};

        if ((c & 0xE0) == 0xC0) {
            continuation = 1;
            codepoint = c & 0x1F;
            if (codepoint == 0) {
                return false;
            }
        } else if ((c & 0xF0) == 0xE0) {
            continuation = 2;
            codepoint = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            continuation = 3;
            codepoint = c & 0x07;
        } else {
            return false;
        }

        if (i + continuation >= text.size()) {
            return false;
        }

        for (std::size_t j = 1; j <= continuation; ++j) {
            const auto next =
                static_cast<unsigned char>(
                    text[i + j]);

            if ((next & 0xC0) != 0x80) {
                return false;
            }

            codepoint =
                (codepoint << 6) |
                (next & 0x3F);
        }

        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }

        i += continuation + 1;
    }

    return true;
}

} // namespace iphox::chat
