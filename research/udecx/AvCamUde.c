/* AvCam UdeCx controller compile-only foundation.
 * This is not a camera yet: no emulated UDECXUSBDEVICE is plugged in.
 * A separate ROOT\AvCamUdeController identity prevents accidentally
 * replacing the installed legacy AvCamBus driver.
 */
#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <usbioctl.h>
#include <ude/1.0/UdeCx.h>

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvCamUdeEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AvCamUdeEvtIoDeviceControl;
EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY AvCamUdeEvtQueryUsbCapability;

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
    UDECX_WDF_DEVICE_CONFIG udeConfig;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;
    UNICODE_STRING reference;

    UNREFERENCED_PARAMETER(driver);

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

    status = WdfDeviceCreate(&deviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
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
