#pragma once

#include "IDecisionBackend.hpp"

namespace iphox::cognitive {

// Safe bootstrap backend.
// It only answers questions explicitly represented by deterministic
// state keys. Everything semantic or ambiguous becomes Abstain.
class BaselineDecisionBackend final : public IDecisionBackend {
public:
    [[nodiscard]] BackendInfo Info() const override;

    [[nodiscard]] DecisionResponse Evaluate(
        const DecisionRequest& request,
        std::stop_token stopToken) override;
};

} // namespace iphox::cognitive
