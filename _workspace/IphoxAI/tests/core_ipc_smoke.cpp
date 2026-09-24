#include "iphox/ipc/Payload.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/runtime/CoreProcessHost.hpp"
#include "iphox/runtime/CoreRpcClient.hpp"

#include <cassert>
#include <iostream>

int wmain() {
    iphox::runtime::CoreProcessHost core;

    assert(core.StartSiblingCore());
    assert(core.IsRunning());

    iphox::ipc::Frame hello;
    hello.header.type =
        iphox::ipc::MessageType::Hello;
    hello.header.requestId = 1000;

    const auto helloAck =
        iphox::runtime::CoreRpcClient::Request(
            hello,
            5000);

    assert(helloAck.has_value());

    assert(
        helloAck->header.type ==
        iphox::ipc::MessageType::Hello);

    assert(
        iphox::ipc::FromPayload(
            helloAck->payload) ==
        "IphoxCore Native R0");

    iphox::ipc::Frame ping;
    ping.header.type =
        iphox::ipc::MessageType::Ping;
    ping.header.requestId = 1001;

    const auto pong =
        iphox::runtime::CoreRpcClient::Request(
            ping,
            5000);

    assert(pong.has_value());

    assert(
        pong->header.type ==
        iphox::ipc::MessageType::Pong);

    assert(
        pong->header.requestId ==
        1001);

    assert(
        (pong->header.flags &
            iphox::ipc::kFlagResponse) != 0);

    iphox::ipc::Frame shutdown;
    shutdown.header.type =
        iphox::ipc::MessageType::CoreShutdown;
    shutdown.header.requestId = 1002;

    const auto ack =
        iphox::runtime::CoreRpcClient::Request(
            shutdown,
            5000);

    assert(ack.has_value());

    assert(
        ack->header.type ==
        iphox::ipc::MessageType::CoreShutdown);

    assert(
        ack->header.requestId ==
        1002);

    assert(
        (ack->header.flags &
            iphox::ipc::kFlagResponse) != 0);

    assert(core.WaitForExit(3000));
    assert(!core.IsRunning());

    core.Close();

    std::cout
        << "IphoxCore transient RPC smoke: PASS\n";

    return 0;
}
