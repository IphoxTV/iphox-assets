#include "iphox/core/CoreService.hpp"
#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/CoreLifecycle.hpp"
#include "iphox/generation/LlamaCppHttpEngine.hpp"
#include "iphox/ipc/SecurePipeServer.hpp"

#include <chrono>
#include <iostream>

int wmain() {
    using namespace std::chrono_literals;

    iphox::foundation::CoreLifecycle lifecycle;

    if (!lifecycle.BeginStart()) {
        return 10;
    }

    iphox::ipc::SecurePipeServer pipe;
    if (!pipe.Open()) {
        lifecycle.MarkFaulted();
        std::wcerr
            << L"IphoxCore: unable to create secure pipe\n";
        return 11;
    }

    iphox::generation::LlamaCppHttpEngine engine;
    iphox::cognitive::BaselineDecisionBackend decisions;
    iphox::cognitive::Supervisor supervisor{decisions};
    iphox::core::CoreService service{
        engine,
        supervisor,
        1024
    };

    if (!lifecycle.MarkReady()) {
        lifecycle.MarkFaulted();
        return 12;
    }

    std::wcout
        << L"IphoxCore Native R0 READY\n"
        << L"Pipe: "
        << iphox::ipc::kCorePipeName
        << L"\n";

    bool stopRequested = false;

    while (!stopRequested) {
        if (!pipe.WaitForClient()) {
            lifecycle.MarkFaulted();
            return 13;
        }

        for (;;) {
            const auto frame =
                pipe.ReadFrame();

            if (!frame.has_value()) {
                break;
            }

            auto lease =
                lifecycle.TryAcquireJob();

            if (!lease.has_value()) {
                break;
            }

            auto result =
                service.Handle(*frame);

            if (result.requestShutdown) {
                stopRequested = true;
                (void)lifecycle.RequestStop();
            }

            if (!pipe.WriteFrame(
                    result.response)) {
                break;
            }

            if (stopRequested) {
                break;
            }
        }

        pipe.Disconnect();
    }

    if (!lifecycle.WaitForDrain(2s)) {
        lifecycle.MarkFaulted();
        return 14;
    }

    if (!lifecycle.MarkStopped()) {
        lifecycle.MarkFaulted();
        return 15;
    }

    return 0;
}
