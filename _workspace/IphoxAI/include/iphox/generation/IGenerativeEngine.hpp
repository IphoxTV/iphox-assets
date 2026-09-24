#pragma once

#include <stop_token>
#include <string>

namespace iphox::generation {

enum class GenerationStatus {
    Completed,
    Cancelled,
    Unavailable,
    Failed
};

enum class EngineStatus {
    Ready,
    Loading,
    Unavailable,
    Failed
};

struct EngineProbeResult {
    EngineStatus status{EngineStatus::Failed};
    std::string detail;
};

struct GenerationRequest {
    std::string prompt;
};

struct GenerationResult {
    GenerationStatus status{GenerationStatus::Failed};
    std::string text;
    std::string errorCode;
};

class IGenerativeEngine {
public:
    virtual ~IGenerativeEngine() = default;

    [[nodiscard]] virtual EngineProbeResult Probe(
        std::stop_token stopToken) = 0;

    [[nodiscard]] virtual GenerationResult Generate(
        const GenerationRequest& request,
        std::stop_token stopToken) = 0;
};

} // namespace iphox::generation
