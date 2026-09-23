#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace iphox::foundation {

enum class RegisterResult : std::uint8_t {
    NewRequest,
    Duplicate,
    Conflict
};

struct RequestSnapshot {
    std::uint64_t requestId{};
    std::string fingerprint;
    bool completed{};
};

class RequestRegistry final {
public:
    explicit RequestRegistry(std::size_t capacity = 1024);

    [[nodiscard]] RegisterResult Register(
        std::uint64_t requestId,
        std::string fingerprint);

    void MarkCompleted(std::uint64_t requestId);

    [[nodiscard]] std::optional<RequestSnapshot> Find(
        std::uint64_t requestId) const;

    [[nodiscard]] std::size_t Size() const;

private:
    void EvictIfNeeded();

    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<std::uint64_t> order_;
    std::unordered_map<std::uint64_t, RequestSnapshot> records_;
};

} // namespace iphox::foundation
