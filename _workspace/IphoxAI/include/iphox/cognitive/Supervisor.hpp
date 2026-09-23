#pragma once

#include "DecisionReceipt.hpp"
#include "DecisionValidator.hpp"
#include "IDecisionBackend.hpp"

#include <cstdint>
#include <string>

namespace iphox::cognitive {

struct SupervisorResult {
    DecisionReceipt receipt;
    bool requiresGeneration{};
    bool requiresMemory{};
};

class Supervisor final {
public:
    explicit Supervisor(IDecisionBackend& backend)
        : backend_(backend) {}

    [[nodiscard]] SupervisorResult Evaluate(
        std::uint64_t requestId,
        const DecisionRequest& request,
        std::string stateFingerprint,
        std::string bodyPolicyFingerprint,
        std::stop_token stopToken);

    [[nodiscard]] static bool ReceiptMatchesState(
        const DecisionReceipt& receipt,
        const std::string& currentStateFingerprint) noexcept;

private:
    IDecisionBackend& backend_;
};

} // namespace iphox::cognitive
