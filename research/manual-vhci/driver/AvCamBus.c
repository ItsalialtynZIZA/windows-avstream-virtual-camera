#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>
#include <usb.h>
#include <usbioctl.h>

#include "../shared/AvCamManualIoctl.h"
#include "../shared/AvCamUsbDescriptors.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvCamEvtDeviceAdd;
EVT_WDF_CHILD_LIST_CREATE_DEVICE AvCamEvtChildListCreateDevice;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AvCamEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamEvtIoInternalDeviceControl;

static const GUID GUID_DEVINTERFACE_AVCAM_MANUAL_BUS =
{ 0x4ef708b4, 0x08c6, 0x4a21, { 0xa7, 0x48, 0xa1, 0x69, 0x64, 0x65, 0x88, 0x31 } };

typedef struct _AVCAM_CHILD_IDENTIFICATION_DESCRIPTION {
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    ULONG SerialNo;
} AVCAM_CHILD_IDENTIFICATION_DESCRIPTION,
  *PAVCAM_CHILD_IDENTIFICATION_DESCRIPTION;

typedef struct _AVCAM_BUS_CONTEXT {
    WDFCHILDLIST ChildList;
    BOOLEAN ChildPresent;
} AVCAM_BUS_CONTEXT, *PAVCAM_BUS_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AVCAM_BUS_CONTEXT, AvCamGetBusContext);

static
PVOID
AvCamMapTransferBuffer(
    _In_opt_ PVOID TransferBuffer,
    _In_opt_ PMDL TransferBufferMdl
    )
{
    if (TransferBuffer != NULL) {
        return TransferBuffer;
    }

    if (TransferBufferMdl != NULL) {
        return MmGetSystemAddressForMdlSafe(TransferBufferMdl, NormalPagePriority);
    }

    return NULL;
}

static
NTSTATUS
AvCamCompleteDescriptorRequest(
    _Inout_ PURB Urb
    )
{
    PVOID destination;
    const UCHAR* source;
    ULONG sourceLength;
    ULONG bytesToCopy;
    PURB_CONTROL_DESCRIPTOR_REQUEST descriptorRequest;

    if (Urb == NULL ||
        Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST)) {
        return STATUS_INVALID_PARAMETER;
    }

    descriptorRequest = &Urb->UrbControlDescriptorRequest;
    source = NULL;
    sourceLength = 0;

    switch (descriptorRequest->DescriptorType) {
    case USB_DEVICE_DESCRIPTOR_TYPE:
        if (descriptorRequest->Index != 0) {
            break;
        }
        source = g_AvCamUsbDeviceDescriptor;
        sourceLength = AVCAM_USB_DEVICE_DESCRIPTOR_SIZE;
        break;

    case USB_CONFIGURATION_DESCRIPTOR_TYPE:
        if (descriptorRequest->Index != 0) {
            break;
        }
        source = g_AvCamUsbConfigurationDescriptor;
        sourceLength = AVCAM_USB_CONFIGURATION_DESCRIPTOR_SIZE;
        break;

    case USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE:
        if (descriptorRequest->Index != 0) {
            break;
        }
        source = g_AvCamUsbDeviceQualifierDescriptor;
        sourceLength = AVCAM_USB_DEVICE_QUALIFIER_DESCRIPTOR_SIZE;
        break;

    case USB_STRING_DESCRIPTOR_TYPE:
        switch (descriptorRequest->Index) {
        case 0:
            source = g_AvCamUsbStringLangId;
            sourceLength = (ULONG)sizeof(g_AvCamUsbStringLangId);
            break;
        case 1:
            if (descriptorRequest->LanguageId == 0 || descriptorRequest->LanguageId == 0x0409) {
                source = g_AvCamUsbStringManufacturer;
                sourceLength = (ULONG)sizeof(g_AvCamUsbStringManufacturer);
            }
            break;
        case 2:
            if (descriptorRequest->LanguageId == 0 || descriptorRequest->LanguageId == 0x0409) {
                source = g_AvCamUsbStringProduct;
                sourceLength = (ULONG)sizeof(g_AvCamUsbStringProduct);
            }
            break;
        case 3:
            if (descriptorRequest->LanguageId == 0 || descriptorRequest->LanguageId == 0x0409) {
                source = g_AvCamUsbStringSerial;
                sourceLength = (ULONG)sizeof(g_AvCamUsbStringSerial);
            }
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }

    if (source == NULL) {
        Urb->UrbHeader.Status = USBD_STATUS_STALL_PID;
        descriptorRequest->TransferBufferLength = 0;
        return STATUS_NOT_SUPPORTED;
    }

    bytesToCopy = descriptorRequest->TransferBufferLength;
    if (bytesToCopy > sourceLength) {
        bytesToCopy = sourceLength;
    }

    if (bytesToCopy == 0) {
        descriptorRequest->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    destination = AvCamMapTransferBuffer(
        descriptorRequest->TransferBuffer,
        descriptorRequest->TransferBufferMDL
        );

    if (destination == NULL) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        descriptorRequest->TransferBufferLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(destination, source, bytesToCopy);
    descriptorRequest->TransferBufferLength = bytesToCopy;
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;

    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamProcessUrb(
    _In_ WDFREQUEST Request
    )
{
    PIRP irp;
    PIO_STACK_LOCATION irpStack;
    PURB urb;

    irp = WdfRequestWdmGetIrp(Request);
    if (irp == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    irpStack = IoGetCurrentIrpStackLocation(irp);
    if (irpStack == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    urb = (PURB)irpStack->Parameters.Others.Argument1;
    if (urb == NULL || urb->UrbHeader.Length < sizeof(struct _URB_HEADER)) {
        return STATUS_INVALID_PARAMETER;
    }

    switch (urb->UrbHeader.Function) {
    case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        return AvCamCompleteDescriptorRequest(urb);

    default:
        urb->UrbHeader.Status = USBD_STATUS_INVALID_URB_FUNCTION;
        return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
AvCamSetChildIds(
    _Inout_ PWDFDEVICE_INIT ChildInit
    )
{
    NTSTATUS status;
    DECLARE_CONST_UNICODE_STRING(deviceId, L"USB\\VID_1234&PID_5678");
    DECLARE_CONST_UNICODE_STRING(hardwareId, L"USB\\VID_1234&PID_5678");
    DECLARE_CONST_UNICODE_STRING(compatibleId, L"USB\\Class_0e&SubClass_03&Prot_00");
    DECLARE_CONST_UNICODE_STRING(instanceId, L"0001");
    DECLARE_CONST_UNICODE_STRING(description, L"AvCam Research Virtual USB Camera");

    status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddHardwareID(ChildInit, &hardwareId);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddCompatibleID(ChildInit, &compatibleId);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAssignInstanceID(ChildInit, &instanceId);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddDeviceText(
        ChildInit,
        &description,
        &description,
        0x0409
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WdfPdoInitSetDefaultLocale(ChildInit, 0x0409);
    WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(ChildInit, FALSE);

    return STATUS_SUCCESS;
}

NTSTATUS
AvCamEvtChildListCreateDevice(
    _In_ WDFCHILDLIST ChildList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    _Inout_ PWDFDEVICE_INIT ChildInit
    )
{
    NTSTATUS status;
    WDFDEVICE childDevice;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PAVCAM_CHILD_IDENTIFICATION_DESCRIPTION childDescription;

    UNREFERENCED_PARAMETER(ChildList);

    childDescription = CONTAINING_RECORD(
        IdentificationDescription,
        AVCAM_CHILD_IDENTIFICATION_DESCRIPTION,
        Header
        );

    if (childDescription->SerialNo != AVCAM_CHILD_SERIAL) {
        return STATUS_INVALID_PARAMETER;
    }

    status = AvCamSetChildIds(ChildInit);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfDeviceCreate(&ChildInit, &attributes, &childDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel
        );
    queueConfig.EvtIoInternalDeviceControl = AvCamEvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(
        childDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );

    return status;
}

VOID
AvCamEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB) {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    status = AvCamProcessUrb(Request);
    WdfRequestComplete(Request, status);
}

static
NTSTATUS
AvCamPlugChild(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    PAVCAM_BUS_CONTEXT context;
    AVCAM_CHILD_IDENTIFICATION_DESCRIPTION description;

    context = AvCamGetBusContext(Device);
    if (context->ChildPresent) {
        return STATUS_OBJECT_NAME_EXISTS;
    }

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
        &description.Header,
        sizeof(description)
        );
    description.SerialNo = AVCAM_CHILD_SERIAL;

    status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
        context->ChildList,
        &description.Header,
        NULL
        );

    if (NT_SUCCESS(status)) {
        context->ChildPresent = TRUE;
    }

    return status;
}

static
NTSTATUS
AvCamUnplugChild(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    PAVCAM_BUS_CONTEXT context;
    AVCAM_CHILD_IDENTIFICATION_DESCRIPTION description;

    context = AvCamGetBusContext(Device);
    if (!context->ChildPresent) {
        return STATUS_NOT_FOUND;
    }

    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
        &description.Header,
        sizeof(description)
        );
    description.SerialNo = AVCAM_CHILD_SERIAL;

    status = WdfChildListUpdateChildDescriptionAsMissing(
        context->ChildList,
        &description.Header
        );

    if (NT_SUCCESS(status)) {
        context->ChildPresent = FALSE;
    }

    return status;
}

VOID
AvCamEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status;
    WDFDEVICE device;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    device = WdfIoQueueGetDevice(Queue);

    switch (IoControlCode) {
    case IOCTL_AVCAM_PLUG_DEVICE:
        status = AvCamPlugChild(device);
        break;

    case IOCTL_AVCAM_UNPLUG_DEVICE:
        status = AvCamUnplugChild(device);
        break;

    case IOCTL_AVCAM_WRITE_FRAME:
        status = STATUS_NOT_SUPPORTED;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(Request, status);
}

NTSTATUS
AvCamEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_CHILD_LIST_CONFIG childListConfig;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PAVCAM_BUS_CONTEXT context;

    UNREFERENCED_PARAMETER(Driver);

    WDF_CHILD_LIST_CONFIG_INIT(
        &childListConfig,
        sizeof(AVCAM_CHILD_IDENTIFICATION_DESCRIPTION),
        AvCamEvtChildListCreateDevice
        );

    WdfFdoInitSetDefaultChildListConfig(
        DeviceInit,
        &childListConfig,
        WDF_NO_OBJECT_ATTRIBUTES
        );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        AVCAM_BUS_CONTEXT
        );

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = AvCamGetBusContext(device);
    context->ChildList = WdfFdoGetDefaultChildList(device);
    context->ChildPresent = FALSE;

    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_AVCAM_MANUAL_BUS,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
        );
    queueConfig.EvtIoDeviceControl = AvCamEvtIoDeviceControl;

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );

    return status;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, AvCamEvtDeviceAdd);

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
        );
}
