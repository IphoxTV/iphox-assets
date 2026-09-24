#include "iphox/core/CoreService.hpp"
#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/CoreLifecycle.hpp"
#include "iphox/generation/LlamaCppHttpEngine.hpp"
#include "iphox/runtime/FileLogger.hpp"
#include "iphox/runtime/LlamaServerProcessHost.hpp"
#include "iphox/runtime/Paths.hpp"
#include "iphox/runtime/RuntimeConfig.hpp"
#include "iphox/ipc/SecurePipeServer.hpp"

#include <chrono>
#include <iostream>
#include <thread>

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

    const auto root =
        iphox::runtime::ExecutableDirectory();

    iphox::runtime::FileLogger logger{
        root / L"logs" / L"IphoxCore.log"
    };

    logger.Write(
        iphox::runtime::LogLevel::Info,
        "IphoxCore starting");

    const auto runtimeConfig =
        iphox::runtime::RuntimeConfigLoader::Load(
            root / L"IphoxAI.config");

    if (!runtimeConfig.valid) {
        for (const auto& issue :
             runtimeConfig.issues) {
            logger.Write(
                iphox::runtime::LogLevel::Error,
                "config: " + issue);
        }

        lifecycle.MarkFaulted();
        return 16;
    }

    logger.Write(
        iphox::runtime::LogLevel::Info,
        runtimeConfig.fileFound
            ? "runtime config loaded"
            : "runtime config missing; defaults active");

    iphox::generation::LlamaCppHttpEngine engine{
        runtimeConfig.config.llama
    };

    iphox::runtime::LlamaServerProcessHost
        ownedLlamaServer;

    std::jthread llamaStartupThread;

    if (runtimeConfig.config.server.autostart) {
        logger.Write(
            iphox::runtime::LogLevel::Info,
            "llama-server autostart enabled");

        llamaStartupThread =
            std::jthread(
                [&engine,
                 &ownedLlamaServer,
                 &logger,
                 root,
                 config = runtimeConfig.config](
                    std::stop_token stopToken) {

                    const auto initial =
                        engine.Probe(
                            stopToken);

                    if (initial.status ==
                        iphox::generation::
                            EngineStatus::Ready) {

                        logger.Write(
                            iphox::runtime::LogLevel::Info,
                            "llama.cpp already ready; autostart skipped");
                        return;
                    }

                    iphox::runtime::
                        LlamaServerLaunchConfig
                            launch;

                    launch.serverPath =
                        iphox::runtime::
                            ResolvePortablePath(
                                root,
                                config.server.serverPath);

                    launch.modelPath =
                        iphox::runtime::
                            ResolvePortablePath(
                                root,
                                config.server.modelPath);

                    launch.port =
                        config.llama.port;

                    launch.contextSize =
                        config.server.contextSize;

                    launch.gpuLayers =
                        config.server.gpuLayers;

                    if (!ownedLlamaServer.Start(
                            launch)) {

                        logger.Write(
                            iphox::runtime::LogLevel::Error,
                            "llama-server autostart failed");
                        return;
                    }

                    logger.Write(
                        iphox::runtime::LogLevel::Info,
                        "owned llama-server started");

                    const auto deadline =
                        std::chrono::
                            steady_clock::now() +
                        std::chrono::milliseconds{
                            config.server.
                                startupTimeoutMs
                        };

                    while (!stopToken.stop_requested() &&
                           std::chrono::
                               steady_clock::now() <
                               deadline &&
                           ownedLlamaServer.
                               IsRunning()) {

                        const auto probe =
                            engine.Probe(
                                stopToken);

                        if (probe.status ==
                            iphox::generation::
                                EngineStatus::Ready) {

                            logger.Write(
                                iphox::runtime::LogLevel::Info,
                                "owned llama-server ready");
                            return;
                        }

                        std::this_thread::sleep_for(
                            std::chrono::
                                milliseconds{250});
                    }
                });
    }

    iphox::cognitive::BaselineDecisionBackend decisions;
    iphox::cognitive::Supervisor supervisor{decisions};
    iphox::core::CoreService service{
        engine,
        supervisor,
        1024,
        runtimeConfig.config.chat.maxTurns,
        runtimeConfig.config.chat.maxBytes,
        runtimeConfig.config.chat.systemPrompt
    };

    if (!lifecycle.MarkReady()) {
        lifecycle.MarkFaulted();
        return 12;
    }

    logger.Write(
        iphox::runtime::LogLevel::Info,
        "IphoxCore ready");

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

    logger.Write(
        iphox::runtime::LogLevel::Info,
        "IphoxCore stopping");

    if (llamaStartupThread.joinable()) {
        llamaStartupThread.request_stop();
        llamaStartupThread.join();
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
