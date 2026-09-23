#pragma once

#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/ipc/Protocol.hpp"

#include <cstdint>
#include <string>

namespace iphox::core {

struct HandleResult {
    ipc::Frame response;
    bool requestShutdown{};
};

class CoreService final {
public:
    explicit CoreService(
        std::size_t requestCapacity = 1024);

    [[nodiscard]] HandleResult Handle(
        const ipc::Frame& request);

private:
    [[nodiscard]] static std::string Fingerprint(
        const ipc::Frame& request);

    [[nodiscard]] static ipc::Frame MakeResponse(
        const ipc::Frame& request,
        ipc::MessageType type);

    [[nodiscard]] static ipc::Frame MakeError(
        const ipc::Frame& request,
        std::string code);

    foundation::RequestRegistry requests_;
};

} // namespace iphox::core
