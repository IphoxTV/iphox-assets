#include "iphox/capability/CapabilityAuthority.hpp"

namespace iphox::capability {

CapabilityResult CapabilityAuthority::Authorize(
    const cognitive::DecisionReceipt& receipt,
    const std::string& currentStateFingerprint,
    const CapabilityRequest& request) noexcept {

    if (!receipt.accepted) {
        return {false, "DECISION_NOT_ACCEPTED"};
    }

    if (receipt.stale ||
        receipt.stateFingerprint != currentStateFingerprint) {
        return {false, "STALE_DECISION"};
    }

    if (request.capability.empty()) {
        return {false, "INVALID_CAPABILITY"};
    }

    // R0 deliberately does not grant real capabilities yet.
    // Policy wiring belongs to the recovered Body authority.
    return {false, "POLICY_UNAVAILABLE"};
}

} // namespace iphox::capability
