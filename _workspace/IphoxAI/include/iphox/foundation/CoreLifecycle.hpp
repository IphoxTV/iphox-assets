#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace iphox::foundation {

enum class CoreState : std::uint8_t {
    Created,
    Starting,
    Ready,
    Stopping,
    Stopped,
    Faulted
};

class CoreLifecycle;

class JobLease final {
public:
    JobLease() = default;
    JobLease(const JobLease&) = delete;
    JobLease& operator=(const JobLease&) = delete;

    JobLease(JobLease&& other) noexcept;
    JobLease& operator=(JobLease&& other) noexcept;

    ~JobLease();

    [[nodiscard]] explicit operator bool() const noexcept {
        return owner_ != nullptr;
    }

private:
    friend class CoreLifecycle;

    explicit JobLease(CoreLifecycle* owner)
        : owner_(owner) {}

    void Reset() noexcept;

    CoreLifecycle* owner_{};
};

class CoreLifecycle final {
public:
    [[nodiscard]] bool BeginStart();
    [[nodiscard]] bool MarkReady();
    [[nodiscard]] bool RequestStop();
    [[nodiscard]] bool MarkStopped();
    void MarkFaulted();

    [[nodiscard]] std::optional<JobLease> TryAcquireJob();

    template <class Rep, class Period>
    [[nodiscard]] bool WaitForDrain(
        const std::chrono::duration<Rep, Period>& timeout) {

        std::unique_lock lock{mutex_};
        return drained_.wait_for(
            lock,
            timeout,
            [&] {
                return activeJobs_ == 0;
            });
    }

    [[nodiscard]] CoreState State() const;
    [[nodiscard]] std::size_t ActiveJobs() const;

private:
    friend class JobLease;

    void ReleaseJob() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    CoreState state_{CoreState::Created};
    std::size_t activeJobs_{};
};

} // namespace iphox::foundation
