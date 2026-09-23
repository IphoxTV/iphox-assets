#pragma once

#include "DecisionTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace iphox::cognitive {

enum class DecisionCodecStatus : std::uint8_t {
    Ok,
    TooShort,
    BadMagic,
    UnsupportedVersion,
    TooLarge,
    CountLimit,
    InvalidType,
    InvalidValue,
    LengthMismatch
};

template <typename T>
struct CodecResult {
    DecisionCodecStatus status{DecisionCodecStatus::TooShort};
    T value{};
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return status == DecisionCodecStatus::Ok;
    }
};

class DecisionCodec final {
public:
    [[nodiscard]] static std::vector<std::byte> EncodeRequest(
        const DecisionRequest& request);

    [[nodiscard]] static CodecResult<DecisionRequest> DecodeRequest(
        std::span<const std::byte> bytes);

    [[nodiscard]] static std::vector<std::byte> EncodeResponse(
        const DecisionResponse& response);

    [[nodiscard]] static CodecResult<DecisionResponse> DecodeResponse(
        std::span<const std::byte> bytes);
};

} // namespace iphox::cognitive
