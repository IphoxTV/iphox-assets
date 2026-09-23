#include "iphox/foundation/TextEncoding.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <limits>

namespace iphox::foundation {

std::optional<std::wstring> Utf8ToWide(
    std::string_view text) {

    if (text.empty()) {
        return std::wstring{};
    }

    if (text.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }

    const int inputSize =
        static_cast<int>(
            text.size());

    const int required =
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            inputSize,
            nullptr,
            0);

    if (required <= 0) {
        return std::nullopt;
    }

    std::wstring output(
        static_cast<std::size_t>(
            required),
        L'\0');

    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            inputSize,
            output.data(),
            required) != required) {
        return std::nullopt;
    }

    return output;
}

std::optional<std::string> WideToUtf8(
    std::wstring_view text) {

    if (text.empty()) {
        return std::string{};
    }

    if (text.size() >
        static_cast<std::size_t>(
            (std::numeric_limits<int>::max)())) {
        return std::nullopt;
    }

    const int inputSize =
        static_cast<int>(
            text.size());

    const int required =
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            inputSize,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (required <= 0) {
        return std::nullopt;
    }

    std::string output(
        static_cast<std::size_t>(
            required),
        '\0');

    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text.data(),
            inputSize,
            output.data(),
            required,
            nullptr,
            nullptr) != required) {
        return std::nullopt;
    }

    return output;
}

} // namespace iphox::foundation
