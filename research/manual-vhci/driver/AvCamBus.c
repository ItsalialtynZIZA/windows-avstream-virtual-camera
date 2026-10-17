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
EVT_WDFDEVICE_WDM_IRP_PREPROCESS AvCamEvtQueryInterfacePreprocess;

static const GUID GUID_DEVINTERFACE_AVCAM_MANUAL_BUS =
{ 0x4ef708b4, 0x08c6, 0x4a21, { 0xa7, 0x48, 0xa1, 0x69, 0x64, 0x65, 0x88, 0x31 } };

#define AVCAM_DIAGNOSTIC_BUILD_ID 0x20261016u
#define AVCAM_DIAGNOSTIC_DRIVER_ENTRY_MARKER 0xA11C0001u

typedef struct _AVCAM_CHILD_IDENTIFICATION_DESCRIPTION {
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    ULONG SerialNo;
} AVCAM_CHILD_IDENTIFICATION_DESCRIPTION,
  *PAVCAM_CHILD_IDENTIFICATION_DESCRIPTION;

typedef struct _AVCAM_BUS_CONTEXT {
    WDFCHILDLIST ChildList;
    BOOLEAN ChildPresent;
} AVCAM_BUS_CONTEXT, *PAVCAM_BUS_CONTEXT;

typedef struct _AVCAM_CHILD_CONTEXT {
    BOOLEAN Configured;
    UCHAR StreamingAlternateSetting;
    ULONG ConfigurationToken;
    ULONG VideoControlInterfaceToken;
    ULONG VideoStreamingInterfaceToken;
    ULONG StreamPipeToken;
} AVCAM_CHILD_CONTEXT, *PAVCAM_CHILD_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AVCAM_BUS_CONTEXT, AvCamGetBusContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AVCAM_CHILD_CONTEXT, AvCamGetChildContext);

/* Observation-only trace: requests are handled exactly as before. */
static KSPIN_LOCK g_AvCamTraceLock;
static AVCAM_DIAG_EVENT g_AvCamTrace[AVCAM_DIAG_TRACE_CAPACITY];
static ULONG g_AvCamTraceNextSequence;
static ULONG g_AvCamTraceCount;

static
VOID
AvCamRecordDiagnostic(
    _In_ ULONG EventType,
    _In_ ULONG Code,
    _In_ NTSTATUS Status
    )
{
    KIRQL oldIrql;
    ULONG sequence;
    ULONG slot;

    KeAcquireSpinLock(&g_AvCamTraceLock, &oldIrql);
    sequence = g_AvCamTraceNextSequence++;
    slot = sequence % AVCAM_DIAG_TRACE_CAPACITY;
    g_AvCamTrace[slot].Sequence = sequence;
    g_AvCamTrace[slot].EventType = EventType;
    g_AvCamTrace[slot].Code = Code;
    g_AvCamTrace[slot].Status = (LONG)Status;
    if (g_AvCamTraceCount < AVCAM_DIAG_TRACE_CAPACITY) {
        ++g_AvCamTraceCount;
    }
    KeReleaseSpinLock(&g_AvCamTraceLock, oldIrql);
}

static
NTSTATUS
AvCamReadDiagnostics(
    _In_ WDFREQUEST Request
    )
{
    PAVCAM_DIAG_BUFFER output;
    NTSTATUS status;
    KIRQL oldIrql;
    ULONG count;
    ULONG first;
    ULONG index;

    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(AVCAM_DIAG_BUFFER),
        (PVOID*)&output,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(output, sizeof(*output));
    KeAcquireSpinLock(&g_AvCamTraceLock, &oldIrql);
    count = g_AvCamTraceCount;
    output->Count = count;
    output->TotalEvents = g_AvCamTraceNextSequence;
    first = (g_AvCamTraceNextSequence - count) % AVCAM_DIAG_TRACE_CAPACITY;
    for (index = 0; index < count; ++index) {
        output->Events[index] =
            g_AvCamTrace[(first + index) % AVCAM_DIAG_TRACE_CAPACITY];
    }
    KeReleaseSpinLock(&g_AvCamTraceLock, oldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
AvCamEvtQueryInterfacePreprocess(
    _In_ WDFDEVICE Device,
    _Inout_ PIRP Irp
    )
{
    PIO_STACK_LOCATION irpStack;
    ULONG interfaceId;

    irpStack = IoGetCurrentIrpStackLocation(Irp);
    interfaceId = 0;
    if (irpStack != NULL &&
        irpStack->Parameters.QueryInterface.InterfaceType != NULL) {
        interfaceId =
            irpStack->Parameters.QueryInterface.InterfaceType->Data1;
    }

    /* STATUS_PENDING here means observed-before-framework, not a result. */
    AvCamRecordDiagnostic(
        AVCAM_DIAG_EVENT_QUERY_INTERFACE,
        interfaceId,
        STATUS_PENDING
        );
    IoSkipCurrentIrpStackLocation(Irp);
    return WdfDeviceWdmDispatchPreprocessedIrp(Device, Irp);
}


static
VOID
AvCamWriteDiagnosticDword(
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PCWSTR ValueName,
    _In_ ULONG Value
    )
{
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING valueName;
    HANDLE keyHandle;
    NTSTATUS status;

    if (RegistryPath == NULL || ValueName == NULL) {
        return;
    }

    InitializeObjectAttributes(
        &objectAttributes,
        RegistryPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );

    keyHandle = NULL;
    status = ZwOpenKey(&keyHandle, KEY_SET_VALUE, &objectAttributes);
    if (!NT_SUCCESS(status)) {
        return;
    }

    RtlInitUnicodeString(&valueName, ValueName);
    (VOID)ZwSetValueKey(
        keyHandle,
        &valueName,
        0,
        REG_DWORD,
        &Value,
        sizeof(Value)
        );

    ZwClose(keyHandle);
}

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
    struct _URB_CONTROL_DESCRIPTOR_REQUEST* descriptorRequest;

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
AvCamPopulateInterfaceInformation(
    _In_ PAVCAM_CHILD_CONTEXT ChildContext,
    _Inout_ PUSBD_INTERFACE_INFORMATION Interface
    )
{
    ULONG requiredLength;
    const ULONG baseLength = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);

    if (ChildContext == NULL || Interface == NULL || Interface->Length < baseLength) {
        return STATUS_INVALID_PARAMETER;
    }

    requiredLength = baseLength;

    if (Interface->InterfaceNumber == 0 && Interface->AlternateSetting == 0) {
        Interface->Class = 0x0E;
        Interface->SubClass = 0x01;
        Interface->Protocol = 0x00;
        Interface->Reserved = 0;
        Interface->InterfaceHandle =
            (USBD_INTERFACE_HANDLE)&ChildContext->VideoControlInterfaceToken;
        Interface->NumberOfPipes = 0;
    }
    else if (Interface->InterfaceNumber == 1 && Interface->AlternateSetting == 0) {
        Interface->Class = 0x0E;
        Interface->SubClass = 0x02;
        Interface->Protocol = 0x00;
        Interface->Reserved = 0;
        Interface->InterfaceHandle =
            (USBD_INTERFACE_HANDLE)&ChildContext->VideoStreamingInterfaceToken;
        Interface->NumberOfPipes = 0;
    }
    else if (Interface->InterfaceNumber == 1 && Interface->AlternateSetting == 1) {
        requiredLength += sizeof(USBD_PIPE_INFORMATION);
        if (Interface->Length < requiredLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        Interface->Class = 0x0E;
        Interface->SubClass = 0x02;
        Interface->Protocol = 0x00;
        Interface->Reserved = 0;
        Interface->InterfaceHandle =
            (USBD_INTERFACE_HANDLE)&ChildContext->VideoStreamingInterfaceToken;
        Interface->NumberOfPipes = 1;

        Interface->Pipes[0].MaximumPacketSize = 3072;
        Interface->Pipes[0].EndpointAddress = 0x81;
        Interface->Pipes[0].Interval = 1;
        Interface->Pipes[0].PipeType = UsbdPipeTypeIsochronous;
        Interface->Pipes[0].PipeHandle =
            (USBD_PIPE_HANDLE)&ChildContext->StreamPipeToken;
        Interface->Pipes[0].MaximumTransferSize = 0;
        Interface->Pipes[0].PipeFlags = 0;
    }
    else {
        return STATUS_NOT_SUPPORTED;
    }

    Interface->Length = (USHORT)requiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamSelectConfiguration(
    _Inout_ PURB Urb,
    _Inout_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    NTSTATUS status;
    ULONG remaining;
    PUCHAR cursor;
    struct _URB_SELECT_CONFIGURATION* selectConfiguration;
    const ULONG interfaceOffset = FIELD_OFFSET(struct _URB_SELECT_CONFIGURATION, Interface);
    const ULONG interfaceBaseLength = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);

    if (Urb == NULL || ChildContext == NULL || Urb->UrbHeader.Length < interfaceOffset) {
        return STATUS_INVALID_PARAMETER;
    }

    selectConfiguration = &Urb->UrbSelectConfiguration;

    if (selectConfiguration->ConfigurationDescriptor == NULL) {
        ChildContext->Configured = FALSE;
        ChildContext->StreamingAlternateSetting = 0;
        selectConfiguration->ConfigurationHandle = NULL;
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    if (selectConfiguration->ConfigurationDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE ||
        selectConfiguration->ConfigurationDescriptor->bConfigurationValue != 1) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    remaining = Urb->UrbHeader.Length - interfaceOffset;
    cursor = (PUCHAR)&selectConfiguration->Interface;

    while (remaining != 0) {
        PUSBD_INTERFACE_INFORMATION interfaceInformation;
        USHORT suppliedLength;

        if (remaining < interfaceBaseLength) {
            Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }

        interfaceInformation = (PUSBD_INTERFACE_INFORMATION)cursor;
        suppliedLength = interfaceInformation->Length;
        if (suppliedLength < interfaceBaseLength || suppliedLength > remaining) {
            Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }

        status = AvCamPopulateInterfaceInformation(ChildContext, interfaceInformation);
        if (!NT_SUCCESS(status)) {
            Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
            return status;
        }

        if (interfaceInformation->InterfaceNumber == 1) {
            ChildContext->StreamingAlternateSetting = interfaceInformation->AlternateSetting;
        }

        cursor += suppliedLength;
        remaining -= suppliedLength;
    }

    ChildContext->Configured = TRUE;
    selectConfiguration->ConfigurationHandle =
        (USBD_CONFIGURATION_HANDLE)&ChildContext->ConfigurationToken;
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;

    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamSelectInterface(
    _Inout_ PURB Urb,
    _Inout_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    NTSTATUS status;
    struct _URB_SELECT_INTERFACE* selectInterface;
    const ULONG interfaceOffset = FIELD_OFFSET(struct _URB_SELECT_INTERFACE, Interface);
    const ULONG interfaceBaseLength = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes);

    if (Urb == NULL || ChildContext == NULL ||
        Urb->UrbHeader.Length < interfaceOffset + interfaceBaseLength) {
        return STATUS_INVALID_PARAMETER;
    }

    selectInterface = &Urb->UrbSelectInterface;
    if (!ChildContext->Configured ||
        selectInterface->ConfigurationHandle !=
            (USBD_CONFIGURATION_HANDLE)&ChildContext->ConfigurationToken) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    if (selectInterface->Interface.Length < interfaceBaseLength ||
        interfaceOffset + selectInterface->Interface.Length > Urb->UrbHeader.Length) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    status = AvCamPopulateInterfaceInformation(
        ChildContext,
        &selectInterface->Interface
        );
    if (!NT_SUCCESS(status)) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return status;
    }

    if (selectInterface->Interface.InterfaceNumber == 1) {
        ChildContext->StreamingAlternateSetting =
            selectInterface->Interface.AlternateSetting;
    }

    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamGetConfiguration(
    _Inout_ PURB Urb,
    _In_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    PUCHAR destination;
    struct _URB_CONTROL_GET_CONFIGURATION_REQUEST* request;

    if (Urb == NULL || ChildContext == NULL ||
        Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST)) {
        return STATUS_INVALID_PARAMETER;
    }

    request = &Urb->UrbControlGetConfigurationRequest;
    if (request->TransferBufferLength < 1) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_BUFFER_TOO_SMALL;
    }

    destination = (PUCHAR)AvCamMapTransferBuffer(
        request->TransferBuffer,
        request->TransferBufferMDL
        );
    if (destination == NULL) {
        request->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    destination[0] = ChildContext->Configured ? 1 : 0;
    request->TransferBufferLength = 1;
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamGetInterface(
    _Inout_ PURB Urb,
    _In_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    UCHAR alternateSetting;
    PUCHAR destination;
    struct _URB_CONTROL_GET_INTERFACE_REQUEST* request;

    if (Urb == NULL || ChildContext == NULL ||
        Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_GET_INTERFACE_REQUEST)) {
        return STATUS_INVALID_PARAMETER;
    }

    request = &Urb->UrbControlGetInterfaceRequest;
    if (!ChildContext->Configured || request->TransferBufferLength < 1) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    if (request->Interface == 0) {
        alternateSetting = 0;
    }
    else if (request->Interface == 1) {
        alternateSetting = ChildContext->StreamingAlternateSetting;
    }
    else {
        request->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    destination = (PUCHAR)AvCamMapTransferBuffer(
        request->TransferBuffer,
        request->TransferBufferMDL
        );
    if (destination == NULL) {
        request->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    destination[0] = alternateSetting;
    request->TransferBufferLength = 1;
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AvCamProcessUrb(
    _In_ WDFREQUEST Request,
    _Inout_ PAVCAM_CHILD_CONTEXT ChildContext
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

    case URB_FUNCTION_SELECT_CONFIGURATION:
        return AvCamSelectConfiguration(urb, ChildContext);

    case URB_FUNCTION_SELECT_INTERFACE:
        return AvCamSelectInterface(urb, ChildContext);

    case URB_FUNCTION_GET_CONFIGURATION:
        return AvCamGetConfiguration(urb, ChildContext);

    case URB_FUNCTION_GET_INTERFACE:
        return AvCamGetInterface(urb, ChildContext);

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
    DECLARE_CONST_UNICODE_STRING(deviceId, L"USB\\VID_1234&PID_5678&MI_00");
    DECLARE_CONST_UNICODE_STRING(hardwareIdInterface, L"USB\\VID_1234&PID_5678&MI_00");
    DECLARE_CONST_UNICODE_STRING(hardwareIdDevice, L"USB\\VID_1234&PID_5678");
    DECLARE_CONST_UNICODE_STRING(compatibleIdFull, L"USB\\Class_0E&SubClass_01&Prot_00");
    DECLARE_CONST_UNICODE_STRING(compatibleIdSubclass, L"USB\\Class_0E&SubClass_01");
    DECLARE_CONST_UNICODE_STRING(compatibleIdClass, L"USB\\Class_0E");
    DECLARE_CONST_UNICODE_STRING(instanceId, L"0001");
    DECLARE_CONST_UNICODE_STRING(description, L"AvCam Research Virtual USB Camera");

    status = WdfPdoInitAssignDeviceID(ChildInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddHardwareID(ChildInit, &hardwareIdInterface);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddHardwareID(ChildInit, &hardwareIdDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddCompatibleID(ChildInit, &compatibleIdFull);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddCompatibleID(ChildInit, &compatibleIdSubclass);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfPdoInitAddCompatibleID(ChildInit, &compatibleIdClass);
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
    _In_ PWDFDEVICE_INIT ChildInit
    )
{
    NTSTATUS status;
    WDFDEVICE childDevice;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PAVCAM_CHILD_CONTEXT childContext;
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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, AVCAM_CHILD_CONTEXT);
    {
        UCHAR queryInterfaceMinor = IRP_MN_QUERY_INTERFACE;
        status = WdfDeviceInitAssignWdmIrpPreprocessCallback(
            ChildInit,
            AvCamEvtQueryInterfacePreprocess,
            IRP_MJ_PNP,
            &queryInterfaceMinor,
            1
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    status = WdfDeviceCreate(&ChildInit, &attributes, &childDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    childContext = AvCamGetChildContext(childDevice);
    RtlZeroMemory(childContext, sizeof(*childContext));
    childContext->ConfigurationToken = 0x43464731;
    childContext->VideoControlInterfaceToken = 0x56433030;
    childContext->VideoStreamingInterfaceToken = 0x56533031;
    childContext->StreamPipeToken = 0x45503831;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
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
    WDFDEVICE device;
    PAVCAM_CHILD_CONTEXT childContext;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB) {
        AvCamRecordDiagnostic(
            AVCAM_DIAG_EVENT_INTERNAL_IOCTL,
            IoControlCode,
            STATUS_NOT_SUPPORTED
            );
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    device = WdfIoQueueGetDevice(Queue);
    childContext = AvCamGetChildContext(device);
    {
        ULONG function = (ULONG)-1;
        PIRP irp = WdfRequestWdmGetIrp(Request);
        if (irp != NULL) {
            PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(irp);
            if (irpStack != NULL) {
                PURB urb = (PURB)irpStack->Parameters.Others.Argument1;
                if (urb != NULL &&
                    urb->UrbHeader.Length >= sizeof(struct _URB_HEADER)) {
                    function = urb->UrbHeader.Function;
                }
            }
        }
        status = AvCamProcessUrb(Request, childContext);
        AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_URB, function, status);
    }
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

    case IOCTL_AVCAM_READ_DIAGNOSTICS:
        status = AvCamReadDiagnostics(Request);
        WdfRequestCompleteWithInformation(
            Request,
            status,
            NT_SUCCESS(status) ? sizeof(AVCAM_DIAG_BUFFER) : 0
            );
        return;

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
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;

    AvCamWriteDiagnosticDword(
        RegistryPath,
        L"DiagnosticBuildId",
        AVCAM_DIAGNOSTIC_BUILD_ID
        );
    AvCamWriteDiagnosticDword(
        RegistryPath,
        L"DriverEntryReached",
        AVCAM_DIAGNOSTIC_DRIVER_ENTRY_MARKER
        );
    AvCamWriteDiagnosticDword(
        RegistryPath,
        L"WdfDriverCreateReturned",
        0
        );

    KeInitializeSpinLock(&g_AvCamTraceLock);
    WDF_DRIVER_CONFIG_INIT(&config, AvCamEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
        );

    AvCamWriteDiagnosticDword(
        RegistryPath,
        L"WdfDriverCreateStatus",
        (ULONG)status
        );
    AvCamWriteDiagnosticDword(
        RegistryPath,
        L"WdfDriverCreateReturned",
        1
        );

    return status;
}
