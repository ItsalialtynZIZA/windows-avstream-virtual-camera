#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include <usbdlib.h>
#include <ude/1.0/UdeCx.h>

/* One emulated high-speed UVC device on one software USB controller.
 * The current milestone is compile-only. No child is plugged in unless
 * the controller's explicit experimental gate enables the code path.
 */
typedef struct _AVCAM_UDE_CONTROLLER_CONTEXT {
    UDECXUSBDEVICE UsbDevice;
    UDECXUSBENDPOINT ControlEndpoint;
    UDECXUSBENDPOINT VideoEndpoint;
    BOOLEAN ChildPlugged;
} AVCAM_UDE_CONTROLLER_CONTEXT, *PAVCAM_UDE_CONTROLLER_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_CONTROLLER_CONTEXT, AvCamUdeControllerGetContext);

NTSTATUS AvCamUdeUsbCreateAndPlug(_In_ WDFDEVICE Controller);
VOID AvCamUdeUsbDisconnect(_In_ WDFDEVICE Controller);

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamUdeEvtEndpointIo;
EVT_UDECX_USB_ENDPOINT_RESET AvCamUdeEvtEndpointReset;
