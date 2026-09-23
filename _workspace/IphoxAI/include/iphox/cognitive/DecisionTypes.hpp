#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace iphox::cognitive {

using QuestionId = std::string;

enum class QuestionKind : std::uint8_t {
    Noul,
    Choice,
    Score
};

struct NoulCriteria {
    std::string trueCriteria;
    std::string falseCriteria;
};

struct NoulQuestion {
    std::string instructions;
    std::optional<NoulCriteria> criteria;
};

struct ChoiceOption {
    std::string id;
    std::string criteria;
};

struct ChoiceQuestion {
    std::string instructions;
    std::vector<ChoiceOption> options;
};

struct ScoreLevel {
    std::string criteria;
};

struct ScoreQuestion {
    std::string instructions;
    std::vector<ScoreLevel> levels;
};

using DecisionQuestion =
    std::variant<NoulQuestion, ChoiceQuestion, ScoreQuestion>;

struct Probability {
    std::string id;
    double value{};
};

struct NoulAnswer {
    // Probability that the answer is YES / TRUE.
    double yesProbability{};
};

struct ChoiceAnswer {
    std::string selected;
    std::vector<Probability> probabilities;
    std::optional<double> reportedConfidence;
};

struct ScoreAnswer {
    double expectedScore{};
    std::vector<double> probabilities;
    std::optional<double> reportedConfidence;
};

struct AbstainAnswer {
    std::string reason;
};

using DecisionAnswer =
    std::variant<NoulAnswer, ChoiceAnswer, ScoreAnswer, AbstainAnswer>;

struct DecisionRequest {
    std::string schemaId;
    std::uint32_t schemaVersion{1};
    std::map<std::string, std::string> state;
    std::map<QuestionId, DecisionQuestion> questions;
};

struct DecisionResponse {
    std::string backendId;
    std::string backendModel;
    std::map<QuestionId, DecisionAnswer> answers;
};

inline double NoulDecisionMargin(const NoulAnswer& answer) noexcept {
    const double distance = answer.yesProbability >= 0.5
        ? answer.yesProbability - 0.5
        : 0.5 - answer.yesProbability;
    return distance * 2.0;
}

} // namespace iphox::cognitive
