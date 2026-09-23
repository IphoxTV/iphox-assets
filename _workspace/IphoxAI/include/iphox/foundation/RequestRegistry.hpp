#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace iphox::foundation {

enum class RegisterResult : std::uint8_t {
    NewRequest,
    Duplicate,
    Conflict,
    CapacityExhausted
};

struct RequestSnapshot {
    std::uint64_t requestId{};
    std::string fingerprint;
    bool completed{};
    std::vector<std::byte> encodedResponse;
};

class RequestRegistry final {
public:
    explicit RequestRegistry(std::size_t capacity = 1024);

    [[nodiscard]] RegisterResult Register(
        std::uint64_t requestId,
        std::string fingerprint);

    [[nodiscard]] bool MarkCompleted(
        std::uint64_t requestId,
        std::vector<std::byte> encodedResponse);

    [[nodiscard]] std::optional<RequestSnapshot> Find(
        std::uint64_t requestId) const;

    [[nodiscard]] std::size_t Size() const;

private:
    [[nodiscard]] bool EvictOneCompleted();

    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<std::uint64_t> order_;
    std::unordered_map<std::uint64_t, RequestSnapshot> records_;
};

} // namespace iphox::foundation
