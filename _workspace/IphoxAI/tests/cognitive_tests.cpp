#include "iphox/capability/CapabilityAuthority.hpp"
#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/DecisionValidator.hpp"
#include "iphox/cognitive/StateProjection.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/rpc/RpcAuthority.hpp"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <vector>

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

DecisionReceipt MakeAcceptedReceipt() {
    DecisionReceipt receipt;
    receipt.requestId = 77;
    receipt.schemaId = "test";
    receipt.schemaVersion = 1;
    receipt.backendId = "test";
    receipt.backendModel = "test";
    receipt.stateFingerprint = "state-A";
    receipt.bodyPolicyFingerprint = "body-A";
    receipt.route = DecisionRoute::Deterministic;
    receipt.accepted = true;
    return receipt;
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
    response.answers["intent"] = AbstainAnswer{"test"};
    response.answers["complexity"] = AbstainAnswer{"test"};

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

void TestRequestIdempotencyAndConflict() {
    iphox::foundation::RequestRegistry registry{8};

    assert(
        registry.Register(42, "ABC") ==
        iphox::foundation::RegisterResult::NewRequest);

    assert(
        registry.Register(42, "ABC") ==
        iphox::foundation::RegisterResult::Duplicate);

    assert(
        registry.Register(42, "XYZ") ==
        iphox::foundation::RegisterResult::Conflict);

    registry.MarkCompleted(42);

    const auto found = registry.Find(42);
    assert(found.has_value());
    assert(found->completed);
}

void TestRequestRegistryHardBound() {
    iphox::foundation::RequestRegistry registry{2};

    assert(
        registry.Register(1, "A") ==
        iphox::foundation::RegisterResult::NewRequest);
    assert(
        registry.Register(2, "B") ==
        iphox::foundation::RegisterResult::NewRequest);

    assert(
        registry.Register(3, "C") ==
        iphox::foundation::RegisterResult::CapacityExhausted);
    assert(registry.Size() == 2);

    registry.MarkCompleted(1);

    assert(
        registry.Register(3, "C") ==
        iphox::foundation::RegisterResult::NewRequest);
    assert(registry.Size() == 2);
    assert(!registry.Find(1).has_value());
}

class EchoController final : public iphox::rpc::IDomainController {
public:
    iphox::rpc::RpcResult Handle(
        const iphox::rpc::RpcRequest& request) override {

        return {true, "OK", request.method};
    }
};

void TestRpcFailClosed() {
    iphox::rpc::DomainDispatcher dispatcher;
    EchoController chat;

    dispatcher.Bind(iphox::rpc::Domain::Chat, chat);

    auto unknown = dispatcher.Dispatch({
        .requestId = 1,
        .method = "made-up:thing",
        .payload = {}
    });
    assert(!unknown.ok);
    assert(unknown.code == "UNKNOWN_RPC");

    auto unavailable = dispatcher.Dispatch({
        .requestId = 2,
        .method = "memory:query",
        .payload = {}
    });
    assert(!unavailable.ok);
    assert(unavailable.code == "DOMAIN_UNAVAILABLE");

    auto handled = dispatcher.Dispatch({
        .requestId = 3,
        .method = "chat:submit",
        .payload = {}
    });
    assert(handled.ok);
    assert(handled.code == "OK");
}

void TestCapabilityAuthorityFailsClosed() {
    auto receipt = MakeAcceptedReceipt();

    const auto unavailable =
        iphox::capability::CapabilityAuthority::Authorize(
            receipt,
            "state-A",
            {"filesystem.read", "project"});

    assert(!unavailable.allowed);
    assert(unavailable.code == "POLICY_UNAVAILABLE");

    const auto stale =
        iphox::capability::CapabilityAuthority::Authorize(
            receipt,
            "state-B",
            {"filesystem.read", "project"});

    assert(!stale.allowed);
    assert(stale.code == "STALE_DECISION");

    receipt.accepted = false;

    const auto rejected =
        iphox::capability::CapabilityAuthority::Authorize(
            receipt,
            "state-A",
            {"filesystem.read", "project"});

    assert(!rejected.allowed);
    assert(rejected.code == "DECISION_NOT_ACCEPTED");
}

void TestProtocolRoundTrip() {
    iphox::ipc::Frame frame;
    frame.header.type = iphox::ipc::MessageType::DecisionEvaluate;
    frame.header.requestId = 99;
    frame.header.flags = 7;
    frame.payload = {
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30}
    };

    const auto encoded = iphox::ipc::Protocol::Encode(frame);
    const auto decoded = iphox::ipc::Protocol::Decode(encoded);

    assert(decoded.ok());
    assert(decoded.frame.header.type ==
        iphox::ipc::MessageType::DecisionEvaluate);
    assert(decoded.frame.header.requestId == 99);
    assert(decoded.frame.header.flags == 7);
    assert(decoded.frame.payload == frame.payload);
}

void TestProtocolRejectsMalformedFrames() {
    iphox::ipc::Frame frame;
    frame.header.type = iphox::ipc::MessageType::Ping;
    frame.header.requestId = 2;

    auto encoded = iphox::ipc::Protocol::Encode(frame);

    auto badMagic = encoded;
    badMagic[0] = std::byte{'X'};
    assert(
        iphox::ipc::Protocol::Decode(badMagic).status ==
        iphox::ipc::FrameStatus::BadMagic);

    auto truncated = encoded;
    truncated.pop_back();
    assert(
        iphox::ipc::Protocol::Decode(truncated).status ==
        iphox::ipc::FrameStatus::TooShort ||
        iphox::ipc::Protocol::Decode(truncated).status ==
        iphox::ipc::FrameStatus::LengthMismatch);
}

} // namespace

int main() {
    TestDeterministicHappyPath();
    TestUnknownDecisionAbstains();
    TestStateProjectionAllowList();
    TestInvalidProbabilityRejected();
    TestCancellationFailsClosed();
    TestRequestIdempotencyAndConflict();
    TestRequestRegistryHardBound();
    TestRpcFailClosed();
    TestCapabilityAuthorityFailsClosed();
    TestProtocolRoundTrip();
    TestProtocolRejectsMalformedFrames();

    std::cout << "IphoxAI native core baseline tests: PASS\n";
    return 0;
}
