#include "iphox/ipc/Payload.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/runtime/CoreProcessHost.hpp"
#include "iphox/runtime/CoreRpcClient.hpp"

#include <cassert>
#include <iostream>

namespace {

void VerifyHelloAndPing(
    std::uint64_t baseRequestId) {

    iphox::ipc::Frame hello;
    hello.header.type =
        iphox::ipc::MessageType::Hello;
    hello.header.requestId =
        baseRequestId;

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
    ping.header.requestId =
        baseRequestId + 1;

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
        baseRequestId + 1);

    assert(
        (pong->header.flags &
            iphox::ipc::kFlagResponse) != 0);
}

void ShutdownCore(
    std::uint64_t requestId) {

    iphox::ipc::Frame shutdown;
    shutdown.header.type =
        iphox::ipc::MessageType::CoreShutdown;
    shutdown.header.requestId =
        requestId;

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
        requestId);

    assert(
        (ack->header.flags &
            iphox::ipc::kFlagResponse) != 0);
}

} // namespace

int wmain() {
    iphox::runtime::CoreProcessHost core;

    // First lifecycle.
    assert(core.StartSiblingCore());
    assert(core.IsRunning());

    VerifyHelloAndPing(1000);
    ShutdownCore(1002);

    assert(core.WaitForExit(3000));
    assert(!core.IsRunning());

    core.Close();

    // Recovery lifecycle: the exact same process host must be
    // reusable after a Core exit, matching the UI auto-restart path.
    assert(core.StartSiblingCore());
    assert(core.IsRunning());

    VerifyHelloAndPing(2000);
    ShutdownCore(2002);

    assert(core.WaitForExit(3000));
    assert(!core.IsRunning());

    core.Close();

    std::cout
        << "IphoxCore transient RPC + restart smoke: PASS\n";

    return 0;
}
