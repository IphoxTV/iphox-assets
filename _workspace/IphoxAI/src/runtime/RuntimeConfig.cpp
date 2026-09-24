#include "iphox/runtime/RuntimeConfig.hpp"

#include "iphox/foundation/TextEncoding.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

namespace iphox::runtime {
namespace {

std::string Trim(
    std::string_view value) {

    std::size_t first = 0;
    std::size_t last = value.size();

    while (first < last &&
           std::isspace(
               static_cast<unsigned char>(
                   value[first]))) {
        ++first;
    }

    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(
                   value[last - 1]))) {
        --last;
    }

    return std::string{
        value.substr(
            first,
            last - first)
    };
}

template <typename T>
bool ParseUnsigned(
    const std::string& text,
    T minimum,
    T maximum,
    T& output) {

    static_assert(
        std::is_unsigned_v<T>);

    T value{};

    const auto* first = text.data();
    const auto* last =
        first + text.size();

    const auto parsed =
        std::from_chars(
            first,
            last,
            value);

    if (parsed.ec != std::errc{} ||
        parsed.ptr != last ||
        value < minimum ||
        value > maximum) {
        return false;
    }

    output = value;
    return true;
}

bool ParseInt(
    const std::string& text,
    int minimum,
    int maximum,
    int& output) {

    int value{};

    const auto* first = text.data();
    const auto* last =
        first + text.size();

    const auto parsed =
        std::from_chars(
            first,
            last,
            value);

    if (parsed.ec != std::errc{} ||
        parsed.ptr != last ||
        value < minimum ||
        value > maximum) {
        return false;
    }

    output = value;
    return true;
}

void Issue(
    RuntimeConfigResult& result,
    std::size_t line,
    std::string message) {

    result.valid = false;
    result.issues.push_back(
        "line " +
        std::to_string(line) +
        ": " +
        std::move(message));
}

} // namespace

RuntimeConfigResult RuntimeConfigLoader::Parse(
    std::string_view text) {

    RuntimeConfigResult result;
    std::set<std::string> seen;

    std::istringstream input{
        std::string{text}
    };

    std::string line;
    std::size_t lineNumber{};

    while (std::getline(input, line)) {
        ++lineNumber;

        const auto trimmed =
            Trim(line);

        if (trimmed.empty() ||
            trimmed.front() == '#' ||
            trimmed.front() == ';') {
            continue;
        }

        const auto equal =
            trimmed.find('=');

        if (equal == std::string::npos) {
            Issue(
                result,
                lineNumber,
                "expected key=value");
            continue;
        }

        const auto key =
            Trim(
                std::string_view{trimmed}
                    .substr(0, equal));

        const auto value =
            Trim(
                std::string_view{trimmed}
                    .substr(equal + 1));

        if (key.empty()) {
            Issue(
                result,
                lineNumber,
                "empty key");
            continue;
        }

        if (!seen.insert(key).second) {
            Issue(
                result,
                lineNumber,
                "duplicate key: " + key);
            continue;
        }

        if (key == "runtime.host") {
            const auto wide =
                foundation::Utf8ToWide(value);

            if (!wide.has_value() ||
                wide->empty() ||
                !generation::LlamaCppHttpEngine::IsLoopbackHost(
                    *wide)) {
                Issue(
                    result,
                    lineNumber,
                    "runtime.host must be loopback");
                continue;
            }

            result.config.llama.host =
                *wide;

        } else if (key == "runtime.port") {
            std::uint16_t port{};

            if (!ParseUnsigned<std::uint16_t>(
                    value,
                    1,
                    65535,
                    port)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid runtime.port");
                continue;
            }

            result.config.llama.port =
                port;

        } else if (key == "runtime.n_predict") {
            std::uint32_t nPredict{};

            if (!ParseUnsigned<std::uint32_t>(
                    value,
                    1,
                    32768,
                    nPredict)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid runtime.n_predict");
                continue;
            }

            result.config.llama.nPredict =
                nPredict;

        } else if (
            key ==
            "runtime.resolve_timeout_ms") {

            if (!ParseInt(
                    value,
                    100,
                    600000,
                    result.config.llama.resolveTimeoutMs)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid resolve timeout");
            }

        } else if (
            key ==
            "runtime.connect_timeout_ms") {

            if (!ParseInt(
                    value,
                    100,
                    600000,
                    result.config.llama.connectTimeoutMs)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid connect timeout");
            }

        } else if (
            key ==
            "runtime.send_timeout_ms") {

            if (!ParseInt(
                    value,
                    100,
                    600000,
                    result.config.llama.sendTimeoutMs)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid send timeout");
            }

        } else if (
            key ==
            "runtime.receive_timeout_ms") {

            if (!ParseInt(
                    value,
                    100,
                    600000,
                    result.config.llama.receiveTimeoutMs)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid receive timeout");
            }

        } else if (key == "chat.max_turns") {
            std::uint32_t maxTurns{};

            if (!ParseUnsigned<std::uint32_t>(
                    value,
                    1,
                    256,
                    maxTurns)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid chat.max_turns");
            } else {
                result.config.chat.maxTurns =
                    static_cast<std::size_t>(
                        maxTurns);
            }

        } else if (key == "chat.max_bytes") {
            std::uint32_t maxBytes{};

            if (!ParseUnsigned<std::uint32_t>(
                    value,
                    1024,
                    4u * 1024u * 1024u,
                    maxBytes)) {
                Issue(
                    result,
                    lineNumber,
                    "invalid chat.max_bytes");
            } else {
                result.config.chat.maxBytes =
                    static_cast<std::size_t>(
                        maxBytes);
            }

        } else if (key == "chat.system_prompt") {
            if (value.empty()) {
                Issue(
                    result,
                    lineNumber,
                    "chat.system_prompt cannot be empty");
            } else {
                result.config.chat.systemPrompt =
                    value;
            }

        } else {
            // Forward-compatible: unknown keys are ignored.
        }
    }

    return result;
}

RuntimeConfigResult RuntimeConfigLoader::Load(
    const std::filesystem::path& path) {

    RuntimeConfigResult result;

    std::ifstream input{
        path,
        std::ios::binary
    };

    if (!input) {
        result.fileFound = false;
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    result =
        Parse(buffer.str());

    result.fileFound = true;
    return result;
}

} // namespace iphox::runtime
