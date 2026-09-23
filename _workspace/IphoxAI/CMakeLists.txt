cmake_minimum_required(VERSION 3.24)
project(IphoxAI_Native_R0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(iphox_core STATIC
    src/cognitive/BaselineDecisionBackend.cpp
    src/cognitive/DecisionValidator.cpp
    src/cognitive/Supervisor.cpp
    src/foundation/CoreLifecycle.cpp
    src/foundation/RequestRegistry.cpp
    src/rpc/RpcAuthority.cpp
    src/capability/CapabilityAuthority.cpp
    src/ipc/Protocol.cpp
    src/ipc/SecurePipeClient.cpp
    src/ipc/SecurePipeServer.cpp
    src/runtime/CoreProcessHost.cpp
)

target_include_directories(iphox_core PUBLIC include)
target_link_libraries(iphox_core PUBLIC advapi32)

if(MSVC)
    target_compile_options(iphox_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(iphox_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(IphoxCore
    app/IphoxCore/main.cpp
)
target_link_libraries(IphoxCore PRIVATE iphox_core)

add_executable(IphoxAI WIN32
    app/IphoxAI/main.cpp
)
target_link_libraries(IphoxAI PRIVATE
    iphox_core
    d2d1
    dwrite
    shell32
    user32
)

add_executable(iphox_core_tests
    tests/cognitive_tests.cpp
)
target_link_libraries(iphox_core_tests PRIVATE iphox_core)

add_executable(iphox_core_ipc_smoke
    tests/core_ipc_smoke.cpp
)
target_link_libraries(iphox_core_ipc_smoke PRIVATE iphox_core)

enable_testing()
add_test(NAME iphox_core_tests COMMAND iphox_core_tests)
add_test(NAME iphox_core_ipc_smoke COMMAND iphox_core_ipc_smoke)
