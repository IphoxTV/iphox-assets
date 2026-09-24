#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace iphox::ipc {

inline constexpr std::array<std::byte, 4> kMagic{
    std::byte{'I'},
    std::byte{'P'},
    std::byte{'H'},
    std::byte{'X'}
};

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMaxPayloadBytes = 4u * 1024u * 1024u;

inline constexpr std::uint32_t kFlagResponse = 1u << 0;
inline constexpr std::uint32_t kFlagError = 1u << 1;

enum class MessageType : std::uint16_t {
    Hello = 1,
    Snapshot = 2,
    Ping = 3,
    Pong = 4,

    ChatSubmit = 100,
    ChatStatus = 101,
    ChatCancel = 102,
    ChatClear = 103,

    BodyStatus = 200,
    BodyVerify = 201,

    DecisionEvaluate = 300,
    DecisionTrace = 301,

    MemoryQuery = 400,
    MemoryResult = 401,

    RuntimeStatus = 500,

    CoreShutdown = 900,

    Error = 0xFFFF
};

enum class FrameStatus : std::uint8_t {
    Ok,
    TooShort,
    BadMagic,
    UnsupportedVersion,
    PayloadTooLarge,
    LengthMismatch,
    UnknownType
};

struct FrameHeader {
    std::uint16_t version{kProtocolVersion};
    MessageType type{MessageType::Error};
    std::uint64_t requestId{};
    std::uint32_t flags{};
    std::uint32_t payloadSize{};
};

struct Frame {
    FrameHeader header;
    std::vector<std::byte> payload;
};

struct DecodeResult {
    FrameStatus status{FrameStatus::TooShort};
    Frame frame;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return status == FrameStatus::Ok;
    }
};

class Protocol final {
public:
    [[nodiscard]] static std::vector<std::byte> Encode(
        const Frame& frame);

    [[nodiscard]] static DecodeResult Decode(
        std::span<const std::byte> bytes);

    [[nodiscard]] static bool IsKnownType(
        MessageType type) noexcept;
};

} // namespace iphox::ipc
