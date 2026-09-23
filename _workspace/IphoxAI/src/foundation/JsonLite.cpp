#include "iphox/foundation/JsonLite.hpp"

#include <cctype>
#include <cstdint>
#include <limits>

namespace iphox::foundation {
namespace {

void AppendUtf8(
    std::string& out,
    std::uint32_t codepoint) {

    if (codepoint <= 0x7F) {
        out.push_back(
            static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(
            static_cast<char>(
                0xC0 | (codepoint >> 6)));
        out.push_back(
            static_cast<char>(
                0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(
            static_cast<char>(
                0xE0 | (codepoint >> 12)));
        out.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 6) & 0x3F)));
        out.push_back(
            static_cast<char>(
                0x80 |
                (codepoint & 0x3F)));
    } else {
        out.push_back(
            static_cast<char>(
                0xF0 | (codepoint >> 18)));
        out.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 12) & 0x3F)));
        out.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 6) & 0x3F)));
        out.push_back(
            static_cast<char>(
                0x80 |
                (codepoint & 0x3F)));
    }
}

std::optional<std::uint32_t> Hex4(
    std::string_view json,
    std::size_t offset) {

    if (offset + 4 > json.size()) {
        return std::nullopt;
    }

    std::uint32_t value{};

    for (std::size_t i = 0; i < 4; ++i) {
        const char ch = json[offset + i];
        std::uint32_t digit{};

        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(
                ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(
                ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(
                ch - 'A' + 10);
        } else {
            return std::nullopt;
        }

        value =
            (value << 4) |
            digit;
    }

    return value;
}

std::optional<std::string> ParseString(
    std::string_view json,
    std::size_t& offset) {

    if (offset >= json.size() ||
        json[offset] != '"') {
        return std::nullopt;
    }

    ++offset;
    std::string out;

    while (offset < json.size()) {
        const unsigned char raw =
            static_cast<unsigned char>(
                json[offset++]);

        if (raw == '"') {
            return out;
        }

        if (raw < 0x20) {
            return std::nullopt;
        }

        if (raw != '\\') {
            out.push_back(
                static_cast<char>(raw));
            continue;
        }

        if (offset >= json.size()) {
            return std::nullopt;
        }

        const char escaped =
            json[offset++];

        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            out.push_back(escaped);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;

        case 'u': {
            const auto first =
                Hex4(json, offset);

            if (!first.has_value()) {
                return std::nullopt;
            }

            offset += 4;
            std::uint32_t codepoint =
                *first;

            if (codepoint >= 0xD800 &&
                codepoint <= 0xDBFF) {

                if (offset + 6 > json.size() ||
                    json[offset] != '\\' ||
                    json[offset + 1] != 'u') {
                    return std::nullopt;
                }

                const auto second =
                    Hex4(
                        json,
                        offset + 2);

                if (!second.has_value() ||
                    *second < 0xDC00 ||
                    *second > 0xDFFF) {
                    return std::nullopt;
                }

                offset += 6;

                codepoint =
                    0x10000 +
                    ((codepoint - 0xD800) << 10) +
                    (*second - 0xDC00);

            } else if (
                codepoint >= 0xDC00 &&
                codepoint <= 0xDFFF) {
                return std::nullopt;
            }

            if (codepoint > 0x10FFFF) {
                return std::nullopt;
            }

            AppendUtf8(
                out,
                codepoint);
            break;
        }

        default:
            return std::nullopt;
        }
    }

    return std::nullopt;
}

void SkipWhitespace(
    std::string_view json,
    std::size_t& offset) {

    while (offset < json.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   json[offset]))) {
        ++offset;
    }
}

} // namespace

std::string JsonEscape(
    std::string_view utf8) {

    static constexpr char kHex[] =
        "0123456789abcdef";

    std::string out;
    out.reserve(utf8.size() + 16);

    for (const unsigned char ch : utf8) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out.push_back(
                    kHex[(ch >> 4) & 0x0F]);
                out.push_back(
                    kHex[ch & 0x0F]);
            } else {
                out.push_back(
                    static_cast<char>(ch));
            }
            break;
        }
    }

    return out;
}

std::optional<std::string> JsonStringField(
    std::string_view json,
    std::string_view field) {

    std::size_t offset = 0;

    while (offset < json.size()) {
        if (json[offset] != '"') {
            ++offset;
            continue;
        }

        const auto keyStart = offset;
        auto key =
            ParseString(
                json,
                offset);

        if (!key.has_value()) {
            offset = keyStart + 1;
            continue;
        }

        SkipWhitespace(json, offset);

        if (offset >= json.size() ||
            json[offset] != ':') {
            continue;
        }

        ++offset;
        SkipWhitespace(json, offset);

        if (*key != field) {
            continue;
        }

        return ParseString(
            json,
            offset);
    }

    return std::nullopt;
}

} // namespace iphox::foundation
