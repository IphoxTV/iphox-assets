#include "iphox/cognitive/Supervisor.hpp"

#include <stdexcept>
#include <utility>

namespace iphox::cognitive {

SupervisorResult Supervisor::Evaluate(
    std::uint64_t requestId,
    const DecisionRequest& request,
    std::string stateFingerprint,
    std::string bodyPolicyFingerprint,
    std::stop_token stopToken) {

    const auto requestValidation =
        DecisionValidator::ValidateRequest(request);

    if (!requestValidation.ok()) {
        throw std::invalid_argument("invalid decision request");
    }

    auto response = backend_.Evaluate(request, stopToken);

    const auto responseValidation =
        DecisionValidator::ValidateResponse(request, response);

    DecisionReceipt receipt;
    receipt.requestId = requestId;
    receipt.schemaId = request.schemaId;
    receipt.schemaVersion = request.schemaVersion;
    receipt.backendId = response.backendId;
    receipt.backendModel = response.backendModel;
    receipt.stateFingerprint = std::move(stateFingerprint);
    receipt.bodyPolicyFingerprint = std::move(bodyPolicyFingerprint);
    receipt.response = std::move(response);

    if (!responseValidation.ok() || stopToken.stop_requested()) {
        receipt.route = DecisionRoute::Abstain;
        receipt.accepted = false;
        return {
            .receipt = std::move(receipt),
            .requiresGeneration = true,
            .requiresMemory = false
        };
    }

    for (const auto& [_, answer] : receipt.response.answers) {
        if (std::holds_alternative<AbstainAnswer>(answer)) {
            receipt.route = DecisionRoute::Abstain;
            receipt.accepted = false;
            return {
                .receipt = std::move(receipt),
                .requiresGeneration = true,
                .requiresMemory = false
            };
        }
    }

    receipt.route = DecisionRoute::Deterministic;
    receipt.accepted = true;

    return {
        .receipt = std::move(receipt),
        .requiresGeneration = false,
        .requiresMemory = false
    };
}

bool Supervisor::ReceiptMatchesState(
    const DecisionReceipt& receipt,
    const std::string& currentStateFingerprint) noexcept {

    return !receipt.stale &&
        receipt.stateFingerprint == currentStateFingerprint;
}

} // namespace iphox::cognitive
