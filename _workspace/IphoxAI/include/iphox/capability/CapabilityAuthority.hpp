#pragma once

#include "iphox/cognitive/DecisionReceipt.hpp"

#include <string>

namespace iphox::capability {

struct CapabilityRequest {
    std::string capability;
    std::string resource;
};

struct CapabilityResult {
    bool allowed{};
    std::string code;
};

class CapabilityAuthority final {
public:
    [[nodiscard]] static CapabilityResult Authorize(
        const cognitive::DecisionReceipt& receipt,
        const std::string& currentStateFingerprint,
        const CapabilityRequest& request) noexcept;
};

} // namespace iphox::capability
