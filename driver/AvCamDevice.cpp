#include "AvCamCommon.h"
#include "AvCamDevice.h"
#include "AvCamFilter.h"

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
    return STATUS_SUCCESS;
}
} // namespace

const KSDEVICE_DISPATCH g_AvCamDeviceDispatch =
{
    AvCamDeviceAdd,
    AvCamDeviceStart,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
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
