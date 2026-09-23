# IphoxAI Native R0

Native C++ recovery and modernization of IphoxAI.

## Current executable slice

The branch builds two real Windows x64 executables:

- `IphoxAI.exe` — Win32 + Direct2D + DirectWrite shell, DPI-aware, tray lifecycle.
- `IphoxCore.exe` — isolated Core process owned by the UI through a Windows Job Object.

The UI starts the Core from the same directory, validates its identity with a typed `Hello` handshake, performs `Ping/Pong`, and shuts it down only on explicit application exit. Closing the main window hides IphoxAI to the tray.

## Core transport

UI and Core communicate through a user-scoped local named pipe:

- binary `IPHX` framing, protocol version 1;
- maximum frame payload enforced;
- exact frame-length validation;
- remote pipe clients rejected;
- ACL restricted to SYSTEM + current Windows user;
- request IDs are idempotent;
- request conflicts use SHA-256 fingerprints through Windows CNG;
- completed duplicate requests replay the cached response;
- active jobs are drained during controlled shutdown.

## Chat

`ChatSubmit` has its own versioned binary payload (`CHT1`) with UTF-8 and size validation.

Generation is behind `IGenerativeEngine`.

Current runtime backend:

- `LlamaCppHttpEngine`
- native WinHTTP
- loopback-only hosts (`127.0.0.1`, `localhost`, `::1`)
- default llama.cpp port 8080
- `POST /completion`
- bounded request/response sizes and timeouts

No Node, Python, Electron, WebView, curl process, or browser runtime is required by IphoxAI.

## Cognitive core

The JEV-inspired layer is vendor-neutral.

Implemented:

- `NoulQuestion`
- `ChoiceQuestion`
- `ScoreQuestion`
- `AbstainAnswer`
- `IDecisionBackend`
- deterministic `BaselineDecisionBackend`
- `DecisionValidator`
- allow-listed `StateProjection`
- `DecisionReceipt`
- `Supervisor`
- binary `DecisionCodec` request/response transport

`DecisionEvaluate` now travels end-to-end through:

`IPC -> DecisionCodec -> Supervisor -> IDecisionBackend -> DecisionTrace`

The decision layer has no capability execution authority.

## Fail-closed behavior

The current tests cover, among other cases:

- malformed IPC frames;
- invalid probabilities;
- unknown decisions;
- stale decision receipts;
- unknown RPC domains;
- missing domain handlers;
- denied capabilities;
- cancellation;
- hard-bounded request registry;
- duplicate request replay;
- conflicting request IDs;
- Core job drain on shutdown;
- chat payload validation;
- invalid decision payloads;
- SHA-256 known vector;
- JSON escaping/extraction;
- llama.cpp loopback policy;
- full chat routing through a mock generative engine;
- real cross-process Core pipe smoke test.

## CI checkpoint

Windows CI configures with CMake, builds Release x64, runs the tests, then packages:

- `bin/IphoxAI.exe`
- `bin/IphoxCore.exe`
- complete R0 source
- tests
- CMake configuration
- exact Git commit

into `IphoxAI_Native_R0_CHECKPOINT.zip`.

## Recovery status

This branch is an isolated staging workspace. The recovered `IphoxAI.zip` remains the authoritative migration source for legacy Body, memory, UI and other behavior that has not yet been reconnected.

Do not infer legacy binary formats from this R0 branch. They must be migrated from verified recovered source/data rather than re-created approximately.
