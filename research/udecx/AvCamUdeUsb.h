#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <wdfusb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <ude/1.0/UdeCx.h>
#include "AvCamUdeControl.h"

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

typedef struct _AVCAM_UDE_USB_DEVICE_CONTEXT {
    WDFDEVICE Controller;
    AVCAM_UVC_STREAM_STATE Stream;
} AVCAM_UDE_USB_DEVICE_CONTEXT, *PAVCAM_UDE_USB_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_USB_DEVICE_CONTEXT, AvCamUdeUsbGetContext);

/* An endpoint queue belongs to the controller; retain the identity
 * of the UdeCx child and which pipe it represents for safe dispatch. */
typedef struct _AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT {
    UDECXUSBDEVICE UsbDevice;
    UCHAR EndpointAddress;
} AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT, *PAVCAM_UDE_ENDPOINT_QUEUE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    AVCAM_UDE_ENDPOINT_QUEUE_CONTEXT, AvCamUdeEndpointQueueGetContext);

NTSTATUS AvCamUdeInspectIsoUrb(_In_ WDFREQUEST Request);

NTSTATUS AvCamUdeUsbCreateAndPlug(_In_ WDFDEVICE Controller);
VOID AvCamUdeUsbDisconnect(_In_ WDFDEVICE Controller);

EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD AvCamUdeEvtDefaultEndpointAdd;
EVT_UDECX_USB_DEVICE_ENDPOINT_ADD AvCamUdeEvtDynamicEndpointAdd;
EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE AvCamUdeEvtEndpointsConfigure;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL AvCamUdeEvtEndpointIo;
EVT_UDECX_USB_ENDPOINT_RESET AvCamUdeEvtEndpointReset;
