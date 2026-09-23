#include "iphox/cognitive/BaselineDecisionBackend.hpp"
#include "iphox/cognitive/Supervisor.hpp"
#include "iphox/foundation/RequestRegistry.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/rpc/RpcAuthority.hpp"

#include <iostream>

int wmain() {
    iphox::cognitive::BaselineDecisionBackend decisions;
    iphox::cognitive::Supervisor supervisor{decisions};
    iphox::foundation::RequestRegistry requests{1024};
    iphox::rpc::DomainDispatcher dispatcher;

    (void)supervisor;
    (void)requests;
    (void)dispatcher;

    std::wcout
        << L"IphoxCore Native R0\n"
        << L"IPC protocol: " << iphox::ipc::kProtocolVersion << L"\n"
        << L"Decision backend: baseline-deterministic\n"
        << L"Status: READY\n";

    return 0;
}
