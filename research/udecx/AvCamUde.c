/* AvCam UdeCx controller compile-only foundation.
 * This is not a camera yet: no emulated UDECXUSBDEVICE is plugged in.
 * A separate ROOT\AvCamUdeController identity prevents accidentally
 * replacing the installed legacy AvCamBus driver.
 */
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include <usbdlib.h>
#include <initguid.h>
#include <usbioctl.h>
#include <ude/1.0/UdeCx.h>
#include "AvCamUdeUsb.h"

/* Explicit safety gate: this build does not enumerate a USB child. */
static BOOLEAN g_AvCamUdeEnableAutoplug = FALSE;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvCamUdeEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AvCamUdeEvtIoDeviceControl;
EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY AvCamUdeEvtQueryUsbCapability;
EVT_WDF_DEVICE_D0_ENTRY AvCamUdeEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT AvCamUdeEvtD0Exit;

static const WCHAR kControllerName[] = L"\\Device\\AvCamUdeController0";
static const WCHAR kControllerLink[] = L"\\DosDevices\\AvCamUdeController0";
static const WCHAR kUsbControllerReference[] = L"GUID_DEVINTERFACE_USB_HOST_CONTROLLER";

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT driverObject,
            _In_ PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, AvCamUdeEvtDeviceAdd);
    return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES,
                           &config, WDF_NO_HANDLE);
}

NTSTATUS
AvCamUdeEvtDeviceAdd(_In_ WDFDRIVER driver,
                     _Inout_ PWDFDEVICE_INIT deviceInit)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_PNPPOWER_EVENT_CALLBACKS powerCallbacks;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    UDECX_WDF_DEVICE_CONFIG udeConfig;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;
    UNICODE_STRING reference;

    UNREFERENCED_PARAMETER(driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&powerCallbacks);
    powerCallbacks.EvtDeviceD0Entry = AvCamUdeEvtD0Entry;
    powerCallbacks.EvtDeviceD0Exit = AvCamUdeEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &powerCallbacks);

    /* UdeCx must attach to the FDO before WdfDeviceCreate. */
    status = UdecxInitializeWdfDeviceInit(deviceInit);
    if (!NT_SUCCESS(status)) return status;

    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK,
        WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);
    fileConfig.FileObjectClass = WdfFileObjectWdfCannotUseFsContexts;
    WdfDeviceInitSetFileObjectConfig(deviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    RtlInitUnicodeString(&deviceName, kControllerName);
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) return status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, AVCAM_UDE_CONTROLLER_CONTEXT);
    status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&symbolicLink, kControllerLink);
    status = WdfDeviceCreateSymbolicLink(device, &symbolicLink);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&reference, kUsbControllerReference);
    status = WdfDeviceCreateDeviceInterface(
        device, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER, &reference);
    if (!NT_SUCCESS(status)) return status;

    UDECX_WDF_DEVICE_CONFIG_INIT(&udeConfig, AvCamUdeEvtQueryUsbCapability);
    status = UdecxWdfDeviceAddUsbDeviceEmulation(device, &udeConfig);
    if (!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = AvCamUdeEvtIoDeviceControl;
    queueConfig.PowerManaged = WdfFalse;
    return WdfIoQueueCreate(device, &queueConfig,
                            WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

NTSTATUS
AvCamUdeEvtD0Entry(_In_ WDFDEVICE device,
                    _In_ WDF_POWER_DEVICE_STATE previousState)
{
    UNREFERENCED_PARAMETER(previousState);

    /* Deliberately disabled until UVC endpoints, class controls, resets,
     * and cancellation are implemented and independently validated. */
    if (!g_AvCamUdeEnableAutoplug) {
        return STATUS_SUCCESS;
    }
    return AvCamUdeUsbCreateAndPlug(device);
}

NTSTATUS
AvCamUdeEvtD0Exit(_In_ WDFDEVICE device,
                   _In_ WDF_POWER_DEVICE_STATE targetState)
{
    UNREFERENCED_PARAMETER(targetState);
    AvCamUdeUsbDisconnect(device);
    return STATUS_SUCCESS;
}

VOID
AvCamUdeEvtIoDeviceControl(_In_ WDFQUEUE queue,
                          _In_ WDFREQUEST request,
                          _In_ size_t outputLength,
                          _In_ size_t inputLength,
                          _In_ ULONG ioctl)
{
    UNREFERENCED_PARAMETER(outputLength);
    UNREFERENCED_PARAMETER(inputLength);
    UNREFERENCED_PARAMETER(ioctl);

    /* Forward standard host-controller requests to the UdeCx extension. */
    if (UdecxWdfDeviceTryHandleUserIoctl(WdfIoQueueGetDevice(queue), request)) {
        return;
    }
    WdfRequestComplete(request, STATUS_INVALID_DEVICE_REQUEST);
}

NTSTATUS
AvCamUdeEvtQueryUsbCapability(_In_ WDFDEVICE device,
                              _In_ PGUID capability,
                              _In_ ULONG outputLength,
                              _Out_writes_to_opt_(outputLength, *resultLength) PVOID outputBuffer,
                              _Out_ PULONG resultLength)
{
    UNREFERENCED_PARAMETER(device);
    UNREFERENCED_PARAMETER(capability);
    UNREFERENCED_PARAMETER(outputLength);
    UNREFERENCED_PARAMETER(outputBuffer);

    if (resultLength != NULL) {
        *resultLength = 0;
    }
    /* No emulated USB device or speed promises are made at this stage. */
    return STATUS_NOT_SUPPORTED;
}
