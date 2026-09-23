#include "iphox/runtime/CoreProcessHost.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace iphox::runtime {
namespace {

std::filesystem::path CurrentExecutablePath() {
    std::vector<wchar_t> buffer(32768);

    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));

    if (length == 0 ||
        length >= buffer.size()) {
        return {};
    }

    return std::filesystem::path{
        std::wstring{
            buffer.data(),
            length
        }
    };
}

} // namespace

CoreProcessHost::~CoreProcessHost() {
    Close();
}

bool CoreProcessHost::StartSiblingCore() {
    Close();

    const auto executable = CurrentExecutablePath();
    if (executable.empty()) {
        return false;
    }

    const auto corePath =
        executable.parent_path() /
        L"IphoxCore.exe";

    if (!std::filesystem::exists(corePath)) {
        return false;
    }

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_ == nullptr) {
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!SetInformationJobObject(
            job_,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        Close();
        return false;
    }

    std::wstring commandLine =
        L"\"" +
        corePath.wstring() +
        L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            corePath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED |
                CREATE_NO_WINDOW,
            nullptr,
            corePath.parent_path().c_str(),
            &startup,
            &process)) {
        Close();
        return false;
    }

    if (!AssignProcessToJobObject(
            job_,
            process.hProcess)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        Close();
        return false;
    }

    process_ = process.hProcess;

    if (ResumeThread(process.hThread) ==
        static_cast<DWORD>(-1)) {
        TerminateProcess(process_, 1);
        CloseHandle(process.hThread);
        Close();
        return false;
    }

    CloseHandle(process.hThread);
    return true;
}

bool CoreProcessHost::WaitForExit(
    DWORD timeoutMs) const noexcept {

    if (process_ == nullptr) {
        return true;
    }

    return WaitForSingleObject(
        process_,
        timeoutMs) == WAIT_OBJECT_0;
}

bool CoreProcessHost::IsRunning() const noexcept {
    if (process_ == nullptr) {
        return false;
    }

    DWORD exitCode{};
    if (!GetExitCodeProcess(
            process_,
            &exitCode)) {
        return false;
    }

    return exitCode == STILL_ACTIVE;
}

void CoreProcessHost::Close() noexcept {
    if (process_ != nullptr) {
        CloseHandle(process_);
        process_ = nullptr;
    }

    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE guarantees that an
    // unresponsive Core cannot survive the owning UI process.
    if (job_ != nullptr) {
        CloseHandle(job_);
        job_ = nullptr;
    }
}

} // namespace iphox::runtime
