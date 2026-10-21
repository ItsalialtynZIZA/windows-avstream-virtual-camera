// Adaptation hook for the Microsoft sample Device MFT.
// Copy only fresh, 1280x720 MJPG frames. Otherwise preserve real capture.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <mutex>
#include <atomic>
#include <evntprov.h>
#include <unordered_set>
#include <vector>
#include <cstring>
#include "AvCamPhysicalWire.h"
#include "AvCamPhysicalHook.h"

namespace {
std::mutex g_modesMutex;
std::unordered_set<const void*> g_mjpegQueues;
std::mutex g_mapMutex;
HANDLE g_mapping = nullptr;
const unsigned char* g_view = nullptr;

std::once_flag g_registerOnce;
REGHANDLE g_eventHandle = 0;
// Logman: {A56F32CA-31E5-4C6D-95F2-74DB9DA141E9}
const GUID kProvider = {0xa56f32ca, 0x31e5, 0x4c6d,
    {0x95,0xf2,0x74,0xdb,0x9d,0xa1,0x41,0xe9}};
std::atomic<unsigned long long> g_sampleChecks{0};
std::atomic<unsigned long long> g_replaceSuccess{0};
std::atomic<unsigned long long> g_mapFailures{0};

void TraceRaw(const wchar_t* message) {
    std::call_once(g_registerOnce, [] {
        (void)EventRegister(&kProvider, nullptr, nullptr, &g_eventHandle);
    });
    if (g_eventHandle && message) {
        (void)EventWriteString(g_eventHandle, 4, 0, message);
    }
}

void TraceMediaType(const wchar_t* path, IMFMediaType* input,
                    IMFMediaType* output, bool enabled) {
    GUID inSubtype{}, outSubtype{};
    UINT32 inW=0, inH=0, outW=0, outH=0;
    if (input) {
        (void)input->GetGUID(MF_MT_SUBTYPE, &inSubtype);
        (void)MFGetAttributeSize(input, MF_MT_FRAME_SIZE, &inW, &inH);
    }
    if (output) {
        (void)output->GetGUID(MF_MT_SUBTYPE, &outSubtype);
        (void)MFGetAttributeSize(output, MF_MT_FRAME_SIZE, &outW, &outH);
    }
    wchar_t message[320]{};
    (void)swprintf_s(message, L"%s active=%d input=%08X/%ux%u output=%08X/%ux%u",
        path, enabled ? 1 : 0,
        inSubtype.Data1, inW, inH, outSubtype.Data1, outW, outH);
    TraceRaw(message);
}


bool IsMjpeg1280(IMFMediaType* type) {
    if (!type) return false;
    GUID subtype{};
    UINT32 width = 0, height = 0;
    return SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
        subtype == MFVideoFormat_MJPG &&
        SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) &&
        width == AvCamPhysical::kWidth &&
        height == AvCamPhysical::kHeight;
}

bool EnsureMap() {
    std::lock_guard<std::mutex> lock(g_mapMutex);
    if (g_view) return true;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
                                      AvCamPhysical::kMappingName);
    if (!mapping) {
        const auto attempts = ++g_mapFailures;
        if (attempts <= 3 || attempts % 300 == 0) {
            wchar_t msg[128]{};
            (void)swprintf_s(msg, L"OpenFileMapping failed error=%lu attempts=%llu",
                GetLastError(), attempts);
            TraceRaw(msg);
        }
        return false;
    }
    const auto* view = static_cast<const unsigned char*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, AvCamPhysical::kTotalBytes));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    g_mapping = mapping;
    g_view = view;
    return true;
}
bool ReadFreshJpeg(std::vector<unsigned char>& dest) {
    if (!EnsureMap()) return false;
    const auto* header =
        reinterpret_cast<const AvCamPhysical::FrameHeader*>(g_view);
    const LONG before = header->sequence;
    if ((before & 1) != 0) return false;
    MemoryBarrier();
    if (header->magic != AvCamPhysical::kMagic ||
        header->version != AvCamPhysical::kVersion ||
        header->width != AvCamPhysical::kWidth ||
        header->height != AvCamPhysical::kHeight) return false;
    const std::uint32_t length = header->length;
    if (length < 32u || length > AvCamPhysical::kCapacity) return false;
    const std::uint64_t stamp = header->timestamp;
    const std::uint64_t now = GetTickCount64() * 10000ull;
    if (stamp > now || now - stamp > 20000000ull) return false;
    dest.resize(length);
    memcpy(dest.data(), g_view + sizeof(*header), length);
    MemoryBarrier();
    if (header->sequence != before || (header->sequence & 1) != 0)
        return false;
    return dest[0] == 0xff && dest[1] == 0xd8 &&
        dest[length - 2] == 0xff && dest[length - 1] == 0xd9;
}
} // namespace

void AvCamSetQueueFormat(const void* queue, IMFMediaType* input,
                         IMFMediaType* output) {
    const bool enabled = IsMjpeg1280(input) && IsMjpeg1280(output);
    {
        std::lock_guard<std::mutex> lock(g_modesMutex);
        g_mjpegQueues.erase(queue);
        if (enabled) g_mjpegQueues.insert(queue);
    }
    TraceMediaType(L"QueueFormat", input, output, enabled);
}
void AvCamTrace(const wchar_t* message) {
    TraceRaw(message);
}
void AvCamRemoveQueue(const void* queue) {
    std::lock_guard<std::mutex> lock(g_modesMutex);
    g_mjpegQueues.erase(queue);
}
bool AvCamReplaceMjpegSample(const void* queue, IMFSample* original,
                             IMFSample** replacement) {
    if (!replacement || !original) return false;
    *replacement = nullptr;
    // Windows 11 multi-app capture shares the physical camera across clients.
    // This MFT is device-scoped, not Chrome-scoped. Until the original-input
    // protection and per-client isolation are proven, never replace frames
    // in a production build. Experimental builds must explicitly opt in.
#ifndef AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT
    // Observe pipeline activity without touching frame data. The hook is
    // device-scoped: a count here does not identify individual applications.
    const auto observations = ++g_sampleChecks;
    if (observations == 1 || observations == 30 ||
        observations % 900 == 0) {
        wchar_t event[160]{};
        (void)swprintf_s(event,
            L"AvCamMultiAppSafePassThrough count=%llu replaced=0",
            observations);
        TraceRaw(event);
    }
    return false;
#else
    const auto checks = ++g_sampleChecks;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(g_modesMutex);
        active = (g_mjpegQueues.find(queue) != g_mjpegQueues.end());
    }
    if (!active) {
        if (checks <= 3 || checks % 300 == 0)
            TraceRaw(L"SampleSkipped: queue not negotiated as MJPG 1280x720");
        return false;
    }
    std::vector<unsigned char> jpeg;
    if (!ReadFreshJpeg(jpeg)) {
        if (checks <= 3 || checks % 300 == 0)
            TraceRaw(L"SampleSkipped: shared JPEG missing, invalid or stale");
        return false;
    }
    Microsoft::WRL::ComPtr<IMFSample> result;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateSample(&result)) ||
        FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(jpeg.size()), &buffer)))
        return false;
    BYTE* ptr = nullptr;
    if (FAILED(buffer->Lock(&ptr, nullptr, nullptr))) return false;
    memcpy(ptr, jpeg.data(), jpeg.size());
    buffer->Unlock();
    if (FAILED(buffer->SetCurrentLength(static_cast<DWORD>(jpeg.size()))) ||
        FAILED(result->AddBuffer(buffer.Get()))) return false;
    // Fail open: if the replacement cannot preserve the original sample's
    // metadata and timing, return false so the original Logitech sample flows.
    // A partially initialized replacement must never be sent downstream.
    if (FAILED(original->CopyAllItems(result.Get()))) {
        TraceRaw(L"SampleSkipped: metadata copy failed; keeping original frame");
        return false;
    }
    LONGLONG time = 0, duration = 0;
    if (SUCCEEDED(original->GetSampleTime(&time)) &&
        FAILED(result->SetSampleTime(time))) {
        TraceRaw(L"SampleSkipped: timestamp copy failed; keeping original frame");
        return false;
    }
    if (SUCCEEDED(original->GetSampleDuration(&duration)) &&
        FAILED(result->SetSampleDuration(duration))) {
        TraceRaw(L"SampleSkipped: duration copy failed; keeping original frame");
        return false;
    }
    *replacement = result.Detach();
    const auto replacements = ++g_replaceSuccess;
    if (replacements <= 3 || replacements % 300 == 0)
        TraceRaw(L"SampleReplaced: fresh OBS MJPEG frame supplied to Device MFT");
    return true;
#endif // AVCAM_EXPERIMENTAL_UNSCOPED_MFT_REPLACEMENT
}
