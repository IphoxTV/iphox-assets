#include "iphox/cognitive/DecisionValidator.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace iphox::cognitive {
namespace {

bool IsProbability(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

void AddIssue(
    ValidationResult& result,
    const std::string& questionId,
    std::string message) {

    result.issues.push_back({
        questionId,
        std::move(message)
    });
}

} // namespace

ValidationResult DecisionValidator::ValidateRequest(
    const DecisionRequest& request) {

    ValidationResult result;

    if (request.schemaId.empty()) {
        AddIssue(result, {}, "schemaId is empty");
    }

    if (request.questions.empty()) {
        AddIssue(result, {}, "request contains no questions");
    }

    for (const auto& [id, question] : request.questions) {
        if (id.empty()) {
            AddIssue(result, id, "question id is empty");
        }

        if (const auto* noul =
                std::get_if<NoulQuestion>(&question)) {

            if (noul->instructions.empty()) {
                AddIssue(result, id, "Noul instructions are empty");
            }

        } else if (const auto* choice =
                       std::get_if<ChoiceQuestion>(&question)) {

            if (choice->instructions.empty()) {
                AddIssue(result, id, "Choice instructions are empty");
            }

            if (choice->options.size() < 2) {
                AddIssue(result, id, "Choice requires at least two options");
            }

            std::set<std::string> seen;
            for (const auto& option : choice->options) {
                if (option.id.empty()) {
                    AddIssue(result, id, "Choice option id is empty");
                } else if (!seen.insert(option.id).second) {
                    AddIssue(result, id, "duplicate Choice option id");
                }
            }

        } else if (const auto* score =
                       std::get_if<ScoreQuestion>(&question)) {

            if (score->instructions.empty()) {
                AddIssue(result, id, "Score instructions are empty");
            }

            if (score->levels.size() < 2) {
                AddIssue(result, id, "Score requires at least two levels");
            }
        }
    }

    return result;
}

ValidationResult DecisionValidator::ValidateResponse(
    const DecisionRequest& request,
    const DecisionResponse& response) {

    ValidationResult result;

    for (const auto& [questionId, question] : request.questions) {
        const auto it = response.answers.find(questionId);

        if (it == response.answers.end()) {
            AddIssue(result, questionId, "missing answer");
            continue;
        }

        const auto& answer = it->second;

        if (std::holds_alternative<AbstainAnswer>(answer)) {
            continue;
        }

        if (std::holds_alternative<NoulQuestion>(question)) {
            const auto* typed = std::get_if<NoulAnswer>(&answer);

            if (typed == nullptr) {
                AddIssue(result, questionId, "answer type mismatch");
                continue;
            }

            if (!IsProbability(typed->yesProbability)) {
                AddIssue(result, questionId, "invalid Noul probability");
            }
            continue;
        }

        if (const auto* choice =
                std::get_if<ChoiceQuestion>(&question)) {

            const auto* typed = std::get_if<ChoiceAnswer>(&answer);

            if (typed == nullptr) {
                AddIssue(result, questionId, "answer type mismatch");
                continue;
            }

            const auto selectedExists = std::any_of(
                choice->options.begin(),
                choice->options.end(),
                [&](const ChoiceOption& option) {
                    return option.id == typed->selected;
                });

            if (!selectedExists) {
                AddIssue(
                    result,
                    questionId,
                    "selected choice not in schema");
            }

            double sum{};
            for (const auto& probability : typed->probabilities) {
                if (!IsProbability(probability.value)) {
                    AddIssue(
                        result,
                        questionId,
                        "invalid Choice probability");
                }
                sum += probability.value;
            }

            if (!typed->probabilities.empty() &&
                std::abs(sum - 1.0) > 1e-6) {
                AddIssue(
                    result,
                    questionId,
                    "Choice probabilities do not sum to 1");
            }

            if (typed->reportedConfidence.has_value() &&
                !IsProbability(*typed->reportedConfidence)) {
                AddIssue(
                    result,
                    questionId,
                    "invalid reported confidence");
            }

            continue;
        }

        if (const auto* score =
                std::get_if<ScoreQuestion>(&question)) {

            const auto* typed = std::get_if<ScoreAnswer>(&answer);

            if (typed == nullptr) {
                AddIssue(result, questionId, "answer type mismatch");
                continue;
            }

            if (!std::isfinite(typed->expectedScore) ||
                typed->expectedScore < 0.0 ||
                typed->expectedScore >
                    static_cast<double>(score->levels.size() - 1)) {
                AddIssue(result, questionId, "Score outside schema");
            }

            if (!typed->probabilities.empty() &&
                typed->probabilities.size() != score->levels.size()) {
                AddIssue(
                    result,
                    questionId,
                    "Score probability count mismatch");
            }

            double sum{};
            for (const auto probability : typed->probabilities) {
                if (!IsProbability(probability)) {
                    AddIssue(
                        result,
                        questionId,
                        "invalid Score probability");
                }
                sum += probability;
            }

            if (!typed->probabilities.empty() &&
                std::abs(sum - 1.0) > 1e-6) {
                AddIssue(
                    result,
                    questionId,
                    "Score probabilities do not sum to 1");
            }

            if (typed->reportedConfidence.has_value() &&
                !IsProbability(*typed->reportedConfidence)) {
                AddIssue(
                    result,
                    questionId,
                    "invalid reported confidence");
            }
        }
    }

    return result;
}

} // namespace iphox::cognitive
