#include "iphox/rpc/RpcAuthority.hpp"

namespace iphox::rpc {
namespace {

bool StartsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
        value.substr(0, prefix.size()) == prefix;
}

} // namespace

Domain RpcAuthority::Classify(std::string_view method) noexcept {
    if (method == "hello" || method == "ping" ||
        StartsWith(method, "diagnostics:")) {
        return Domain::Diagnostics;
    }
    if (StartsWith(method, "body:")) {
        return Domain::Body;
    }
    if (StartsWith(method, "memory:")) {
        return Domain::Memory;
    }
    if (StartsWith(method, "decision:") ||
        StartsWith(method, "cognitive:")) {
        return Domain::Cognitive;
    }
    if (StartsWith(method, "chat:")) {
        return Domain::Chat;
    }
    if (StartsWith(method, "runtime:")) {
        return Domain::Runtime;
    }
    if (StartsWith(method, "capability:")) {
        return Domain::Capability;
    }
    if (StartsWith(method, "core:") ||
        StartsWith(method, "lifecycle:")) {
        return Domain::Lifecycle;
    }
    return Domain::Unknown;
}

void DomainDispatcher::Bind(
    Domain domain,
    IDomainController& controller) {

    if (domain == Domain::Unknown) {
        return;
    }
    controllers_.insert_or_assign(domain, &controller);
}

RpcResult DomainDispatcher::Dispatch(
    const RpcRequest& request) const {

    const auto domain = RpcAuthority::Classify(request.method);

    if (domain == Domain::Unknown) {
        return {
            .ok = false,
            .code = "UNKNOWN_RPC",
            .payload = {}
        };
    }

    const auto it = controllers_.find(domain);
    if (it == controllers_.end() || it->second == nullptr) {
        return {
            .ok = false,
            .code = "DOMAIN_UNAVAILABLE",
            .payload = {}
        };
    }

    return it->second->Handle(request);
}

} // namespace iphox::rpc
