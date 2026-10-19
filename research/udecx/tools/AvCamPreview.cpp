/*
 * AvCam Preview: functional Windows desktop capture application.
 * The physical Logitech C270 (or OBS virtual source) is rendered locally
 * through Windows Media Foundation. This is NOT a virtual USB camera and
 * can optionally upload MJPEG to an existing UdeCx controller.
 * USB upload is OFF by default and never installs or enables a driver.
 *
 * No installation, elevation, registry changes, or persistent image output.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../../../feeder/src/MediaFoundationFrameSource.h"
#include "../../../shared/VirtualCameraProtocol.h"
#include "../AvCamUdeUploadProtocol.h"
#include "AvCamUdeMjpegEncoder.h"

namespace {
constexpr UINT kPreviewReady = WM_APP + 1u;
constexpr UINT_PTR kStatusTimer = 1u;
constexpr int kSourceId = 101;
constexpr int kStartId = 102;
constexpr int kStopId = 103;
constexpr int kStatusId = 104;
constexpr int kUsbId = 105;
constexpr int kRefreshId = 106;
constexpr wchar_t kControllerPath[] = LR"(\\.\AvCamUdeController0)";

struct Controller {
    HWND window = nullptr;
    HWND sourceCombo = nullptr;
    HWND startButton = nullptr;
    HWND stopButton = nullptr;
    HWND status = nullptr;
    HWND usbCheck = nullptr;
    HWND refreshButton = nullptr;
    std::vector<std::wstring> availableSources;
    std::thread worker;
    std::mutex frameMutex;
    std::vector<unsigned char> latest;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    std::atomic<bool> frameAvailable{false};
    std::atomic<bool> captureFailed{false};
    std::atomic<bool> usbUnavailable{false};
    std::atomic<bool> encodeFailed{false};
    std::atomic<DWORD> controllerError{ERROR_SUCCESS};
    std::atomic<std::uint64_t> captured{0};
    std::atomic<std::uint64_t> encoded{0};
    std::atomic<std::uint64_t> uploaded{0};
    std::atomic<std::uint64_t> busy{0};
    std::wstring selectedName;
    std::wstring selectedUsbId;
    bool upload = false;

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

void CaptureThread(std::wstring name, std::wstring usbId, bool upload) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
        g.captureFailed.store(true);
        g.running.store(false);
        PostMessageW(g.window, kPreviewReady, 0, 0);
        return;
    }

    MediaFoundationFrameSource source(std::move(name), std::move(usbId));
    const bool opened = source.Start();
    if (!opened) {
        g.captureFailed.store(true);
        g.running.store(false);
        PostMessageW(g.window, kPreviewReady, 0, 0);
        CoUninitialize();
        return;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    HANDLE controller = INVALID_HANDLE_VALUE;
    if (upload) {
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wic)))) {
            g.encodeFailed.store(true);
            g.usbUnavailable.store(true);
        } else {
            controller = CreateFileW(
                kControllerPath, GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (controller == INVALID_HANDLE_VALUE) {
                g.controllerError.store(GetLastError());
                g.usbUnavailable.store(true);
            }
        }
    }
    std::vector<unsigned char> jpeg;
    std::vector<unsigned char> packet;
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
        if (controller != INVALID_HANDLE_VALUE) {
            const bool encoded = AvCamUde::EncodeBgraToMjpeg(
                wic.Get(), sample.data(), AVCAM_FRAME_WIDTH,
                AVCAM_FRAME_HEIGHT, AVCAM_FRAME_STRIDE_BGRA32, jpeg);
            if (!encoded) {
                g.encodeFailed.store(true);
                g.usbUnavailable.store(true);
                CloseHandle(controller);
                controller = INVALID_HANDLE_VALUE;
            } else {
                ++g.encoded;
                AVCAM_UDE_UPLOAD_HEADER header{};
                header.Magic = AVCAM_UDE_UPLOAD_MAGIC;
                header.Version = AVCAM_UDE_UPLOAD_VERSION;
                header.Width = AVCAM_UDE_UPLOAD_WIDTH;
                header.Height = AVCAM_UDE_UPLOAD_HEIGHT;
                header.FrameBytes = static_cast<unsigned>(jpeg.size());
                packet.resize(sizeof(header) + jpeg.size());
                memcpy(packet.data(), &header, sizeof(header));
                memcpy(packet.data() + sizeof(header),
                       jpeg.data(), jpeg.size());
                DWORD returned = 0;
                if (DeviceIoControl(controller, IOCTL_AVCAM_UDE_SUBMIT_MJPEG,
                        packet.data(), static_cast<DWORD>(packet.size()),
                        nullptr, 0, &returned, nullptr)) {
                    ++g.uploaded;
                } else {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BUSY) {
                        // Drop only this upload. Keep preview frame delivery active.
                        ++g.busy;
                    } else {
                        g.controllerError.store(error);
                        g.usbUnavailable.store(true);
                        CloseHandle(controller);
                        controller = INVALID_HANDLE_VALUE;
                    }
                }
            }
        }
        if (!PostMessageW(g.window, kPreviewReady, 0, 0)) {
            break;
        }
    }
    if (controller != INVALID_HANDLE_VALUE) CloseHandle(controller);
    source.Stop();
    CoUninitialize();
    g.running.store(false);
    PostMessageW(g.window, kPreviewReady, 0, 0);
}

void RefreshSources() {
    if (g.running.load()) return;
    const LRESULT selected = SendMessageW(g.sourceCombo, CB_GETCURSEL, 0, 0);
    std::wstring oldName;
    if (selected >= 0 &&
        static_cast<std::size_t>(selected) < g.availableSources.size()) {
        oldName = g.availableSources[static_cast<std::size_t>(selected)];
    }
    std::vector<std::wstring> sources = EnumerateMediaFoundationVideoSources();
    /* Keep supported defaults available when devices are unplugged;
     * clicking Start will report a normal capture error in that case.
     */
    for (const std::wstring& builtIn :
         {std::wstring(L"Logi C270 HD WebCam"),
          std::wstring(L"OBS Virtual Camera")}) {
        if (std::find(sources.begin(), sources.end(), builtIn) ==
            sources.end()) {
            sources.push_back(builtIn);
        }
    }
    g.availableSources = std::move(sources);
    SendMessageW(g.sourceCombo, CB_RESETCONTENT, 0, 0);
    int chosen = 0;
    for (std::size_t i = 0; i < g.availableSources.size(); ++i) {
        const std::wstring& candidate = g.availableSources[i];
        SendMessageW(g.sourceCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(candidate.c_str()));
        if (!oldName.empty() && candidate == oldName) {
            chosen = static_cast<int>(i);
        } else if (oldName.empty() && candidate == L"Logi C270 HD WebCam") {
            chosen = static_cast<int>(i);
        }
    }
    SendMessageW(g.sourceCombo, CB_SETCURSEL,
                 static_cast<WPARAM>(chosen), 0);
    SetControlStatus(L"Выберите камеру и нажмите «Начать»");
}

void StartCapture() {
    g.Stop();
    const LRESULT idx = SendMessageW(g.sourceCombo, CB_GETCURSEL, 0, 0);
    if (idx < 0 ||
        static_cast<std::size_t>(idx) >= g.availableSources.size()) {
        SetControlStatus(L"Источник не выбран");
        return;
    }
    g.selectedName = g.availableSources[static_cast<std::size_t>(idx)];
    g.selectedUsbId = g.selectedName == L"Logi C270 HD WebCam" ?
        L"VID_046D&PID_0825" : L"";
    g.upload = SendMessageW(g.usbCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.stop.store(false);
    g.captureFailed.store(false);
    g.usbUnavailable.store(false);
    g.encodeFailed.store(false);
    g.controllerError.store(ERROR_SUCCESS);
    g.frameAvailable.store(false);
    g.captured.store(0);
    g.encoded.store(0);
    g.uploaded.store(0);
    g.busy.store(0);
    {
        std::lock_guard<std::mutex> lock(g.frameMutex);
        g.latest.clear();
    }
    g.running.store(true);
    EnableWindow(g.startButton, FALSE);
    EnableWindow(g.stopButton, TRUE);
    EnableWindow(g.sourceCombo, FALSE);
    EnableWindow(g.usbCheck, FALSE);
    EnableWindow(g.refreshButton, FALSE);
    SetControlStatus(L"Подключение к камере...");
    g.worker = std::thread(CaptureThread, g.selectedName, g.selectedUsbId, g.upload);
    InvalidateRect(g.window, nullptr, FALSE);
}

void StopCapture() {
    g.Stop();
    EnableWindow(g.startButton, TRUE);
    EnableWindow(g.stopButton, FALSE);
    EnableWindow(g.sourceCombo, TRUE);
    EnableWindow(g.usbCheck, TRUE);
    EnableWindow(g.refreshButton, TRUE);
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
        EnableWindow(g.usbCheck, TRUE);
        EnableWindow(g.refreshButton, TRUE);
        SetControlStatus(L"Ошибка захвата: камера отсутствует, занята или формат неподдерживаем");
    } else if (g.running.load()) {
        wchar_t text[220]{};
        if (g.upload && g.usbUnavailable.load()) {
            if (g.encodeFailed.load()) {
                swprintf_s(text, L"USB: ошибка MJPEG. Локальный просмотр: %.1f FPS", fps);
            } else if (g.controllerError.load() == ERROR_ACCESS_DENIED) {
                swprintf_s(text, L"UdeCx: нет прав (нужен администратор). Просмотр: %.1f FPS", fps);
            } else {
                swprintf_s(text, L"UdeCx недоступен (Win32 %lu). Просмотр: %.1f FPS",
                    static_cast<unsigned long>(g.controllerError.load()), fps);
            }
        } else if (g.upload) {
            swprintf_s(text, L"Кадров: %llu | %.1f FPS | MJPEG: %llu | UdeCx: %llu | busy: %llu",
                static_cast<unsigned long long>(frames), fps,
                static_cast<unsigned long long>(g.encoded.load()),
                static_cast<unsigned long long>(g.uploaded.load()),
                static_cast<unsigned long long>(g.busy.load()));
        } else {
            swprintf_s(text, L"Локальный захват: %llu кадров | %.1f FPS | 1280 x 720",
                static_cast<unsigned long long>(frames), fps);
        }
        SetControlStatus(text);
    }
}

void PaintPreview(HWND hwnd, HDC hdc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT area{16, 115, client.right - 16, client.bottom - 42};
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
        g.refreshButton = CreateWindowW(
            L"BUTTON", L"Обновить камеры", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 47, 231, 29, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshId)),
            instance, nullptr);
        g.startButton = CreateWindowW(
            L"BUTTON", L"Начать", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 12, 110, 29, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartId)), instance, nullptr);
        g.stopButton = CreateWindowW(
            L"BUTTON", L"Остановить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            646, 12, 115, 29, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopId)), instance, nullptr);
        EnableWindow(g.stopButton, FALSE);
        g.usbCheck = CreateWindowW(
            L"BUTTON", L"Передавать MJPEG в UdeCx (экспериментально)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            16, 53, 460, 24, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUsbId)),
            instance, nullptr);
        SendMessageW(g.usbCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        g.status = CreateWindowW(
            L"STATIC", L"Остановлено", WS_CHILD | WS_VISIBLE,
            16, 84, 820, 23, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)),
            instance, nullptr);
        RefreshSources();
        SetTimer(hwnd, kStatusTimer, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kRefreshId && HIWORD(wParam) == BN_CLICKED) {
            RefreshSources();
            return 0;
        }
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
    const HRESULT uiCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(uiCom)) return 3;
    HWND window = CreateWindowExW(
        0, cls, L"AvCam Preview | Logitech C270 / OBS", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 930, 670, nullptr, nullptr, instance, nullptr);
    if (!window) {
        CoUninitialize();
        return 2;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
