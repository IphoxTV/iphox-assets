#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include "iphox/chat/ChatCodec.hpp"
#include "iphox/foundation/TextEncoding.hpp"
#include "iphox/ipc/Payload.hpp"
#include "iphox/ipc/Protocol.hpp"
#include "iphox/runtime/CoreProcessHost.hpp"
#include "iphox/runtime/CoreRpcClient.hpp"
#include "iphox/runtime/SingleInstanceGuard.hpp"

#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kWindowClass[] =
    L"IphoxAI.Native.Window";

constexpr wchar_t kWindowTitle[] =
    L"IphoxAI";

constexpr UINT kTrayMessage =
    WM_APP + 42;

constexpr UINT kChatResultMessage =
    WM_APP + 43;

constexpr UINT kRuntimeResultMessage =
    WM_APP + 44;

constexpr UINT_PTR kRuntimeTimerId = 2001;

constexpr UINT kTrayId = 1;
constexpr UINT kTrayOpen = 1001;
constexpr UINT kTrayExit = 1002;
constexpr UINT kTrayNewChat = 1003;
constexpr UINT kChatInput = 1101;
constexpr UINT kChatSend = 1102;

struct ChatUiResult {
    std::uint64_t requestId{};
    bool ok{};
    std::wstring text;
};

struct RuntimeUiResult {
    std::wstring text;
};

class NativeWindow final {
public:
    ~NativeWindow() {
        ShutdownCore();
        RemoveTrayIcon();
        ReleaseDeviceResources();

        if (uiFont_ != nullptr) {
            DeleteObject(uiFont_);
            uiFont_ = nullptr;
        }

        if (controlBrush_ != nullptr) {
            DeleteObject(controlBrush_);
            controlBrush_ = nullptr;
        }

        if (textFormat_ != nullptr) {
            textFormat_->Release();
            textFormat_ = nullptr;
        }

        if (dwriteFactory_ != nullptr) {
            dwriteFactory_->Release();
            dwriteFactory_ = nullptr;
        }

        if (d2dFactory_ != nullptr) {
            d2dFactory_->Release();
            d2dFactory_ = nullptr;
        }
    }

    bool Initialize(
        HINSTANCE instance,
        int showCommand) {

        instance_ = instance;

        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                &d2dFactory_))) {
            return false;
        }

        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(
                    &dwriteFactory_)))) {
            return false;
        }

        if (FAILED(
                dwriteFactory_->CreateTextFormat(
                    L"Segoe UI Variable Display",
                    nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    26.0f,
                    L"it-IT",
                    &textFormat_))) {
            return false;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance_;
        wc.lpfnWndProc =
            &NativeWindow::WindowProc;
        wc.lpszClassName = kWindowClass;
        wc.hCursor =
            LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon =
            LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;

        if (RegisterClassExW(&wc) == 0) {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1100,
            720,
            nullptr,
            nullptr,
            instance_,
            this);

        if (hwnd_ == nullptr) {
            return false;
        }

        if (!CreateControls()) {
            return false;
        }

        AddTrayIcon();
        StartCore();

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run() {
        MSG msg{};

        while (GetMessageW(
                   &msg,
                   nullptr,
                   0,
                   0) > 0) {

            if (msg.hwnd == input_ &&
                msg.message == WM_KEYDOWN &&
                msg.wParam == VK_RETURN) {

                SubmitChat();
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return static_cast<int>(
            msg.wParam);
    }

private:
    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {

        NativeWindow* self = nullptr;

        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(
                    lParam);

            self =
                static_cast<NativeWindow*>(
                    create->lpCreateParams);

            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(
                    self));

            self->hwnd_ = hwnd;
        } else {
            self =
                reinterpret_cast<NativeWindow*>(
                    GetWindowLongPtrW(
                        hwnd,
                        GWLP_USERDATA));
        }

        if (self != nullptr) {
            return self->HandleMessage(
                message,
                wParam,
                lParam);
        }

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    LRESULT HandleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {

        switch (message) {
        case WM_PAINT:
            Paint();
            return 0;

        case WM_SIZE:
            if (renderTarget_ != nullptr) {
                const auto width =
                    LOWORD(lParam);

                const auto height =
                    HIWORD(lParam);

                renderTarget_->Resize(
                    D2D1::SizeU(
                        width,
                        height));
            }

            LayoutControls(
                LOWORD(lParam),
                HIWORD(lParam));

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE);
            return 0;

        case WM_DPICHANGED: {
            const auto* rect =
                reinterpret_cast<RECT*>(
                    lParam);

            SetWindowPos(
                hwnd_,
                nullptr,
                rect->left,
                rect->top,
                rect->right - rect->left,
                rect->bottom - rect->top,
                SWP_NOACTIVATE |
                    SWP_NOZORDER);

            RecreateUiFont();
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(
                hwnd_,
                SW_HIDE);
            return 0;

        case WM_QUERYENDSESSION:
            ShutdownCore();
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) ==
                kTrayOpen) {

                RestoreFromTray();
                return 0;
            }

            if (LOWORD(wParam) ==
                kTrayExit) {

                ShutdownCore();
                DestroyWindow(hwnd_);
                return 0;
            }

            if (LOWORD(wParam) ==
                kTrayNewChat) {

                ClearConversation();
                return 0;
            }

            if (LOWORD(wParam) ==
                    kChatSend &&
                HIWORD(wParam) ==
                    BN_CLICKED) {

                SubmitChat();
                return 0;
            }
            break;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const auto dc =
                reinterpret_cast<HDC>(
                    wParam);

            SetTextColor(
                dc,
                RGB(236, 232, 218));

            SetBkColor(
                dc,
                RGB(25, 25, 23));

            return reinterpret_cast<LRESULT>(
                controlBrush_);
        }

        case kChatResultMessage:
            HandleChatResult(
                reinterpret_cast<ChatUiResult*>(
                    lParam));
            return 0;

        case kRuntimeResultMessage:
            HandleRuntimeResult(
                reinterpret_cast<RuntimeUiResult*>(
                    lParam));
            return 0;

        case WM_TIMER:
            if (wParam == kRuntimeTimerId) {
                ProbeRuntimeAsync();
                return 0;
            }
            break;

        case kTrayMessage:
            if (LOWORD(lParam) ==
                WM_LBUTTONDBLCLK) {

                RestoreFromTray();
                return 0;
            }

            if (LOWORD(lParam) ==
                    WM_RBUTTONUP ||
                LOWORD(lParam) ==
                    WM_CONTEXTMENU) {

                ShowTrayMenu();
                return 0;
            }
            break;

        case WM_DESTROY:
            ShutdownCore();
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(
            hwnd_,
            message,
            wParam,
            lParam);
    }

    bool CreateControls() {
        transcript_ =
            CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    ES_MULTILINE |
                    ES_READONLY |
                    ES_AUTOVSCROLL |
                    WS_VSCROLL,
                0,
                0,
                0,
                0,
                hwnd_,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(
                        1201)),
                instance_,
                nullptr);

        input_ =
            CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD |
                    WS_VISIBLE |
                    ES_AUTOHSCROLL,
                0,
                0,
                0,
                0,
                hwnd_,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(
                        kChatInput)),
                instance_,
                nullptr);

        sendButton_ =
            CreateWindowExW(
                0,
                L"BUTTON",
                L"Invia",
                WS_CHILD |
                    WS_VISIBLE |
                    BS_PUSHBUTTON,
                0,
                0,
                0,
                0,
                hwnd_,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(
                        kChatSend)),
                instance_,
                nullptr);

        if (transcript_ == nullptr ||
            input_ == nullptr ||
            sendButton_ == nullptr) {
            return false;
        }

        SendMessageW(
            input_,
            EM_SETLIMITTEXT,
            32768,
            0);

        controlBrush_ =
            CreateSolidBrush(
                RGB(25, 25, 23));

        if (controlBrush_ == nullptr) {
            return false;
        }

        RecreateUiFont();

        AppendTranscript(
            L"Sistema",
            L"IphoxAI Native R0 avviato.");

        RECT client{};
        GetClientRect(
            hwnd_,
            &client);

        LayoutControls(
            client.right - client.left,
            client.bottom - client.top);

        UpdateChatControls();
        return true;
    }

    void RecreateUiFont() {
        if (uiFont_ != nullptr) {
            DeleteObject(uiFont_);
            uiFont_ = nullptr;
        }

        const UINT dpi =
            hwnd_ != nullptr
            ? GetDpiForWindow(hwnd_)
            : 96;

        const int height =
            -MulDiv(
                11,
                static_cast<int>(dpi),
                72);

        uiFont_ =
            CreateFontW(
                height,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH |
                    FF_DONTCARE,
                L"Segoe UI Variable Text");

        if (uiFont_ != nullptr) {
            for (HWND control :
                 {transcript_,
                  input_,
                  sendButton_}) {

                if (control != nullptr) {
                    SendMessageW(
                        control,
                        WM_SETFONT,
                        reinterpret_cast<WPARAM>(
                            uiFont_),
                        TRUE);
                }
            }
        }
    }

    void LayoutControls(
        int width,
        int height) {

        if (transcript_ == nullptr ||
            input_ == nullptr ||
            sendButton_ == nullptr) {
            return;
        }

        constexpr int margin = 48;
        constexpr int top = 155;
        constexpr int bottom = 34;
        constexpr int inputHeight = 42;
        constexpr int buttonWidth = 108;
        constexpr int gap = 10;

        const int usableWidth =
            (std::max)(
                180,
                width - margin * 2);

        const int transcriptHeight =
            (std::max)(
                80,
                height -
                    top -
                    inputHeight -
                    bottom -
                    18);

        MoveWindow(
            transcript_,
            margin,
            top,
            usableWidth,
            transcriptHeight,
            TRUE);

        const int inputY =
            top +
            transcriptHeight +
            12;

        MoveWindow(
            input_,
            margin,
            inputY,
            (std::max)(
                80,
                usableWidth -
                    buttonWidth -
                    gap),
            inputHeight,
            TRUE);

        MoveWindow(
            sendButton_,
            margin +
                usableWidth -
                buttonWidth,
            inputY,
            buttonWidth,
            inputHeight,
            TRUE);
    }

    bool EnsureDeviceResources() {
        if (renderTarget_ != nullptr) {
            return true;
        }

        RECT client{};
        GetClientRect(
            hwnd_,
            &client);

        const auto size =
            D2D1::SizeU(
                static_cast<UINT>(
                    (std::max<LONG>)(
                        1,
                        client.right -
                            client.left)),
                static_cast<UINT>(
                    (std::max<LONG>)(
                        1,
                        client.bottom -
                            client.top)));

        if (FAILED(
                d2dFactory_->
                    CreateHwndRenderTarget(
                        D2D1::
                            RenderTargetProperties(),
                        D2D1::
                            HwndRenderTargetProperties(
                                hwnd_,
                                size),
                        &renderTarget_))) {
            return false;
        }

        if (FAILED(
                renderTarget_->
                    CreateSolidColorBrush(
                        D2D1::ColorF(
                            0.92f,
                            0.90f,
                            0.84f,
                            1.0f),
                        &textBrush_))) {

            ReleaseDeviceResources();
            return false;
        }

        return true;
    }

    void ReleaseDeviceResources() {
        if (textBrush_ != nullptr) {
            textBrush_->Release();
            textBrush_ = nullptr;
        }

        if (renderTarget_ != nullptr) {
            renderTarget_->Release();
            renderTarget_ = nullptr;
        }
    }

    void Paint() {
        PAINTSTRUCT ps{};
        BeginPaint(
            hwnd_,
            &ps);

        if (EnsureDeviceResources()) {
            renderTarget_->BeginDraw();

            renderTarget_->Clear(
                D2D1::ColorF(
                    0.055f,
                    0.055f,
                    0.052f,
                    1.0f));

            const wchar_t title[] =
                L"IphoxAI";

            renderTarget_->DrawText(
                title,
                ARRAYSIZE(title) - 1,
                textFormat_,
                D2D1::RectF(
                    48.0f,
                    38.0f,
                    900.0f,
                    94.0f),
                textBrush_);

            IDWriteTextFormat*
                statusFormat = nullptr;

            if (SUCCEEDED(
                    dwriteFactory_->
                        CreateTextFormat(
                            L"Segoe UI Variable Text",
                            nullptr,
                            DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL,
                            15.0f,
                            L"it-IT",
                            &statusFormat))) {

                renderTarget_->DrawText(
                    coreStatus_.c_str(),
                    static_cast<UINT32>(
                        coreStatus_.size()),
                    statusFormat,
                    D2D1::RectF(
                        50.0f,
                        98.0f,
                        980.0f,
                        140.0f),
                    textBrush_);

                statusFormat->Release();
            }

            const auto hr =
                renderTarget_->EndDraw();

            if (hr ==
                D2DERR_RECREATE_TARGET) {

                ReleaseDeviceResources();
            }
        }

        EndPaint(
            hwnd_,
            &ps);
    }

    void StartCore() {
        coreStatus_ =
            L"Avvio IphoxCore...";

        UpdateChatControls();

        if (!coreProcess_.StartSiblingCore()) {
            coreStatus_ =
                L"IphoxCore: avvio fallito";

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE);
            return;
        }

        iphox::ipc::Frame hello;
        hello.header.type =
            iphox::ipc::MessageType::Hello;
        hello.header.requestId =
            nextRequestId_++;

        const auto helloAck =
            iphox::runtime::CoreRpcClient::
                Request(hello, 5000);

        if (!helloAck.has_value() ||
            helloAck->header.type !=
                iphox::ipc::MessageType::Hello ||
            (helloAck->header.flags &
                iphox::ipc::kFlagError) != 0) {

            coreStatus_ =
                L"IphoxCore: hello non valido";

            coreProcess_.Close();
            UpdateChatControls();

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE);
            return;
        }

        const auto identity =
            iphox::ipc::FromPayload(
                helloAck->payload);

        if (identity !=
            "IphoxCore Native R0") {

            coreStatus_ =
                L"IphoxCore: identità inattesa";

            coreProcess_.Close();
            UpdateChatControls();

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE);
            return;
        }

        iphox::ipc::Frame ping;
        ping.header.type =
            iphox::ipc::MessageType::Ping;
        ping.header.requestId =
            nextRequestId_++;

        const auto pong =
            iphox::runtime::CoreRpcClient::
                Request(ping, 5000);

        if (!pong.has_value() ||
            pong->header.type !=
                iphox::ipc::MessageType::Pong ||
            (pong->header.flags &
                iphox::ipc::kFlagError) != 0) {

            coreStatus_ =
                L"IphoxCore: ping/pong non valido";

            coreProcess_.Close();
            UpdateChatControls();

            InvalidateRect(
                hwnd_,
                nullptr,
                FALSE);
            return;
        }

        coreReady_ = true;
        runtimeStatus_ =
            L"llama.cpp: verifica in corso";

        coreStatus_ =
            L"IphoxCore connesso · Native C++ · " +
            runtimeStatus_;

        AppendTranscript(
            L"Sistema",
            L"Core connesso.");

        SetTimer(
            hwnd_,
            kRuntimeTimerId,
            5000,
            nullptr);

        ProbeRuntimeAsync();

        UpdateChatControls();

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE);

        SetFocus(input_);
    }

    void ProbeRuntimeAsync() {
        if (!coreReady_ ||
            chatBusy_.load() ||
            runtimeProbeBusy_.load() ||
            shuttingDown_.load()) {
            return;
        }

        if (runtimeThread_.joinable()) {
            runtimeThread_.join();
        }

        runtimeProbeBusy_.store(true);

        const auto requestId =
            nextRequestId_++;

        runtimeThread_ =
            std::thread(
                [this, requestId] {
                    auto result =
                        std::make_unique<
                            RuntimeUiResult>();

                    iphox::ipc::Frame frame;
                    frame.header.type =
                        iphox::ipc::MessageType::
                            RuntimeStatus;
                    frame.header.requestId =
                        requestId;

                    const auto response =
                        iphox::runtime::
                            CoreRpcClient::Request(
                                frame,
                                1500);

                    if (!response.has_value()) {
                        result->text =
                            L"llama.cpp: non raggiungibile";
                    } else if (
                        response->header.type ==
                            iphox::ipc::MessageType::
                                RuntimeStatus &&
                        (response->header.flags &
                            iphox::ipc::kFlagError) == 0) {

                        const auto utf8 =
                            iphox::ipc::FromPayload(
                                response->payload);

                        const auto wide =
                            iphox::foundation::
                                Utf8ToWide(
                                    utf8);

                        result->text =
                            wide.has_value()
                            ? L"llama.cpp: " + *wide
                            : L"llama.cpp: stato non valido";
                    } else {
                        result->text =
                            L"llama.cpp: probe fallita";
                    }

                    if (shuttingDown_.load()) {
                        return;
                    }

                    auto* raw =
                        result.release();

                    if (!PostMessageW(
                            hwnd_,
                            kRuntimeResultMessage,
                            0,
                            reinterpret_cast<LPARAM>(
                                raw))) {
                        delete raw;
                    }
                });
    }

    void HandleRuntimeResult(
        RuntimeUiResult* rawResult) {

        std::unique_ptr<RuntimeUiResult>
            result{rawResult};

        runtimeProbeBusy_.store(false);

        if (!result ||
            shuttingDown_.load()) {
            return;
        }

        const bool changed =
            runtimeStatus_ != result->text;

        runtimeStatus_ =
            result->text;

        coreStatus_ =
            L"IphoxCore connesso · Native C++ · " +
            runtimeStatus_;

        if (changed) {
            AppendTranscript(
                L"Sistema",
                runtimeStatus_);
        }

        InvalidateRect(
            hwnd_,
            nullptr,
            FALSE);
    }

    void ClearConversation() {
        if (!coreReady_ ||
            chatBusy_.load() ||
            shuttingDown_.load()) {
            return;
        }

        iphox::ipc::Frame clear;
        clear.header.type =
            iphox::ipc::MessageType::
                ChatClear;
        clear.header.requestId =
            nextRequestId_++;

        const auto response =
            iphox::runtime::CoreRpcClient::
                Request(
                    clear,
                    1500);

        if (!response.has_value() ||
            response->header.type !=
                iphox::ipc::MessageType::
                    ChatClear ||
            (response->header.flags &
                iphox::ipc::kFlagError) != 0) {

            AppendTranscript(
                L"Sistema",
                L"Impossibile azzerare il contesto.");
            return;
        }

        SetWindowTextW(
            transcript_,
            L"");

        AppendTranscript(
            L"Sistema",
            L"Nuova chat. Contesto Core azzerato.");

        SetFocus(input_);
    }

    void SubmitChat() {
        if (!coreReady_ ||
            chatBusy_.load() ||
            shuttingDown_.load()) {
            return;
        }

        const int length =
            GetWindowTextLengthW(
                input_);

        if (length <= 0) {
            return;
        }

        std::wstring wide(
            static_cast<std::size_t>(
                length + 1),
            L'\0');

        const int copied =
            GetWindowTextW(
                input_,
                wide.data(),
                length + 1);

        if (copied <= 0) {
            return;
        }

        wide.resize(
            static_cast<std::size_t>(
                copied));

        const auto utf8 =
            iphox::foundation::
                WideToUtf8(wide);

        if (!utf8.has_value() ||
            utf8->empty()) {

            AppendTranscript(
                L"Sistema",
                L"Testo input non valido.");
            return;
        }

        if (chatThread_.joinable()) {
            chatThread_.join();
        }

        const auto requestId =
            nextRequestId_++;

        activeChatRequestId_ =
            requestId;

        chatBusy_.store(true);

        SetWindowTextW(
            input_,
            L"");

        AppendTranscript(
            L"Tu",
            wide);

        AppendTranscript(
            L"IphoxAI",
            L"Generazione in corso…");

        UpdateChatControls();

        chatThread_ =
            std::thread(
                [this,
                 requestId,
                 prompt = *utf8] {

                    auto result =
                        std::make_unique<
                            ChatUiResult>();

                    result->requestId =
                        requestId;

                    iphox::ipc::Frame frame;
                    frame.header.type =
                        iphox::ipc::MessageType::
                            ChatSubmit;
                    frame.header.requestId =
                        requestId;

                    try {
                        frame.payload =
                            iphox::chat::
                                ChatCodec::Encode(
                                    {
                                        .text =
                                            prompt
                                    });
                    } catch (...) {
                        result->text =
                            L"Payload chat non valido.";

                        PostChatResult(
                            std::move(result));
                        return;
                    }

                    const auto response =
                        iphox::runtime::
                            CoreRpcClient::Request(
                                frame,
                                5000);

                    if (!response.has_value()) {
                        result->text =
                            L"Connessione al Core interrotta.";

                        PostChatResult(
                            std::move(result));
                        return;
                    }

                    if (response->header.type ==
                            iphox::ipc::MessageType::
                                ChatStatus &&
                        (response->header.flags &
                            iphox::ipc::kFlagError) ==
                            0) {

                        const auto utf8Text =
                            iphox::ipc::FromPayload(
                                response->payload);

                        const auto wideText =
                            iphox::foundation::
                                Utf8ToWide(
                                    utf8Text);

                        if (!wideText.has_value()) {
                            result->text =
                                L"Risposta UTF-8 non valida.";

                            PostChatResult(
                                std::move(result));
                            return;
                        }

                        result->ok = true;
                        result->text =
                            *wideText;

                        PostChatResult(
                            std::move(result));
                        return;
                    }

                    std::wstring error =
                        L"Errore Core";

                    if (response->header.type ==
                        iphox::ipc::MessageType::
                            Error) {

                        const auto code =
                            iphox::ipc::FromPayload(
                                response->payload);

                        const auto wideCode =
                            iphox::foundation::
                                Utf8ToWide(code);

                        if (wideCode.has_value() &&
                            !wideCode->empty()) {

                            error += L": ";
                            error += *wideCode;
                        }
                    }

                    result->text =
                        std::move(error);

                    PostChatResult(
                        std::move(result));
                });
    }

    void PostChatResult(
        std::unique_ptr<ChatUiResult> result) {

        if (shuttingDown_.load()) {
            return;
        }

        auto* raw =
            result.release();

        if (!PostMessageW(
                hwnd_,
                kChatResultMessage,
                0,
                reinterpret_cast<LPARAM>(
                    raw))) {

            delete raw;
        }
    }

    void HandleChatResult(
        ChatUiResult* rawResult) {

        std::unique_ptr<ChatUiResult>
            result{rawResult};

        if (!result) {
            return;
        }

        if (result->requestId !=
            activeChatRequestId_) {
            return;
        }

        RemoveGeneratingMarker();

        AppendTranscript(
            result->ok
                ? L"IphoxAI"
                : L"Sistema",
            result->text);

        chatBusy_.store(false);
        UpdateChatControls();

        SetFocus(input_);
    }

    void RemoveGeneratingMarker() {
        constexpr wchar_t marker[] =
            L"IphoxAI: Generazione in corso…\r\n\r\n";

        const int length =
            GetWindowTextLengthW(
                transcript_);

        if (length <= 0) {
            return;
        }

        std::wstring text(
            static_cast<std::size_t>(
                length + 1),
            L'\0');

        const int copied =
            GetWindowTextW(
                transcript_,
                text.data(),
                length + 1);

        if (copied <= 0) {
            return;
        }

        text.resize(
            static_cast<std::size_t>(
                copied));

        const auto position =
            text.rfind(marker);

        if (position ==
            std::wstring::npos) {
            return;
        }

        SendMessageW(
            transcript_,
            EM_SETSEL,
            static_cast<WPARAM>(
                position),
            static_cast<LPARAM>(
                position +
                ARRAYSIZE(marker) -
                1));

        SendMessageW(
            transcript_,
            EM_REPLACESEL,
            FALSE,
            reinterpret_cast<LPARAM>(
                L""));
    }

    void AppendTranscript(
        const std::wstring& role,
        const std::wstring& text) {

        if (transcript_ == nullptr) {
            return;
        }

        constexpr int kMaxTranscriptChars =
            262144;

        int length =
            GetWindowTextLengthW(
                transcript_);

        if (length >
            kMaxTranscriptChars) {

            SendMessageW(
                transcript_,
                EM_SETSEL,
                0,
                65536);

            SendMessageW(
                transcript_,
                EM_REPLACESEL,
                FALSE,
                reinterpret_cast<LPARAM>(
                    L""));

            length =
                GetWindowTextLengthW(
                    transcript_);
        }

        std::wstring block;
        block.reserve(
            role.size() +
            text.size() +
            8);

        block += role;
        block += L": ";
        block += text;
        block += L"\r\n\r\n";

        SendMessageW(
            transcript_,
            EM_SETSEL,
            length,
            length);

        SendMessageW(
            transcript_,
            EM_REPLACESEL,
            FALSE,
            reinterpret_cast<LPARAM>(
                block.c_str()));

        SendMessageW(
            transcript_,
            EM_SCROLLCARET,
            0,
            0);
    }

    void UpdateChatControls() {
        const BOOL enabled =
            coreReady_ &&
            !chatBusy_.load() &&
            !shuttingDown_.load();

        if (input_ != nullptr) {
            EnableWindow(
                input_,
                enabled);
        }

        if (sendButton_ != nullptr) {
            EnableWindow(
                sendButton_,
                enabled);
        }
    }

    void ShutdownCore() noexcept {
        if (coreShutdown_) {
            return;
        }

        coreShutdown_ = true;
        shuttingDown_.store(true);

        if (hwnd_ != nullptr) {
            KillTimer(
                hwnd_,
                kRuntimeTimerId);
        }

        UpdateChatControls();

        if (coreReady_ &&
            !chatBusy_.load()) {

            iphox::ipc::Frame shutdown;
            shutdown.header.type =
                iphox::ipc::MessageType::
                    CoreShutdown;
            shutdown.header.requestId =
                nextRequestId_++;

            (void)iphox::runtime::
                CoreRpcClient::Request(
                    shutdown,
                    1000);

            (void)coreProcess_.
                WaitForExit(1000);
        }

        // Closing the Job Object kills a Core that is still
        // blocked in generation or IPC. This is intentional:
        // the UI owner must never leave a Core orphan behind.
        coreProcess_.Close();

        if (chatThread_.joinable()) {
            chatThread_.join();
        }

        if (runtimeThread_.joinable()) {
            runtimeThread_.join();
        }

        chatBusy_.store(false);
        runtimeProbeBusy_.store(false);
        coreReady_ = false;
    }

    void AddTrayIcon() {
        if (trayAdded_ ||
            hwnd_ == nullptr) {
            return;
        }

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayId;
        data.uFlags =
            NIF_MESSAGE |
            NIF_ICON |
            NIF_TIP |
            NIF_SHOWTIP;

        data.uCallbackMessage =
            kTrayMessage;

        data.hIcon =
            LoadIconW(
                nullptr,
                IDI_APPLICATION);

        wcscpy_s(
            data.szTip,
            L"IphoxAI");

        if (Shell_NotifyIconW(
                NIM_ADD,
                &data)) {

            data.uVersion =
                NOTIFYICON_VERSION_4;

            Shell_NotifyIconW(
                NIM_SETVERSION,
                &data);

            trayAdded_ = true;
        }
    }

    void RemoveTrayIcon() {
        if (!trayAdded_ ||
            hwnd_ == nullptr) {
            return;
        }

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayId;

        Shell_NotifyIconW(
            NIM_DELETE,
            &data);

        trayAdded_ = false;
    }

    void RestoreFromTray() {
        ShowWindow(
            hwnd_,
            SW_SHOW);

        ShowWindow(
            hwnd_,
            SW_RESTORE);

        SetForegroundWindow(
            hwnd_);

        if (input_ != nullptr &&
            !chatBusy_.load()) {
            SetFocus(input_);
        }
    }

    void ShowTrayMenu() {
        POINT point{};
        GetCursorPos(&point);

        HMENU menu =
            CreatePopupMenu();

        if (menu == nullptr) {
            return;
        }

        AppendMenuW(
            menu,
            MF_STRING,
            kTrayOpen,
            L"Apri IphoxAI");

        AppendMenuW(
            menu,
            MF_STRING,
            kTrayNewChat,
            L"Nuova chat");

        AppendMenuW(
            menu,
            MF_SEPARATOR,
            0,
            nullptr);

        AppendMenuW(
            menu,
            MF_STRING,
            kTrayExit,
            L"Esci");

        SetForegroundWindow(
            hwnd_);

        TrackPopupMenu(
            menu,
            TPM_RIGHTBUTTON |
                TPM_BOTTOMALIGN |
                TPM_LEFTALIGN,
            point.x,
            point.y,
            0,
            hwnd_,
            nullptr);

        DestroyMenu(menu);
    }

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND transcript_{};
    HWND input_{};
    HWND sendButton_{};

    ID2D1Factory* d2dFactory_{};
    ID2D1HwndRenderTarget*
        renderTarget_{};
    ID2D1SolidColorBrush*
        textBrush_{};

    IDWriteFactory* dwriteFactory_{};
    IDWriteTextFormat* textFormat_{};

    HBRUSH controlBrush_{};
    HFONT uiFont_{};

    iphox::runtime::CoreProcessHost
        coreProcess_;

    std::thread chatThread_;
    std::thread runtimeThread_;

    std::wstring coreStatus_{
        L"IphoxCore non inizializzato"
    };

    std::wstring runtimeStatus_{
        L"llama.cpp: non verificato"
    };

    std::uint64_t nextRequestId_{1};
    std::uint64_t activeChatRequestId_{};

    std::atomic_bool chatBusy_{false};
    std::atomic_bool runtimeProbeBusy_{false};
    std::atomic_bool shuttingDown_{false};

    bool coreReady_{};
    bool coreShutdown_{};
    bool trayAdded_{};
};

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand) {

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    iphox::runtime::SingleInstanceGuard
        instanceGuard{
            L"Local\\IphoxAI.Native.R0.Instance"
        };

    if (!instanceGuard.Acquired()) {
        const HWND existing =
            FindWindowW(
                kWindowClass,
                kWindowTitle);

        if (existing != nullptr) {
            ShowWindow(
                existing,
                SW_SHOW);

            ShowWindow(
                existing,
                SW_RESTORE);

            SetForegroundWindow(
                existing);
        }

        return 0;
    }

    NativeWindow window;

    if (!window.Initialize(
            instance,
            showCommand)) {

        MessageBoxW(
            nullptr,
            L"Impossibile inizializzare "
            L"IphoxAI Native.",
            L"IphoxAI",
            MB_OK |
                MB_ICONERROR);

        return 1;
    }

    return window.Run();
}
