#pragma once

#include "DecisionTypes.hpp"

#include <stop_token>
#include <string>

namespace iphox::cognitive {

struct BackendInfo {
    std::string id;
    std::string model;
    bool remote{};
};

class IDecisionBackend {
public:
    virtual ~IDecisionBackend() = default;

    [[nodiscard]] virtual BackendInfo Info() const = 0;

    [[nodiscard]] virtual DecisionResponse Evaluate(
        const DecisionRequest& request,
        std::stop_token stopToken) = 0;
};

} // namespace iphox::cognitive
