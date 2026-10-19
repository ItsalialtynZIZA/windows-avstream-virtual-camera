/*
 * Native UdeCx USB-device lifecycle. Unlike the legacy KMDF child-PDO bus,
 * UdeCx owns USB enumeration and the Windows USB stack.
 *
 * The controller deliberately does NOT auto-plug this device yet. The
 * endpoint callbacks reject unsupported traffic until UVC control/streaming
 * semantics are implemented and individually regression-tested.
 */
#include "AvCamUdeUsb.h"
#include "../manual-vhci/shared/AvCamUsbDescriptors.h"

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamUdeEvtEndpointIo;
EVT_UDECX_USB_ENDPOINT_RESET AvCamUdeEvtEndpointReset;
EVT_UDECX_USB_ENDPOINT_START AvCamUdeEvtEndpointStart;
EVT_UDECX_USB_ENDPOINT_PURGE AvCamUdeEvtEndpointPurge;

/*
 * Synchronous USB control-URB handling. No request is pended, so there is
 * no ownership transfer or cancellation race in the control path yet.
 *
 * Dynamic video endpoint requests remain explicitly unsupported until
 * UdeCx ISO completion is implemented and tested.
 */
static VOID
AvCamUdeProcessEndpointIo(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t outputLength,
    _In_ size_t inputLength,
    _In_ ULONG ioctl)
{
    PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT endpoint;
    PAVCAM_UDE_USB_DEVICE_CONTEXT device;
    WDF_USB_CONTROL_SETUP_PACKET setup;
    PUCHAR transferBuffer = NULL;
    ULONG transferCapacity = 0;
    unsigned int transferred = 0;
    NTSTATUS status;
    int result;

    UNREFERENCED_PARAMETER(outputLength);
    UNREFERENCED_PARAMETER(inputLength);

    if (ioctl != IOCTL_INTERNAL_USB_SUBMIT_URB) {
        WdfRequestComplete(request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    endpoint = AvCamUdeEndpointQueueGetContext(queue);
    if (endpoint->UsbDevice == NULL) {
        UdecxUrbCompleteWithNtStatus(request, STATUS_INVALID_DEVICE_STATE);
        return;
    }
    if (endpoint->EndpointAddress == 0x81) {
        /* Validate a real ISO URB, but never fake a completed MJPEG frame.
         * Async completion, cancellation, and immutable frame storage
         * must be in place before enabling this transfer endpoint.
         */
        status = AvCamUdeInspectIsoUrb(request);
        UdecxUrbCompleteWithNtStatus(
            request, NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status);
        return;
    }
    if (endpoint->EndpointAddress != 0x00) {
        UdecxUrbCompleteWithNtStatus(request, STATUS_NOT_SUPPORTED);
        return;
    }

    status = UdecxUrbRetrieveControlSetupPacket(request, &setup);
    if (!NT_SUCCESS(status)) {
        UdecxUrbCompleteWithNtStatus(request, status);
        return;
    }

    /* Only the UVC interface class is implemented. Never report false
     * success for unimplemented standard/device/vendor control requests. */
    if (setup.Packet.bm.Byte != AVCAM_UDE_UVC_INTERFACE_CLASS_IN &&
        setup.Packet.bm.Byte != AVCAM_UDE_UVC_INTERFACE_CLASS_OUT) {
        UdecxUrbComplete(request, USBD_STATUS_STALL_PID);
        return;
    }

    status = UdecxUrbRetrieveBuffer(
        request, &transferBuffer, &transferCapacity);
    if (!NT_SUCCESS(status)) {
        UdecxUrbCompleteWithNtStatus(request, status);
        return;
    }

    device = AvCamUdeUsbGetContext(endpoint->UsbDevice);
    result = AvCamUdeHandleUvcInterfaceControl(
        &device->Stream,
        setup.Packet.bm.Byte,
        setup.Packet.bRequest,
        setup.Packet.wValue.Value,
        setup.Packet.wIndex.Value,
        setup.Packet.wLength,
        transferBuffer,
        transferCapacity,
        &transferred);

    if (result == AVCAM_UVC_RESULT_OK) {
        /* Never report a byte count larger than the mapped URB buffer. */
        if (transferred > transferCapacity ||
            transferred > setup.Packet.wLength) {
            UdecxUrbCompleteWithNtStatus(request, STATUS_DATA_ERROR);
            return;
        }
        UdecxUrbSetBytesCompleted(request, (ULONG)transferred);
        UdecxUrbCompleteWithNtStatus(request, STATUS_SUCCESS);
    } else if (result == AVCAM_UVC_RESULT_STALL) {
        UdecxUrbComplete(request, USBD_STATUS_STALL_PID);
    } else {
        UdecxUrbCompleteWithNtStatus(request, STATUS_INVALID_PARAMETER);
    }
}

/*
 * Count the full duration of the inline callback, including its UdeCx
 * completion. This guards concurrent endpoint PURGE against acknowledging
 * while an EP0/ISO callback is still using the USB child or request.
 * Future asynchronous workers MUST retain this count until buffer access
 * and cancellation have both ended, not merely until callback return.
 */
VOID
AvCamUdeEvtEndpointIo(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t outputLength,
    _In_ size_t inputLength,
    _In_ ULONG ioctl)
{
    PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT qctx =
        AvCamUdeEndpointQueueGetContext(queue);
    PAVCAM_UDE_ENDPOINT_CONTEXT ectx;
    BOOLEAN admitted = FALSE;
    BOOLEAN signalPurge = FALSE;

    if (qctx->Endpoint == NULL) {
        WdfRequestComplete(request, STATUS_INVALID_DEVICE_STATE);
        return;
    }
    ectx = AvCamUdeEndpointGetContext(qctx->Endpoint);
    WdfSpinLockAcquire(ectx->Lock);
    admitted = (AvCamUdeDrainEnter(&ectx->Drain) ==
        AVCAM_UDE_DRAIN_OK);
    WdfSpinLockRelease(ectx->Lock);

    if (!admitted) {
        if (ioctl == IOCTL_INTERNAL_USB_SUBMIT_URB) {
            UdecxUrbCompleteWithNtStatus(request, STATUS_DEVICE_NOT_READY);
        } else {
            WdfRequestComplete(request, STATUS_DEVICE_NOT_READY);
        }
        return;
    }

    AvCamUdeProcessEndpointIo(
        queue, request, outputLength, inputLength, ioctl);

    WdfSpinLockAcquire(ectx->Lock);
    (VOID)AvCamUdeDrainLeave(&ectx->Drain);
    if (ectx->Drain.PurgePending && ectx->Drain.Active == 0) {
        signalPurge = TRUE;
    }
    WdfSpinLockRelease(ectx->Lock);
    if (signalPurge) {
        WdfWorkItemEnqueue(ectx->PurgeWorkItem);
    }
}

/* UdeCx owns its associated endpoint queue. Do not call WdfIoQueuePurge
 * or WdfIoQueueStart on it. START/PURGE callbacks can be <= DISPATCH_LEVEL.
 * The UdeCx PURGE acknowledgement API, however, requires PASSIVE_LEVEL.
 */
VOID
AvCamUdeEvtEndpointStart(_In_ UDECXUSBENDPOINT endpoint)
{
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx = AvCamUdeEndpointGetContext(endpoint);
    WdfSpinLockAcquire(ctx->Lock);
    (VOID)AvCamUdeDrainStart(&ctx->Drain);
    WdfSpinLockRelease(ctx->Lock);
}

VOID
AvCamUdeEvtEndpointPurge(_In_ UDECXUSBENDPOINT endpoint)
{
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx = AvCamUdeEndpointGetContext(endpoint);
    BOOLEAN signalPurge = FALSE;
    WdfSpinLockAcquire(ctx->Lock);
    if (AvCamUdeDrainBeginPurge(&ctx->Drain) == AVCAM_UDE_DRAIN_OK &&
        ctx->Drain.Active == 0) {
        signalPurge = TRUE;
    }
    WdfSpinLockRelease(ctx->Lock);

    /* Never acknowledge inline at DISPATCH_LEVEL. If a callback is
     * active, its final Leave will enqueue the sole acknowledgment.
     */
    if (signalPurge) {
        WdfWorkItemEnqueue(ctx->PurgeWorkItem);
    }
}

VOID
AvCamUdeEvtEndpointPurgeWorkItem(_In_ WDFWORKITEM workItem)
{
    UDECXUSBENDPOINT endpoint = (UDECXUSBENDPOINT)
        WdfWorkItemGetParentObject(workItem);
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx = AvCamUdeEndpointGetContext(endpoint);
    BOOLEAN acknowledge;
    WdfSpinLockAcquire(ctx->Lock);
    acknowledge = (AvCamUdeDrainClaimPurgeAck(&ctx->Drain) != 0);
    WdfSpinLockRelease(ctx->Lock);
    if (acknowledge) {
        UdecxUsbEndpointPurgeComplete(endpoint);
    }
}

VOID
AvCamUdeEvtEndpointReset(
    _In_ UDECXUSBENDPOINT endpoint,
    _In_ WDFREQUEST request)
{
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx =
        AvCamUdeEndpointGetContext(endpoint);
    BOOLEAN resetSafe;

    /* UdeCx can issue RESET while callbacks still own request buffers.
     * We do not report a successful reset until every inline request has
     * finished and no PURGE is outstanding. Future async workers must also
     * be counted in this same Drain before enabling ISO streaming.
     */
    WdfSpinLockAcquire(ctx->Lock);
    resetSafe = (AvCamUdeDrainResetWhenIdle(&ctx->Drain) ==
        AVCAM_UDE_DRAIN_OK);
    WdfSpinLockRelease(ctx->Lock);

    WdfRequestCompleteWithInformation(
        request, resetSafe ? STATUS_SUCCESS : STATUS_DEVICE_BUSY, 0);
}

/*
 * Dynamic endpoints are mandatory: the UVC VideoStreaming interface has
 * alternate setting 0 with zero pipes, and alternate setting 1 with 0x81.
 * Unlike simple endpoint init, this init object is provided by UdeCx.
 * The framework retains its lifetime until UdecxUsbEndpointCreate consumes it.
 */
static
NTSTATUS
AvCamUdeCreateDynamicEndpoint(
    _In_ WDFDEVICE controller,
    _In_ UDECXUSBDEVICE usbDevice,
    _In_ PUDECXUSBENDPOINT_INIT providedInit,
    _In_ UCHAR address,
    _Out_ UDECXUSBENDPOINT* endpoint)
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_OBJECT_ATTRIBUTES endpointAttributes;
    WDF_OBJECT_ATTRIBUTES childAttributes;
    WDF_WORKITEM_CONFIG workItemConfig;
    PAVCAM_UDE_ENDPOINT_CONTEXT ctx;
    UDECX_USB_ENDPOINT_CALLBACKS callbacks;
    PUDECXUSBENDPOINT_INIT localInit = providedInit;
    WDFQUEUE queue;

    if (localInit == NULL || endpoint == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    *endpoint = NULL;
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoInternalDeviceControl = AvCamUdeEvtEndpointIo;
    queueConfig.PowerManaged = WdfFalse;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &queueAttributes, AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT);
    status = WdfIoQueueCreate(controller, &queueConfig,
                              &queueAttributes, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    AvCamUdeEndpointQueueGetContext(queue)->UsbDevice = usbDevice;
    AvCamUdeEndpointQueueGetContext(queue)->EndpointAddress = address;

    UdecxUsbEndpointInitSetEndpointAddress(localInit, address);
    UDECX_USB_ENDPOINT_CALLBACKS_INIT(&callbacks, AvCamUdeEvtEndpointReset);
    callbacks.EvtUsbEndpointStart = AvCamUdeEvtEndpointStart;
    callbacks.EvtUsbEndpointPurge = AvCamUdeEvtEndpointPurge;
    UdecxUsbEndpointInitSetCallbacks(localInit, &callbacks);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &endpointAttributes, AVCAM_UDE_ENDPOINT_CONTEXT);
    status = UdecxUsbEndpointCreate(
        &localInit, &endpointAttributes, endpoint);
    if (!NT_SUCCESS(status)) {
        /* The endpoint init belongs to the UdeCx callback, not this helper. */
        WdfObjectDelete(queue);
        return status;
    }

    ctx = AvCamUdeEndpointGetContext(*endpoint);
    AvCamUdeDrainInit(&ctx->Drain);
    WDF_OBJECT_ATTRIBUTES_INIT(&childAttributes);
    childAttributes.ParentObject = (WDFOBJECT)*endpoint;
    status = WdfSpinLockCreate(&childAttributes, &ctx->Lock);
    if (!NT_SUCCESS(status)) {
        goto FailEndpoint;
    }
    WDF_WORKITEM_CONFIG_INIT(
        &workItemConfig, AvCamUdeEvtEndpointPurgeWorkItem);
    status = WdfWorkItemCreate(
        &workItemConfig, &childAttributes, &ctx->PurgeWorkItem);
    if (!NT_SUCCESS(status)) {
        goto FailEndpoint;
    }
    AvCamUdeEndpointQueueGetContext(queue)->Endpoint = *endpoint;
    UdecxUsbEndpointSetWdfIoQueue(*endpoint, queue);
    return STATUS_SUCCESS;

FailEndpoint:
    WdfObjectDelete(*endpoint);
    *endpoint = NULL;
    WdfObjectDelete(queue);
    return status;
}

NTSTATUS
AvCamUdeEvtDefaultEndpointAdd(
    _In_ UDECXUSBDEVICE usbDevice,
    _In_ PUDECXUSBENDPOINT_INIT endpointInit)
{
    PAVCAM_UDE_USB_DEVICE_CONTEXT deviceContext =
        AvCamUdeUsbGetContext(usbDevice);
    PAVCAM_UDE_CONTROLLER_CONTEXT controllerContext =
        AvCamUdeControllerGetContext(deviceContext->Controller);

    return AvCamUdeCreateDynamicEndpoint(
        deviceContext->Controller, usbDevice, endpointInit, 0x00,
        &controllerContext->ControlEndpoint);
}

NTSTATUS
AvCamUdeEvtDynamicEndpointAdd(
    _In_ UDECXUSBDEVICE usbDevice,
    _In_ PUDECX_USB_ENDPOINT_INIT_AND_METADATA endpointToCreate)
{
    PAVCAM_UDE_USB_DEVICE_CONTEXT deviceContext =
        AvCamUdeUsbGetContext(usbDevice);
    PAVCAM_UDE_CONTROLLER_CONTEXT controllerContext =
        AvCamUdeControllerGetContext(deviceContext->Controller);
    PUSB_ENDPOINT_DESCRIPTOR descriptor;

    if (endpointToCreate == NULL ||
        endpointToCreate->UdecxUsbEndpointInit == NULL ||
        endpointToCreate->EndpointDescriptor == NULL ||
        endpointToCreate->EndpointDescriptorBufferLength <
            sizeof(USB_ENDPOINT_DESCRIPTOR)) {
        return STATUS_INVALID_PARAMETER;
    }

    descriptor = endpointToCreate->EndpointDescriptor;
    if (descriptor->bEndpointAddress != 0x81 ||
        (descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_TYPE_ISOCHRONOUS) {
        return STATUS_NOT_SUPPORTED;
    }

    return AvCamUdeCreateDynamicEndpoint(
        deviceContext->Controller, usbDevice,
        endpointToCreate->UdecxUsbEndpointInit,
        descriptor->bEndpointAddress, &controllerContext->VideoEndpoint);
}

VOID
AvCamUdeEvtEndpointsConfigure(
    _In_ UDECXUSBDEVICE usbDevice,
    _In_ WDFREQUEST request,
    _In_ PUDECX_ENDPOINTS_CONFIGURE_PARAMS params)
{
    UNREFERENCED_PARAMETER(usbDevice);
    UNREFERENCED_PARAMETER(params);
    /* No physical endpoint programming is needed, but the framework needs
     * acknowledgement of configuration and alternate-setting transitions.
     * Actual video requests still fail closed until UVC is implemented. */
    WdfRequestComplete(request, STATUS_SUCCESS);
}

NTSTATUS
AvCamUdeUsbCreateAndPlug(_In_ WDFDEVICE controller)
{
    PAVCAM_UDE_CONTROLLER_CONTEXT context =
        AvCamUdeControllerGetContext(controller);
    PUDECXUSBDEVICE_INIT init;
    UDECXUSBDEVICE usbDevice = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    UDECX_USB_DEVICE_PLUG_IN_OPTIONS options;
    UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS callbacks;
    UNICODE_STRING manufacturer;
    UNICODE_STRING product;
    UNICODE_STRING serial;
    NTSTATUS status;

    if (context->UsbDevice != NULL || context->ChildPlugged) {
        return STATUS_DEVICE_BUSY;
    }

    init = UdecxUsbDeviceInitAllocate(controller);
    if (init == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UDECX_USB_DEVICE_CALLBACKS_INIT(&callbacks);
    callbacks.EvtUsbDeviceDefaultEndpointAdd = AvCamUdeEvtDefaultEndpointAdd;
    callbacks.EvtUsbDeviceEndpointAdd = AvCamUdeEvtDynamicEndpointAdd;
    callbacks.EvtUsbDeviceEndpointsConfigure = AvCamUdeEvtEndpointsConfigure;
    UdecxUsbDeviceInitSetStateChangeCallbacks(init, &callbacks);

    UdecxUsbDeviceInitSetSpeed(init, UdecxUsbHighSpeed);
    UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);

    status = UdecxUsbDeviceInitAddDescriptor(
        init, (PUCHAR)g_AvCamUsbDeviceDescriptor,
        sizeof(g_AvCamUsbDeviceDescriptor));
    if (!NT_SUCCESS(status)) goto Fail;

    status = UdecxUsbDeviceInitAddDescriptor(
        init, (PUCHAR)g_AvCamUsbConfigurationDescriptor,
        sizeof(g_AvCamUsbConfigurationDescriptor));
    if (!NT_SUCCESS(status)) goto Fail;

    status = UdecxUsbDeviceInitAddDescriptorWithIndex(
        init, (PUCHAR)g_AvCamUsbStringLangId,
        sizeof(g_AvCamUsbStringLangId), 0);
    if (!NT_SUCCESS(status)) goto Fail;

    RtlInitUnicodeString(&manufacturer, L"AvCam Research");
    RtlInitUnicodeString(&product, L"AvCam Virtual USB Camera");
    RtlInitUnicodeString(&serial, L"AVCAM000");

    status = UdecxUsbDeviceInitAddStringDescriptor(
        init, &manufacturer, 1, 0x0409);
    if (!NT_SUCCESS(status)) goto Fail;
    status = UdecxUsbDeviceInitAddStringDescriptor(
        init, &product, 2, 0x0409);
    if (!NT_SUCCESS(status)) goto Fail;
    status = UdecxUsbDeviceInitAddStringDescriptor(
        init, &serial, 3, 0x0409);
    if (!NT_SUCCESS(status)) goto Fail;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes, AVCAM_UDE_USB_DEVICE_CONTEXT);
    status = UdecxUsbDeviceCreate(&init, &attributes, &usbDevice);
    if (!NT_SUCCESS(status)) goto Fail;
    AvCamUdeUsbGetContext(usbDevice)->Controller = controller;
    AvCamUvcInitState(&AvCamUdeUsbGetContext(usbDevice)->Stream);

    /* UdeCx now creates EP0 and alternate-setting video EP 0x81 through
     * callbacks when its USB configuration changes. */

    UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);
    options.Usb20PortNumber = 1;
    status = UdecxUsbDevicePlugIn(usbDevice, &options);
    if (!NT_SUCCESS(status)) goto Fail;

    /* User-mode publishers observe child presence under the same lock
     * that protects source storage across future USB streaming workers.
     */
    WdfWaitLockAcquire(context->FrameLock, NULL);
    context->UsbDevice = usbDevice;
    context->ChildPlugged = TRUE;
    WdfWaitLockRelease(context->FrameLock);
    return STATUS_SUCCESS;

Fail:
    if (init != NULL) {
        UdecxUsbDeviceInitFree(init);
    }
    if (usbDevice != NULL) {
        WdfObjectDelete(usbDevice);
    }
    context->ControlEndpoint = NULL;
    context->VideoEndpoint = NULL;
    return status;
}

VOID
AvCamUdeUsbDisconnect(_In_ WDFDEVICE controller)
{
    PAVCAM_UDE_CONTROLLER_CONTEXT context =
        AvCamUdeControllerGetContext(controller);
    UDECXUSBDEVICE usbDevice;
    BOOLEAN plugged;

    /* Revoke user-mode ingestion BEFORE deleting the child. Never hold
     * FrameLock across UdecxUsbDevicePlugOutAndDelete: UdeCx can invoke
     * endpoint callbacks as part of its removal path.
     */
    WdfWaitLockAcquire(context->FrameLock, NULL);
    usbDevice = context->UsbDevice;
    plugged = context->ChildPlugged;
    context->ChildPlugged = FALSE;
    context->UsbDevice = NULL;
    WdfWaitLockRelease(context->FrameLock);

    if (usbDevice != NULL) {
        if (plugged) {
            (VOID)UdecxUsbDevicePlugOutAndDelete(usbDevice);
        } else {
            WdfObjectDelete(usbDevice);
        }
        context->ControlEndpoint = NULL;
        context->VideoEndpoint = NULL;
    }
}
