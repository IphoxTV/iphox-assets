#include "iphox/runtime/LlamaServerProcessHost.hpp"

#include <filesystem>
#include <string>

namespace iphox::runtime {
namespace {

std::wstring QuoteArg(
    const std::wstring& value) {

    if (value.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes =
        value.find_first_of(
            L" \t\"") !=
        std::wstring::npos;

    if (!needsQuotes) {
        return value;
    }

    std::wstring out;
    out.push_back(L'\"');

    std::size_t backslashes{};

    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }

        if (ch == L'\"') {
            out.append(
                backslashes * 2 + 1,
                L'\\');

            out.push_back(L'\"');
            backslashes = 0;
            continue;
        }

        out.append(
            backslashes,
            L'\\');

        backslashes = 0;
        out.push_back(ch);
    }

    out.append(
        backslashes * 2,
        L'\\');

    out.push_back(L'\"');
    return out;
}

bool IsValidGpuLayers(
    const std::wstring& value) {

    if (value == L"auto" ||
        value == L"all") {
        return true;
    }

    if (value.empty()) {
        return false;
    }

    for (const wchar_t ch : value) {
        if (ch < L'0' ||
            ch > L'9') {
            return false;
        }
    }

    return true;
}

} // namespace

LlamaServerProcessHost::~LlamaServerProcessHost() {
    Stop();
}

bool LlamaServerProcessHost::Start(
    const LlamaServerLaunchConfig& config) {

    Stop();

    if (config.port == 0 ||
        config.contextSize == 0 ||
        !IsValidGpuLayers(
            config.gpuLayers) ||
        !std::filesystem::is_regular_file(
            config.serverPath) ||
        !std::filesystem::is_regular_file(
            config.modelPath)) {
        return false;
    }

    job_ =
        CreateJobObjectW(
            nullptr,
            nullptr);

    if (job_ == nullptr) {
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        limits{};

    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    if (!SetInformationJobObject(
            job_,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        Stop();
        return false;
    }

    std::wstring commandLine =
        QuoteArg(
            config.serverPath.wstring());

    commandLine +=
        L" --host 127.0.0.1 --port ";

    commandLine +=
        std::to_wstring(
            config.port);

    commandLine +=
        L" --ctx-size ";

    commandLine +=
        std::to_wstring(
            config.contextSize);

    commandLine +=
        L" --n-gpu-layers ";

    commandLine +=
        config.gpuLayers;

    commandLine +=
        L" -m ";

    commandLine +=
        QuoteArg(
            config.modelPath.wstring());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process{};

    const auto workingDirectory =
        config.serverPath.parent_path();

    if (!CreateProcessW(
            config.serverPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED |
                CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty()
                ? nullptr
                : workingDirectory.c_str(),
            &startup,
            &process)) {
        Stop();
        return false;
    }

    if (!AssignProcessToJobObject(
            job_,
            process.hProcess)) {

        TerminateProcess(
            process.hProcess,
            1);

        CloseHandle(
            process.hThread);

        CloseHandle(
            process.hProcess);

        Stop();
        return false;
    }

    process_ =
        process.hProcess;

    if (ResumeThread(
            process.hThread) ==
        static_cast<DWORD>(-1)) {

        CloseHandle(
            process.hThread);

        Stop();
        return false;
    }

    CloseHandle(
        process.hThread);

    return true;
}

bool LlamaServerProcessHost::IsRunning()
    const noexcept {

    if (process_ == nullptr) {
        return false;
    }

    DWORD exitCode{};

    if (!GetExitCodeProcess(
            process_,
            &exitCode)) {
        return false;
    }

    return exitCode ==
        STILL_ACTIVE;
}

bool LlamaServerProcessHost::WaitForExit(
    DWORD timeoutMs) const noexcept {

    if (process_ == nullptr) {
        return true;
    }

    return WaitForSingleObject(
        process_,
        timeoutMs) ==
        WAIT_OBJECT_0;
}

void LlamaServerProcessHost::Stop() noexcept {
    if (process_ != nullptr) {
        CloseHandle(
            process_);
        process_ = nullptr;
    }

    if (job_ != nullptr) {
        CloseHandle(
            job_);
        job_ = nullptr;
    }
}

} // namespace iphox::runtime
