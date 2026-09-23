#include "iphox/foundation/CoreLifecycle.hpp"

#include <utility>

namespace iphox::foundation {

JobLease::JobLease(JobLease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

JobLease& JobLease::operator=(JobLease&& other) noexcept {
    if (this != &other) {
        Reset();
        owner_ = std::exchange(other.owner_, nullptr);
    }
    return *this;
}

JobLease::~JobLease() {
    Reset();
}

void JobLease::Reset() noexcept {
    if (owner_ != nullptr) {
        owner_->ReleaseJob();
        owner_ = nullptr;
    }
}

bool CoreLifecycle::BeginStart() {
    std::scoped_lock lock{mutex_};

    if (state_ != CoreState::Created) {
        return false;
    }

    state_ = CoreState::Starting;
    return true;
}

bool CoreLifecycle::MarkReady() {
    std::scoped_lock lock{mutex_};

    if (state_ != CoreState::Starting) {
        return false;
    }

    state_ = CoreState::Ready;
    return true;
}

bool CoreLifecycle::RequestStop() {
    std::scoped_lock lock{mutex_};

    if (state_ == CoreState::Stopped ||
        state_ == CoreState::Faulted) {
        return false;
    }

    if (state_ == CoreState::Stopping) {
        return true;
    }

    state_ = CoreState::Stopping;

    if (activeJobs_ == 0) {
        drained_.notify_all();
    }

    return true;
}

bool CoreLifecycle::MarkStopped() {
    std::scoped_lock lock{mutex_};

    if (state_ != CoreState::Stopping ||
        activeJobs_ != 0) {
        return false;
    }

    state_ = CoreState::Stopped;
    return true;
}

void CoreLifecycle::MarkFaulted() {
    std::scoped_lock lock{mutex_};
    state_ = CoreState::Faulted;
    drained_.notify_all();
}

std::optional<JobLease> CoreLifecycle::TryAcquireJob() {
    std::scoped_lock lock{mutex_};

    if (state_ != CoreState::Ready) {
        return std::nullopt;
    }

    ++activeJobs_;
    return JobLease{this};
}

CoreState CoreLifecycle::State() const {
    std::scoped_lock lock{mutex_};
    return state_;
}

std::size_t CoreLifecycle::ActiveJobs() const {
    std::scoped_lock lock{mutex_};
    return activeJobs_;
}

void CoreLifecycle::ReleaseJob() noexcept {
    std::scoped_lock lock{mutex_};

    if (activeJobs_ == 0) {
        state_ = CoreState::Faulted;
        drained_.notify_all();
        return;
    }

    --activeJobs_;

    if (activeJobs_ == 0) {
        drained_.notify_all();
    }
}

} // namespace iphox::foundation
