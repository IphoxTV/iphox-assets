#include "iphox/foundation/RequestRegistry.hpp"

#include <stdexcept>
#include <utility>

namespace iphox::foundation {

RequestRegistry::RequestRegistry(std::size_t capacity)
    : capacity_(capacity) {
    if (capacity_ == 0) {
        throw std::invalid_argument("RequestRegistry capacity must be non-zero");
    }
}

RegisterResult RequestRegistry::Register(
    std::uint64_t requestId,
    std::string fingerprint) {

    std::scoped_lock lock{mutex_};

    if (const auto it = records_.find(requestId); it != records_.end()) {
        return it->second.fingerprint == fingerprint
            ? RegisterResult::Duplicate
            : RegisterResult::Conflict;
    }

    order_.push_back(requestId);
    records_.emplace(
        requestId,
        RequestSnapshot{
            .requestId = requestId,
            .fingerprint = std::move(fingerprint),
            .completed = false
        });

    EvictIfNeeded();
    return RegisterResult::NewRequest;
}

void RequestRegistry::MarkCompleted(std::uint64_t requestId) {
    std::scoped_lock lock{mutex_};

    if (const auto it = records_.find(requestId); it != records_.end()) {
        it->second.completed = true;
    }
}

std::optional<RequestSnapshot> RequestRegistry::Find(
    std::uint64_t requestId) const {

    std::scoped_lock lock{mutex_};

    if (const auto it = records_.find(requestId); it != records_.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::size_t RequestRegistry::Size() const {
    std::scoped_lock lock{mutex_};
    return records_.size();
}

void RequestRegistry::EvictIfNeeded() {
    while (records_.size() > capacity_ && !order_.empty()) {
        const auto oldest = order_.front();
        order_.pop_front();

        const auto it = records_.find(oldest);
        if (it == records_.end()) {
            continue;
        }

        if (!it->second.completed) {
            // Active requests must never be evicted. Put it back and stop;
            // boundedness resumes as requests complete.
            order_.push_back(oldest);
            break;
        }

        records_.erase(it);
    }
}

} // namespace iphox::foundation
