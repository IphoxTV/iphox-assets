#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/DecisionValidator.hpp"
#include "iphox/cognitive/StateProjection.hpp"
#include "iphox/cognitive/Supervisor.hpp"

#include <cassert>
#include <iostream>
#include <stop_token>
#include <stdexcept>

using namespace iphox::cognitive;

namespace {

DecisionRequest MakeRequest() {
    DecisionRequest request;
    request.schemaId = "iphox.turn-routing";
    request.schemaVersion = 1;

    ChoiceQuestion intent;
    intent.instructions = "Classify the immediate turn.";
    intent.options = {
        {"conversation", "ordinary conversation"},
        {"project_work", "continue or modify active project"},
        {"control", "control a system or application"}
    };

    NoulQuestion needsMemory;
    needsMemory.instructions = "Does the turn require prior project context?";

    ScoreQuestion complexity;
    complexity.instructions = "How much deliberate reasoning is required?";
    complexity.levels = {
        {"trivial"},
        {"normal"},
        {"complex"},
        {"deep"}
    };

    request.questions.emplace("intent", std::move(intent));
    request.questions.emplace("needs_memory", std::move(needsMemory));
    request.questions.emplace("complexity", std::move(complexity));

    return request;
}

void TestDeterministicHappyPath() {
    auto request = MakeRequest();
    request.state["decision.intent"] = "project_work";
    request.state["decision.needs_memory"] = "true";
    request.state["decision.complexity"] = "2";

    BaselineDecisionBackend backend;
    Supervisor supervisor{backend};

    std::stop_source source;

    auto result = supervisor.Evaluate(
        10,
        request,
        "state-A",
        "body-A",
        source.get_token());

    assert(result.receipt.accepted);
    assert(!result.requiresGeneration);
    assert(result.receipt.route == DecisionRoute::Deterministic);
    assert(Supervisor::ReceiptMatchesState(result.receipt, "state-A"));
    assert(!Supervisor::ReceiptMatchesState(result.receipt, "state-B"));
}

void TestUnknownDecisionAbstains() {
    auto request = MakeRequest();
    request.state["decision.intent"] = "project_work";

    BaselineDecisionBackend backend;
    Supervisor supervisor{backend};

    std::stop_source source;

    auto result = supervisor.Evaluate(
        11,
        request,
        "state-A",
        "body-A",
        source.get_token());

    assert(!result.receipt.accepted);
    assert(result.requiresGeneration);
    assert(result.receipt.route == DecisionRoute::Abstain);
}

void TestStateProjectionAllowList() {
    StateProjection projection{
        {"active_project", "runtime_ready"}
    };

    projection.Add("active_project", "IphoxAI");

    bool rejected = false;
    try {
        projection.Add("api_key", "secret");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }

    assert(rejected);
}

void TestInvalidProbabilityRejected() {
    auto request = MakeRequest();

    DecisionResponse response;
    response.backendId = "test";
    response.backendModel = "test";
    response.answers["needs_memory"] = NoulAnswer{1.4};
    response.answers["intent"] =
        AbstainAnswer{"test"};
    response.answers["complexity"] =
        AbstainAnswer{"test"};

    const auto validation =
        DecisionValidator::ValidateResponse(
            request,
            response);

    assert(!validation.ok());
}

void TestCancellationFailsClosed() {
    auto request = MakeRequest();
    request.state["decision.intent"] = "project_work";
    request.state["decision.needs_memory"] = "true";
    request.state["decision.complexity"] = "2";

    BaselineDecisionBackend backend;
    Supervisor supervisor{backend};

    std::stop_source source;
    source.request_stop();

    auto result = supervisor.Evaluate(
        12,
        request,
        "state-A",
        "body-A",
        source.get_token());

    assert(!result.receipt.accepted);
    assert(result.receipt.route == DecisionRoute::Abstain);
}

} // namespace

int main() {
    TestDeterministicHappyPath();
    TestUnknownDecisionAbstains();
    TestStateProjectionAllowList();
    TestInvalidProbabilityRejected();
    TestCancellationFailsClosed();

    std::cout << "IphoxAI cognitive baseline tests: PASS\n";
    return 0;
}
