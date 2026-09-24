#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace iphox::runtime {

class SingleInstanceGuard final {
public:
    explicit SingleInstanceGuard(
        const std::wstring& name);

    SingleInstanceGuard(
        const SingleInstanceGuard&) = delete;

    SingleInstanceGuard& operator=(
        const SingleInstanceGuard&) = delete;

    ~SingleInstanceGuard();

    [[nodiscard]] bool Acquired() const noexcept {
        return acquired_;
    }

private:
    HANDLE mutex_{};
    bool acquired_{};
};

} // namespace iphox::runtime
