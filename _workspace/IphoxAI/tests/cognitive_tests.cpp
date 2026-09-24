#include "iphox/capability/CapabilityAuthority.hpp"
#include "iphox/chat/ChatCodec.hpp"
#include "iphox/chat/ConversationHistory.hpp"
#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/DecisionCodec.hpp"
#include "iphox/cognitive/DecisionValidator.hpp"
#include "iphox/cognitive/StateProjection.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/core/CoreService.hpp"
#include "iphox/foundation/CoreLifecycle.hpp"
#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/foundation/Sha256.hpp"
#include "iphox/foundation/TextEncoding.hpp"
#include "iphox/foundation/JsonLite.hpp"
#include "iphox/generation/LlamaCppHttpEngine.hpp"
#include "iphox/generation/LlamaCppWire.hpp"
#include "iphox/generation/UnavailableGenerativeEngine.hpp"
#include "iphox/ipc/Payload.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/rpc/RpcAuthority.hpp"
#include "iphox/runtime/RuntimeConfig.hpp"
#include "iphox/runtime/FileLogger.hpp"
#include "iphox/runtime/LlamaServerProcessHost.hpp"
#include "iphox/runtime/Paths.hpp"

#include <cassert>
#include <chrono>
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
    needsMemory.instructions =
        "Does the turn require prior project context?";

    ScoreQuestion complexity;
    complexity.instructions =
        "How much deliberate reasoning is required?";
    complexity.levels = {
        {"trivial"},
        {"normal"},
        {"complex"},
        {"deep"}
    };

    request.questions.emplace(
        "intent",
        std::move(intent));
    request.questions.emplace(
        "needs_memory",
        std::move(needsMemory));
    request.questions.emplace(
        "complexity",
        std::move(complexity));

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
    request.state["decision.intent"] =
        "project_work";
    request.state["decision.needs_memory"] =
        "true";
    request.state["decision.complexity"] =
        "2";

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
    assert(
        result.receipt.route ==
        DecisionRoute::Deterministic);
    assert(
        Supervisor::ReceiptMatchesState(
            result.receipt,
            "state-A"));
    assert(
        !Supervisor::ReceiptMatchesState(
            result.receipt,
            "state-B"));
}

void TestUnknownDecisionAbstains() {
    auto request = MakeRequest();
    request.state["decision.intent"] =
        "project_work";

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
    assert(
        result.receipt.route ==
        DecisionRoute::Abstain);
}

void TestStateProjectionAllowList() {
    StateProjection projection{
        {"active_project", "runtime_ready"}
    };

    projection.Add(
        "active_project",
        "IphoxAI");

    bool rejected = false;

    try {
        projection.Add(
            "api_key",
            "secret");
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
    response.answers["needs_memory"] =
        NoulAnswer{1.4};
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
    request.state["decision.intent"] =
        "project_work";
    request.state["decision.needs_memory"] =
        "true";
    request.state["decision.complexity"] =
        "2";

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
    assert(
        result.receipt.route ==
        DecisionRoute::Abstain);
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

    assert(
        registry.MarkCompleted(
            42,
            {std::byte{0x01}}));

    const auto found =
        registry.Find(42);

    assert(found.has_value());
    assert(found->completed);
    assert(found->encodedResponse.size() == 1);
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

    assert(
        registry.MarkCompleted(
            1,
            {std::byte{0x10}}));

    assert(
        registry.Register(3, "C") ==
        iphox::foundation::RegisterResult::NewRequest);

    assert(registry.Size() == 2);
    assert(!registry.Find(1).has_value());
}

void TestCoreLifecycleDrainsOwnedWork() {
    iphox::foundation::CoreLifecycle lifecycle;

    assert(lifecycle.BeginStart());
    assert(lifecycle.MarkReady());

    auto first =
        lifecycle.TryAcquireJob();

    auto second =
        lifecycle.TryAcquireJob();

    assert(first.has_value());
    assert(second.has_value());
    assert(lifecycle.ActiveJobs() == 2);

    assert(lifecycle.RequestStop());

    auto rejected =
        lifecycle.TryAcquireJob();

    assert(!rejected.has_value());

    first.reset();

    assert(lifecycle.ActiveJobs() == 1);
    assert(
        !lifecycle.WaitForDrain(
            std::chrono::milliseconds{1}));

    second.reset();

    assert(
        lifecycle.WaitForDrain(
            std::chrono::milliseconds{10}));

    assert(lifecycle.MarkStopped());

    assert(
        lifecycle.State() ==
        iphox::foundation::CoreState::Stopped);
}

class EchoController final
    : public iphox::rpc::IDomainController {

public:
    iphox::rpc::RpcResult Handle(
        const iphox::rpc::RpcRequest& request) override {

        return {
            true,
            "OK",
            request.method
        };
    }
};

void TestRpcFailClosed() {
    iphox::rpc::DomainDispatcher dispatcher;
    EchoController chat;

    dispatcher.Bind(
        iphox::rpc::Domain::Chat,
        chat);

    auto unknown =
        dispatcher.Dispatch({
            .requestId = 1,
            .method = "made-up:thing",
            .payload = {}
        });

    assert(!unknown.ok);
    assert(unknown.code == "UNKNOWN_RPC");

    auto unavailable =
        dispatcher.Dispatch({
            .requestId = 2,
            .method = "memory:query",
            .payload = {}
        });

    assert(!unavailable.ok);
    assert(
        unavailable.code ==
        "DOMAIN_UNAVAILABLE");

    auto handled =
        dispatcher.Dispatch({
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
    assert(
        unavailable.code ==
        "POLICY_UNAVAILABLE");

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
    assert(
        rejected.code ==
        "DECISION_NOT_ACCEPTED");
}

void TestProtocolRoundTrip() {
    iphox::ipc::Frame frame;
    frame.header.type =
        iphox::ipc::MessageType::DecisionEvaluate;
    frame.header.requestId = 99;
    frame.header.flags = 7;
    frame.payload = {
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30}
    };

    const auto encoded =
        iphox::ipc::Protocol::Encode(frame);

    const auto decoded =
        iphox::ipc::Protocol::Decode(encoded);

    assert(decoded.ok());

    assert(
        decoded.frame.header.type ==
        iphox::ipc::MessageType::DecisionEvaluate);

    assert(
        decoded.frame.header.requestId ==
        99);

    assert(
        decoded.frame.header.flags ==
        7);

    assert(
        decoded.frame.payload ==
        frame.payload);
}

void TestProtocolRejectsMalformedFrames() {
    iphox::ipc::Frame frame;
    frame.header.type =
        iphox::ipc::MessageType::Ping;
    frame.header.requestId = 2;

    auto encoded =
        iphox::ipc::Protocol::Encode(frame);

    auto badMagic = encoded;
    badMagic[0] = std::byte{'X'};

    assert(
        iphox::ipc::Protocol::Decode(
            badMagic).status ==
        iphox::ipc::FrameStatus::BadMagic);

    auto truncated = encoded;
    truncated.pop_back();

    const auto result =
        iphox::ipc::Protocol::Decode(
            truncated);

    assert(
        result.status ==
            iphox::ipc::FrameStatus::TooShort ||
        result.status ==
            iphox::ipc::FrameStatus::LengthMismatch);
}

void TestCoreServiceFailClosedAndIdempotent() {
    iphox::generation::UnavailableGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};
    iphox::core::CoreService service{engine, supervisor, 8};

    iphox::ipc::Frame ping;
    ping.header.type =
        iphox::ipc::MessageType::Ping;
    ping.header.requestId = 700;

    const auto first =
        service.Handle(ping);

    assert(
        first.response.header.type ==
        iphox::ipc::MessageType::Pong);

    assert(
        (first.response.header.flags &
            iphox::ipc::kFlagResponse) != 0);

    const auto duplicate =
        service.Handle(ping);

    assert(
        iphox::ipc::Protocol::Encode(
            first.response) ==
        iphox::ipc::Protocol::Encode(
            duplicate.response));

    iphox::ipc::Frame conflict = ping;
    conflict.payload =
        iphox::ipc::ToPayload("changed");

    const auto conflictResult =
        service.Handle(conflict);

    assert(
        conflictResult.response.header.type ==
        iphox::ipc::MessageType::Error);

    assert(
        iphox::ipc::FromPayload(
            conflictResult.response.payload) ==
        "REQUEST_ID_CONFLICT");

    iphox::ipc::Frame chat;
    chat.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    chat.header.requestId = 701;
    chat.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "hello"});

    const auto chatResult =
        service.Handle(chat);

    assert(
        chatResult.response.header.type ==
        iphox::ipc::MessageType::Error);

    assert(
        (chatResult.response.header.flags &
            iphox::ipc::kFlagError) != 0);

    assert(
        iphox::ipc::FromPayload(
            chatResult.response.payload) ==
        "ENGINE_UNAVAILABLE");
}

void TestCoreServiceRejectsResponseAsRequest() {
    iphox::generation::UnavailableGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};
    iphox::core::CoreService service{engine, supervisor, 8};

    iphox::ipc::Frame invalid;
    invalid.header.type =
        iphox::ipc::MessageType::Ping;
    invalid.header.requestId = 800;
    invalid.header.flags =
        iphox::ipc::kFlagResponse;

    const auto result =
        service.Handle(invalid);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::Error);

    assert(
        iphox::ipc::FromPayload(
            result.response.payload) ==
        "REQUEST_MARKED_AS_RESPONSE");
}


void TestChatCodecRoundTripAndValidation() {
    const iphox::chat::ChatSubmit input{
        .text = "ciao ð"
    };

    const auto encoded =
        iphox::chat::ChatCodec::Encode(input);

    const auto decoded =
        iphox::chat::ChatCodec::Decode(encoded);

    assert(decoded.ok());
    assert(decoded.value.text == input.text);

    auto broken = encoded;
    broken.back() = std::byte{0xFF};

    const auto invalid =
        iphox::chat::ChatCodec::Decode(broken);

    assert(
        invalid.status ==
        iphox::chat::ChatCodecStatus::InvalidUtf8);
}

void TestChatSubmitRejectsMalformedPayload() {
    iphox::generation::UnavailableGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};
    iphox::core::CoreService service{engine, supervisor, 8};

    iphox::ipc::Frame chat;
    chat.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    chat.header.requestId = 900;
    chat.payload = {
        std::byte{'b'},
        std::byte{'a'},
        std::byte{'d'}
    };

    const auto result =
        service.Handle(chat);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::Error);

    assert(
        iphox::ipc::FromPayload(
            result.response.payload) ==
        "INVALID_CHAT_PAYLOAD");
}


void TestDecisionCodecRoundTrip() {
    auto request = MakeRequest();
    request.state["decision.intent"] = "project_work";
    request.state["decision.needs_memory"] = "true";
    request.state["decision.complexity"] = "2";

    const auto encoded =
        iphox::cognitive::DecisionCodec::EncodeRequest(
            request);

    const auto decoded =
        iphox::cognitive::DecisionCodec::DecodeRequest(
            encoded);

    assert(decoded.ok());
    assert(decoded.value.schemaId == request.schemaId);
    assert(decoded.value.schemaVersion == request.schemaVersion);
    assert(decoded.value.state == request.state);
    assert(decoded.value.questions.size() == request.questions.size());
}

void TestCoreServiceDecisionEvaluate() {
    iphox::generation::UnavailableGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};
    iphox::core::CoreService service{
        engine,
        supervisor,
        8
    };

    auto decisionRequest = MakeRequest();
    decisionRequest.state["decision.intent"] = "project_work";
    decisionRequest.state["decision.needs_memory"] = "true";
    decisionRequest.state["decision.complexity"] = "2";

    iphox::ipc::Frame frame;
    frame.header.type =
        iphox::ipc::MessageType::DecisionEvaluate;
    frame.header.requestId = 950;
    frame.payload =
        iphox::cognitive::DecisionCodec::EncodeRequest(
            decisionRequest);

    const auto result =
        service.Handle(frame);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::DecisionTrace);

    assert(
        (result.response.header.flags &
            iphox::ipc::kFlagError) == 0);

    const auto decoded =
        iphox::cognitive::DecisionCodec::DecodeResponse(
            result.response.payload);

    assert(decoded.ok());

    const auto intent =
        decoded.value.answers.find("intent");

    assert(intent != decoded.value.answers.end());

    const auto* choice =
        std::get_if<ChoiceAnswer>(
            &intent->second);

    assert(choice != nullptr);
    assert(choice->selected == "project_work");
    assert(choice->reportedConfidence.has_value());
    assert(*choice->reportedConfidence == 1.0);
}

void TestCoreServiceRejectsMalformedDecisionPayload() {
    iphox::generation::UnavailableGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};
    iphox::core::CoreService service{
        engine,
        supervisor,
        8
    };

    iphox::ipc::Frame frame;
    frame.header.type =
        iphox::ipc::MessageType::DecisionEvaluate;
    frame.header.requestId = 951;
    frame.payload = {
        std::byte{'x'},
        std::byte{'x'}
    };

    const auto result =
        service.Handle(frame);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::Error);

    assert(
        iphox::ipc::FromPayload(
            result.response.payload) ==
        "INVALID_DECISION_PAYLOAD");
}


void TestSha256KnownVector() {
    const std::string input = "abc";

    std::vector<std::byte> bytes;
    bytes.reserve(input.size());

    for (const unsigned char ch : input) {
        bytes.push_back(
            static_cast<std::byte>(ch));
    }

    const auto digest =
        iphox::foundation::Sha256(bytes);

    assert(digest.has_value());

    assert(
        iphox::foundation::Hex(*digest) ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad");
}


class EchoGenerativeEngine final
    : public iphox::generation::IGenerativeEngine {

public:
    iphox::generation::EngineProbeResult Probe(
        std::stop_token stopToken) override {

        if (stopToken.stop_requested()) {
            return {
                .status = iphox::generation::EngineStatus::Unavailable,
                .detail = "CANCELLED"
            };
        }

        return {
            .status = iphox::generation::EngineStatus::Ready,
            .detail = "mock-ready"
        };
    }

    iphox::generation::GenerationResult Generate(
        const iphox::generation::GenerationRequest& request,
        std::stop_token stopToken) override {

        if (stopToken.stop_requested()) {
            return {
                .status = iphox::generation::GenerationStatus::Cancelled,
                .text = {},
                .errorCode = "CANCELLED"
            };
        }

        if (request.messages.empty()) {
            return {
                .status = iphox::generation::GenerationStatus::Failed,
                .text = {},
                .errorCode = "EMPTY"
            };
        }

        return {
            .status = iphox::generation::GenerationStatus::Completed,
            .text = "echo:" + request.messages.back().text,
            .errorCode = {}
        };
    }
};

void TestJsonLite() {
    const std::string raw =
        "quote=\" slash=\\ newline=\n";

    const auto escaped =
        iphox::foundation::JsonEscape(raw);

    const auto json =
        std::string{"{\"content\":\""} +
        escaped +
        "\"}";

    const auto parsed =
        iphox::foundation::JsonStringField(
            json,
            "content");

    assert(parsed.has_value());
    assert(*parsed == raw);

    const auto unicode =
        iphox::foundation::JsonStringField(
            "{\"content\":\"\\u20ac \\ud83d\\udc4b\"}",
            "content");

    assert(unicode.has_value());
    assert(*unicode == "â¬ ð");
}

void TestLlamaHostRestriction() {
    assert(
        iphox::generation::LlamaCppHttpEngine::IsLoopbackHost(
            L"127.0.0.1"));

    assert(
        iphox::generation::LlamaCppHttpEngine::IsLoopbackHost(
            L"localhost"));

    assert(
        iphox::generation::LlamaCppHttpEngine::IsLoopbackHost(
            L"::1"));

    assert(
        !iphox::generation::LlamaCppHttpEngine::IsLoopbackHost(
            L"192.168.1.20"));

    assert(
        !iphox::generation::LlamaCppHttpEngine::IsLoopbackHost(
            L"example.com"));
}

void TestCoreServiceCompletedChatPath() {
    EchoGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};

    iphox::core::CoreService service{
        engine,
        supervisor,
        8
    };

    iphox::ipc::Frame chat;
    chat.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    chat.header.requestId = 980;
    chat.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "ciao"});

    const auto result =
        service.Handle(chat);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::ChatStatus);

    assert(
        (result.response.header.flags &
            iphox::ipc::kFlagError) == 0);

    assert(
        iphox::ipc::FromPayload(
            result.response.payload) ==
        "echo:ciao");
}


void TestRuntimeConfigDefaultsAndValidation() {
    const auto defaults =
        iphox::runtime::RuntimeConfigLoader::Parse("");

    assert(defaults.valid);
    assert(
        defaults.config.llama.host ==
        L"127.0.0.1");
    assert(defaults.config.llama.port == 8080);

    const auto configured =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "runtime.host=localhost\n"
            "runtime.port=18080\n"
            "runtime.n_predict=768\n"
            "runtime.connect_timeout_ms=2500\n"
            "chat.max_turns=12\n"
            "chat.max_bytes=32768\n"
            "chat.system_prompt=Custom Iphox prompt\n");

    assert(configured.valid);
    assert(
        configured.config.llama.host ==
        L"localhost");
    assert(
        configured.config.llama.port ==
        18080);
    assert(
        configured.config.llama.nPredict ==
        768);
    assert(
        configured.config.llama.connectTimeoutMs ==
        2500);
    assert(
        configured.config.chat.maxTurns ==
        12);
    assert(
        configured.config.chat.maxBytes ==
        32768);
    assert(
        configured.config.chat.systemPrompt ==
        "Custom Iphox prompt");

    const auto invalidChat =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "chat.max_turns=0\n"
            "chat.max_bytes=12\n"
            "chat.system_prompt=\n");

    assert(!invalidChat.valid);

    const auto remote =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "runtime.host=192.168.1.10\n");

    assert(!remote.valid);

    const auto duplicate =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "runtime.port=8080\n"
            "runtime.port=8081\n");

    assert(!duplicate.valid);
}


void TestStrictTextEncodingRoundTrip() {
    const std::string utf8 =
        "IphoxAI â¬ ð";

    const auto wide =
        iphox::foundation::Utf8ToWide(
            utf8);

    assert(wide.has_value());

    const auto roundTrip =
        iphox::foundation::WideToUtf8(
            *wide);

    assert(roundTrip.has_value());
    assert(*roundTrip == utf8);

    const std::string invalid{
        static_cast<char>(0xC0),
        static_cast<char>(0xAF)
    };

    assert(
        !iphox::foundation::Utf8ToWide(
            invalid).has_value());
}


void TestCoreServiceRuntimeStatus() {
    EchoGenerativeEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};

    iphox::core::CoreService service{
        engine,
        supervisor,
        8
    };

    iphox::ipc::Frame status;
    status.header.type =
        iphox::ipc::MessageType::RuntimeStatus;
    status.header.requestId = 990;

    const auto result =
        service.Handle(status);

    assert(
        result.response.header.type ==
        iphox::ipc::MessageType::RuntimeStatus);

    assert(
        iphox::ipc::FromPayload(
            result.response.payload) ==
        "READY:mock-ready");
}


void TestConversationHistoryIsBoundedAndOrdered() {
    iphox::chat::ConversationHistory history{
        2,
        1024,
        "system"
    };

    history.AddTurn("u1", "a1");
    history.AddTurn("u2", "a2");
    history.AddTurn("u3", "a3");

    assert(history.Size() == 2);

    const auto request =
        history.BuildRequest("u4");

    assert(request.messages.size() == 6);

    assert(
        request.messages[0].role ==
        iphox::generation::GenerationRole::System);

    assert(request.messages[1].text == "u2");
    assert(request.messages[2].text == "a2");
    assert(request.messages[3].text == "u3");
    assert(request.messages[4].text == "a3");
    assert(request.messages[5].text == "u4");
}

class ContextCaptureEngine final
    : public iphox::generation::IGenerativeEngine {

public:
    iphox::generation::EngineProbeResult Probe(
        std::stop_token) override {

        return {
            .status = iphox::generation::EngineStatus::Ready,
            .detail = "capture"
        };
    }

    iphox::generation::GenerationResult Generate(
        const iphox::generation::GenerationRequest& request,
        std::stop_token) override {

        last = request;
        ++calls;

        return {
            .status = iphox::generation::GenerationStatus::Completed,
            .text = "ok",
            .errorCode = {}
        };
    }

    iphox::generation::GenerationRequest last;
    int calls{};
};

void TestCoreServiceCarriesConversationContext() {
    ContextCaptureEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};

    iphox::core::CoreService service{
        engine,
        supervisor,
        16
    };

    iphox::ipc::Frame first;
    first.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    first.header.requestId = 1200;
    first.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "first"});

    const auto firstResult =
        service.Handle(first);

    assert(
        firstResult.response.header.type ==
        iphox::ipc::MessageType::ChatStatus);

    iphox::ipc::Frame second;
    second.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    second.header.requestId = 1201;
    second.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "second"});

    const auto secondResult =
        service.Handle(second);

    assert(
        secondResult.response.header.type ==
        iphox::ipc::MessageType::ChatStatus);

    assert(engine.last.messages.size() == 4);
    assert(engine.last.messages[1].text == "first");
    assert(engine.last.messages[2].text == "ok");
    assert(engine.last.messages[3].text == "second");
}


void TestDuplicateChatDoesNotDuplicateHistory() {
    ContextCaptureEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};

    iphox::core::CoreService service{
        engine,
        supervisor,
        16
    };

    iphox::ipc::Frame first;
    first.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    first.header.requestId = 1300;
    first.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "hello"});

    const auto firstResult =
        service.Handle(first);

    const auto duplicateResult =
        service.Handle(first);

    assert(engine.calls == 1);

    assert(
        iphox::ipc::Protocol::Encode(
            firstResult.response) ==
        iphox::ipc::Protocol::Encode(
            duplicateResult.response));

    iphox::ipc::Frame second;
    second.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    second.header.requestId = 1301;
    second.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "next"});

    const auto secondResult =
        service.Handle(second);

    assert(
        secondResult.response.header.type ==
        iphox::ipc::MessageType::ChatStatus);

    assert(engine.calls == 2);

    assert(engine.last.messages.size() == 4);
    assert(engine.last.messages[1].text == "hello");
    assert(engine.last.messages[2].text == "ok");
    assert(engine.last.messages[3].text == "next");
}


void TestLlamaCppWireUsesTypedRoles() {
    iphox::generation::GenerationRequest request;

    request.messages = {
        {
            .role = iphox::generation::GenerationRole::System,
            .text = "system"
        },
        {
            .role = iphox::generation::GenerationRole::User,
            .text = "hello"
        },
        {
            .role = iphox::generation::GenerationRole::Assistant,
            .text = "world"
        }
    };

    const auto body =
        iphox::generation::LlamaCppWire::BuildChatRequest(
            request,
            512);

    assert(body.has_value());

    assert(
        body->find("\"role\":\"system\"") !=
        std::string::npos);

    assert(
        body->find("\"role\":\"user\"") !=
        std::string::npos);

    assert(
        body->find("\"role\":\"assistant\"") !=
        std::string::npos);

    assert(
        body->find("\"max_tokens\":512") !=
        std::string::npos);

    const auto parsed =
        iphox::generation::LlamaCppWire::ParseChatContent(
            "{\"choices\":[{\"message\":"
            "{\"role\":\"assistant\","
            "\"content\":\"ciao\"}}]}");

    assert(parsed.has_value());
    assert(*parsed == "ciao");
}


void TestPortableRuntimePathsAndAutostartConfig() {
    const auto parsed =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "runtime.autostart=true\n"
            "runtime.server_path=runtime\\\\llama-server.exe\n"
            "runtime.model_path=models\\\\qwen.gguf\n"
            "runtime.ctx_size=16384\n"
            "runtime.gpu_layers=all\n"
            "runtime.startup_timeout_ms=45000\n");

    assert(parsed.valid);
    assert(parsed.config.server.autostart);
    assert(
        parsed.config.server.contextSize ==
        16384);
    assert(
        parsed.config.server.gpuLayers ==
        L"all");
    assert(
        parsed.config.server.startupTimeoutMs ==
        45000);

    const std::filesystem::path root{
        L"C:\\Portable\\IphoxAI"
    };

    const auto server =
        iphox::runtime::ResolvePortablePath(
            root,
            parsed.config.server.serverPath);

    assert(
        server ==
        std::filesystem::path{
            L"C:\\Portable\\IphoxAI\\runtime\\llama-server.exe"
        }.lexically_normal());

    const auto bad =
        iphox::runtime::RuntimeConfigLoader::Parse(
            "runtime.autostart=maybe\n"
            "runtime.gpu_layers=banana\n");

    assert(!bad.valid);

    iphox::runtime::LlamaServerProcessHost host;

    iphox::runtime::LlamaServerLaunchConfig invalid;
    invalid.serverPath =
        L"Z:\\does-not-exist\\llama-server.exe";
    invalid.modelPath =
        L"Z:\\does-not-exist\\model.gguf";

    assert(!host.Start(invalid));
    assert(!host.IsRunning());
}


void TestChatClearResetsCoreConversation() {
    ContextCaptureEngine engine;
    BaselineDecisionBackend decisions;
    Supervisor supervisor{decisions};

    iphox::core::CoreService service{
        engine,
        supervisor,
        16
    };

    iphox::ipc::Frame first;
    first.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    first.header.requestId = 1400;
    first.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "before-clear"});

    (void)service.Handle(first);

    iphox::ipc::Frame clear;
    clear.header.type =
        iphox::ipc::MessageType::ChatClear;
    clear.header.requestId = 1401;

    const auto clearResult =
        service.Handle(clear);

    assert(
        clearResult.response.header.type ==
        iphox::ipc::MessageType::ChatClear);

    iphox::ipc::Frame after;
    after.header.type =
        iphox::ipc::MessageType::ChatSubmit;
    after.header.requestId = 1402;
    after.payload =
        iphox::chat::ChatCodec::Encode(
            {.text = "after-clear"});

    (void)service.Handle(after);

    assert(engine.last.messages.size() == 2);
    assert(
        engine.last.messages[0].role ==
        iphox::generation::GenerationRole::System);
    assert(
        engine.last.messages[1].text ==
        "after-clear");
}


void TestFileLoggerIsBoundedAndRotates() {
    const auto root =
        std::filesystem::temp_directory_path() /
        L"iphoxai_native_r0_logger_test";

    std::error_code ec;
    std::filesystem::remove_all(
        root,
        ec);

    const auto path =
        root /
        L"IphoxCore.log";

    {
        iphox::runtime::FileLogger logger{
            path,
            256
        };

        assert(logger.Enabled());

        for (int i = 0; i < 20; ++i) {
            logger.Write(
                iphox::runtime::LogLevel::Info,
                "line with enough text to force rotation");
        }
    }

    assert(
        std::filesystem::exists(
            path));

    auto backup = path;
    backup += L".1";

    assert(
        std::filesystem::exists(
            backup));

    assert(
        std::filesystem::file_size(
            path) <= 256);

    std::filesystem::remove_all(
        root,
        ec);
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
    TestCoreLifecycleDrainsOwnedWork();
    TestRpcFailClosed();
    TestCapabilityAuthorityFailsClosed();
    TestProtocolRoundTrip();
    TestProtocolRejectsMalformedFrames();
    TestCoreServiceFailClosedAndIdempotent();
    TestCoreServiceRejectsResponseAsRequest();
    TestChatCodecRoundTripAndValidation();
    TestChatSubmitRejectsMalformedPayload();
    TestDecisionCodecRoundTrip();
    TestCoreServiceDecisionEvaluate();
    TestCoreServiceRejectsMalformedDecisionPayload();
    TestSha256KnownVector();
    TestJsonLite();
    TestLlamaHostRestriction();
    TestCoreServiceCompletedChatPath();
    TestRuntimeConfigDefaultsAndValidation();
    TestStrictTextEncodingRoundTrip();
    TestCoreServiceRuntimeStatus();
    TestConversationHistoryIsBoundedAndOrdered();
    TestCoreServiceCarriesConversationContext();
    TestDuplicateChatDoesNotDuplicateHistory();
    TestLlamaCppWireUsesTypedRoles();
    TestPortableRuntimePathsAndAutostartConfig();
    TestChatClearResetsCoreConversation();
    TestFileLoggerIsBoundedAndRotates();

    std::cout
        << "IphoxAI native core baseline tests: PASS\n";

    return 0;
}
