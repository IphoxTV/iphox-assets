#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/CoreLifecycle.hpp"
#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/ipc/SecurePipeServer.hpp"
#include "iphox/rpc/RpcAuthority.hpp"

#include <chrono>
#include <iostream>

namespace {

iphox::ipc::Frame MakeResponse(
    const iphox::ipc::Frame& request,
    iphox::ipc::MessageType type,
    std::uint32_t extraFlags = 0) {

    iphox::ipc::Frame response;
    response.header.type = type;
    response.header.requestId = request.header.requestId;
    response.header.flags =
        iphox::ipc::kFlagResponse | extraFlags;
    return response;
}

} // namespace

int wmain() {
    using namespace std::chrono_literals;

    iphox::foundation::CoreLifecycle lifecycle;
    iphox::foundation::RequestRegistry requests{1024};

    iphox::cognitive::BaselineDecisionBackend decisions;
    iphox::cognitive::Supervisor supervisor{decisions};
    iphox::rpc::DomainDispatcher dispatcher;

    (void)supervisor;
    (void)dispatcher;

    if (!lifecycle.BeginStart()) {
        return 10;
    }

    iphox::ipc::SecurePipeServer pipe;
    if (!pipe.Open()) {
        lifecycle.MarkFaulted();
        std::wcerr << L"IphoxCore: unable to create secure pipe\n";
        return 11;
    }

    if (!lifecycle.MarkReady()) {
        lifecycle.MarkFaulted();
        return 12;
    }

    std::wcout
        << L"IphoxCore Native R0 READY\n"
        << L"Pipe: " << iphox::ipc::kCorePipeName << L"\n";

    bool stopRequested = false;

    while (!stopRequested) {
        if (!pipe.WaitForClient()) {
            lifecycle.MarkFaulted();
            return 13;
        }

        for (;;) {
            const auto frame = pipe.ReadFrame();
            if (!frame.has_value()) {
                break;
            }

            const auto registerResult = requests.Register(
                frame->header.requestId,
                std::to_string(
                    static_cast<std::uint16_t>(
                        frame->header.type)));

            if (registerResult ==
                iphox::foundation::RegisterResult::Conflict) {

                auto error = MakeResponse(
                    *frame,
                    iphox::ipc::MessageType::Error,
                    iphox::ipc::kFlagError);

                if (!pipe.WriteFrame(error)) {
                    break;
                }
                continue;
            }

            if (registerResult ==
                iphox::foundation::RegisterResult::CapacityExhausted) {

                auto error = MakeResponse(
                    *frame,
                    iphox::ipc::MessageType::Error,
                    iphox::ipc::kFlagError);

                if (!pipe.WriteFrame(error)) {
                    break;
                }
                continue;
            }

            auto lease = lifecycle.TryAcquireJob();
            if (!lease.has_value()) {
                break;
            }

            iphox::ipc::Frame response;

            switch (frame->header.type) {
            case iphox::ipc::MessageType::Ping:
                response = MakeResponse(
                    *frame,
                    iphox::ipc::MessageType::Pong);
                break;

            case iphox::ipc::MessageType::CoreShutdown:
                response = MakeResponse(
                    *frame,
                    iphox::ipc::MessageType::CoreShutdown);
                stopRequested = true;
                lifecycle.RequestStop();
                break;

            default:
                response = MakeResponse(
                    *frame,
                    iphox::ipc::MessageType::Error,
                    iphox::ipc::kFlagError);
                break;
            }

            requests.MarkCompleted(
                frame->header.requestId);

            if (!pipe.WriteFrame(response)) {
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
