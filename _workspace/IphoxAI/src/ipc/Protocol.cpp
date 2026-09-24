#include "iphox/ipc/Protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace iphox::ipc {
namespace {

constexpr std::size_t kHeaderSize =
    kMagic.size() +
    sizeof(std::uint16_t) +
    sizeof(std::uint16_t) +
    sizeof(std::uint64_t) +
    sizeof(std::uint32_t) +
    sizeof(std::uint32_t);

template <typename T>
void AppendLe(std::vector<std::byte>& out, T value) {
    static_assert(std::is_integral_v<T>);

    using U = std::make_unsigned_t<T>;
    U raw = static_cast<U>(value);

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(
            static_cast<std::byte>((raw >> (i * 8)) & U{0xFF}));
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
            std::to_integer<unsigned int>(bytes[offset + i]))
            << (i * 8);
    }

    value = static_cast<T>(raw);
    offset += sizeof(T);
    return true;
}

DecodeResult Fail(FrameStatus status, std::string error) {
    DecodeResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

} // namespace

std::vector<std::byte> Protocol::Encode(
    const Frame& frame) {

    if (frame.payload.size() > kMaxPayloadBytes) {
        throw std::invalid_argument("IPC payload exceeds maximum size");
    }

    if (!IsKnownType(frame.header.type)) {
        throw std::invalid_argument("IPC message type is unknown");
    }

    std::vector<std::byte> out;
    out.reserve(kHeaderSize + frame.payload.size());

    out.insert(out.end(), kMagic.begin(), kMagic.end());

    AppendLe(out, frame.header.version);
    AppendLe(out, static_cast<std::uint16_t>(frame.header.type));
    AppendLe(out, frame.header.requestId);
    AppendLe(out, frame.header.flags);
    AppendLe(out, static_cast<std::uint32_t>(frame.payload.size()));

    out.insert(
        out.end(),
        frame.payload.begin(),
        frame.payload.end());

    return out;
}

DecodeResult Protocol::Decode(
    std::span<const std::byte> bytes) {

    if (bytes.size() < kHeaderSize) {
        return Fail(FrameStatus::TooShort, "frame shorter than header");
    }

    if (!std::equal(
            kMagic.begin(),
            kMagic.end(),
            bytes.begin())) {
        return Fail(FrameStatus::BadMagic, "invalid IPC magic");
    }

    std::size_t offset = kMagic.size();

    std::uint16_t version{};
    std::uint16_t typeRaw{};
    std::uint64_t requestId{};
    std::uint32_t flags{};
    std::uint32_t payloadSize{};

    if (!ReadLe(bytes, offset, version) ||
        !ReadLe(bytes, offset, typeRaw) ||
        !ReadLe(bytes, offset, requestId) ||
        !ReadLe(bytes, offset, flags) ||
        !ReadLe(bytes, offset, payloadSize)) {
        return Fail(FrameStatus::TooShort, "truncated IPC header");
    }

    if (version != kProtocolVersion) {
        return Fail(
            FrameStatus::UnsupportedVersion,
            "unsupported IPC protocol version");
    }

    if (payloadSize > kMaxPayloadBytes) {
        return Fail(
            FrameStatus::PayloadTooLarge,
            "IPC payload size exceeds configured maximum");
    }

    if (bytes.size() != kHeaderSize + payloadSize) {
        return Fail(
            FrameStatus::LengthMismatch,
            "IPC frame length does not match payload size");
    }

    const auto type = static_cast<MessageType>(typeRaw);
    if (!IsKnownType(type)) {
        return Fail(
            FrameStatus::UnknownType,
            "unknown IPC message type");
    }

    DecodeResult result;
    result.status = FrameStatus::Ok;
    result.frame.header.version = version;
    result.frame.header.type = type;
    result.frame.header.requestId = requestId;
    result.frame.header.flags = flags;
    result.frame.header.payloadSize = payloadSize;

    result.frame.payload.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.end());

    return result;
}

bool Protocol::IsKnownType(MessageType type) noexcept {
    switch (type) {
    case MessageType::Hello:
    case MessageType::Snapshot:
    case MessageType::Ping:
    case MessageType::Pong:
    case MessageType::ChatSubmit:
    case MessageType::ChatStatus:
    case MessageType::ChatCancel:
    case MessageType::ChatClear:
    case MessageType::BodyStatus:
    case MessageType::BodyVerify:
    case MessageType::DecisionEvaluate:
    case MessageType::DecisionTrace:
    case MessageType::MemoryQuery:
    case MessageType::MemoryResult:
    case MessageType::RuntimeStatus:
    case MessageType::CoreShutdown:
    case MessageType::Error:
        return true;
    }

    return false;
}

} // namespace iphox::ipc
