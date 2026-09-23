#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace iphox::rpc {

enum class Domain : std::uint8_t {
    Diagnostics,
    Body,
    Memory,
    Cognitive,
    Chat,
    Runtime,
    Capability,
    Lifecycle,
    Unknown
};

struct RpcRequest {
    std::uint64_t requestId{};
    std::string method;
    std::string payload;
};

struct RpcResult {
    bool ok{};
    std::string code;
    std::string payload;
};

class IDomainController {
public:
    virtual ~IDomainController() = default;
    [[nodiscard]] virtual RpcResult Handle(const RpcRequest& request) = 0;
};

class RpcAuthority final {
public:
    [[nodiscard]] static Domain Classify(std::string_view method) noexcept;
};

class DomainDispatcher final {
public:
    void Bind(Domain domain, IDomainController& controller);

    [[nodiscard]] RpcResult Dispatch(const RpcRequest& request) const;

private:
    std::map<Domain, IDomainController*> controllers_;
};

} // namespace iphox::rpc
