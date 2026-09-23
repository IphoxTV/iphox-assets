#pragma once

#include "IGenerativeEngine.hpp"

namespace iphox::generation {

class UnavailableGenerativeEngine final
    : public IGenerativeEngine {

public:
    [[nodiscard]] GenerationResult Generate(
        const GenerationRequest& request,
        std::stop_token stopToken) override;
};

} // namespace iphox::generation
