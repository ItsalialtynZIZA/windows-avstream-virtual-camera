/*
 * AvCam Preview: functional Windows desktop capture application.
 * The physical Logitech C270 (or OBS virtual source) is rendered locally
 * through Windows Media Foundation. This is NOT a virtual USB camera and
 * never opens the experimental AvCam UdeCx kernel controller.
 *
 * No installation, elevation, registry changes, or persistent image output.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../../../feeder/src/MediaFoundationFrameSource.h"
#include "../../../shared/VirtualCameraProtocol.h"

namespace {
constexpr UINT kPreviewReady = WM_APP + 1u;
constexpr UINT_PTR kStatusTimer = 1u;
constexpr int kSourceId = 101;
constexpr int kStartId = 102;
constexpr int kStopId = 103;
constexpr int kStatusId = 104;

struct Controller {
    HWND window = nullptr;
    HWND sourceCombo = nullptr;
    HWND startButton = nullptr;
    HWND stopButton = nullptr;
    HWND status = nullptr;
    std::thread worker;
    std::mutex frameMutex;
    std::vector<unsigned char> latest;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    std::atomic<bool> frameAvailable{false};
    std::atomic<bool> captureFailed{false};
    std::atomic<std::uint64_t> captured{0};
    int selected = 0;

    void Stop() {
        stop.store(true);
        if (worker.joinable()) {
            worker.join();
        }
        running.store(false);
    }
    ~Controller() {
        Stop();
    }
};
Controller g;

void SetControlStatus(const wchar_t* message) {
    if (g.status) SetWindowTextW(g.status, message);
}

void CaptureThread(int selected) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        g.captureFailed.store(true);
        g.running.store(false);
        PostMessageW(g.window, kPreviewReady, 0, 0);
        return;
    }

    const bool obs = selected == 1;
    MediaFoundationFrameSource source(
        obs ? L"OBS Virtual Camera" : L"Logi C270 HD WebCam",
        obs ? L"" : L"VID_046D&PID_0825");
    const bool opened = source.Start();
    if (!opened) {
        g.captureFailed.store(true);
        g.running.store(false);
        PostMessageW(g.window, kPreviewReady, 0, 0);
        CoUninitialize();
        return;
    }

    std::vector<unsigned char> sample(AVCAM_FRAME_BYTES_BGRA32);
    while (!g.stop.load()) {
        std::uint64_t timestamp = 0;
        if (!source.ReadFrame(sample.data(), sample.size(), timestamp)) {
            if (!g.stop.load()) g.captureFailed.store(true);
            break;
        }
        {
            std::lock_guard<std::mutex> lock(g.frameMutex);
            if (g.latest.size() != sample.size()) {
                g.latest.resize(sample.size());
            }
            memcpy(g.latest.data(), sample.data(), sample.size());
        }
        g.frameAvailable.store(true);
        ++g.captured;
        if (!PostMessageW(g.window, kPreviewReady, 0, 0)) {
            break;
        }
    }
    source.Stop();
    CoUninitialize();
    g.running.store(false);
    PostMessageW(g.window, kPreviewReady, 0, 0);
}

void StartCapture() {
    g.Stop();
    const LRESULT idx = SendMessageW(g.sourceCombo, CB_GETCURSEL, 0, 0);
    g.selected = idx == 1 ? 1 : 0;
    g.stop.store(false);
    g.captureFailed.store(false);
    g.frameAvailable.store(false);
    g.captured.store(0);
    {
        std::lock_guard<std::mutex> lock(g.frameMutex);
        g.latest.clear();
    }
    g.running.store(true);
    EnableWindow(g.startButton, FALSE);
    EnableWindow(g.stopButton, TRUE);
    EnableWindow(g.sourceCombo, FALSE);
    SetControlStatus(L"Подключение к камере...");
    g.worker = std::thread(CaptureThread, g.selected);
    InvalidateRect(g.window, nullptr, FALSE);
}

void StopCapture() {
    g.Stop();
    EnableWindow(g.startButton, TRUE);
    EnableWindow(g.stopButton, FALSE);
    EnableWindow(g.sourceCombo, TRUE);
    SetControlStatus(L"Остановлено");
    InvalidateRect(g.window, nullptr, FALSE);
}

void UpdateStatus() {
    static std::uint64_t previousFrames = 0;
    static ULONGLONG previousTick = 0;
    const ULONGLONG now = GetTickCount64();
    const std::uint64_t frames = g.captured.load();
    if (previousTick == 0u || frames < previousFrames) {
        previousFrames = frames;
        previousTick = now;
    }
    const ULONGLONG elapsed = now - previousTick;
    const double fps = elapsed > 0u ?
        1000.0 * static_cast<double>(frames - previousFrames) /
            static_cast<double>(elapsed) : 0.0;
    previousFrames = frames;
    previousTick = now;

    if (g.captureFailed.load()) {
        EnableWindow(g.startButton, TRUE);
        EnableWindow(g.stopButton, FALSE);
        EnableWindow(g.sourceCombo, TRUE);
        SetControlStatus(L"Ошибка захвата: камера не найдена или занята");
    } else if (g.running.load()) {
        wchar_t text[180]{};
        swprintf_s(text, L"Идёт захват: %llu кадров | %.1f FPS | 1280 x 720",
                   static_cast<unsigned long long>(frames), fps);
        SetControlStatus(text);
    }
}

void PaintPreview(HWND hwnd, HDC hdc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT area{16, 102, client.right - 16, client.bottom - 42};
    if (area.right <= area.left || area.bottom <= area.top) return;

    HBRUSH background = CreateSolidBrush(RGB(24, 28, 36));
    FillRect(hdc, &area, background);
    DeleteObject(background);
    std::lock_guard<std::mutex> lock(g.frameMutex);
    if (!g.frameAvailable.load() ||
        g.latest.size() != AVCAM_FRAME_BYTES_BGRA32) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(210, 216, 227));
        DrawTextW(hdc, L"Выберите источник и нажмите «Начать»", -1, &area,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    int imageWidth = width;
    int imageHeight = (imageWidth * 9) / 16;
    if (imageHeight > height) {
        imageHeight = height;
        imageWidth = (imageHeight * 16) / 9;
    }
    const int left = area.left + (width - imageWidth) / 2;
    const int top = area.top + (height - imageHeight) / 2;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(AVCAM_FRAME_WIDTH);
    info.bmiHeader.biHeight = -static_cast<LONG>(AVCAM_FRAME_HEIGHT);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, left, top, imageWidth, imageHeight,
                  0, 0, AVCAM_FRAME_WIDTH, AVCAM_FRAME_HEIGHT,
                  g.latest.data(), &info, DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g.window = hwnd;
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        CreateWindowW(L"STATIC", L"Источник видео:", WS_CHILD | WS_VISIBLE,
                      16, 15, 112, 23, hwnd, nullptr, instance, nullptr);
        g.sourceCombo = CreateWindowExW(
            0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            135, 12, 385, 180, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSourceId)), instance, nullptr);
        SendMessageW(g.sourceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Logi C270 HD WebCam (USB)"));
        SendMessageW(g.sourceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"OBS Virtual Camera"));
        SendMessageW(g.sourceCombo, CB_SETCURSEL, 0, 0);
        g.startButton = CreateWindowW(
            L"BUTTON", L"Начать", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 12, 110, 29, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartId)), instance, nullptr);
        g.stopButton = CreateWindowW(
            L"BUTTON", L"Остановить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            646, 12, 115, 29, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopId)), instance, nullptr);
        EnableWindow(g.stopButton, FALSE);
        g.status = CreateWindowW(
            L"STATIC", L"Остановлено", WS_CHILD | WS_VISIBLE,
            16, 60, 740, 28, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)),
            instance, nullptr);
        SetTimer(hwnd, kStatusTimer, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kStartId && HIWORD(wParam) == BN_CLICKED) {
            StartCapture();
            return 0;
        }
        if (LOWORD(wParam) == kStopId && HIWORD(wParam) == BN_CLICKED) {
            StopCapture();
            return 0;
        }
        break;
    case kPreviewReady:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wParam == kStatusTimer) UpdateStatus();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        PaintPreview(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        KillTimer(hwnd, kStatusTimer);
        g.Stop();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t cls[] = L"AvCamPreviewMainWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) return 1;
    HWND window = CreateWindowExW(
        0, cls, L"AvCam Preview | Logitech C270 / OBS", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 930, 670, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
