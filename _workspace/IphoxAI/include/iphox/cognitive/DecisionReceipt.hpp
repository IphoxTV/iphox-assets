#pragma once

#include "DecisionTypes.hpp"

#include <cstdint>
#include <string>

namespace iphox::cognitive {

enum class DecisionRoute : std::uint8_t {
    Deterministic,
    Memory,
    Tool,
    Generative,
    Abstain
};

struct DecisionReceipt {
    std::uint64_t requestId{};

    std::string schemaId;
    std::uint32_t schemaVersion{};

    std::string backendId;
    std::string backendModel;

    // Cryptographic hashes will replace these opaque fingerprints when
    // the recovered foundation crypto layer is reconnected.
    std::string stateFingerprint;
    std::string bodyPolicyFingerprint;

    DecisionResponse response;
    DecisionRoute route{DecisionRoute::Abstain};

    bool accepted{};
    bool stale{};
};

} // namespace iphox::cognitive
