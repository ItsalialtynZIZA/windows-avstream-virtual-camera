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

#define AVCAM_UDE_FRAME_POOL_TAG 'fUcA'

/* Default release/research image: NO USB child, and ISO remains OFF.
 *
 * The separate, explicit I_UNDERSTAND_DISPOSABLE_VM_ONLY build setting
 * below can test UdeCx enumeration in a throwaway VM. That mode does not
 * enable streaming or camera output. Never ship/install its SYS on the
 * user's primary Windows host. This guard is compile-time only, not an
 * assertion that the machine actually runs under a hypervisor.
 */
static BOOLEAN g_AvCamUdeEnableAutoplug = FALSE;

static BOOLEAN
AvCamUdeAutoplugEnabled(VOID)
{
#if defined(AVCAM_UDE_VM_ENUMERATION_ONLY)
    return TRUE;
#else
    return g_AvCamUdeEnableAutoplug;
#endif
}

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AvCamUdeEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL AvCamUdeEvtIoDeviceControl;
EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY AvCamUdeEvtQueryUsbCapability;
EVT_WDF_DEVICE_D0_ENTRY AvCamUdeEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT AvCamUdeEvtD0Exit;
EVT_WDF_OBJECT_CONTEXT_CLEANUP AvCamUdeEvtControllerCleanup;

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
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    UDECX_WDF_DEVICE_CONFIG udeConfig;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;
    UNICODE_STRING reference;
    UNICODE_STRING security;

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

    /* Private MJPEG publishing is restricted to SYSTEM/Administrators.
     * USBHUB3 enumeration continues under the OS account.
     */
    RtlInitUnicodeString(&security, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    status = WdfDeviceInitAssignSDDLString(deviceInit, &security);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&deviceName, kControllerName);
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) return status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, AVCAM_UDE_CONTROLLER_CONTEXT);
    /* UdeCx URB buffer helpers are PASSIVE_LEVEL-only. */
    deviceAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    deviceAttributes.EvtCleanupCallback = AvCamUdeEvtControllerCleanup;
    status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) return status;

    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfWaitLockCreate(
        &lockAttributes, &AvCamUdeControllerGetContext(device)->FrameLock);
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
    if (!AvCamUdeAutoplugEnabled()) {
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

/*
 * Frame buffers are allocated only if an emulated child was explicitly
 * plugged in. These are immutable while leased by the ISO pump. Callers
 * MUST own FrameLock before calling this helper or the publisher.
 */
static NTSTATUS
AvCamUdeEnsureFrameStoreLocked(_Inout_ PAVCAM_UDE_CONTROLLER_CONTEXT ctx)
{
    int result;
    if (ctx->FrameSourceReady) return STATUS_SUCCESS;
    if (ctx->FrameStorage[0] != NULL || ctx->FrameStorage[1] != NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    ctx->FrameStorage[0] = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AVCAM_UVC_FRAME_MAX_BYTES, AVCAM_UDE_FRAME_POOL_TAG);
    if (ctx->FrameStorage[0] == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    ctx->FrameStorage[1] = (PUCHAR)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, AVCAM_UVC_FRAME_MAX_BYTES, AVCAM_UDE_FRAME_POOL_TAG);
    if (ctx->FrameStorage[1] == NULL) {
        ExFreePoolWithTag(ctx->FrameStorage[0], AVCAM_UDE_FRAME_POOL_TAG);
        ctx->FrameStorage[0] = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    result = AvCamUdeFrameSourceInit(
        &ctx->FrameSource,
        ctx->FrameStorage[0], AVCAM_UVC_FRAME_MAX_BYTES,
        ctx->FrameStorage[1], AVCAM_UVC_FRAME_MAX_BYTES);
    if (result != AVCAM_UDE_FRAME_OK) {
        ExFreePoolWithTag(ctx->FrameStorage[1], AVCAM_UDE_FRAME_POOL_TAG);
        ExFreePoolWithTag(ctx->FrameStorage[0], AVCAM_UDE_FRAME_POOL_TAG);
        ctx->FrameStorage[0] = NULL;
        ctx->FrameStorage[1] = NULL;
        return STATUS_DATA_ERROR;
    }
    ctx->FrameSourceReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
AvCamUdeEvtControllerCleanup(_In_ WDFOBJECT object)
{
    PAVCAM_UDE_CONTROLLER_CONTEXT ctx =
        AvCamUdeControllerGetContext((WDFDEVICE)object);
    /* WDF guarantees device queues and children have stopped before
     * final device cleanup. Runtime verifier remains required.
     */
    if (ctx->FrameStorage[0] != NULL) {
        ExFreePoolWithTag(ctx->FrameStorage[0], AVCAM_UDE_FRAME_POOL_TAG);
        ctx->FrameStorage[0] = NULL;
    }
    if (ctx->FrameStorage[1] != NULL) {
        ExFreePoolWithTag(ctx->FrameStorage[1], AVCAM_UDE_FRAME_POOL_TAG);
        ctx->FrameStorage[1] = NULL;
    }
    ctx->FrameSourceReady = FALSE;
}

VOID
AvCamUdeEvtIoDeviceControl(_In_ WDFQUEUE queue,
                          _In_ WDFREQUEST request,
                          _In_ size_t outputLength,
                          _In_ size_t inputLength,
                          _In_ ULONG ioctl)
{
    WDFDEVICE controller = WdfIoQueueGetDevice(queue);
    PAVCAM_UDE_CONTROLLER_CONTEXT ctx = AvCamUdeControllerGetContext(controller);

    UNREFERENCED_PARAMETER(outputLength);
    UNREFERENCED_PARAMETER(inputLength);

    if (ioctl == IOCTL_AVCAM_UDE_SUBMIT_MJPEG) {
        PVOID buffer = NULL;
        size_t bufferBytes = 0;
        const unsigned char* jpeg = NULL;
        unsigned int frameBytes = 0;
        NTSTATUS status;
        int publishResult;

        status = WdfRequestRetrieveInputBuffer(
            request, sizeof(AVCAM_UDE_UPLOAD_HEADER),
            &buffer, &bufferBytes);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(request, status);
            return;
        }
        if (bufferBytes > AVCAM_UVC_FRAME_MAX_BYTES +
                sizeof(AVCAM_UDE_UPLOAD_HEADER) ||
            AvCamUdeValidateUpload(
                (const unsigned char*)buffer, (unsigned int)bufferBytes,
                &jpeg, &frameBytes) != AVCAM_UDE_UPLOAD_OK) {
            WdfRequestComplete(request, STATUS_INVALID_PARAMETER);
            return;
        }
        WdfWaitLockAcquire(ctx->FrameLock, NULL);
        if (!ctx->ChildPlugged || ctx->UsbDevice == NULL) {
            status = STATUS_DEVICE_NOT_READY;
        } else {
            status = AvCamUdeEnsureFrameStoreLocked(ctx);
            if (NT_SUCCESS(status)) {
                publishResult = AvCamUdeFrameSourcePublish(
                    &ctx->FrameSource, jpeg, frameBytes);
                if (publishResult == AVCAM_UDE_FRAME_OK) {
                    ++ctx->UploadedFrames;
                    status = STATUS_SUCCESS;
                } else if (publishResult == AVCAM_UDE_FRAME_BUSY) {
                    status = STATUS_DEVICE_BUSY;
                } else {
                    status = STATUS_INVALID_PARAMETER;
                }
            }
        }
        WdfWaitLockRelease(ctx->FrameLock);
        WdfRequestComplete(request, status);
        return;
    }

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
