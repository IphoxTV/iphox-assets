#include "iphox/runtime/CoreRpcClient.hpp"

#include "iphox/ipc/SecurePipeClient.hpp"
#include "iphox/ipc/SecurePipeServer.hpp"

namespace iphox::runtime {

std::optional<ipc::Frame> CoreRpcClient::Request(
    const ipc::Frame& request,
    unsigned long connectTimeoutMs) {

    ipc::SecurePipeClient client;

    if (!client.Connect(
            ipc::kCorePipeName,
            connectTimeoutMs)) {
        return std::nullopt;
    }

    if (!client.WriteFrame(request)) {
        return std::nullopt;
    }

    const auto response =
        client.ReadFrame();

    client.Close();

    if (!response.has_value() ||
        response->header.requestId !=
            request.header.requestId ||
        (response->header.flags &
            ipc::kFlagResponse) == 0) {
        return std::nullopt;
    }

    return response;
}

} // namespace iphox::runtime
