#include "AvCamCommon.h"
#include "AvCamDevice.h"
#include "AvCamFilter.h"
#include "../shared/VirtualCameraProtocol.h"

namespace
{
constexpr ULONG kAvCamDeviceContextTag = 'DvCA';

struct AvCamDeviceContext
{
    UNICODE_STRING ProducerInterfaceLink;
    BOOLEAN ProducerInterfaceEnabled;
};

void AvCamDeviceContextCleanup(_In_ PVOID Item)
{
    auto* context = static_cast<AvCamDeviceContext*>(Item);
    if (context == nullptr)
    {
        return;
    }

    if (context->ProducerInterfaceEnabled && context->ProducerInterfaceLink.Buffer != nullptr)
    {
        IoSetDeviceInterfaceState(&context->ProducerInterfaceLink, FALSE);
        context->ProducerInterfaceEnabled = FALSE;
    }

    if (context->ProducerInterfaceLink.Buffer != nullptr)
    {
        RtlFreeUnicodeString(&context->ProducerInterfaceLink);
    }

    ExFreePoolWithTag(context, kAvCamDeviceContextTag);
}

NTSTATUS AvCamDeviceAdd(_In_ PKSDEVICE Device)
{
    if (Device == nullptr || Device->PhysicalDeviceObject == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }

    auto* context = static_cast<AvCamDeviceContext*>(
        ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(AvCamDeviceContext),
            kAvCamDeviceContextTag));

    if (context == nullptr)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(context, sizeof(*context));

    NTSTATUS status = IoRegisterDeviceInterface(
        Device->PhysicalDeviceObject,
        &GUID_DEVINTERFACE_AVCAM_FRAME_SINK,
        nullptr,
        &context->ProducerInterfaceLink);

    if (!NT_SUCCESS(status))
    {
        ExFreePoolWithTag(context, kAvCamDeviceContextTag);
        return status;
    }

    status = KsAddItemToObjectBag(
        Device->Bag,
        context,
        AvCamDeviceContextCleanup);

    if (!NT_SUCCESS(status))
    {
        RtlFreeUnicodeString(&context->ProducerInterfaceLink);
        ExFreePoolWithTag(context, kAvCamDeviceContextTag);
        return status;
    }

    Device->Context = context;
    return STATUS_SUCCESS;
}

NTSTATUS AvCamDeviceStart(
    _In_ PKSDEVICE Device,
    _In_ PIRP Irp,
    _In_ PCM_RESOURCE_LIST TranslatedResourceList,
    _In_ PCM_RESOURCE_LIST UntranslatedResourceList)
{
    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(TranslatedResourceList);
    UNREFERENCED_PARAMETER(UntranslatedResourceList);

    if (Device == nullptr || Device->Context == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    auto* context = static_cast<AvCamDeviceContext*>(Device->Context);
    if (context->ProducerInterfaceLink.Buffer == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (context->ProducerInterfaceEnabled)
    {
        return STATUS_SUCCESS;
    }

    const NTSTATUS status =
        IoSetDeviceInterfaceState(&context->ProducerInterfaceLink, TRUE);

    if (NT_SUCCESS(status))
    {
        context->ProducerInterfaceEnabled = TRUE;
    }

    return status;
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
