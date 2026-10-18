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

static
NTSTATUS
AvCamUdeCreateEndpoint(
    _In_ WDFDEVICE controller,
    _In_ UDECXUSBDEVICE usbDevice,
    _In_ UCHAR address,
    _Out_ UDECXUSBENDPOINT* endpoint)
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    UDECX_USB_ENDPOINT_CALLBACKS callbacks;
    PUDECXUSBENDPOINT_INIT init;
    WDFQUEUE queue;

    *endpoint = NULL;
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoInternalDeviceControl = AvCamUdeEvtEndpointIo;
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(controller, &queueConfig,
                              WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    init = UdecxUsbSimpleEndpointInitAllocate(usbDevice);
    if (init == NULL) {
        WdfObjectDelete(queue);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UdecxUsbEndpointInitSetEndpointAddress(init, address);
    UDECX_USB_ENDPOINT_CALLBACKS_INIT(&callbacks, AvCamUdeEvtEndpointReset);
    UdecxUsbEndpointInitSetCallbacks(init, &callbacks);

    status = UdecxUsbEndpointCreate(&init, WDF_NO_OBJECT_ATTRIBUTES,
                                    endpoint);
    if (NT_SUCCESS(status)) {
        UdecxUsbEndpointSetWdfIoQueue(*endpoint, queue);
    } else {
        if (init != NULL) {
            UdecxUsbEndpointInitFree(init);
        }
        WdfObjectDelete(queue);
    }
    return status;
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

    UdecxUsbDeviceInitSetSpeed(init, UdecxUsbHighSpeed);
    UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeSimple);

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

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = UdecxUsbDeviceCreate(&init, &attributes, &usbDevice);
    if (!NT_SUCCESS(status)) goto Fail;

    status = AvCamUdeCreateEndpoint(
        controller, usbDevice, 0x00, &context->ControlEndpoint);
    if (!NT_SUCCESS(status)) goto Fail;
    status = AvCamUdeCreateEndpoint(
        controller, usbDevice, 0x81, &context->VideoEndpoint);
    if (!NT_SUCCESS(status)) goto Fail;

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
