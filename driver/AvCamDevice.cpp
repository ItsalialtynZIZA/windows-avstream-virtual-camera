#include "AvCamCommon.h"
#include "AvCamDevice.h"
#include "AvCamFilter.h"
#include "AvCamFrameStore.h"

namespace
{
NTSTATUS AvCamDeviceAdd(_In_ PKSDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    return STATUS_SUCCESS;
}

NTSTATUS AvCamDeviceStart(
    _In_ PKSDEVICE Device,
    _In_ PIRP Irp,
    _In_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_ PCM_RESOURCE_LIST UntranslatedResourceList)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(TranslatedResourceList);
    UNREFERENCED_PARAMETER(UntranslatedResourceList);

    // The producer's private GET_STATS works even without a frame store,
    // but SUBMIT_FRAME fails with STATUS_DEVICE_NOT_READY. Initialize the
    // bounded three-slot frame store before AVStream enables the device.
    // Return allocation failures instead of silently claiming a live camera.
    return AvCamFrameStoreInitialize();
}

// Device PnP teardown must release the nonpaged frame allocations. Shutdown
// is idempotent: Remove can follow Stop without double freeing the store.
void AvCamDeviceStop(_In_ PKSDEVICE Device, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    AvCamFrameStoreShutdown();
}

void AvCamDeviceRemove(_In_ PKSDEVICE Device, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    AvCamFrameStoreShutdown();
}
} // namespace

const KSDEVICE_DISPATCH g_AvCamDeviceDispatch =
{
    AvCamDeviceAdd,
    AvCamDeviceStart,
    nullptr,                 // PostStart
    nullptr,                 // QueryStop
    nullptr,                 // CancelStop
    AvCamDeviceStop,         // Stop
    nullptr,                 // QueryRemove
    nullptr,                 // CancelRemove
    AvCamDeviceRemove,       // Remove
    nullptr,                 // QueryCapabilities
    nullptr,                 // SurpriseRemoval
    nullptr,                 // QueryPower
    nullptr,                 // SetPower
    nullptr                  // QueryInterface
};

static const KSFILTER_DESCRIPTOR* g_AvCamFilterDescriptors[] =
{
    &g_AvCamFilterDescriptor
};

const KSDEVICE_DESCRIPTOR g_AvCamDeviceDescriptor =
{
    &g_AvCamDeviceDispatch,
    RTL_NUMBER_OF(g_AvCamFilterDescriptors),
    g_AvCamFilterDescriptors,
    KSDEVICE_DESCRIPTOR_VERSION
};
