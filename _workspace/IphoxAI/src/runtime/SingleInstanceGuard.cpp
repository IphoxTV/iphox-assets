#include "iphox/runtime/SingleInstanceGuard.hpp"

namespace iphox::runtime {

SingleInstanceGuard::SingleInstanceGuard(
    const std::wstring& name) {

    if (name.empty()) {
        return;
    }

    SetLastError(ERROR_SUCCESS);

    mutex_ =
        CreateMutexW(
            nullptr,
            FALSE,
            name.c_str());

    if (mutex_ == nullptr) {
        return;
    }

    acquired_ =
        GetLastError() !=
        ERROR_ALREADY_EXISTS;
}

SingleInstanceGuard::~SingleInstanceGuard() {
    if (mutex_ != nullptr) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

} // namespace iphox::runtime
