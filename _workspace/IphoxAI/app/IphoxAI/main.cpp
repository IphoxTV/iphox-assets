#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"IphoxAI.Native.Window";
constexpr wchar_t kWindowTitle[] = L"IphoxAI";
constexpr UINT kTrayMessage = WM_APP + 42;
constexpr UINT kTrayId = 1;
constexpr UINT kTrayOpen = 1001;
constexpr UINT kTrayExit = 1002;

class NativeWindow final {
public:
    ~NativeWindow() {
        RemoveTrayIcon();
        ReleaseDeviceResources();

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

    bool Initialize(HINSTANCE instance, int showCommand) {
        instance_ = instance;

        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                &d2dFactory_))) {
            return false;
        }

        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(&dwriteFactory_)))) {
            return false;
        }

        if (FAILED(dwriteFactory_->CreateTextFormat(
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
        wc.lpfnWndProc = &NativeWindow::WindowProc;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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

        AddTrayIcon();

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    int Run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
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
                reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<NativeWindow*>(
                create->lpCreateParams);

            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));

            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<NativeWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self != nullptr) {
            return self->HandleMessage(
                message,
                wParam,
                lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {

        switch (message) {
        case WM_PAINT:
            Paint();
            ValidateRect(hwnd_, nullptr);
            return 0;

        case WM_SIZE:
            if (renderTarget_ != nullptr) {
                const auto width = LOWORD(lParam);
                const auto height = HIWORD(lParam);
                renderTarget_->Resize(
                    D2D1::SizeU(width, height));
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_DPICHANGED: {
            const auto* rect =
                reinterpret_cast<RECT*>(lParam);

            SetWindowPos(
                hwnd_,
                nullptr,
                rect->left,
                rect->top,
                rect->right - rect->left,
                rect->bottom - rect->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }

        case WM_CLOSE:
            // Historical IphoxAI behavior: X hides to tray.
            ShowWindow(hwnd_, SW_HIDE);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == kTrayOpen) {
                RestoreFromTray();
                return 0;
            }
            if (LOWORD(wParam) == kTrayExit) {
                DestroyWindow(hwnd_);
                return 0;
            }
            break;

        case kTrayMessage:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                RestoreFromTray();
                return 0;
            }
            if (LOWORD(lParam) == WM_RBUTTONUP ||
                LOWORD(lParam) == WM_CONTEXTMENU) {
                ShowTrayMenu();
                return 0;
            }
            break;

        case WM_DESTROY:
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

    bool EnsureDeviceResources() {
        if (renderTarget_ != nullptr) {
            return true;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);

        const auto size = D2D1::SizeU(
            static_cast<UINT>(
                std::max<LONG>(1, client.right - client.left)),
            static_cast<UINT>(
                std::max<LONG>(1, client.bottom - client.top)));

        if (FAILED(d2dFactory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(
                    hwnd_,
                    size),
                &renderTarget_))) {
            return false;
        }

        if (FAILED(renderTarget_->CreateSolidColorBrush(
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
        BeginPaint(hwnd_, &ps);

        if (EnsureDeviceResources()) {
            renderTarget_->BeginDraw();

            renderTarget_->Clear(
                D2D1::ColorF(
                    0.055f,
                    0.055f,
                    0.052f,
                    1.0f));

            const wchar_t title[] = L"IphoxAI";
            const wchar_t status[] =
                L"Native C++ recovery core active";

            renderTarget_->DrawTextW(
                title,
                ARRAYSIZE(title) - 1,
                textFormat_,
                D2D1::RectF(
                    48.0f,
                    42.0f,
                    900.0f,
                    100.0f),
                textBrush_);

            IDWriteTextFormat* statusFormat = nullptr;
            if (SUCCEEDED(dwriteFactory_->CreateTextFormat(
                    L"Segoe UI Variable Text",
                    nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL,
                    15.0f,
                    L"it-IT",
                    &statusFormat))) {

                renderTarget_->DrawTextW(
                    status,
                    ARRAYSIZE(status) - 1,
                    statusFormat,
                    D2D1::RectF(
                        50.0f,
                        102.0f,
                        900.0f,
                        150.0f),
                    textBrush_);

                statusFormat->Release();
            }

            const auto hr = renderTarget_->EndDraw();
            if (hr == D2DERR_RECREATE_TARGET) {
                ReleaseDeviceResources();
            }
        }

        EndPaint(hwnd_, &ps);
    }

    void AddTrayIcon() {
        if (trayAdded_ || hwnd_ == nullptr) {
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
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

        wcscpy_s(data.szTip, L"IphoxAI");

        if (Shell_NotifyIconW(NIM_ADD, &data)) {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
            trayAdded_ = true;
        }
    }

    void RemoveTrayIcon() {
        if (!trayAdded_ || hwnd_ == nullptr) {
            return;
        }

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayId;

        Shell_NotifyIconW(NIM_DELETE, &data);
        trayAdded_ = false;
    }

    void RestoreFromTray() {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }

    void ShowTrayMenu() {
        POINT point{};
        GetCursorPos(&point);

        HMENU menu = CreatePopupMenu();
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
            MF_SEPARATOR,
            0,
            nullptr);
        AppendMenuW(
            menu,
            MF_STRING,
            kTrayExit,
            L"Esci");

        SetForegroundWindow(hwnd_);

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

    ID2D1Factory* d2dFactory_{};
    ID2D1HwndRenderTarget* renderTarget_{};
    ID2D1SolidColorBrush* textBrush_{};

    IDWriteFactory* dwriteFactory_{};
    IDWriteTextFormat* textFormat_{};

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

    NativeWindow window;

    if (!window.Initialize(instance, showCommand)) {
        MessageBoxW(
            nullptr,
            L"Impossibile inizializzare IphoxAI Native.",
            L"IphoxAI",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.Run();
}
