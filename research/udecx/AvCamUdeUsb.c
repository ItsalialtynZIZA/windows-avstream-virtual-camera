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

VOID
AvCamUdeEvtEndpointIo(
    _In_ WDFQUEUE queue,
    _In_ WDFREQUEST request,
    _In_ size_t outputLength,
    _In_ size_t inputLength,
    _In_ ULONG ioctl)
{
    UNREFERENCED_PARAMETER(queue);
    UNREFERENCED_PARAMETER(outputLength);
    UNREFERENCED_PARAMETER(inputLength);

    if (ioctl == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        /* No fabricated successful transfers: UVC class controls and
         * isochronous endpoint handling are not implemented yet. */
        UdecxUrbCompleteWithNtStatus(request, STATUS_NOT_SUPPORTED);
    } else {
        WdfRequestComplete(request, STATUS_INVALID_DEVICE_REQUEST);
    }
}

VOID
AvCamUdeEvtEndpointReset(
    _In_ UDECXUSBENDPOINT endpoint,
    _In_ WDFREQUEST request)
{
    UNREFERENCED_PARAMETER(endpoint);
    WdfRequestComplete(request, STATUS_SUCCESS);
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
    _In_ PUDECXUSBENDPOINT_INIT providedInit,
    _In_ UCHAR address,
    _Out_ UDECXUSBENDPOINT* endpoint)
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
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
    status = WdfIoQueueCreate(controller, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    UdecxUsbEndpointInitSetEndpointAddress(localInit, address);
    UDECX_USB_ENDPOINT_CALLBACKS_INIT(&callbacks, AvCamUdeEvtEndpointReset);
    UdecxUsbEndpointInitSetCallbacks(localInit, &callbacks);
    status = UdecxUsbEndpointCreate(
        &localInit, WDF_NO_OBJECT_ATTRIBUTES, endpoint);
    if (!NT_SUCCESS(status)) {
        /* The endpoint init belongs to the UdeCx callback, not this helper. */
        WdfObjectDelete(queue);
        return status;
    }

    UdecxUsbEndpointSetWdfIoQueue(*endpoint, queue);
    return STATUS_SUCCESS;
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
        deviceContext->Controller, endpointInit, 0x00,
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
        deviceContext->Controller, endpointToCreate->UdecxUsbEndpointInit,
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

    /* UdeCx now creates EP0 and alternate-setting video EP 0x81 through
     * callbacks when its USB configuration changes. */

    UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&options);
    options.Usb20PortNumber = 1;
    status = UdecxUsbDevicePlugIn(usbDevice, &options);
    if (!NT_SUCCESS(status)) goto Fail;

    context->UsbDevice = usbDevice;
    context->ChildPlugged = TRUE;
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

    if (context->UsbDevice != NULL) {
        if (context->ChildPlugged) {
            (VOID)UdecxUsbDevicePlugOutAndDelete(context->UsbDevice);
        } else {
            WdfObjectDelete(context->UsbDevice);
        }
        context->UsbDevice = NULL;
        context->ControlEndpoint = NULL;
        context->VideoEndpoint = NULL;
        context->ChildPlugged = FALSE;
    }
}
