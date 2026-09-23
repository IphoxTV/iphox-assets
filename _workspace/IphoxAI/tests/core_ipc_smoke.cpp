#include "iphox/ipc/Protocol.hpp"
#include "iphox/ipc/SecurePipeClient.hpp"
#include "iphox/ipc/SecurePipeServer.hpp"
#include "iphox/runtime/CoreProcessHost.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int wmain() {
    iphox::runtime::CoreProcessHost core;
    assert(core.StartSiblingCore());
    assert(core.IsRunning());

    iphox::ipc::SecurePipeClient client;
    assert(client.Connect(
        iphox::ipc::kCorePipeName,
        5000));

    iphox::ipc::Frame ping;
    ping.header.type = iphox::ipc::MessageType::Ping;
    ping.header.requestId = 1001;

    assert(client.WriteFrame(ping));

    const auto pong = client.ReadFrame();
    assert(pong.has_value());
    assert(
        pong->header.type ==
        iphox::ipc::MessageType::Pong);
    assert(pong->header.requestId == 1001);
    assert(
        (pong->header.flags &
            iphox::ipc::kFlagResponse) != 0);

    iphox::ipc::Frame shutdown;
    shutdown.header.type =
        iphox::ipc::MessageType::CoreShutdown;
    shutdown.header.requestId = 1002;

    assert(client.WriteFrame(shutdown));

    const auto ack = client.ReadFrame();
    assert(ack.has_value());
    assert(
        ack->header.type ==
        iphox::ipc::MessageType::CoreShutdown);
    assert(ack->header.requestId == 1002);
    assert(
        (ack->header.flags &
            iphox::ipc::kFlagResponse) != 0);

    client.Close();

    assert(core.WaitForExit(3000));
    assert(!core.IsRunning());

    core.Close();

    std::cout
        << "IphoxCore IPC smoke: PASS\n";

    return 0;
}
