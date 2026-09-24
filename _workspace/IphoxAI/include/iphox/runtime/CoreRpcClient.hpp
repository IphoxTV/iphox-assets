#pragma once

#include "iphox/ipc/Protocol.hpp"

#include <optional>

namespace iphox::runtime {

class CoreRpcClient final {
public:
    [[nodiscard]] static std::optional<ipc::Frame> Request(
        const ipc::Frame& request,
        unsigned long connectTimeoutMs = 5000);
};

} // namespace iphox::runtime
