#pragma once

#include "DecisionTypes.hpp"

#include <string>
#include <vector>

namespace iphox::cognitive {

struct ValidationIssue {
    std::string questionId;
    std::string message;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }
};

class DecisionValidator final {
public:
    [[nodiscard]] static ValidationResult ValidateRequest(
        const DecisionRequest& request);

    [[nodiscard]] static ValidationResult ValidateResponse(
        const DecisionRequest& request,
        const DecisionResponse& response);
};

} // namespace iphox::cognitive
