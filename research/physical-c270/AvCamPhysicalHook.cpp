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
    if (!mapping) return false;
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
    std::lock_guard<std::mutex> lock(g_modesMutex);
    g_mjpegQueues.erase(queue);
    if (IsMjpeg1280(input) && IsMjpeg1280(output))
        g_mjpegQueues.insert(queue);
}
void AvCamRemoveQueue(const void* queue) {
    std::lock_guard<std::mutex> lock(g_modesMutex);
    g_mjpegQueues.erase(queue);
}
bool AvCamReplaceMjpegSample(const void* queue, IMFSample* original,
                             IMFSample** replacement) {
    if (!replacement || !original) return false;
    *replacement = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_modesMutex);
        if (g_mjpegQueues.find(queue) == g_mjpegQueues.end()) return false;
    }
    std::vector<unsigned char> jpeg;
    if (!ReadFreshJpeg(jpeg)) return false;
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
    original->CopyAllItems(result.Get());
    LONGLONG time = 0, duration = 0;
    if (SUCCEEDED(original->GetSampleTime(&time))) result->SetSampleTime(time);
    if (SUCCEEDED(original->GetSampleDuration(&duration))) result->SetSampleDuration(duration);
    *replacement = result.Detach();
    return true;
}
