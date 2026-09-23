#include "iphox/generation/UnavailableGenerativeEngine.hpp"

namespace iphox::generation {

GenerationResult UnavailableGenerativeEngine::Generate(
    const GenerationRequest&,
    std::stop_token stopToken) {

    if (stopToken.stop_requested()) {
        return {
            .status = GenerationStatus::Cancelled,
            .text = {},
            .errorCode = "CANCELLED"
        };
    }

    return {
        .status = GenerationStatus::Unavailable,
        .text = {},
        .errorCode = "ENGINE_UNAVAILABLE"
    };
}

} // namespace iphox::generation
