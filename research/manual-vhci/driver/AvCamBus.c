#include <ntddk.h>
#include <wdf.h>
#include <wdm.h>
#include <usb.h>
#include <usbioctl.h>
#include <usbbusif.h>

#include "../shared/AvCamManualIoctl.h"
#include "../shared/AvCamUsbDescriptors.h"
#include "../shared/AvCamUvcControls.h"
#include "../shared/AvCamUvcPayload.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvCamEvtDeviceAdd;
EVT_WDF_CHILD_LIST_CREATE_DEVICE AvCamEvtChildListCreateDevice;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AvCamEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamEvtIoInternalDeviceControl;
EVT_WDFDEVICE_WDM_IRP_PREPROCESS AvCamEvtQueryInterfacePreprocess;
EVT_WDF_DRIVER_UNLOAD AvCamEvtDriverUnload;

/* Well-known USB hub PnP interface GUID, from Windows usbbusif.h. */
static const GUID AVCAM_USB_BUS_INTERFACE_USBDI_GUID =
{ 0xb1a96a13, 0x3de0, 0x4574, { 0x9b, 0x01, 0xc0, 0x8f, 0xea, 0xb3, 0x18, 0xd6 } };

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
    AVCAM_UVC_STREAM_STATE Uvc;
    AVCAM_UVC_PACKETIZER Packetizer;
    ULONGLONG NextFrameDue100ns;
} AVCAM_CHILD_CONTEXT, *PAVCAM_CHILD_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AVCAM_BUS_CONTEXT, AvCamGetBusContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(AVCAM_CHILD_CONTEXT, AvCamGetChildContext);

/* Observation-only trace: requests are handled exactly as before. */
static KSPIN_LOCK g_AvCamTraceLock;
static AVCAM_DIAG_EVENT g_AvCamTrace[AVCAM_DIAG_TRACE_CAPACITY];
static ULONG g_AvCamTraceNextSequence;
static ULONG g_AvCamTraceCount;
static volatile LONG g_AvCamIsochSampleCount;
/* Two nonpaged slots; writes go to pending, readers use active until EOF. */
static FAST_MUTEX g_AvCamFrameLock;
static PUCHAR g_AvCamActiveFrame;
static PUCHAR g_AvCamPendingFrame;
static ULONG g_AvCamActiveBytes;
static ULONG g_AvCamPendingBytes;
static BOOLEAN g_AvCamPendingReady;

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

/*
 * Minimal, conservative USB 2.0 hub interface experiment (version 1).
 * Do not advertise success for unimplemented bus services.
 */
static VOID USB_BUSIFFN
AvCamGetUsbdiVersion(
    _In_opt_ PVOID BusContext,
    _Out_opt_ PUSBD_VERSION_INFORMATION VersionInformation,
    _Out_opt_ PULONG HcdCapabilities
    )
{
    UNREFERENCED_PARAMETER(BusContext);
    if (VersionInformation != NULL) {
        VersionInformation->USBDI_Version = 0x00000600;
        VersionInformation->Supported_USB_Version = 0x00000200;
    }
    if (HcdCapabilities != NULL) {
        *HcdCapabilities = 0;
    }
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 1, STATUS_SUCCESS);
}

static NTSTATUS USB_BUSIFFN
AvCamQueryBusTime(
    _In_opt_ PVOID BusContext,
    _Out_opt_ PULONG FrameNumber
    )
{
    UNREFERENCED_PARAMETER(BusContext);
    if (FrameNumber == NULL) {
        AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 2, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    /* USB frames are 1ms. The value wraps on the 2048-frame boundary. */
    *FrameNumber = (ULONG)((KeQueryInterruptTime() / 10000ull) % 2048ull);
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 2, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

static NTSTATUS USB_BUSIFFN
AvCamUnsupportedIsoOut(
    _In_opt_ PVOID BusContext,
    _In_opt_ PURB Urb
    )
{
    UNREFERENCED_PARAMETER(BusContext);
    UNREFERENCED_PARAMETER(Urb);
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 3, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS USB_BUSIFFN
AvCamUnsupportedBusInformation(
    _In_opt_ PVOID BusContext,
    _In_ ULONG Level,
    _Inout_opt_ PVOID BusInformationBuffer,
    _Inout_opt_ PULONG BusInformationBufferLength,
    _Out_opt_ PULONG BusInformationActualLength
    )
{
    UNREFERENCED_PARAMETER(BusContext);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(BusInformationBuffer);
    UNREFERENCED_PARAMETER(BusInformationBufferLength);
    if (BusInformationActualLength != NULL) {
        *BusInformationActualLength = 0;
    }
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 4, STATUS_NOT_SUPPORTED);
    return STATUS_NOT_SUPPORTED;
}

static BOOLEAN USB_BUSIFFN
AvCamIsDeviceHighSpeed(
    _In_opt_ PVOID BusContext
    )
{
    UNREFERENCED_PARAMETER(BusContext);
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_USB_BUS_CALLBACK, 5, STATUS_SUCCESS);
    return TRUE;
}

static NTSTATUS
AvCamAddUsbdiV1QueryInterface(
    _In_ WDFDEVICE ChildDevice
    )
{
    USB_BUS_INTERFACE_USBDI_V1 busInterface;
    WDF_QUERY_INTERFACE_CONFIG queryConfig;

    RtlZeroMemory(&busInterface, sizeof(busInterface));
    busInterface.Size = sizeof(busInterface);
    busInterface.Version = USB_BUSIF_USBDI_VERSION_1;
    busInterface.BusContext = ChildDevice;
    busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
    busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
    busInterface.GetUSBDIVersion = AvCamGetUsbdiVersion;
    busInterface.QueryBusTime = AvCamQueryBusTime;
    busInterface.SubmitIsoOutUrb = AvCamUnsupportedIsoOut;
    busInterface.QueryBusInformation = AvCamUnsupportedBusInformation;
    busInterface.IsDeviceHighSpeed = AvCamIsDeviceHighSpeed;

    WDF_QUERY_INTERFACE_CONFIG_INIT(
        &queryConfig,
        (PINTERFACE)&busInterface,
        &AVCAM_USB_BUS_INTERFACE_USBDI_GUID,
        NULL
        );
    return WdfDeviceAddQueryInterface(ChildDevice, &queryConfig);
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
    if (irpStack != NULL) {
        /* Version and Size are USHORTs; pack only for diagnostics. */
        const ULONG versionAndSize =
            ((ULONG)irpStack->Parameters.QueryInterface.Size << 16) |
            (ULONG)irpStack->Parameters.QueryInterface.Version;
        AvCamRecordDiagnostic(
            AVCAM_DIAG_EVENT_QUERY_INTERFACE_DETAILS,
            interfaceId,
            (NTSTATUS)versionAndSize
            );
    }
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
        AvCamUvcInitState(&ChildContext->Uvc);
        AvCamUvcPacketizerReset(&ChildContext->Packetizer);
        ChildContext->NextFrameDue100ns = 0;
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
        AvCamUvcPacketizerReset(&ChildContext->Packetizer);
        ChildContext->NextFrameDue100ns = 0;
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

/*
 * UVC 1.10 class-specific requests to the VideoStreaming interface.
 * They use URB_FUNCTION_CLASS_INTERFACE and a USB control transfer buffer.
 */
static NTSTATUS
AvCamProcessUvcClassInterface(
    _Inout_ PURB Urb,
    _Inout_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST* request;
    PUCHAR buffer;
    ULONG controlCode;
    ULONG originalLength;
    unsigned int actualLength;
    int result;
    NTSTATUS status;

    if (Urb == NULL || ChildContext == NULL ||
        Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST)) {
        return STATUS_INVALID_PARAMETER;
    }

    request = &Urb->UrbControlVendorClassRequest;
    originalLength = request->TransferBufferLength;
    controlCode = ((ULONG)request->Request << 24) |
        ((ULONG)(request->Value >> 8) << 16) |
        (ULONG)request->Index;

    AvCamRecordDiagnostic(
        AVCAM_DIAG_EVENT_UVC_REQUEST_LENGTH,
        controlCode,
        (NTSTATUS)((originalLength & 0x00FFFFFFu) |
            ((request->TransferFlags & USBD_TRANSFER_DIRECTION_IN) != 0 ?
                0x40000000u : 0))
        );

    buffer = (PUCHAR)AvCamMapTransferBuffer(
        request->TransferBuffer,
        request->TransferBufferMDL
        );
    actualLength = 0;
    result = AvCamUvcProcessClassInterface(
        &ChildContext->Uvc,
        request->Request,
        request->Value,
        request->Index,
        (request->TransferFlags & USBD_TRANSFER_DIRECTION_IN) != 0,
        buffer,
        originalLength,
        &actualLength
        );

    if (result == AVCAM_UVC_RESULT_OK) {
        request->TransferBufferLength = actualLength;
        Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
        status = STATUS_SUCCESS;
    } else if (result == AVCAM_UVC_RESULT_STALL) {
        request->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_STALL_PID;
        status = STATUS_NOT_SUPPORTED;
    } else {
        request->TransferBufferLength = 0;
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        status = STATUS_INVALID_PARAMETER;
    }
    if (NT_SUCCESS(status) && request->Request == AVCAM_UVC_SET_CUR &&
        (request->Value >> 8) == AVCAM_UVC_VS_COMMIT_CONTROL) {
        AvCamUvcPacketizerReset(&ChildContext->Packetizer);
        ChildContext->NextFrameDue100ns = 0;
    }
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_UVC_CONTROL, controlCode, status);
    return status;
}


/*
 * Sample only the first four ISO URBs so the diagnostic ring retains the
 * successful UVC Probe/Commit sequence. Observation only: this milestone
 * deliberately does not manufacture successful streaming transfers.
 */
static
VOID
AvCamTraceIsochRequest(
    _Inout_ PURB Urb,
    _In_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    struct _URB_ISOCH_TRANSFER* transfer;
    ULONG headerBytes;
    ULONG availablePackets;
    ULONG stateFlags;
    ULONG sample;
    LONG sampleNo;

    sampleNo = InterlockedIncrement(&g_AvCamIsochSampleCount);
    if (sampleNo > 4) {
        return;
    }

    headerBytes = FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket);
    if (Urb->UrbHeader.Length < headerBytes) {
        AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_REQUEST, 0,
                             STATUS_INVALID_PARAMETER);
        return;
    }

    transfer = &Urb->UrbIsochronousTransfer;
    availablePackets = (Urb->UrbHeader.Length - headerBytes) /
                       sizeof(USBD_ISO_PACKET_DESCRIPTOR);

    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_REQUEST,
                         transfer->NumberOfPackets,
                         (NTSTATUS)transfer->TransferBufferLength);
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_FLAGS,
                         transfer->TransferFlags,
                         (NTSTATUS)transfer->StartFrame);

    stateFlags = 0;
    if (ChildContext->Configured) stateFlags |= 0x01u;
    if (ChildContext->StreamingAlternateSetting == 1) stateFlags |= 0x02u;
    if (ChildContext->Uvc.Committed) stateFlags |= 0x04u;
    if (transfer->PipeHandle ==
        (USBD_PIPE_HANDLE)&ChildContext->StreamPipeToken) stateFlags |= 0x08u;
    if ((transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN) != 0) stateFlags |= 0x10u;
    if (transfer->TransferBufferMDL != NULL) stateFlags |= 0x20u;
    if (transfer->TransferBuffer != NULL) stateFlags |= 0x40u;
    if (transfer->NumberOfPackets > 0 &&
        transfer->NumberOfPackets <= availablePackets) stateFlags |= 0x80u;

    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_STATE,
                         stateFlags, (NTSTATUS)availablePackets);

    if (transfer->NumberOfPackets > 0 &&
        transfer->NumberOfPackets <= availablePackets) {
        sample = transfer->NumberOfPackets - 1;
        AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_OFFSETS,
                             transfer->IsoPacket[0].Offset,
                             (NTSTATUS)transfer->IsoPacket[sample].Offset);
    }
}


/* Validate an isochronous IN URB before touching any packet buffer.
 * Pacing is experimental: one microframe costs 125us at high speed.
 * Device callbacks explicitly run at PASSIVE_LEVEL to permit the wait.
 */
static
NTSTATUS
AvCamProcessIsochTransfer(
    _Inout_ PURB Urb,
    _Inout_ PAVCAM_CHILD_CONTEXT ChildContext
    )
{
    struct _URB_ISOCH_TRANSFER* transfer;
    ULONG base = FIELD_OFFSET(struct _URB_ISOCH_TRANSFER, IsoPacket);
    ULONG packets;
    ULONG supplied;
    ULONG originalBytes;
    PUCHAR buffer;
    ULONG i;
    ULONG filled = 0;
    ULONG actualBytes = 0;
    BOOLEAN eof = FALSE;
    ULONGLONG now;
    LARGE_INTEGER delay;

    if (Urb->UrbHeader.Length < base) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }
    transfer = &Urb->UrbIsochronousTransfer;
    packets = transfer->NumberOfPackets;
    supplied = (Urb->UrbHeader.Length - base) /
        sizeof(USBD_ISO_PACKET_DESCRIPTOR);
    originalBytes = transfer->TransferBufferLength;
    if (!ChildContext->Configured ||
        ChildContext->StreamingAlternateSetting != 1 ||
        !ChildContext->Uvc.Committed ||
        transfer->PipeHandle != (USBD_PIPE_HANDLE)&ChildContext->StreamPipeToken ||
        (transfer->TransferFlags & USBD_TRANSFER_DIRECTION_IN) == 0 ||
        packets == 0 || packets > 1024 || packets > supplied ||
        originalBytes == 0 || originalBytes > AVCAM_MAX_FRAME_BYTES) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < packets; ++i) {
        ULONG start = transfer->IsoPacket[i].Offset;
        ULONG end = i + 1 < packets ?
            transfer->IsoPacket[i + 1].Offset : originalBytes;
        if (end < start || end > originalBytes ||
            end - start <= AVCAM_UVC_PAYLOAD_HEADER_BYTES ||
            end - start > AVCAM_UVC_PAYLOAD_MAX_BYTES) {
            Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
            return STATUS_INVALID_PARAMETER;
        }
    }
    buffer = (PUCHAR)AvCamMapTransferBuffer(
        transfer->TransferBuffer, transfer->TransferBufferMDL);
    if (buffer == NULL) {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&g_AvCamFrameLock);
    now = KeQueryInterruptTime();
    if (ChildContext->Packetizer.Offset == 0 &&
        now >= ChildContext->NextFrameDue100ns &&
        g_AvCamPendingReady) {
        PUCHAR swap = g_AvCamActiveFrame;
        g_AvCamActiveFrame = g_AvCamPendingFrame;
        g_AvCamPendingFrame = swap;
        g_AvCamActiveBytes = g_AvCamPendingBytes;
        g_AvCamPendingReady = FALSE;
    }

    for (i = 0; i < packets; ++i) {
        PUSBD_ISO_PACKET_DESCRIPTOR packet = &transfer->IsoPacket[i];
        packet->Length = 0;
        packet->Status = USBD_STATUS_SUCCESS;
        if (g_AvCamActiveBytes > 0 && !eof &&
            (ChildContext->Packetizer.Offset != 0 ||
             now >= ChildContext->NextFrameDue100ns)) {
            ULONG capacity = i + 1 < packets ?
                transfer->IsoPacket[i + 1].Offset - packet->Offset :
                originalBytes - packet->Offset;
            unsigned int written = 0;
            int result = AvCamUvcPacketizeMjpeg(
                &ChildContext->Packetizer, g_AvCamActiveFrame,
                g_AvCamActiveBytes, buffer + packet->Offset,
                capacity, &written);
            if (result == AVCAM_UVC_PACKET_OK) {
                packet->Length = written;
                actualBytes += written;
                ++filled;
                if (ChildContext->Packetizer.Offset == 0) {
                    eof = TRUE;
                    ChildContext->NextFrameDue100ns =
                        now + AVCAM_UVC_FRAME_INTERVAL_100NS;
                }
            }
        }
    }
    ExReleaseFastMutex(&g_AvCamFrameLock);

    transfer->ErrorCount = 0;
    transfer->TransferBufferLength = actualBytes;
    if ((transfer->TransferFlags & USBD_START_ISO_TRANSFER_ASAP) != 0) {
        transfer->StartFrame =
            (ULONG)((KeQueryInterruptTime() / 10000ull) % 2048ull);
    }
    Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
    if (g_AvCamIsochSampleCount <= 4) {
        AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_ISOCH_COMPLETION,
                              filled, (NTSTATUS)actualBytes);
    }

    delay.QuadPart = -((LONGLONG)packets * 1250ll);
    (VOID)KeDelayExecutionThread(KernelMode, FALSE, &delay);
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

    case URB_FUNCTION_CLASS_INTERFACE:
        return AvCamProcessUvcClassInterface(urb, ChildContext);

    case URB_FUNCTION_ISOCH_TRANSFER:
        AvCamTraceIsochRequest(urb, ChildContext);
        return AvCamProcessIsochTransfer(urb, ChildContext);

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
    attributes.ExecutionLevel = WdfExecutionLevelPassive;
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

    status = AvCamAddUsbdiV1QueryInterface(childDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    childContext = AvCamGetChildContext(childDevice);
    RtlZeroMemory(childContext, sizeof(*childContext));
    childContext->ConfigurationToken = 0x43464731;
    childContext->VideoControlInterfaceToken = 0x56433030;
    childContext->VideoStreamingInterfaceToken = 0x56533031;
    childContext->StreamPipeToken = 0x45503831;
    AvCamUvcInitState(&childContext->Uvc);
    AvCamUvcPacketizerReset(&childContext->Packetizer);

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


static
NTSTATUS
AvCamWriteMjpegFrame(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength
    )
{
    PAVCAM_FRAME_HEADER header;
    PIRP irp;
    PUCHAR data;
    NTSTATUS status;

    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(AVCAM_FRAME_HEADER), (PVOID*)&header, NULL);
    if (!NT_SUCCESS(status)) return status;
    if (header->Width != AVCAM_UVC_WIDTH ||
        header->Height != AVCAM_UVC_HEIGHT ||
        header->FourCC != 0x47504A4Du ||
        header->PayloadBytes < 4 ||
        header->PayloadBytes > AVCAM_UVC_FRAME_MAX_BYTES ||
        OutputBufferLength < header->PayloadBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    irp = WdfRequestWdmGetIrp(Request);
    if (irp == NULL || irp->MdlAddress == NULL ||
        MmGetMdlByteCount(irp->MdlAddress) < header->PayloadBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    data = (PUCHAR)MmGetSystemAddressForMdlSafe(
        irp->MdlAddress, NormalPagePriority);
    if (data == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if (data[0] != 0xFF || data[1] != 0xD8 ||
        data[header->PayloadBytes - 2] != 0xFF ||
        data[header->PayloadBytes - 1] != 0xD9) {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&g_AvCamFrameLock);
    RtlCopyMemory(g_AvCamPendingFrame, data, header->PayloadBytes);
    g_AvCamPendingBytes = header->PayloadBytes;
    g_AvCamPendingReady = TRUE;
    ExReleaseFastMutex(&g_AvCamFrameLock);
    AvCamRecordDiagnostic(AVCAM_DIAG_EVENT_FRAME_WRITE,
                          header->PayloadBytes, STATUS_SUCCESS);
    return STATUS_SUCCESS;
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
        status = AvCamWriteMjpegFrame(Request, OutputBufferLength);
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
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

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


static
VOID
AvCamFreeFrameBuffers(VOID)
{
    if (g_AvCamActiveFrame != NULL) {
        ExFreePoolWithTag(g_AvCamActiveFrame, 'mcVA');
        g_AvCamActiveFrame = NULL;
    }
    if (g_AvCamPendingFrame != NULL) {
        ExFreePoolWithTag(g_AvCamPendingFrame, 'mcVA');
        g_AvCamPendingFrame = NULL;
    }
}

VOID
AvCamEvtDriverUnload(_In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
    AvCamFreeFrameBuffers();
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
    ExInitializeFastMutex(&g_AvCamFrameLock);
    g_AvCamActiveFrame = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AVCAM_UVC_FRAME_MAX_BYTES, 'mcVA');
    g_AvCamPendingFrame = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AVCAM_UVC_FRAME_MAX_BYTES, 'mcVA');
    if (g_AvCamActiveFrame == NULL || g_AvCamPendingFrame == NULL) {
        AvCamFreeFrameBuffers();
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    WDF_DRIVER_CONFIG_INIT(&config, AvCamEvtDeviceAdd);
    config.EvtDriverUnload = AvCamEvtDriverUnload;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
        );

    if (!NT_SUCCESS(status)) {
        AvCamFreeFrameBuffers();
    }
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
