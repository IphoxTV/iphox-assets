#pragma once

#include "IGenerativeEngine.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <string>

namespace iphox::generation {

struct LlamaCppHttpConfig {
    std::wstring host{L"127.0.0.1"};
    INTERNET_PORT port{8080};
    std::uint32_t nPredict{512};

    int resolveTimeoutMs{1000};
    int connectTimeoutMs{1500};
    int sendTimeoutMs{5000};
    int receiveTimeoutMs{120000};
};

class LlamaCppHttpEngine final
    : public IGenerativeEngine {

public:
    explicit LlamaCppHttpEngine(
        LlamaCppHttpConfig config = {});

    [[nodiscard]] EngineProbeResult Probe(
        std::stop_token stopToken) override;

    [[nodiscard]] GenerationResult Generate(
        const GenerationRequest& request,
        std::stop_token stopToken) override;

    [[nodiscard]] static bool IsLoopbackHost(
        const std::wstring& host) noexcept;

private:
    LlamaCppHttpConfig config_;
};

} // namespace iphox::generation
