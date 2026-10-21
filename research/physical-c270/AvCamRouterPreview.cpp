// Independent native Windows preview of AvCam application-space IPC.
// Never registers a camera, hooks Chrome, or touches Logitech device settings.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <chrono>
#include <cwchar>
#include <cstdint>
#include <string>
#include <utility>
#include "AvCamRouterIpc.h"
#include "AvCamPreviewSelection.h"

namespace {
using avcam::ipc::Channel;
using avcam::ipc::Snapshot;
using avcam::preview::Mode;
using avcam::preview::Source;
constexpr UINT_PTR kTimer = 1;
constexpr UINT kRefreshMs = 33;

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Feed {
    explicit Feed(Channel c) : channel(c) {}
    Channel channel;
    avcam::ipc::Subscriber reader;
    Snapshot last;
    std::uint64_t sequence = 0;
    std::uint64_t openedMs = 0;
    bool attached = false;

    void Poll(std::uint64_t now) {
        if (!attached) {
            attached = reader.Start(channel);
            if (attached) { openedMs = now; sequence = 0; }
            else return;
        }
        // Explicit invalidation must remove OBS immediately, not after TTL.
        if (!reader.IsActive()) {
            last.bgra.clear();
            sequence = 0;
            if (now >= openedMs && now - openedMs > 2000) Detach();
            return;
        }
        Snapshot next;
        if (reader.TryRead(next, sequence)) {
            sequence = next.sequence;
            last = std::move(next);
        }
        if (!last.bgra.empty() &&
            !avcam::preview::Fresh(now, last.capturedMs)) {
            // Reattach to a restarted router instead of pinning an orphaned
            // file mapping whose original producer no longer exists.
            Detach();
        }
    }
    bool Fresh(std::uint64_t now) const {
        return attached && reader.IsActive() &&
            last.bgra.size() == avcam::ipc::FrameBytes &&
            avcam::preview::Fresh(now, last.capturedMs);
    }
    void Detach() {
        reader.Stop();
        last.bgra.clear();
        sequence = 0;
        attached = false;
    }
};

struct State {
    explicit State(Mode m) : mode(m), raw(Channel::Raw), obs(Channel::Obs) {}
    Mode mode;
    Feed raw;
    Feed obs;
};

void Label(HDC dc, const RECT& rect, const wchar_t* title) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 239, 245));
    RECT r = rect;
    DrawTextW(dc, title, -1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

void DrawFeed(HDC dc, const RECT& area, const Snapshot* snapshot,
              const wchar_t* title, const wchar_t* missing) {
    const HBRUSH bg = CreateSolidBrush(RGB(19, 23, 31));
    FillRect(dc, &area, bg);
    DeleteObject(bg);
    RECT caption = area;
    caption.bottom = caption.top + 28;
    Label(dc, caption, title);
    RECT video = area;
    video.top += 30;
    video.left += 8;
    video.right -= 8;
    video.bottom -= 8;
    const int availableW = video.right - video.left;
    const int availableH = video.bottom - video.top;
    if (availableW <= 0 || availableH <= 0) return;
    if (!snapshot || snapshot->bgra.size() != avcam::ipc::FrameBytes) {
        Label(dc, video, missing);
        return;
    }
    // Maintain 16:9 aspect; BGRA data is bottom-up interpreted as a top-down DIB.
    int dstW = availableW;
    int dstH = static_cast<int>(
        static_cast<std::int64_t>(dstW) * avcam::ipc::Height / avcam::ipc::Width);
    if (dstH > availableH) {
        dstH = availableH;
        dstW = static_cast<int>(
            static_cast<std::int64_t>(dstH) * avcam::ipc::Width / avcam::ipc::Height);
    }
    const int x = video.left + (availableW - dstW) / 2;
    const int y = video.top + (availableH - dstH) / 2;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(avcam::ipc::Width);
    info.bmiHeader.biHeight = -static_cast<LONG>(avcam::ipc::Height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, dstW, dstH, 0, 0,
        static_cast<int>(avcam::ipc::Width),
        static_cast<int>(avcam::ipc::Height),
        snapshot->bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
}

void Paint(HWND hwnd, State& state) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const HBRUSH background = CreateSolidBrush(RGB(12, 15, 22));
    FillRect(dc, &client, background);
    DeleteObject(background);
    const std::uint64_t now = NowMs();
    const bool rawFresh = state.raw.Fresh(now);
    const bool obsFresh = state.obs.Fresh(now);
    RECT display = client;
    display.bottom -= 34;
    if (state.mode == Mode::Compare) {
        RECT left = display, right = display;
        left.right = client.left + (client.right - client.left) / 2;
        right.left = left.right + 1;
        DrawFeed(dc, left, rawFresh ? &state.raw.last : nullptr,
                 L"LOGITECH C270 / RAW", L"Waiting for RAW");
        DrawFeed(dc, right, obsFresh ? &state.obs.last : nullptr,
                 L"OBS / PROCESSED", L"Waiting for OBS");
    } else {
        const Source chosen = avcam::preview::Select(rawFresh, obsFresh, state.mode);
        const Snapshot* frame = chosen == Source::Obs ? &state.obs.last :
                                chosen == Source::Raw ? &state.raw.last : nullptr;
        const wchar_t* heading = chosen == Source::Obs ? L"OUTPUT / OBS PROCESSED" :
                                 chosen == Source::Raw ? L"OUTPUT / ORIGINAL RAW" :
                                 L"OUTPUT / SOURCE UNAVAILABLE";
        DrawFeed(dc, display, frame, heading, L"Waiting for AvCam Router --publish-ipc");
    }
    RECT footer = client;
    footer.top = display.bottom;
    const wchar_t* status = state.mode == Mode::Compare
        ? L"Preview only  |  RAW and OBS independent  |  No driver or Chrome changes"
        : obsFresh ? L"OBS online  |  No physical webcam substitution"
                   : L"OBS unavailable  |  RAW fallback in output mode  |  Preview only";
    Label(dc, footer, status);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd, kTimer, kRefreshMs, nullptr);
        return 0;
    case WM_TIMER:
        if (wparam == kTimer && state) {
            const std::uint64_t now = NowMs();
            if (state->mode != Mode::Obs) state->raw.Poll(now);
            if (state->mode != Mode::Raw) state->obs.Poll(now);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
        if (state) Paint(hwnd, *state);
        else {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimer);
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int PreviewSelfTest() {
    using namespace avcam::preview;
    if (Select(true, true, Mode::Output) != Source::Obs ||
        Select(true, false, Mode::Output) != Source::Raw ||
        Select(false, false, Mode::Output) != Source::None ||
        !Fresh(2200, 200, 2000) || Fresh(2201, 200, 2000))
        return 1;
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!args) return 10;
    Mode mode = Mode::Output;
    bool foundMode = false;
    bool selfTest = false;
    bool valid = true;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(args[i], L"--self-test") == 0 && argc == 2) {
            selfTest = true;
        } else if (wcscmp(args[i], L"--mode") == 0 &&
                   i + 1 < argc && !foundMode) {
            foundMode = true;
            ++i;
            if (wcscmp(args[i], L"output") == 0) mode = Mode::Output;
            else if (wcscmp(args[i], L"compare") == 0) mode = Mode::Compare;
            else if (wcscmp(args[i], L"raw") == 0) mode = Mode::Raw;
            else if (wcscmp(args[i], L"obs") == 0) mode = Mode::Obs;
            else valid = false;
        } else {
            valid = false;
        }
    }
    LocalFree(args);
    if (!valid) {
        MessageBoxW(nullptr,
            L"Usage: AvCamRouterPreview.exe [--mode output|compare|raw|obs]\n"
            L"Or: AvCamRouterPreview.exe --self-test",
            L"AvCam Preview: invalid arguments", MB_OK | MB_ICONWARNING);
        return 9;
    }
    if (selfTest) return PreviewSelfTest();
    const wchar_t* className = L"AvCamRouterPreviewWindowV1";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) return 11;
    State state(mode);
    HWND window = CreateWindowExW(0, className,
        L"AvCam Router Preview  |  RAW / OBS", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 750, nullptr,
        nullptr, instance, &state);
    if (!window) return 12;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
