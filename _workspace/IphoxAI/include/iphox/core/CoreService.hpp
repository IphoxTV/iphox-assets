#pragma once

#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/generation/IGenerativeEngine.hpp"
#include "iphox/ipc/Protocol.hpp"

#include <cstdint>
#include <stop_token>
#include <string>

namespace iphox::core {

struct HandleResult {
    ipc::Frame response;
    bool requestShutdown{};
};

class CoreService final {
public:
    CoreService(
        generation::IGenerativeEngine& engine,
        std::size_t requestCapacity = 1024);

    [[nodiscard]] HandleResult Handle(
        const ipc::Frame& request,
        std::stop_token stopToken = {});

private:
    [[nodiscard]] static std::string Fingerprint(
        const ipc::Frame& request);

    [[nodiscard]] static ipc::Frame MakeResponse(
        const ipc::Frame& request,
        ipc::MessageType type);

    [[nodiscard]] static ipc::Frame MakeError(
        const ipc::Frame& request,
        std::string code);

    generation::IGenerativeEngine& engine_;
    foundation::RequestRegistry requests_;
};

} // namespace iphox::core
