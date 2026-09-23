#include "iphox/cognitive/BaselineDecisionBackend.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace iphox::cognitive {
namespace {

std::optional<bool> ParseBool(const std::string& value) {
    if (value == "1" || value == "true" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        return false;
    }
    return std::nullopt;
}

std::optional<double> ParseDouble(const std::string& value) {
    double result{};
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto parsed = std::from_chars(first, last, result);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        return std::nullopt;
    }
    return result;
}

} // namespace

BackendInfo BaselineDecisionBackend::Info() const {
    return {
        .id = "baseline-deterministic",
        .model = "none",
        .remote = false
    };
}

DecisionResponse BaselineDecisionBackend::Evaluate(
    const DecisionRequest& request,
    std::stop_token stopToken) {

    DecisionResponse response;
    const auto info = Info();
    response.backendId = info.id;
    response.backendModel = info.model;

    for (const auto& [questionId, question] : request.questions) {
        if (stopToken.stop_requested()) {
            response.answers.emplace(
                questionId,
                AbstainAnswer{"cancelled"});
            continue;
        }

        const auto stateIt = request.state.find("decision." + questionId);
        if (stateIt == request.state.end()) {
            response.answers.emplace(
                questionId,
                AbstainAnswer{"no deterministic state"});
            continue;
        }

        const auto& raw = stateIt->second;

        if (std::holds_alternative<NoulQuestion>(question)) {
            const auto value = ParseBool(raw);
            if (!value.has_value()) {
                response.answers.emplace(
                    questionId,
                    AbstainAnswer{"invalid deterministic bool"});
                continue;
            }

            response.answers.emplace(
                questionId,
                NoulAnswer{*value ? 1.0 : 0.0});
            continue;
        }

        if (const auto* choice = std::get_if<ChoiceQuestion>(&question)) {
            const auto match = std::find_if(
                choice->options.begin(),
                choice->options.end(),
                [&](const ChoiceOption& option) {
                    return option.id == raw;
                });

            if (match == choice->options.end()) {
                response.answers.emplace(
                    questionId,
                    AbstainAnswer{"unknown deterministic choice"});
                continue;
            }

            ChoiceAnswer answer;
            answer.selected = raw;
            answer.reportedConfidence = 1.0;
            answer.probabilities.reserve(choice->options.size());

            for (const auto& option : choice->options) {
                answer.probabilities.push_back({
                    option.id,
                    option.id == raw ? 1.0 : 0.0
                });
            }

            response.answers.emplace(questionId, std::move(answer));
            continue;
        }

        if (const auto* score = std::get_if<ScoreQuestion>(&question)) {
            const auto value = ParseDouble(raw);
            if (!value.has_value() || score->levels.empty()) {
                response.answers.emplace(
                    questionId,
                    AbstainAnswer{"invalid deterministic score"});
                continue;
            }

            const auto count = score->levels.size();
            if (*value < 0.0 || *value > static_cast<double>(count - 1)) {
                response.answers.emplace(
                    questionId,
                    AbstainAnswer{"score outside schema"});
                continue;
            }

            ScoreAnswer answer;
            answer.expectedScore = *value;
            answer.reportedConfidence = 1.0;
            answer.probabilities.assign(count, 0.0);

            const auto index = static_cast<std::size_t>(*value);
            if (static_cast<double>(index) == *value && index < count) {
                answer.probabilities[index] = 1.0;
            }

            response.answers.emplace(questionId, std::move(answer));
        }
    }

    return response;
}

} // namespace iphox::cognitive
